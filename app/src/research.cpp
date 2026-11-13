// Research helpers for the frame-rate work. Off by default.
// --zoids_scan_consts: after default.xex loads, log the image addresses that
// hold the usual frame-time constants (1/30, 1/60, 30, 60 as float and double).
#include "research.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(zoids_scan_consts, false, "Zoids", "Research: log image addresses of frame-time constants");
REXCVAR_DEFINE_STRING(zoids_dump_image, "", "Zoids",
                      "Research: write the loaded image 0x82000000-0x82B10000 to this file (game-derived: keep it private)");

namespace zoids {

void ScanFrameConstants() {
  uint8_t* base = REX_KERNEL_MEMORY()->virtual_membase();
  constexpr uint32_t kStart = 0x82000000, kEnd = 0x82B10000;
  if (const std::string dump = REXCVAR_GET(zoids_dump_image); !dump.empty()) {
    if (FILE* f = std::fopen(dump.c_str(), "wb")) {
      std::fwrite(base + kStart, 1, kEnd - kStart, f);
      std::fclose(f);
      REXLOG_INFO("zoids research: image written to {}", dump);
    }
  }
  if (!REXCVAR_GET(zoids_scan_consts)) return;
  struct Pat {
    const char* name;
    std::vector<uint8_t> bytes;
  };
  const Pat pats[] = {
      {"f32 1/30", {0x3D, 0x08, 0x88, 0x89}},
      {"f32 1/60", {0x3C, 0x88, 0x88, 0x89}},
      {"f32 30.0", {0x41, 0xF0, 0x00, 0x00}},
      {"f32 60.0", {0x42, 0x70, 0x00, 0x00}},
      {"f64 1/30", {0x3F, 0xA1, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}},
      {"f64 1/60", {0x3F, 0x91, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}},
  };
  for (const auto& p : pats) {
    std::string hits;
    int n = 0;
    for (uint32_t a = kStart; a + p.bytes.size() <= kEnd; a += 4) {
      if (std::memcmp(base + a, p.bytes.data(), p.bytes.size()) == 0) {
        if (n < 60) hits += (hits.empty() ? "" : " ") + std::to_string(a);
        ++n;
      }
    }
    REXLOG_INFO("zoids scan: {} x{}: {}", p.name, n, hits);
  }
}

}  // namespace zoids
