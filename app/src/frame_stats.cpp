// Guest frame pacing and stats, driven by the game's D3D Swap (sub_821092E0,
// the routine that calls VdSwap once per presented frame).
//
// - F3 overlay: FPS, frame time and frame count (GuestFrameStats).
// - VSync off: the game paces itself on the guest vblank, which then runs far
//   faster, so the game logic would run too fast. Frames are held at 30 here
//   (the game's own rate) unless --zoids_fps_cap says otherwise.
// - --zoids_fps_log: the rate in zoids.log every 4 seconds.
#include "generated/default/zoidsassault_pch.h"

#include "frame_stats.h"
#include "sixty.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(zoids_fps_log, false, "Zoids", "Log the guest frame rate (swaps per second) every 4 seconds");
REXCVAR_DEFINE_INT32(zoids_fps_cap, 0, "Zoids",
                     "Guest frame cap in FPS (0 = automatic: 30 when VSync is off, none with VSync)");

extern "C" REX_FUNC(__imp__sub_821092E0);

namespace {
using clk = std::chrono::steady_clock;

std::mutex g_mu;
clk::time_point g_last{}, g_win_start{}, g_log_start{};
uint64_t g_frames = 0, g_win_frames = 0, g_log_frames = 0;
double g_fps = 0, g_ft_ms = 0;
clk::time_point g_next{};

int EffectiveCap() {
  const int cap = REXCVAR_GET(zoids_fps_cap);
  if (cap > 0) return cap;
  if (zoids::SixtyMode()) return 60;  // experimental 60: VSync is off (launcher)
  // `vsync` belongs to the GPU plugin, so read it by name.
  static const bool vsync = !rex::cvar::GetFlagInfo("vsync") || rex::cvar::Query<bool>("vsync");
  return vsync ? 0 : 30;
}

void Pace(int cap) {
  const auto period = std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(1.0 / cap));
  auto now = clk::now();
  if (g_next.time_since_epoch().count() == 0 || now - g_next > period * 4) g_next = now;
  while (true) {
    now = clk::now();
    if (now >= g_next) break;
    const auto left = g_next - now;
    if (left > std::chrono::microseconds(1500))
      std::this_thread::sleep_for(left - std::chrono::microseconds(1500));
    else
      std::this_thread::yield();
  }
  g_next += period;
}

void Count() {
  std::lock_guard lk(g_mu);
  const auto now = clk::now();
  if (g_frames) g_ft_ms = std::chrono::duration<double, std::milli>(now - g_last).count();
  g_last = now;
  ++g_frames;
  if (g_win_frames == 0) g_win_start = now;
  ++g_win_frames;
  const double secs = std::chrono::duration<double>(now - g_win_start).count();
  if (secs >= 1.0) {
    g_fps = (g_win_frames - 1) / secs;
    g_win_frames = 0;
  }
  if (REXCVAR_GET(zoids_fps_log)) {
    if (g_log_frames == 0) g_log_start = now;
    ++g_log_frames;
    const double ls = std::chrono::duration<double>(now - g_log_start).count();
    if (ls >= 4.0) {
      REXLOG_INFO("zoids fps: {:.1f} swaps/s over {:.1f} s (cap {})", (g_log_frames - 1) / ls, ls, EffectiveCap());
      g_log_frames = 0;
    }
  }
}
// Research: the guest call chain at the swap. Each frame stores the caller's
// return address at -8 from its back-chain pointer (stw r12,-8(r1)). Each
// distinct chain is logged once.
void LogSwapStack(const PPCContext& ctx, uint8_t* base) {
  static std::mutex mu;
  static std::vector<std::string> seen;
  std::string chain = fmt::format("{:08X}", uint32_t(ctx.lr));
  uint32_t sp = ctx.r1.u32;
  for (int i = 0; i < 16 && sp >= 0x70000000 && sp < 0x80000000; ++i) {
    const uint32_t back = REX_LOAD_U32(sp);
    if (back <= sp || back >= 0x80000000) break;
    const uint32_t lr = REX_LOAD_U32(back - 8);
    if (lr < 0x82000000 || lr >= 0x82500000) break;
    chain += fmt::format(" <- {:08X}", lr);
    sp = back;
  }
  std::lock_guard lk(mu);
  for (const auto& s : seen)
    if (s == chain) return;
  if (seen.size() > 64) return;
  seen.push_back(chain);
  REXLOG_INFO("zoids swap stack #{} (frame {}): {}", seen.size(), g_frames, chain);
}
}  // namespace

REXCVAR_DEFINE_BOOL(zoids_swap_stack, false, "Zoids", "Research: log each distinct guest call chain into the swap");

extern "C" REX_FUNC(sub_821092E0) {
  REX_FUNC_PROLOGUE();
  if (REXCVAR_GET(zoids_swap_stack)) LogSwapStack(ctx, base);
  if (const int cap = EffectiveCap(); cap > 0) Pace(cap);
  Count();
  __imp__sub_821092E0(ctx, base);
}

namespace zoids {
rex::ui::FrameStats GuestFrameStats() {
  std::lock_guard lk(g_mu);
  rex::ui::FrameStats s;
  s.fps = g_fps;
  s.frame_time_ms = g_ft_ms;
  s.frame_count = g_frames;
  return s;
}
}  // namespace zoids
