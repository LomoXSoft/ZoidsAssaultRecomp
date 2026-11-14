#pragma once

#include <cstdint>

struct PPCContext;

namespace zoids {

// Experimental 60 FPS (--zoids_interp): handles one command segment queued by
// the game's D3D. Returns true when it queued the segment itself.
bool InterpKick(PPCContext& ctx, uint8_t* base);

}  // namespace zoids
