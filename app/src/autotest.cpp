// Research test harness for the frame-rate work. Off by default.
//
// --zoids_input_script=<file>: scripted controller input for player 1, by
// guest frame (swap count). One entry per line:
//     <frame> <buttons> [frames held, default 3]
// buttons: A B X Y START BACK UP DOWN LEFT RIGHT LB RB LS RS LT RT, joined with
// '+', and stick values as LX=<n> LY=<n> RX=<n> RY=<n> (-32768..32767).
// Lines starting with # are comments. A real controller still works.
//
// --zoids_shot_dir=<dir> with --zoids_shot_every=<frames>: the game's front
// buffer is written to <dir>\shot_<frame>.bmp (with --readback_resolve=full).
#include "generated/default/zoidsassault_pch.h"

#include "autotest.h"
#include "gpu_probe.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_STRING(zoids_input_script, "", "Zoids", "Research: scripted controller input file");
REXCVAR_DEFINE_STRING(zoids_shot_dir, "", "Zoids", "Research: folder for game window captures");
REXCVAR_DEFINE_INT32(zoids_shot_every, 0, "Zoids", "Research: capture the game window every N frames");

extern "C" REX_FUNC(__imp__sub_82293748);

namespace {

struct Entry {
  uint64_t frame = 0, count = 3;
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  bool has_stick[4] = {};
  int16_t stick[4] = {};
};

std::vector<Entry> g_script;
std::once_flag g_load_once;
std::atomic<uint64_t> g_swaps{0};

uint16_t ButtonBit(const std::string& n) {
  static const struct {
    const char* name;
    uint16_t bit;
  } k[] = {{"UP", 0x0001},    {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008}, {"START", 0x0010},
           {"BACK", 0x0020},  {"LS", 0x0040},   {"RS", 0x0080},   {"LB", 0x0100},    {"RB", 0x0200},
           {"A", 0x1000},     {"B", 0x2000},    {"X", 0x4000},    {"Y", 0x8000}};
  for (auto& e : k)
    if (n == e.name) return e.bit;
  return 0;
}

void LoadScript() {
  const std::string path = REXCVAR_GET(zoids_input_script);
  if (path.empty()) return;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    Entry e;
    std::string keys;
    if (!(ls >> e.frame >> keys)) continue;
    ls >> e.count;
    std::stringstream ks(keys);
    std::string k;
    while (std::getline(ks, k, '+')) {
      for (auto& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (k == "LT") e.lt = 255;
      else if (k == "RT") e.rt = 255;
      else if (k.size() > 3 && k[2] == '=') {
        static const char* names[] = {"LX", "LY", "RX", "RY"};
        for (int s = 0; s < 4; ++s)
          if (k.compare(0, 2, names[s]) == 0) {
            e.has_stick[s] = true;
            e.stick[s] = static_cast<int16_t>(std::stoi(k.substr(3)));
          }
      } else e.buttons |= ButtonBit(k);
    }
    g_script.push_back(e);
  }
  REXLOG_INFO("zoids input script: {} entries from {}", g_script.size(), path);
}

uint16_t ScriptButtons(uint64_t f) {
  uint16_t b = 0;
  for (const auto& e : g_script)
    if (f >= e.frame && f < e.frame + e.count) b |= e.buttons;
  return b;
}

// Keystroke events (XInputGetKeystroke) made from the script's button edges.
std::mutex g_key_mu;
std::vector<std::pair<uint16_t, uint16_t>> g_keys;  // virtual key, flags

void QueueKeyEdges(uint64_t f) {
  static const struct {
    uint16_t bit, vk;
  } map[] = {{0x1000, 0x5800}, {0x2000, 0x5801}, {0x4000, 0x5802}, {0x8000, 0x5803}, {0x0200, 0x5804},
             {0x0100, 0x5805}, {0x0001, 0x5810}, {0x0002, 0x5811}, {0x0004, 0x5812}, {0x0008, 0x5813},
             {0x0010, 0x5814}, {0x0020, 0x5815}, {0x0040, 0x5816}, {0x0080, 0x5817}};
  const uint16_t now = ScriptButtons(f), before = f ? ScriptButtons(f - 1) : 0;
  if (now == before) return;
  std::lock_guard lk(g_key_mu);
  for (auto& m : map) {
    if ((now & m.bit) && !(before & m.bit)) g_keys.emplace_back(m.vk, 1);   // KEYDOWN
    if (!(now & m.bit) && (before & m.bit)) g_keys.emplace_back(m.vk, 2);   // KEYUP
  }
}

// Xenos 2D tiling (XGAddress2DTiledOffset): byte offset of block (x, y).
int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bpb_log2) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bpb_log2 + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

