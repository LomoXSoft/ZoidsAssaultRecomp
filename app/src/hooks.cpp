// Per-title fixes for Zoids Assault.
//
// 1) XamUserGetName: the SDK returns X_E_NO_SUCH_USER (HRESULT 0x80070525)
//    for unsigned-in slots. This 2008 title treats anything other than
//    ERROR_SUCCESS (0) or ERROR_NO_SUCH_USER (1317) as fatal and exits with
//    E_FAIL before finishing XAPI init.
// 2) sub_82201528: unlinks an object from the manager at 0x824F9E58. That
//    manager is constructed later; skip the unlink if it does not exist yet
//    (this is shutdown after a failed init, which used to be the AV at 0x4).
//
// Other fixes live in zoidsassault_manifest.toml (function hints, and the
// setjmp/longjmp pair at 0x823E81A0 / 0x823E7C30 used by the image decoder).

#include "generated/default/zoidsassault_pch.h"

#include <algorithm>

#include <rex/hook.h>
#include <rex/string.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xtypes.h>

extern "C" REX_FUNC(__imp__sub_82201528);

using namespace rex;

u32 XamUserGetName_entry(u32 user_index, mapped_string buffer, u32 buffer_len) {
  if (user_index >= 4) {
    return X_ERROR_INVALID_PARAMETER;
  }
  if (user_index != 0) {
    // Win32 ERROR_NO_SUCH_USER (1317), not the HRESULT form.
    return X_ERROR_NO_SUCH_USER;
  }
  auto* ks = REX_KERNEL_STATE();
  if (!ks || !ks->user_profile()) {
    return X_ERROR_NO_SUCH_USER;
  }
  const auto user_name = ks->user_profile()->name();
  rex::string::copy_truncating(buffer, user_name, std::min(buffer_len, uint32_t(16)));
  return X_ERROR_SUCCESS;
}

REX_HOOK(__imp__XamUserGetName, XamUserGetName_entry)

extern "C" REX_FUNC(sub_82201528) {
  REX_FUNC_PROLOGUE();
  const uint32_t kManagerPtr = 0x824F9E58;
  if (REX_LOAD_U32(kManagerPtr) == 0) {
    return;  // manager not constructed yet
  }
  __imp__sub_82201528(ctx, base);
}
