#pragma once

namespace zoids {

// Shows the launcher when needed (first run, a new version, Shift held at start,
// or --launcher) and applies the saved display settings as runtime cvars.
// Closing the launcher without Play ends the process.
void ApplyLauncherSettings();

// Logs the display cvars after setup, when the GPU plugin has registered its own.
void LogDisplaySettings();

}  // namespace zoids
