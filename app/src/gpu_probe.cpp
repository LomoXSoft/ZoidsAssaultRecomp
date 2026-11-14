// Research (--zoids_gpu_probe): what the game's GPU command stream looks like,
// for the 60 FPS work. The game's D3D queues each command segment to the GPU
// ring in sub_8210E810(device, {dword count, physical address}); every segment
// is parsed there before the GPU sees it. Per frame (between swaps) it counts
// draws and how shader constants arrive (register writes, SET_CONSTANT,
// LOAD_ALU_CONSTANT), and compares each frame's draws and constants with the
// previous frame's, to see whether frames line up draw for draw.
#include "generated/default/zoidsassault_pch.h"

#include "gpu_probe.h"
#include "autotest.h"
#include "frame_interp.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(zoids_gpu_probe, false, "Zoids", "Research: log GPU command stream statistics per frame");

extern "C" REX_FUNC(__imp__sub_8210E810);

namespace {

struct Draw {
  uint32_t initiator = 0, index_base = 0, vs = 0, ps = 0;
  std::vector<std::pair<uint16_t, uint32_t>> consts;  // ALU constant dwords set since the previous draw
};

struct Frame {
  uint32_t kicks = 0, ib_dwords = 0, nested_ibs = 0;
  uint32_t alu_type0 = 0, alu_setconst = 0, alu_load = 0, alu_load_packets = 0;
  uint32_t fetch_dwords = 0, im_load = 0, im_load_imm = 0;
  uint32_t mem_write = 0, event_write = 0, wait_reg_mem = 0, interrupts = 0, unknown = 0;
  std::map<uint32_t, uint32_t> opcodes;
  std::vector<Draw> draws;
  std::vector<std::pair<uint16_t, uint32_t>> pending;  // constants before the next draw
  uint32_t vs = 0, ps = 0;
};

std::mutex g_mu;
Frame g_cur, g_prev;
uint32_t g_fetch0[6] = {};
int g_swap_logs = 0;
bool g_in_swap = false;
std::mutex g_front_mu;
zoids::FrontBuffer g_front;
bool g_have_prev = false;
uint64_t g_frame_no = 0;

// Accumulated over a log window.
struct Window {
  uint64_t frames = 0, draws = 0, alu_type0 = 0, alu_setconst = 0, alu_load = 0, alu_load_packets = 0;
  uint64_t same_count = 0, sig_match = 0, sig_total = 0, const_changed = 0, const_same = 0;
  uint64_t kicks = 0, ib_dwords = 0, fetch = 0, mem_write = 0, event_write = 0, wait = 0, irq = 0;
  uint64_t im_load = 0, im_load_imm = 0, nested = 0, unknown = 0;
  std::map<uint32_t, uint64_t> opcodes;
} g_win;

inline uint32_t Rd(const uint8_t* p, uint32_t i) { return __builtin_bswap32(reinterpret_cast<const uint32_t*>(p)[i]); }

const uint8_t* Phys(uint32_t addr) { return REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(addr); }

void AluWrite(Frame& f, uint32_t reg, uint32_t value) {
  // ALU (float) constants: registers 0x4000-0x47FF.
  f.pending.emplace_back(static_cast<uint16_t>(reg - 0x4000), value);
}

void ParseIb(Frame& f, uint32_t addr, uint32_t count, int depth) {
  if (depth > 4 || count == 0 || count > (1u << 22)) return;
  const uint8_t* p = Phys(addr);
  f.ib_dwords += count;
  uint32_t i = 0;
  while (i < count) {
    const uint32_t pk = Rd(p, i);
    const uint32_t type = pk >> 30;
    if (type == 0) {
      const uint32_t n = ((pk >> 16) & 0x3FFF) + 1;
      const uint32_t reg = pk & 0x7FFF;
      const bool one = (pk >> 15) & 1;
      for (uint32_t k = 0; k < n && i + 1 + k < count; ++k) {
        const uint32_t r = one ? reg : reg + k;
        if (r >= 0x4000 && r < 0x4800) {
          ++f.alu_type0;
          AluWrite(f, r, Rd(p, i + 1 + k));
        } else if (r >= 0x4800 && r < 0x4900) {
          ++f.fetch_dwords;
          if (r < 0x4806) g_fetch0[r - 0x4800] = Rd(p, i + 1 + k);
        }
      }
      i += 1 + n;
    } else if (type == 1) {
      i += 3;
    } else if (type == 2) {
      i += 1;
    } else {
      const uint32_t op = (pk >> 8) & 0x7F;
      const uint32_t n = ((pk >> 16) & 0x3FFF) + 1;
      ++f.opcodes[op];
      const uint32_t d = i + 1;
      switch (op) {
        case 0x3F:  // INDIRECT_BUFFER
        case 0x37:  // INDIRECT_BUFFER_PFD
          ++f.nested_ibs;
          if (d + 1 < count) ParseIb(f, Rd(p, d), Rd(p, d + 1) & 0xFFFFF, depth + 1);
          break;
        case 0x2D:    // SET_CONSTANT
        case 0x55: {  // SET_CONSTANT2
          const uint32_t ot = Rd(p, d);
          const uint32_t kind = (ot >> 16) & 0xFF, index = ot & 0x7FF;
          for (uint32_t k = 1; k < n && d + k < count; ++k) {
            if (kind == 0) {
              ++f.alu_setconst;
              AluWrite(f, 0x4000 + index + k - 1, Rd(p, d + k));
            } else if (kind == 1) {
              ++f.fetch_dwords;
            }
          }
          break;
        }
        case 0x2F: {  // LOAD_ALU_CONSTANT: address, offset/type, size (dwords)
          if (d + 2 >= count) break;
          const uint32_t src = Rd(p, d) & 0x3FFFFFFF;
          const uint32_t ot = Rd(p, d + 1);
          const uint32_t size = Rd(p, d + 2) & 0xFFF;
          ++f.alu_load_packets;
          if (((ot >> 16) & 0xFF) == 0) {
            const uint8_t* s = Phys(src);
            const uint32_t index = ot & 0x7FF;
            for (uint32_t k = 0; k < size; ++k) {
              ++f.alu_load;
              AluWrite(f, 0x4000 + index + k, Rd(s, k));
            }
          }
          break;
        }
        case 0x27:  // IM_LOAD: shader type, address, size
          ++f.im_load;
          if (d + 1 < count) {
            const uint32_t t = Rd(p, d) & 3;
            (t == 0 ? f.vs : f.ps) = Rd(p, d + 1);
          }
          break;
        case 0x2B:  // IM_LOAD_IMMEDIATE
          ++f.im_load_imm;
          if (d < count) (((Rd(p, d) & 3) == 0) ? f.vs : f.ps) = 0x80000000u | (i & 0xFFFF);
          break;
        case 0x22:    // DRAW_INDX: viz, initiator, [index base, size]
        case 0x36: {  // DRAW_INDX_2: initiator, inline indices
          Draw dr;
          dr.initiator = Rd(p, op == 0x22 ? d + 1 : d);
          dr.index_base = (op == 0x22 && n >= 3) ? Rd(p, d + 2) : 0;
          dr.vs = f.vs;
          dr.ps = f.ps;
          dr.consts.swap(f.pending);
          f.draws.push_back(std::move(dr));
          break;
        }
        case 0x64:  // XE_SWAP (written by VdSwap): 'SWAP', frontbuffer, width, height
          if (d + 3 < count) {
            std::lock_guard fl(g_front_mu);
            g_front.address = Rd(p, d + 1);
            g_front.width = Rd(p, d + 2);
            g_front.height = Rd(p, d + 3);
            for (int k = 0; k < 6; ++k) g_front.fetch[k] = g_fetch0[k];
            g_front.valid = true;
          }
          if (REXCVAR_GET(zoids_gpu_probe) && g_swap_logs < 8) {
            ++g_swap_logs;
            REXLOG_INFO("zoids gpu: XE_SWAP at dword {} of {} (kick #{} this frame, depth {}, inside swap call {})", i,
                        count, f.kicks, depth, g_in_swap);
          }
          break;
        case 0x3D:  // MEM_WRITE
          ++f.mem_write;
          break;
        case 0x46: case 0x58: case 0x59: case 0x5B:  // EVENT_WRITE*
          ++f.event_write;
          break;
        case 0x3C:  // WAIT_REG_MEM
          ++f.wait_reg_mem;
          break;
        case 0x54:  // INTERRUPT
          ++f.interrupts;
          break;
        default:
          break;
      }
      i += 1 + n;
    }
  }
}

void CompareAndAccumulate(const Frame& c, const Frame& p) {
  auto& w = g_win;
  ++w.frames;
  w.draws += c.draws.size();
  w.alu_type0 += c.alu_type0;
  w.alu_setconst += c.alu_setconst;
  w.alu_load += c.alu_load;
  w.alu_load_packets += c.alu_load_packets;
  w.kicks += c.kicks;
  w.ib_dwords += c.ib_dwords;
  w.fetch += c.fetch_dwords;
  w.mem_write += c.mem_write;
  w.event_write += c.event_write;
  w.wait += c.wait_reg_mem;
  w.irq += c.interrupts;
  w.im_load += c.im_load;
  w.im_load_imm += c.im_load_imm;
  w.nested += c.nested_ibs;
  for (auto& [op, n] : c.opcodes) w.opcodes[op] += n;
  if (!g_have_prev) return;
  if (c.draws.size() == p.draws.size()) ++w.same_count;
  const size_t n = std::min(c.draws.size(), p.draws.size());
  for (size_t k = 0; k < n; ++k) {
    const Draw& a = c.draws[k];
    const Draw& b = p.draws[k];
    ++w.sig_total;
    if (a.initiator == b.initiator && a.index_base == b.index_base && a.vs == b.vs && a.ps == b.ps) {
      ++w.sig_match;
      if (a.consts == b.consts)
        ++w.const_same;
      else
        ++w.const_changed;
    }
  }
}

void LogWindow() {
  auto& w = g_win;
  if (!w.frames) return;
  const double f = static_cast<double>(w.frames);
  std::string ops;
  std::vector<std::pair<uint64_t, uint32_t>> top;
  for (auto& [op, n] : w.opcodes) top.emplace_back(n, op);
  std::sort(top.rbegin(), top.rend());
  for (size_t k = 0; k < top.size() && k < 12; ++k) ops += fmt::format(" {:02X}:{:.0f}", top[k].second, top[k].first / f);
  REXLOG_INFO(
      "zoids gpu: {} frames | per frame: kicks {:.1f} dwords {:.0f} draws {:.1f} | ALU dwords: type0 {:.0f} "
      "set_const {:.0f} load {:.0f} ({:.1f} pkts) | fetch {:.0f} im_load {:.1f} imm {:.1f} nested_ib {:.1f} | "
      "mem_write {:.1f} event {:.1f} wait {:.1f} irq {:.1f}",
      w.frames, w.kicks / f, w.ib_dwords / f, w.draws / f, w.alu_type0 / f, w.alu_setconst / f, w.alu_load / f,
      w.alu_load_packets / f, w.fetch / f, w.im_load / f, w.im_load_imm / f, w.nested / f, w.mem_write / f,
      w.event_write / f, w.wait / f, w.irq / f);
  REXLOG_INFO(
      "zoids gpu: same draw count as previous frame {:.0f}% | draws matching previous frame by position {:.1f}% | "
      "of those, constants changed {:.1f}% | opcodes{}",
      100.0 * w.same_count / f, w.sig_total ? 100.0 * w.sig_match / w.sig_total : 0.0,
      (w.const_changed + w.const_same) ? 100.0 * w.const_changed / (w.const_changed + w.const_same) : 0.0, ops);
  w = Window{};
}

}  // namespace

