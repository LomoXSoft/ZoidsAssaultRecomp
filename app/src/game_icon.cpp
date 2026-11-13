// The game's own icon (the 64x64 title image in default.xex's XDBF resource),
// read from the loaded executable at runtime. Nothing game-derived is stored in
// the repository or the release: the window and taskbar get the icon from
// memory, and game\zoids_icon.ico is written next to the exe for shortcuts.
#include "game_icon.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(zoids_write_icon, false, "Zoids",
                    "Write zoids_icon.ico (the game's title image) next to the exe and quit (used by the setup)");

namespace zoids {
namespace {

std::mutex g_mutex;
std::vector<uint8_t> g_png;

std::vector<uint8_t> PngToIco(const std::vector<uint8_t>& png) {
  // One PNG-compressed image (Vista and later read these directly).
  uint32_t w = 0, h = 0;
  if (png.size() >= 24) {
    w = (uint32_t(png[16]) << 24) | (uint32_t(png[17]) << 16) | (uint32_t(png[18]) << 8) | png[19];
    h = (uint32_t(png[20]) << 24) | (uint32_t(png[21]) << 16) | (uint32_t(png[22]) << 8) | png[23];
  }
  std::vector<uint8_t> ico;
  auto u16 = [&](uint16_t v) { ico.push_back(uint8_t(v)); ico.push_back(uint8_t(v >> 8)); };
  auto u32 = [&](uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); };
  u16(0); u16(1); u16(1);                       // reserved, type icon, one image
  ico.push_back(uint8_t(w >= 256 ? 0 : w));     // width (0 = 256)
  ico.push_back(uint8_t(h >= 256 ? 0 : h));     // height
  ico.push_back(0); ico.push_back(0);           // palette, reserved
  u16(1); u16(32);                              // planes, bits per pixel
  u32(uint32_t(png.size())); u32(22);           // data size, offset
  ico.insert(ico.end(), png.begin(), png.end());
  return ico;
}

void WriteIconFile(const std::vector<uint8_t>& png) {
  const auto path = rex::filesystem::GetExecutableFolder() / "zoids_icon.ico";
  const auto ico = PngToIco(png);
  {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (old == ico) return;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(ico.data()), std::streamsize(ico.size()));
  REXLOG_INFO("zoids icon: wrote {} ({} bytes)", path.string(), ico.size());
}

#ifdef _WIN32
void ApplyToWindows(const std::vector<uint8_t>& png) {
  HICON icon = CreateIconFromResourceEx(const_cast<PBYTE>(png.data()), DWORD(png.size()), TRUE, 0x00030000, 0, 0,
                                        LR_DEFAULTCOLOR);
  if (!icon) return;
  EnumWindows(
      [](HWND w, LPARAM p) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId() && !GetWindow(w, GW_OWNER)) {
          SendMessageW(w, WM_SETICON, ICON_BIG, p);
          SendMessageW(w, WM_SETICON, ICON_SMALL, p);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(icon));
}
#endif

}  // namespace

void CaptureGameIcon() {
  auto* ks = REX_KERNEL_STATE();
  if (!ks) return;
  const auto xdbf = ks->title_xdbf();
  const auto block = xdbf.is_valid() ? xdbf.icon() : rex::system::util::XdbfBlock{nullptr, 0};
  if (!block || block.size < 8 || std::memcmp(block.buffer, "\x89PNG", 4) != 0) {
    REXLOG_WARN("zoids icon: no title image in default.xex");
    if (REXCVAR_GET(zoids_write_icon)) ExitProcess(1);
    return;
  }
  std::vector<uint8_t> png(block.buffer, block.buffer + block.size);
  WriteIconFile(png);
  if (REXCVAR_GET(zoids_write_icon)) {
    REXLOG_INFO("zoids icon: --zoids_write_icon done, quitting");
    ExitProcess(0);
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_png = png;
  }
  ApplyGameIcon();
}

void ApplyGameIcon() {
#ifdef _WIN32
  std::vector<uint8_t> png;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    png = g_png;
  }
  if (!png.empty()) ApplyToWindows(png);
#endif
}

}  // namespace zoids
