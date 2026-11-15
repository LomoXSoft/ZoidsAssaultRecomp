// zoidsassault - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>
#include <system_error>

#include <rex/filesystem.h>
#include <rex/logging/types.h>
#include <rex/rex_app.h>

#include <rex/ui/overlay/achievement_toast.h>

#include "achievements.h"
#include "frame_stats.h"
#include "game_icon.h"
#include "launcher.h"
#include "research.h"

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
  void OnPostLoadXexImage() override {
    zoids::ScanFrameConstants();  // research flag only
    zoids::AchievementsInit();    // Recomp Achievements (does nothing outside RecompLauncher)
    zoids::CaptureGameIcon();   // quits here with --zoids_write_icon
  }
  // ReXGlue's unlock toast, hidden while RecompLauncher shows its own.
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    if (!imgui_drawer()) return nullptr;
    return std::make_unique<QuietableToast>(imgui_drawer(), immediate_drawer(), runtime());
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    SetGuestFrameStats(zoids::GuestFrameStats);  // F3 overlay
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
  class QuietableToast : public rex::ui::AchievementToastDialog {
   public:
    using rex::ui::AchievementToastDialog::AchievementToastDialog;
    void Push(const rex::system::AchievementEvent& event) override {
      if (zoids::AchievementsQuietToasts()) return;
      rex::ui::AchievementToastDialog::Push(event);
    }
  };

  static std::filesystem::path SetupRoot() {
    return rex::filesystem::GetExecutableFolder().parent_path();
  }
};
