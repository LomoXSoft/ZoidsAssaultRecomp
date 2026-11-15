#pragma once

#include <cstdint>

struct PPCContext;

namespace zoids {

// Recomp Achievements (achievements.cpp). Each does nothing outside RecompLauncher.
void AchievementsInit();        // after the XEX is loaded
void AchievementsFrame();       // once per presented frame
void AchievementsSaveLoaded();  // the game opened its save
bool AchievementsQuietToasts(); // the launcher shows its own unlock toast
// Research (--zoids_test_achievement), from the swap hook on the game thread.
void AchievementsResearch(PPCContext& ctx, uint8_t* base, uint64_t frame);

}  // namespace zoids
