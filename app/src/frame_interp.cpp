// Experimental 60 FPS presentation by frame interpolation (--zoids_interp).
//
// The game stays at its own 30 updates per second. Its GPU command stream is
// changed where the game's D3D queues each command segment (sub_8210E810, see
// gpu_probe.cpp). A frame is everything between two swap packets (XE_SWAP,
// written by VdSwap). Each frame's commands are copied as they are queued, and
// at its swap the GPU gets:
//
//   game's own commands of frame N+1 with its draws and swap turned into NOPs
//       (its fences, waits and interrupts run as the game made them; sent
//       from a ring of ours, the game's memory is not changed)
//   [I] copy of frame N+1 with the ALU (float) shader constants of each draw
//       blended halfway toward frame N, then XE_SWAP  -> the in-between frame
//   [R] MEM_WRITE marker = k
//       WAIT_REG_MEM gate >= k    (a host thread opens it half a frame after
//                                  the marker appears)
//       copy of frame N+1 as the game made it, XE_SWAP -> frame N+1
//
// Draws are matched with the previous frame by index buffer, draw type and
// shaders, choosing the closest constants among same-looking draws; a draw
// whose constants changed too much (a camera cut) is not blended. Constants
// loaded from memory (LOAD_ALU_CONSTANT) are copied into the buffer so they can
// be blended too. In the copies, interrupts and the game's own waits become
// NOPs: they already ran with the game's commands. Packets the game writes
// straight into the ring (sub_8210E720, e.g. the screen-tile mask of
// predicated tiling) are part of the copies.
//
// --zoids_interp=1: [I] is not blended (every frame shown twice; a test of the
//   mechanics). 2: in-between frames. 3: [I] only, no [R] (shows only the
//   in-between frames at 30; for checking them).
#include "generated/default/zoidsassault_pch.h"

#include "frame_interp.h"
#include "autotest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_INT32(zoids_interp, 0, "Zoids",
                     "Experimental 60 FPS: 1 = show every frame twice (test), 2 = in-between frames, "
                     "3 = only the in-between frames (test, 30 FPS)");
REXCVAR_DEFINE_DOUBLE(zoids_interp_t, 0.5, "Zoids", "Experimental: blend position of the in-between frame");
REXCVAR_DEFINE_DOUBLE(zoids_interp_cut, 0.6, "Zoids",
                      "Experimental: relative constant change above which a draw is not blended");
REXCVAR_DEFINE_DOUBLE(zoids_interp_cut_frame, 0.15, "Zoids",
                      "Experimental: median relative change of the changed draws above which a frame is a camera "
                      "cut and is not blended");
REXCVAR_DEFINE_DOUBLE(zoids_interp_min_match, 0.8, "Zoids",
                      "Experimental: share of draws that must match the previous frame to blend a frame");
REXCVAR_DEFINE_BOOL(zoids_interp_skip_game_draws, true, "Zoids",
                    "Experimental: the game's own run of each frame draws nothing (the copies draw)");
REXCVAR_DEFINE_BOOL(zoids_interp_log, false, "Zoids", "Experimental: log interpolation statistics");

extern "C" REX_FUNC(__imp__sub_8210E810);

