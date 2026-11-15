// Recomp Achievements (third_party/rl_achievements): passes the game's own
// Xbox 360 achievements to RecompLauncher.
//
// The game unlocks its achievements through XGIUserWriteAchievements, which
// ReXGlue already handles (its AchievementManager keeps the list read from the
// XEX and the unlocks). This forwards each new unlock, sends the list (the
// launcher can't read the encrypted XEX), marks loaded saves, and hides
// ReXGlue's own toast while the launcher shows one.
//
// Started without RecompLauncher (no RL_SESSION), the library does nothing.
//
// zoids.log gets a line for every achievement the game writes. Research:
// --zoids_test_achievement=<id> makes the game's own unlock routine unlock that
// achievement once, 10 seconds after start (to test the chain end to end).
#include "generated/default/zoidsassault_pch.h"

#include "achievements.h"

#include <rl_achievements.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_INT32(zoids_test_achievement, 0, "Zoids",
                     "Research: unlock this achievement id through the game's own routine, once, after 10 seconds");

extern "C" REX_FUNC(__imp__sub_82293270);  // XUserWriteAchievements(count, achievements, overlapped)
extern "C" REX_FUNC(sub_821FF558);         // the game's unlock routine (?, user, id)

// The game writes achievements one at a time: {user index, achievement id}.
extern "C" REX_FUNC(sub_82293270) {
  REX_FUNC_PROLOGUE();
  const uint32_t count = ctx.r3.u32, list = ctx.r4.u32;
  for (uint32_t i = 0; i < count && i < 8 && list; ++i)
    REXLOG_INFO("zoids achievements: the game writes achievement {} (user {})", REX_LOAD_U32(list + i * 8 + 4),
                REX_LOAD_U32(list + i * 8));
  __imp__sub_82293270(ctx, base);
}

namespace zoids {

void AchievementsInit() {
  auto* ks = REX_KERNEL_STATE();
  if (!ks) return;
  auto& manager = ks->achievements();
  const auto list = manager.ListAchievements();
  for (const auto& a : list) rl_ach_native_define(a.id, a.label.c_str(), a.description.c_str(), a.gamerscore);
  rl_ach_set_info("zoidsassaultrecomp", ZOIDS_VERSION);
  const bool connected = rl_ach_init("x360", nullptr, nullptr) != 0;
  size_t earlier = 0;
  if (connected) {
    // Unlocks from before this session: the launcher keeps them as earlier ones.
    for (const auto& a : list)
      if (manager.IsUnlocked(a.id)) {
        rl_ach_native_earlier(a.id);
        ++earlier;
      }
    manager.RegisterUnlockCallback([](const rex::system::AchievementEvent& e) {
      REXLOG_INFO("zoids achievements: unlocked {:08X} ({})", e.achievement.id, e.achievement.label);
      rl_ach_native(e.achievement.id);
    });
  }
  REXLOG_INFO("zoids achievements: {} in the game's list, RecompLauncher {}{}", list.size(),
              connected ? "connected" : "not running",
              connected ? fmt::format(", {} unlocked before", earlier) : std::string());
}

void AchievementsFrame() { rl_ach_frame(); }

void AchievementsSaveLoaded() { rl_ach_save_loaded(); }

bool AchievementsQuietToasts() { return rl_ach_quiet_toasts() != 0; }

void AchievementsResearch(PPCContext& ctx, uint8_t* base, uint64_t frame) {
  const int id = REXCVAR_GET(zoids_test_achievement);
  if (id <= 0 || frame != 300) return;
  REXLOG_INFO("zoids achievements: research unlock of achievement {}", id);
  const PPCContext saved = ctx;
  ctx.r3.u64 = 0;
  ctx.r4.u64 = 0;  // user index
  ctx.r5.u64 = static_cast<uint32_t>(id);
  sub_821FF558(ctx, base);
  const uint32_t result = ctx.r3.u32;
  ctx = saved;
  REXLOG_INFO("zoids achievements: research unlock returned {}", result);
}

}  // namespace zoids