// The game's front buffer, read from guest memory (needs --readback_resolve=full
// so resolves reach guest memory). Written as a 24-bit BMP.
void CaptureFront(uint64_t frame) {
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  zoids::FrontBuffer fb;
  if (!zoids::LastFrontBuffer(fb)) return;
  const uint32_t w = fb.width, h = fb.height;
  if (!w || !h || w > 4096 || h > 4096) return;
  const bool tiled = fb.fetch[0] >> 31;
  const uint32_t pitch = ((fb.fetch[0] >> 22) & 0x1FF) << 5;
  const uint32_t format = fb.fetch[1] & 0x3F;
  const uint32_t endian = (fb.fetch[1] >> 6) & 3;
  const uint32_t addr = fb.fetch[1] & 0xFFFFF000u;
  const uint8_t* src = REX_KERNEL_MEMORY()->TranslatePhysical<const uint8_t*>(addr ? addr : fb.address);
  const int stride = ((w * 3 + 3) & ~3);
  std::vector<uint8_t> img(size_t(stride) * h);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const size_t off = tiled ? size_t(TiledOffset2D(x, y, pitch ? pitch : w, 2)) : (size_t(y) * (pitch ? pitch : w) + x) * 4;
      uint32_t v = *reinterpret_cast<const uint32_t*>(src + off);
      if (endian == 2) v = __builtin_bswap32(v);           // 8in32
      else if (endian == 1) v = ((v & 0x00FF00FF) << 8) | ((v >> 8) & 0x00FF00FF);  // 8in16
      else if (endian == 3) v = (v << 16) | (v >> 16);      // 16in32
      uint8_t r, g, b;
      if (format == 7) {  // 2_10_10_10
        r = uint8_t((v & 0x3FF) >> 2); g = uint8_t(((v >> 10) & 0x3FF) >> 2); b = uint8_t(((v >> 20) & 0x3FF) >> 2);
      } else {            // 8_8_8_8 (A8R8G8B8 / A8B8G8R8 decided by eye)
        r = uint8_t(v); g = uint8_t(v >> 8); b = uint8_t(v >> 16);
      }
      uint8_t* d = &img[size_t(h - 1 - y) * stride + x * 3];
      d[0] = b; d[1] = g; d[2] = r;
    }
  }
  const std::filesystem::path out =
      std::filesystem::path(REXCVAR_GET(zoids_shot_dir)) / ("shot_" + std::to_string(frame) + ".bmp");
  if (FILE* f = _wfopen(out.c_str(), L"wb")) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER bh{};
    bh.biSize = sizeof(bh);
    bh.biWidth = int(w);
    bh.biHeight = int(h);
    bh.biPlanes = 1;
    bh.biBitCount = 24;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bh);
    fh.bfSize = fh.bfOffBits + uint32_t(img.size());
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bh, sizeof(bh), 1, f);
    fwrite(img.data(), img.size(), 1, f);
    fclose(f);
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("zoids shot: front buffer {:08X} {}x{} format {} endian {} tiled {} pitch {}", addr, w, h, format,
                endian, tiled, pitch);
  }
}

}  // namespace

// XInputGetState(user, state) -> XamInputGetState(user, 1, state).
extern "C" REX_FUNC(sub_82293748) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, state = ctx.r4.u32;
  __imp__sub_82293748(ctx, base);
  std::call_once(g_load_once, LoadScript);
  if (g_script.empty() || user != 0 || !state) return;
  const uint64_t f = g_swaps.load(std::memory_order_relaxed);
  const bool connected = ctx.r3.u32 == 0;
  uint16_t buttons = connected ? REX_LOAD_U16(state + 4) : 0;
  uint8_t lt = connected ? REX_LOAD_U8(state + 6) : 0, rt = connected ? REX_LOAD_U8(state + 7) : 0;
  int16_t stick[4] = {};
  for (int s = 0; s < 4; ++s) stick[s] = connected ? static_cast<int16_t>(REX_LOAD_U16(state + 8 + 2 * s)) : 0;
  for (const auto& e : g_script) {
    if (f < e.frame || f >= e.frame + e.count) continue;
    buttons |= e.buttons;
    if (e.lt) lt = e.lt;
    if (e.rt) rt = e.rt;
    for (int s = 0; s < 4; ++s)
      if (e.has_stick[s]) stick[s] = e.stick[s];
  }
  static uint32_t packet = 1;
  static uint16_t last = 0xFFFF;
  if (buttons != last) {
    ++packet;
    last = buttons;
    REXLOG_INFO("zoids input: frame {} buttons {:04X} (pad {})", f, buttons, connected ? "connected" : "none");
  }
  REX_STORE_U32(state, packet);
  REX_STORE_U16(state + 4, buttons);
  REX_STORE_U8(state + 6, lt);
  REX_STORE_U8(state + 7, rt);
  for (int s = 0; s < 4; ++s) REX_STORE_U16(state + 8 + 2 * s, static_cast<uint16_t>(stick[s]));
  ctx.r3.u64 = 0;  // ERROR_SUCCESS: a pad is connected
}

// XInputGetKeystroke(user, reserved, keystroke) -> XamInputGetKeystrokeEx.
extern "C" REX_FUNC(__imp__sub_82293820);
extern "C" REX_FUNC(sub_82293820) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, ks = ctx.r5.u32;
  __imp__sub_82293820(ctx, base);
  if (g_script.empty() || ctx.r3.u32 == 0 || !ks || (user != 0 && user != 255)) return;
  std::lock_guard lk(g_key_mu);
  if (g_keys.empty()) return;
  const auto [vk, flags] = g_keys.front();
  g_keys.erase(g_keys.begin());
  REX_STORE_U16(ks + 0, vk);
  REX_STORE_U16(ks + 2, 0);
  REX_STORE_U16(ks + 4, flags);
  REX_STORE_U8(ks + 6, 0);
  REX_STORE_U8(ks + 7, 0);
  ctx.r3.u64 = 0;
  REXLOG_INFO("zoids input: keystroke {:04X} flags {}", vk, flags);
}

namespace zoids {
void AutotestFrame() {
  const uint64_t f = g_swaps.fetch_add(1, std::memory_order_relaxed) + 1;
  if (!g_script.empty()) QueueKeyEdges(f);
  const int every = REXCVAR_GET(zoids_shot_every);
  if (every > 0 && !REXCVAR_GET(zoids_shot_dir).empty() && f % every == 0)
    std::thread([f] { CaptureFront(f); }).detach();
}
uint64_t AutotestSwapCount() { return g_swaps.load(std::memory_order_relaxed); }
bool AutotestWantsFrames() { return REXCVAR_GET(zoids_shot_every) > 0 && !REXCVAR_GET(zoids_shot_dir).empty(); }
}  // namespace zoids
