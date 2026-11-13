// zoidsassault - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/logging/types.h>
#include <rex/rex_app.h>

#include "game_icon.h"
#include "launcher.h"

class ZoidsAssaultApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ZoidsAssaultApp>(new ZoidsAssaultApp(ctx, "zoidsassault",
        PPCImageConfig));
  }

  // Launcher (display settings) before the runtime and the GPU plugin start.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    (void)config;
    zoids::ApplyLauncherSettings();
  }
  // The GPU plugin's cvars exist only now: log what the launcher's choices became.
  void OnPostSetup() override {
    zoids::LogDisplaySettings();
    zoids::ApplyGameIcon();
  }
  // The game's own icon, read from the loaded default.xex (game_icon.cpp).
  void OnPostLoadXexImage() override { zoids::CaptureGameIcon(); }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    zoids::ApplyGameIcon();
  }

  // A setup install keeps the disc files in <root>\gamedata and the log in
  // <root>\zoids.log, beside <root>\game\zoidsassault.exe. Play.bat passes both;
  // without them (a shortcut to the exe) use the same places.
  // --game_data_root and --log_file still win.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (paths.game_data_root.empty()) {
      const auto data = SetupRoot() / "gamedata";
      std::error_code ec;
      if (std::filesystem::is_regular_file(data / "default.xex", ec)) paths.game_data_root = data;
    }
  }
  void OnConfigureLogging(rex::LogConfig& config) override {
    std::error_code ec;
    if (config.log_file.empty() && std::filesystem::is_directory(SetupRoot() / "gamedata", ec))
      config.log_file = SetupRoot() / "zoids.log";
  }

 private:
  static std::filesystem::path SetupRoot() {
    return rex::filesystem::GetExecutableFolder().parent_path();
  }
};
