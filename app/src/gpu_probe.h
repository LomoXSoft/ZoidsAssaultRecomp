#pragma once

#include <cstdint>

namespace zoids {

// Research (--zoids_gpu_probe): close the current frame's GPU stream statistics.
void GpuProbeFrameEnd();
void GpuProbeInSwap(bool in);

// The last front buffer passed to VdSwap, from the command stream.
struct FrontBuffer {
  bool valid = false;
  uint32_t address = 0, width = 0, height = 0;
  uint32_t fetch[6] = {};  // texture fetch constant 0 as written before the swap
};
bool LastFrontBuffer(FrontBuffer& out);

}  // namespace zoids
