#pragma once

#include <cstdint>

namespace zoids {

// Research test harness (autotest.cpp): called once per guest swap.
void AutotestFrame();
uint64_t AutotestSwapCount();
bool AutotestWantsFrames();

}  // namespace zoids