namespace {

using clk = std::chrono::steady_clock;

constexpr uint32_t kAluRegs = 2048;  // registers 0x4000-0x47FF
constexpr uint32_t kSlots = 6;
constexpr uint32_t kSlotBytes = 4u << 20;
constexpr uint32_t kGameRingBytes = 16u << 20;

inline uint32_t Bs(uint32_t v) { return __builtin_bswap32(v); }
inline float AsF(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
inline uint32_t AsU(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
uint32_t Pm4Type3(uint32_t op, uint32_t count) { return (3u << 30) | ((count - 1) << 16) | (op << 8); }
uint32_t ToNop(uint32_t pk) { return (pk & ~(0x7Fu << 8)) | (0x10u << 8); }

struct GuestBlock {
  uint32_t guest = 0;  // guest virtual address
  uint32_t phys = 0;   // GPU (physical) address
  uint8_t* host = nullptr;
};

GuestBlock Alloc(uint32_t size) {
  auto* mem = REX_KERNEL_MEMORY();
  GuestBlock b;
  b.guest = mem->SystemHeapAlloc(size, 4096, rex::memory::kSystemHeapPhysical);
  if (!b.guest) return b;
  b.phys = (b.guest & 0x1FFFFFFF) + (b.guest >= 0xE0000000u ? 0x1000u : 0u);
  b.host = mem->TranslatePhysical<uint8_t*>(b.phys);
  std::memset(b.host, 0, size);
  return b;
}

struct DrawRec {
  uint64_t sig = 0;
};

struct FrameRec {
  std::vector<DrawRec> draws;
  std::vector<uint32_t> snaps;  // kAluRegs host-order values per draw
};

struct State {
  bool init = false, failed = false;
  GuestBlock slot[kSlots];
  uint32_t slot_next = 0;
  GuestBlock ctl;     // +0 marker, +4 gate, +16 descriptor
  GuestBlock game_ring;  // the game's segments as queued (changed copies)
  uint32_t game_ring_pos = 0;
  uint32_t seq = 0;
  std::vector<uint32_t> copy;  // this frame's original commands (big-endian)
  FrameRec prev;
  bool have_prev = false;
  uint64_t st_cut_frames = 0, st_frames = 0, st_draws = 0, st_matched = 0, st_cut = 0, st_blended = 0, st_skipped_frames = 0;
} g;

std::mutex g_mu;
std::vector<uint32_t> g_orig;  // the segment being handled, before changes
std::atomic<bool> g_gate_run{false};

void GateThread() {
  auto* marker = reinterpret_cast<volatile uint32_t*>(g.ctl.host + 0);
  auto* gate = reinterpret_cast<volatile uint32_t*>(g.ctl.host + 4);
  uint32_t seen = 0;
  clk::time_point last_marker{};
  double interval_ms = 33.3;
  double replay_ms = 5.0;  // measured time from opening the gate to the end of [R]
  auto* done = reinterpret_cast<volatile uint32_t*>(g.ctl.host + 8);
  uint32_t n = 0;
  clk::time_point win = clk::now();
  while (g_gate_run.load()) {
    const uint32_t m = *marker;
    if (m == seen) {
      std::this_thread::sleep_for(std::chrono::microseconds(250));
      continue;
    }
    const auto now = clk::now();
    if (last_marker.time_since_epoch().count()) {
      const double d = std::chrono::duration<double, std::milli>(now - last_marker).count();
      if (d > 5 && d < 100) interval_ms = interval_ms * 0.8 + d * 0.2;
    }
    last_marker = now;
    seen = m;
    const double half = std::clamp(interval_ms / 2.0 - replay_ms, 1.0, 25.0);
    const auto target = now + std::chrono::duration_cast<clk::duration>(std::chrono::duration<double, std::milli>(half));
    while (clk::now() + std::chrono::microseconds(1200) < target) std::this_thread::sleep_for(std::chrono::microseconds(500));
    while (clk::now() < target) std::this_thread::yield();
    *gate = m;
    {
      const auto opened = clk::now();
      while (*done != m && clk::now() - opened < std::chrono::milliseconds(60))
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      if (*done == m) {
        const double r = std::chrono::duration<double, std::milli>(clk::now() - opened).count();
        replay_ms = std::clamp(replay_ms * 0.8 + r * 0.2, 0.0, 15.0);
      }
    }
    ++n;
    const double secs = std::chrono::duration<double>(clk::now() - win).count();
    if (secs >= 4.0) {
      if (REXCVAR_GET(zoids_interp_log))
        REXLOG_INFO("zoids interp: {:.1f} extra frames/s (game frame interval {:.1f} ms, replay {:.1f} ms)", n / secs,
                    interval_ms, replay_ms);
      n = 0;
      win = clk::now();
    }
  }
}

bool Init() {
  if (g.init) return !g.failed;
  g.init = true;
  for (auto& s : g.slot) {
    s = Alloc(kSlotBytes);
    if (!s.guest) g.failed = true;
  }
  g.ctl = Alloc(4096);
  if (!g.ctl.guest) g.failed = true;
  g.game_ring = Alloc(kGameRingBytes);
  if (!g.game_ring.guest) g.failed = true;
  if (g.failed) {
    REXLOG_ERROR("zoids interp: guest memory allocation failed, interpolation off");
    return false;
  }
  REXLOG_INFO("zoids interp: mode {}", REXCVAR_GET(zoids_interp));
  g_gate_run = true;
  std::thread(GateThread).detach();
  return true;
}

// Queue guest command dwords through the game's own kick.
void Kick(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t phys, uint32_t dwords, uint32_t flags_hi) {
  if (!dwords) return;
  const uint32_t desc = g.ctl.guest + 16;
  REX_STORE_U32(desc, (flags_hi & 0xFF000000u) | dwords);
  REX_STORE_U32(desc + 4, phys);
  ctx.r3.u64 = device;
  ctx.r4.u64 = desc;
  __imp__sub_8210E810(ctx, base);
}

GuestBlock& NextSlot() {
  GuestBlock& s = g.slot[g.slot_next];
  g.slot_next = (g.slot_next + 1) % kSlots;
  return s;
}

uint64_t ShaderHash(const uint32_t* p, uint32_t n) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t k = 0; k < n && k < 32; ++k) h = (h ^ Bs(p[k])) * 1099511628211ull;
  return h | 1;
}

// One ALU constant dword set before a draw: where it is in the output.
struct Write {
  uint32_t* where;  // big-endian dword in the output (packets or data area)
  uint16_t reg;
};

double Distance(const uint32_t* snap, const std::vector<Write>& pend) {
  double num = 0, den = 1e-3;
  for (const auto& w : pend) {
    const float a = AsF(Bs(*w.where)), b = AsF(snap[w.reg]);
    if (!std::isfinite(a) || !std::isfinite(b)) continue;
    num += std::fabs(a - b);
    den += std::fabs(a) + std::fabs(b);
  }
  return num / den;
}

// Turns a frame copy into a command buffer. If `blend`, the ALU constants are
// blended toward the previous frame; `rec` receives this frame's draws and
// constants (originals). Interrupts and waits become NOPs. Constants loaded
// from memory are copied after the packets (`data_phys` is where that area
// lands for the GPU).
void BuildFrame(const std::vector<uint32_t>& in, std::vector<uint32_t>& out, size_t data_offset, uint32_t data_phys,
                bool blend, FrameRec* rec, bool keep_sync) {
  constexpr size_t kDataMax = 1u << 16;  // dwords for constants loaded from memory
  out.clear();
  out.reserve(data_offset + kDataMax);
  out.assign(in.begin(), in.end());
  out.resize(data_offset + kDataMax, 0);  // packets, then the data area (no reallocation below)
  size_t data_used = data_offset;
  uint32_t shadow[kAluRegs] = {};
  uint32_t vs = 0, ps = 0;
  std::vector<Write> pend;
  // Pass 1 (structure, originals). Pointers into `out` stay valid: no resize below.
  const size_t n_in = in.size();
  struct PendDraw {
    uint64_t sig;
    std::vector<Write> writes;
  };
  std::vector<PendDraw> draws;
  draws.reserve(rec ? 512 : 0);
  std::vector<uint32_t> snaps;
  size_t i = 0;
  while (i < n_in) {
    const uint32_t pk = Bs(out[i]);
    const uint32_t type = pk >> 30;
    uint32_t len = 1;
    if (type == 0) {
      const uint32_t n = ((pk >> 16) & 0x3FFF) + 1;
      const uint32_t reg = pk & 0x7FFF;
      const bool one = (pk >> 15) & 1;
      len = 1 + n;
      for (uint32_t k = 0; k < n && i + 1 + k < n_in; ++k) {
        const uint32_t r = one ? reg : reg + k;
        if (r >= 0x4000 && r < 0x4800) {
          shadow[r - 0x4000] = Bs(out[i + 1 + k]);
          pend.push_back({&out[i + 1 + k], static_cast<uint16_t>(r - 0x4000)});
        }
      }
    } else if (type == 1) {
      len = 3;
    } else if (type == 3) {
      const uint32_t op = (pk >> 8) & 0x7F;
      const uint32_t n = ((pk >> 16) & 0x3FFF) + 1;
      len = 1 + n;
      const size_t d = i + 1;
      switch (op) {
        case 0x54:  // INTERRUPT
        case 0x3C:  // WAIT_REG_MEM
          if (!keep_sync) out[i] = Bs(ToNop(pk));
          break;
        case 0x2D:
        case 0x55: {  // SET_CONSTANT(2), ALU type
          if (d >= n_in) break;
          const uint32_t ot = Bs(out[d]);
          if (((ot >> 16) & 0xFF) != 0) break;
          const uint32_t index = ot & 0x7FF;
          for (uint32_t k = 1; k < n && d + k < n_in; ++k) {
            const uint32_t r = index + k - 1;
            if (r < kAluRegs) {
              shadow[r] = Bs(out[d + k]);
              pend.push_back({&out[d + k], static_cast<uint16_t>(r)});
            }
          }
          break;
        }
        case 0x2F: {  // LOAD_ALU_CONSTANT: address, offset/type, size
          if (d + 2 >= n_in) break;
          const uint32_t a = Bs(out[d]);
          const uint32_t ot = Bs(out[d + 1]);
          const uint32_t size = Bs(out[d + 2]) & 0xFFF;
          if (((ot >> 16) & 0xFF) != 0 || data_used + size > out.size()) break;
          const auto* src = REX_KERNEL_MEMORY()->TranslatePhysical<const uint32_t*>(a & 0x1FFFFFFF);
          const uint32_t index = ot & 0x7FF;
          std::memcpy(&out[data_used], src, size * 4);
          out[d] = Bs((a & 0xE0000003u) | ((data_phys + uint32_t((data_used - data_offset) * 4)) & 0x1FFFFFFCu));
          for (uint32_t k = 0; k < size && index + k < kAluRegs; ++k) {
            shadow[index + k] = Bs(out[data_used + k]);
            pend.push_back({&out[data_used + k], static_cast<uint16_t>(index + k)});
          }
          data_used += size;
          break;
        }
        case 0x27:  // IM_LOAD
          if (d + 1 < n_in) ((Bs(out[d]) & 3) == 0 ? vs : ps) = Bs(out[d + 1]);
          break;
        case 0x2B:  // IM_LOAD_IMMEDIATE
          if (d + 1 < n_in) {
            const uint64_t h = ShaderHash(&out[d + 2], n > 2 ? n - 2 : 0);
            ((Bs(out[d]) & 3) == 0 ? vs : ps) = static_cast<uint32_t>(h ^ (h >> 32));
          }
          break;
        case 0x22:    // DRAW_INDX
        case 0x36: {  // DRAW_INDX_2
          const uint32_t init = (op == 0x22) ? (d + 1 < n_in ? Bs(out[d + 1]) : 0) : (d < n_in ? Bs(out[d]) : 0);
          const uint32_t ib = (op == 0x22 && n >= 3 && d + 2 < n_in) ? Bs(out[d + 2]) : 0;
          uint64_t sig = (uint64_t(init) << 32) ^ ib;
          sig = (sig * 1099511628211ull) ^ (uint64_t(vs) << 17) ^ ps ^ (uint64_t(op) << 56);
          draws.push_back({sig, std::move(pend)});
          pend.clear();
          snaps.insert(snaps.end(), shadow, shadow + kAluRegs);
          break;
        }
        default:
          break;
      }
    }
    i += len;
  }
  out.resize(data_used);
  // Pointers above point into `out`; resize() to a smaller or equal size keeps them.

  // Pass 2: blend toward the previous frame.
  if (blend && g.have_prev && !draws.empty()) {
    const auto& pd = g.prev.draws;
    struct Match {
      size_t prev = SIZE_MAX;
      double dist = 0;
    };
    std::vector<Match> m(draws.size());
    size_t cursor = 0, matched = 0;
    for (size_t k = 0; k < draws.size(); ++k) {
      if (draws[k].writes.empty()) {  // uses constants set for earlier draws
        ++matched;
        continue;
      }
      double best_d = 1e30;
      size_t best = SIZE_MAX;
      const size_t hi = std::min(pd.size(), cursor + 48);
      for (size_t j = cursor; j < hi; ++j) {
        if (pd[j].sig != draws[k].sig) continue;
        const double dd = Distance(&g.prev.snaps[j * kAluRegs], draws[k].writes);
        if (dd < best_d) {
          best_d = dd;
          best = j;
          if (dd == 0) break;
        }
      }
      if (best != SIZE_MAX) {
        m[k] = {best, best_d};
        cursor = best + 1;
        ++matched;
      }
    }
    g.st_draws += draws.size();
    g.st_matched += matched;
    if (REXCVAR_GET(zoids_interp_log)) {
      std::vector<double> ds;
      for (size_t k = 0; k < draws.size(); ++k)
        if (m[k].prev != SIZE_MAX && m[k].dist > 0) ds.push_back(m[k].dist);
      if (!ds.empty()) {
        std::sort(ds.begin(), ds.end());
        if (ds.size() > 100 && ds.back() > 0.02)
          REXLOG_INFO("zoids interp: frame {} changed draws {} median {:.4f} p90 {:.4f} max {:.4f}",
                      zoids::AutotestSwapCount(), ds.size(), ds[ds.size() / 2], ds[ds.size() * 9 / 10], ds.back());
      }
    }
    // A camera cut: most changed draws change a lot at once. Show N+1 twice.
    bool cut_frame = false;
    {
      std::vector<double> ds;
      for (size_t k = 0; k < draws.size(); ++k)
        if (m[k].prev != SIZE_MAX && m[k].dist > 0) ds.push_back(m[k].dist);
      if (ds.size() >= 40) {
        std::nth_element(ds.begin(), ds.begin() + ds.size() / 2, ds.end());
        cut_frame = ds[ds.size() / 2] > REXCVAR_GET(zoids_interp_cut_frame);
      }
    }
    if (cut_frame) {
      ++g.st_cut_frames;
    } else if (matched >= REXCVAR_GET(zoids_interp_min_match) * double(draws.size())) {
      const float t = static_cast<float>(REXCVAR_GET(zoids_interp_t));
      const double cut = REXCVAR_GET(zoids_interp_cut);
      for (size_t k = 0; k < draws.size(); ++k) {
        if (m[k].prev == SIZE_MAX || m[k].dist == 0) continue;
        if (m[k].dist > cut) {
          ++g.st_cut;
          continue;
        }
        const uint32_t* snap = &g.prev.snaps[m[k].prev * kAluRegs];
        for (const auto& w : draws[k].writes) {
          const float b = AsF(Bs(*w.where)), a = AsF(snap[w.reg]);
          if (!std::isfinite(a) || !std::isfinite(b) || a == b) continue;
          *w.where = Bs(AsU(a + (b - a) * t));
          ++g.st_blended;
        }
      }
    } else {
      ++g.st_skipped_frames;
    }
  }
  if (rec) {
    rec->draws.clear();
    rec->draws.reserve(draws.size());
    for (const auto& d : draws) rec->draws.push_back({d.sig});
    rec->snaps = std::move(snaps);
  }
}

void QueueFrame(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t flags_hi) {
  const int mode = REXCVAR_GET(zoids_interp);
  const size_t n = g.copy.size();
  if ((n + 9 + (1u << 16)) * 4 > kSlotBytes) {
    REXLOG_WARN("zoids interp: frame of {} dwords too large", n);
    return;
  }
  // [I]: the frame with blended constants (originals recorded for the next frame).
  GuestBlock& si = NextSlot();
  std::vector<uint32_t> buf;
  FrameRec rec;
  BuildFrame(g.copy, buf, n, si.phys + uint32_t(n * 4), mode >= 2, &rec, false);
  std::memcpy(si.host, buf.data(), buf.size() * 4);
  Kick(ctx, base, device, si.phys, static_cast<uint32_t>(n), flags_hi);
  g.prev = std::move(rec);
  g.have_prev = !g.prev.draws.empty();
  if (mode == 3) return;
  // [R]: marker, gate, the frame as the game made it.
  const size_t nr = g.copy.size();
  if ((nr + 12 + (1u << 16)) * 4 > kSlotBytes) return;
  GuestBlock& sr = NextSlot();
  const uint32_t k = ++g.seq;
  std::vector<uint32_t> head = {Bs(Pm4Type3(0x3D, 2)), Bs(g.ctl.phys + 0), Bs(k),
                                Bs(Pm4Type3(0x3C, 5)), Bs(0x10 | 5), Bs(g.ctl.phys + 4), Bs(k), Bs(0xFFFFFFFFu), Bs(0x100)};
  const size_t hn = head.size();
  // Data area after head + packets + tail.
  const std::vector<uint32_t> tail = {Bs(Pm4Type3(0x3D, 2)), Bs(g.ctl.phys + 8), Bs(k)};  // done = k
  const size_t tn = tail.size();
  BuildFrame(g.copy, buf, nr, sr.phys + uint32_t((hn + nr + tn) * 4), false, nullptr, false);
  std::memcpy(sr.host, head.data(), hn * 4);
  std::memcpy(sr.host + hn * 4, buf.data(), nr * 4);
  std::memcpy(sr.host + (hn + nr) * 4, tail.data(), tn * 4);
  if (buf.size() > nr) std::memcpy(sr.host + (hn + nr + tn) * 4, buf.data() + nr, (buf.size() - nr) * 4);
  Kick(ctx, base, device, sr.phys, static_cast<uint32_t>(hn + nr + tn), flags_hi);
}

bool HandleSegment(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t desc) {
  const uint32_t w0 = REX_LOAD_U32(desc);
  const uint32_t count = w0 & 0xFFFFFF;
  const uint32_t phys = REX_LOAD_U32(desc + 4);
  if (!count || count > (1u << 22)) return false;
  const auto* game_seg = REX_KERNEL_MEMORY()->TranslatePhysical<const uint32_t*>(phys);
  const bool skip_draws = REXCVAR_GET(zoids_interp_skip_game_draws);
  g_orig.assign(game_seg, game_seg + count);
  // The game's memory is left as it is (D3D may queue a segment again, e.g.
  // once per screen tile): the changed version goes into a ring of ours.
  if (size_t(count) * 4 > kGameRingBytes / 4) return false;
  if (g.game_ring_pos + count * 4 > kGameRingBytes) g.game_ring_pos = 0;
  const uint32_t ring_phys = g.game_ring.phys + g.game_ring_pos;
  auto* seg = reinterpret_cast<uint32_t*>(g.game_ring.host + g.game_ring_pos);
  g.game_ring_pos += (count * 4 + 255) & ~255u;
  std::memcpy(seg, game_seg, size_t(count) * 4);
  // The game's own run of each frame keeps its fences, waits and interrupts,
  // but draws nothing and does not present: [I] and [R] do that.
  std::vector<uint32_t> swaps;  // end of each swap packet
  uint32_t i = 0;
  while (i < count) {
    const uint32_t pk = Bs(seg[i]);
    const uint32_t type = pk >> 30;
    uint32_t len = 1;
    if (type == 0) len = 1 + ((pk >> 16) & 0x3FFF) + 1;
    else if (type == 1) len = 3;
    else if (type == 3) {
      len = 1 + ((pk >> 16) & 0x3FFF) + 1;
      const uint32_t op = (pk >> 8) & 0x7F;
      if (op == 0x64 && i + len <= count) {
        seg[i] = Bs(ToNop(pk));
        swaps.push_back(i + len);
      } else if (skip_draws && (op == 0x22 || op == 0x36)) {
        seg[i] = Bs(ToNop(pk));
      }
    }
    i += len;
  }
  uint32_t piece_start = 0;
  for (const uint32_t end : swaps) {
    g.copy.insert(g.copy.end(), g_orig.begin() + piece_start, g_orig.begin() + end);
    Kick(ctx, base, device, ring_phys + piece_start * 4, end - piece_start, w0);
    QueueFrame(ctx, base, device, w0);
    g.copy.clear();
    piece_start = end;
  }
  if (piece_start < count) {
    g.copy.insert(g.copy.end(), g_orig.begin() + piece_start, g_orig.end());
    Kick(ctx, base, device, ring_phys + piece_start * 4, count - piece_start, w0);
  }
  if (g.copy.size() > (1u << 19)) {  // a frame that does not end (loading)
    g.copy.clear();
    g.have_prev = false;
  }
  if (REXCVAR_GET(zoids_interp_log) && g.st_draws > 120 * 400) {
    REXLOG_INFO("zoids interp: draws {} matched {:.1f}% not blended (large change) {} blended constants {} "
                "frames not blended (few matches) {} camera cuts {}",
                g.st_draws, 100.0 * g.st_matched / g.st_draws, g.st_cut, g.st_blended, g.st_skipped_frames,
                g.st_cut_frames);
    g.st_draws = g.st_matched = g.st_cut = g.st_blended = g.st_skipped_frames = g.st_cut_frames = 0;
  }
  return true;
}

}  // namespace

// The game's D3D also writes a few packets straight into the ring
// (sub_8210E720(device, dwords, count)), e.g. the screen-tile mask used by
// predicated tiling. They are part of the frame: add them to the copy.
extern "C" REX_FUNC(__imp__sub_8210E720);
extern "C" REX_FUNC(sub_8210E720) {
  REX_FUNC_PROLOGUE();
  if (REXCVAR_GET(zoids_interp) > 0) {
    const uint32_t src = ctx.r4.u32, n = ctx.r5.u32;
    if (src && n && n < 4096) {
      std::lock_guard lk(g_mu);
      const auto* p = reinterpret_cast<const uint32_t*>(REX_RAW_ADDR(src));
      g.copy.insert(g.copy.end(), p, p + n);
    }
  }
  __imp__sub_8210E720(ctx, base);
}

namespace zoids {

bool InterpKick(PPCContext& ctx, uint8_t* base) {
  if (REXCVAR_GET(zoids_interp) <= 0) return false;
  std::lock_guard lk(g_mu);
  if (!Init()) return false;
  const uint32_t device = ctx.r3.u32, desc = ctx.r4.u32;
  return HandleSegment(ctx, base, device, desc);
}

}  // namespace zoids
