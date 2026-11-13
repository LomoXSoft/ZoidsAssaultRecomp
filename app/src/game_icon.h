#pragma once

namespace zoids {

// After default.xex is loaded: read the game's title image, write
// zoids_icon.ico next to the exe (quit there with --zoids_write_icon) and put
// it on the game window.
void CaptureGameIcon();

// Puts the captured icon on this process's top-level windows (call again once
// the game window exists).
void ApplyGameIcon();

}  // namespace zoids