extern "C" REX_FUNC(sub_8210E810) {
  REX_FUNC_PROLOGUE();
  if (REXCVAR_GET(zoids_gpu_probe) || zoids::AutotestWantsFrames()) {
    const uint32_t desc = ctx.r4.u32;
    const uint32_t count = REX_LOAD_U32(desc) & 0xFFFFFF;
    const uint32_t addr = REX_LOAD_U32(desc + 4);
    std::lock_guard lk(g_mu);
    ++g_cur.kicks;
    ParseIb(g_cur, addr, count, 0);
  }
  if (zoids::InterpKick(ctx, base)) return;
  __imp__sub_8210E810(ctx, base);
}

namespace zoids {
void GpuProbeInSwap(bool in) { g_in_swap = in; }
bool LastFrontBuffer(FrontBuffer& out) {
  std::lock_guard fl(g_front_mu);
  out = g_front;
  return out.valid;
}
void GpuProbeFrameEnd() {
  std::lock_guard lk(g_mu);
  if (!REXCVAR_GET(zoids_gpu_probe)) {
    g_cur = Frame{};
    return;
  }
  CompareAndAccumulate(g_cur, g_prev);
  g_prev = std::move(g_cur);
  g_have_prev = true;
  g_cur = Frame{};
  if (++g_frame_no % 120 == 0) LogWindow();
}
}  // namespace zoids
