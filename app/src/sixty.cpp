// Experimental 60 FPS (--zoids_60). The game is frame-locked: its main loop
// (sub_82229A48, loc_8222A01C) calls each subsystem update once per pass with
// no time step, then renders and swaps (sub_821FCB28). At 60 presents every
// update would run twice as often, so the whole game would be twice as fast.
//
// Instead, in 60 mode each update called from the main loop runs only on even
// passes; odd passes render the same state again. The game keeps its speed and
// presents at 60. Subsystems listed in --zoids_60_full run on every pass, for
// testing which ones can run at 60 (smoother) and which ones then run too fast
// (those need their per-tick steps halved first).
//
// Only calls made from the main loop are gated: the check is the return
// address of each call site, so loading screens and other loops are untouched.
#include "generated/default/zoidsassault_pch.h"

#include "sixty.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(zoids_60, false, "Zoids", "Experimental: present at 60 FPS, game updates at the original 30");
REXCVAR_DEFINE_STRING(zoids_60_full, "", "Zoids",
                      "Experimental (60): main-loop updates that run every frame, as hex addresses "
                      "(e.g. 821F7188,821F8B18, or all); the others run every second frame");

namespace {

std::atomic<uint32_t> g_pass{0};  // main-loop passes (counted at the render call)

bool Listed(uint32_t fn) {
  static const std::string list = [] {
    std::string s = REXCVAR_GET(zoids_60_full);
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return "," + s + ",";
  }();
  if (list == ",ALL,") return true;  // every update every frame (the game runs 2x fast)
  char buf[16];
  std::snprintf(buf, sizeof buf, ",%08X,", fn);
  return list.find(buf) != std::string::npos;
}

// True when this main-loop call should be skipped on this pass.
bool Skip(uint32_t fn, uint32_t lr, uint32_t site_return) {
  if (!REXCVAR_GET(zoids_60) || lr != site_return) return false;
  if ((g_pass.load(std::memory_order_relaxed) & 1) == 0) return false;
  return !Listed(fn);
}

}  // namespace

namespace zoids {
bool SixtyMode() { return REXCVAR_GET(zoids_60); }
}  // namespace zoids

// One wrapper per main-loop update. site = return address of its call in the
// loop (bl address + 4). sub_82228A98 is called from two sites.
#define ZOIDS_GATE(fn, site)                                   \
  extern "C" REX_FUNC(__imp__sub_##fn);                        \
  extern "C" REX_FUNC(sub_##fn) {                              \
    REX_FUNC_PROLOGUE();                                       \
    if (Skip(0x##fn, uint32_t(ctx.lr), site)) return;          \
    __imp__sub_##fn(ctx, base);                                \
  }

ZOIDS_GATE(82202E48, 0x8222A024)
ZOIDS_GATE(82200F98, 0x8222A02C)
ZOIDS_GATE(821ADE40, 0x8222A034)
ZOIDS_GATE(821ACE50, 0x8222A03C)
ZOIDS_GATE(82228B30, 0x8222A044)
ZOIDS_GATE(82200998, 0x8222A04C)
ZOIDS_GATE(821F7188, 0x8222A054)
ZOIDS_GATE(82201400, 0x8222A05C)
ZOIDS_GATE(82229210, 0x8222A070)
ZOIDS_GATE(822292C8, 0x8222A08C)
ZOIDS_GATE(821F8B18, 0x8222A09C)

extern "C" REX_FUNC(__imp__sub_82228A98);
extern "C" REX_FUNC(sub_82228A98) {
  REX_FUNC_PROLOGUE();
  const uint32_t lr = uint32_t(ctx.lr);
  if (Skip(0x82228A98, lr, 0x8222A078) || Skip(0x82228A98, lr, 0x8222A094)) return;
  __imp__sub_82228A98(ctx, base);
}

// The main loop's render + swap call: count the pass after it.
extern "C" REX_FUNC(__imp__sub_821FCB28);
extern "C" REX_FUNC(sub_821FCB28) {
  REX_FUNC_PROLOGUE();
  const bool from_loop = uint32_t(ctx.lr) == 0x8222A0A8;
  __imp__sub_821FCB28(ctx, base);
  if (from_loop) {
    const uint32_t n = g_pass.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n == 1 && REXCVAR_GET(zoids_60))
      REXLOG_INFO("zoids 60: main loop running; full-rate updates: [{}]", REXCVAR_GET(zoids_60_full));
  }
}
