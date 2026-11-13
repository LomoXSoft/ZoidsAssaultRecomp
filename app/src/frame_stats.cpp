// Guest frame counter: counts the game's D3D Swap (sub_821092E0, the routine
// that calls VdSwap once per presented frame) and logs the rate every few
// seconds when --zoids_fps_log is on. Diagnostic only; it does not change pacing.
#include "generated/default/zoidsassault_pch.h"

#include <atomic>
#include <chrono>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(zoids_fps_log, false, "Zoids", "Log the guest frame rate (swaps per second) every 4 seconds");

extern "C" REX_FUNC(__imp__sub_821092E0);

namespace {
std::atomic<uint64_t> g_swaps{0};
std::chrono::steady_clock::time_point g_t0;
uint64_t g_n0 = 0;
}  // namespace

extern "C" REX_FUNC(sub_821092E0) {
  REX_FUNC_PROLOGUE();
  if (REXCVAR_GET(zoids_fps_log)) {
    const uint64_t n = ++g_swaps;
    const auto now = std::chrono::steady_clock::now();
    if (g_n0 == 0) {
      g_t0 = now;
      g_n0 = n;
    } else {
      const double s = std::chrono::duration<double>(now - g_t0).count();
      if (s >= 4.0) {
        REXLOG_INFO("zoids fps: {:.1f} swaps/s over {:.1f} s", (n - g_n0) / s, s);
        g_t0 = now;
        g_n0 = n;
      }
    }
  }
  __imp__sub_821092E0(ctx, base);
}
