// Save-game call log: wraps the game's small XAPI wrappers around the Xam
// content functions and logs their arguments and results to zoids.log.
// These calls happen only when the game looks for, creates or opens a save,
// so the log stays short. --zoids_save_log=false turns it off.
#include "generated/default/zoidsassault_pch.h"

#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(zoids_save_log, true, "Zoids", "Log the game's save-game (Xam content) calls");

extern "C" REX_FUNC(__imp__sub_822934C8);  // XamContentCreateEnumerator
extern "C" REX_FUNC(__imp__sub_82293728);  // XEnumerate -> XamEnumerate
extern "C" REX_FUNC(__imp__sub_82293428);  // XamContentCreateEx
extern "C" REX_FUNC(__imp__sub_822934C0);  // XamContentGetCreator
extern "C" REX_FUNC(__imp__sub_82293AA8);  // XamShowDeviceSelectorUI
extern "C" REX_FUNC(__imp__sub_822934D0);  // XamContentGetDeviceState
extern "C" REX_FUNC(__imp__sub_822934D8);  // XamContentGetDeviceData
extern "C" REX_FUNC(__imp__sub_82293398);  // XamUserGetSigninState
extern "C" REX_FUNC(__imp__sub_822934B0);  // XamContentClose

namespace {

std::string GuestString(uint8_t* base, uint32_t addr, size_t max = 64) {
  if (!addr) return "(null)";
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    const char c = static_cast<char>(base[addr + i]);
    if (!c) break;
    s += (c >= 32 && c < 127) ? c : '?';
  }
  return s;
}

// XCONTENT_DATA: device id, content type, display name (wchar[128]), file name (char[42]).
std::string ContentData(uint8_t* base, uint32_t addr) {
  if (!addr) return "(null)";
  const uint32_t device = REX_LOAD_U32(addr);
  const uint32_t type = REX_LOAD_U32(addr + 4);
  return "device=" + std::to_string(device) + " type=" + std::to_string(type) + " file=\"" +
         GuestString(base, addr + 8 + 256, 42) + "\"";
}

bool On() { return REXCVAR_GET(zoids_save_log); }

}  // namespace

extern "C" REX_FUNC(sub_822934C8) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, device = ctx.r4.u32, type = ctx.r5.u32, flags = ctx.r6.u32, items = ctx.r7.u32;
  const uint32_t handle_ptr = ctx.r9.u32;
  __imp__sub_822934C8(ctx, base);
  if (On())
    REXLOG_INFO("zoids save: XamContentCreateEnumerator(user={} device={} type={} flags={:X} items={}) -> {:08X} handle={:08X}",
                user, device, type, flags, items, ctx.r3.u32, handle_ptr ? REX_LOAD_U32(handle_ptr) : 0);
}

extern "C" REX_FUNC(sub_82293728) {
  REX_FUNC_PROLOGUE();
  // XEnumerate(handle, buffer, size, items_returned_ptr, overlapped): the wrapper
  // inserts the flags argument itself before calling XamEnumerate.
  const uint32_t handle = ctx.r3.u32, buffer = ctx.r4.u32, returned_ptr = ctx.r6.u32;
  __imp__sub_82293728(ctx, base);
  if (On()) {
    const uint32_t n = returned_ptr ? REX_LOAD_U32(returned_ptr) : 0;
    REXLOG_INFO("zoids save: XamEnumerate(handle={:08X}) -> {:08X} items={}{}", handle, ctx.r3.u32, n,
                (n && buffer) ? " first: " + ContentData(base, buffer) : std::string());
  }
}

extern "C" REX_FUNC(sub_82293428) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, root = ctx.r4.u32, data = ctx.r5.u32, flags = ctx.r6.u32;
  const std::string root_s = On() ? GuestString(base, root) : std::string();
  const std::string data_s = On() ? ContentData(base, data) : std::string();
  __imp__sub_82293428(ctx, base);
  if (On())
    REXLOG_INFO("zoids save: XamContentCreateEx(user={} root=\"{}\" {} flags={:X}) -> {:08X}", user, root_s, data_s,
                flags, ctx.r3.u32);
}

extern "C" REX_FUNC(sub_822934C0) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, data = ctx.r4.u32, is_creator_ptr = ctx.r5.u32;
  const std::string data_s = On() ? ContentData(base, data) : std::string();
  __imp__sub_822934C0(ctx, base);
  if (On())
    REXLOG_INFO("zoids save: XamContentGetCreator(user={} {}) -> {:08X} is_creator={}", user, data_s, ctx.r3.u32,
                is_creator_ptr ? REX_LOAD_U32(is_creator_ptr) : 0);
}

extern "C" REX_FUNC(sub_82293AA8) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32, type = ctx.r4.u32;
  __imp__sub_82293AA8(ctx, base);
  if (On()) REXLOG_INFO("zoids save: XamShowDeviceSelectorUI(user={} type={}) -> {:08X}", user, type, ctx.r3.u32);
}

extern "C" REX_FUNC(sub_822934D0) {
  REX_FUNC_PROLOGUE();
  const uint32_t device = ctx.r3.u32;
  __imp__sub_822934D0(ctx, base);
  if (On()) REXLOG_INFO("zoids save: XamContentGetDeviceState(device={}) -> {:08X}", device, ctx.r3.u32);
}

extern "C" REX_FUNC(sub_822934D8) {
  REX_FUNC_PROLOGUE();
  const uint32_t device = ctx.r3.u32;
  __imp__sub_822934D8(ctx, base);
  if (On()) REXLOG_INFO("zoids save: XamContentGetDeviceData(device={}) -> {:08X}", device, ctx.r3.u32);
}

extern "C" REX_FUNC(sub_82293398) {
  REX_FUNC_PROLOGUE();
  const uint32_t user = ctx.r3.u32;
  __imp__sub_82293398(ctx, base);
  static uint32_t last = 0xFFFFFFFF;  // signin state is polled: log changes only
  if (On() && ctx.r3.u32 != last) {
    last = ctx.r3.u32;
    REXLOG_INFO("zoids save: XamUserGetSigninState(user={}) -> {}", user, ctx.r3.u32);
  }
}

extern "C" REX_FUNC(sub_822934B0) {
  REX_FUNC_PROLOGUE();
  const uint32_t root = ctx.r3.u32;
  const std::string root_s = On() ? GuestString(base, root) : std::string();
  __imp__sub_822934B0(ctx, base);
  if (On()) REXLOG_INFO("zoids save: XamContentClose(root=\"{}\") -> {:08X}", root_s, ctx.r3.u32);
}
