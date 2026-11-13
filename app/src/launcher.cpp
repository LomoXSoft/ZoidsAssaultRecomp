// Startup launcher: display settings, shown before the runtime starts.
//
// Config: zoids_settings.cfg next to the executable (key=value lines).
// The window is shown when there is no config, when ask=1, on the first run of
// a new version, when Shift is held at launch, or with --launcher. A value given
// on the command line (or in a toml / REX_ environment variable) wins over it.
#include "launcher.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cctype>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>

#ifndef ZOIDS_VERSION
#define ZOIDS_VERSION "dev"
#endif
#define ZOIDS_W2(x) L##x
#define ZOIDS_W(x) ZOIDS_W2(x)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(launcher, false, "Zoids", "Show the launcher at startup");

namespace zoids {
namespace {

// Display: 0 borderless fullscreen (default), 1 window, 2 exclusive fullscreen.
struct Options {
  int display = 0;
  int scale = 1;  // internal resolution: 1x, 2x, 3x
  int vsync = 1;  // 1 on, 0 off
};

std::filesystem::path ConfigPath() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n && n < MAX_PATH) return std::filesystem::path(buf).parent_path() / "zoids_settings.cfg";
#endif
  return "zoids_settings.cfg";
}

// GPU-plugin cvars (rexgpu-xenos.dll) are registered only when the plugin is
// loaded, after this runs, and SetFlagByName rejects unknown names. ReXGlue
// applies REX_<NAME> environment variables when such a late cvar registers.
void SetCvar(const char* name, const std::string& value) {
  if (rex::cvar::SetFlagByName(name, value)) {
    REXLOG_INFO("zoids launcher: cvar {}={} (applied)", name, value);
    return;
  }
  std::string env = "REX_";
  for (const char* c = name; *c; ++c) env += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
#ifdef _WIN32
  _putenv_s(env.c_str(), value.c_str());
#else
  setenv(env.c_str(), value.c_str(), 1);
#endif
  REXLOG_INFO("zoids launcher: cvar {}={} (deferred via {})", name, value, env);
}

// True when the user set this cvar outside the launcher (command line, toml).
bool Explicit(const char* name) {
  for (auto& f : rex::cvar::GetRegistry())
    if (f.name == name && f.source != rex::cvar::Source::kDefault) return true;
  std::string env = "REX_";
  for (const char* c = name; *c; ++c) env += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
#ifdef _WIN32
  char* v = nullptr;
  size_t len = 0;
  const bool set = _dupenv_s(&v, &len, env.c_str()) == 0 && v && *v;
  std::free(v);
  return set;
#else
  const char* v = std::getenv(env.c_str());
  return v && *v;
#endif
}

#ifdef _WIN32
enum : int {
  kIdDisplay = 101,
  kIdScale,
  kIdVsync,
  kIdAsk,
  kIdHint,
};

struct State {
  Options opt;
  bool ask = true;
  bool play = false;
  bool done = false;
};

HFONT UiFont() {
  static HFONT f = [] {
    NONCLIENTMETRICSW m{sizeof m};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    return CreateFontIndirectW(&m.lfMessageFont);
  }();
  return f;
}

int Cur(HWND h, int id) { return static_cast<int>(SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0)); }

void UpdateHint(HWND h) {
  // Internal resolution multiplies the game's 1280x720 render targets.
  static const wchar_t* kHint[] = {
      L"1x is the original 1280x720. Higher values look sharper and need a faster GPU.",
      L"2x renders at 2560x1440. Needs a mid-range GPU or better.",
      L"3x renders at 3840x2160. Needs a fast GPU.",
  };
  SetDlgItemTextW(h, kIdHint, kHint[std::clamp(Cur(h, kIdScale), 0, 2)]);
}

LRESULT CALLBACK LauncherProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  switch (m) {
    case WM_COMMAND:
      if (LOWORD(w) == kIdScale && HIWORD(w) == CBN_SELCHANGE) {
        UpdateHint(h);
      } else if (LOWORD(w) == IDOK) {
        st->opt.display = std::clamp(Cur(h, kIdDisplay), 0, 2);
        st->opt.scale = std::clamp(Cur(h, kIdScale), 0, 2) + 1;
        st->opt.vsync = Cur(h, kIdVsync) == 1 ? 0 : 1;
        st->ask = IsDlgButtonChecked(h, kIdAsk) == BST_CHECKED;
        st->play = true;
        st->done = true;
        DestroyWindow(h);
      } else if (LOWORD(w) == IDCANCEL) {  // Quit or Esc
        PostMessageW(h, WM_CLOSE, 0, 0);
      }
      return 0;
    case WM_CLOSE:
      st->done = true;
      DestroyWindow(h);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// Returns false when the window was closed without Play.
bool ShowLauncher(Options& opt, bool& ask) {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = LauncherProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.lpszClassName = L"ZoidsLauncher";
  RegisterClassW(&wc);
  const double sc = GetDpiForSystem() / 96.0;
  auto S = [&](int v) { return static_cast<int>(v * sc + 0.5); };
  const DWORD style = WS_CAPTION | WS_SYSMENU;
  RECT rc{0, 0, S(400), S(258)};
  AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
  const int W = rc.right - rc.left, H = rc.bottom - rc.top;
  const int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
  const int y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
  HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, wc.lpszClassName,
                           L"Zoids Assault Recomp " ZOIDS_W(ZOIDS_VERSION), style | WS_VISIBLE, x, y, W,
                           H, nullptr, nullptr, inst, nullptr);
  if (!h) return true;  // No window: start the game with the saved settings.
  State st;
  st.opt = opt;
  st.ask = ask;
  SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));
  auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD s, int cx, int cy, int cw, int ch, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | s, S(cx), S(cy), S(cw), S(ch), h,
                             reinterpret_cast<HMENU>(INT_PTR(id)), inst, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(UiFont()), TRUE);
    return c;
  };
  auto combo = [&](int row, const wchar_t* label, int id, std::initializer_list<const wchar_t*> items, int sel) {
    const int cy = 20 + row * 36;
    mk(L"STATIC", label, SS_CENTERIMAGE, 20, cy, 140, 26, 0);
    HWND c = mk(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 170, cy, 210, 200, id);
    for (const wchar_t* it : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it));
    SendMessageW(c, CB_SETCURSEL, sel, 0);
  };
  combo(0, L"Display:", kIdDisplay, {L"Borderless fullscreen", L"Window", L"Exclusive fullscreen"},
        std::clamp(opt.display, 0, 2));
  combo(1, L"Internal resolution:", kIdScale, {L"1x (1280x720)", L"2x (2560x1440)", L"3x (3840x2160)"},
        std::clamp(opt.scale, 1, 3) - 1);
  combo(2, L"VSync:", kIdVsync, {L"On", L"Off"}, opt.vsync ? 0 : 1);
  mk(L"STATIC", L"", SS_LEFT, 20, 128, 360, 36, kIdHint);
  HWND askbox = mk(L"BUTTON", L"Show this window at every start (or hold Shift)",
                   WS_TABSTOP | BS_AUTOCHECKBOX, 20, 170, 360, 24, kIdAsk);
  SendMessageW(askbox, BM_SETCHECK, ask ? BST_CHECKED : BST_UNCHECKED, 0);
  HWND play = mk(L"BUTTON", L"Play", WS_TABSTOP | BS_DEFPUSHBUTTON, 170, 212, 120, 32, IDOK);
  mk(L"BUTTON", L"Quit", WS_TABSTOP, 300, 212, 80, 32, IDCANCEL);
  UpdateHint(h);
  SetForegroundWindow(h);
  SetFocus(play);
  MSG msg;
  while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  UnregisterClassW(wc.lpszClassName, inst);
  if (!st.play) return false;
  opt = st.opt;
  ask = st.ask;
  return true;
}
#endif

}  // namespace

void ApplyLauncherSettings() {
  const auto path = ConfigPath();
  Options opt;
  bool ask = true;
  bool have_config = false;
  std::string seen_version;
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      have_config = true;
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      const auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
      const int n = std::atoi(v.c_str());
      if (k == "display") opt.display = std::clamp(n, 0, 2);
      else if (k == "scale") opt.scale = std::clamp(n, 1, 3);
      else if (k == "vsync") opt.vsync = n ? 1 : 0;
      else if (k == "ask") ask = n != 0;
      else if (k == "version") seen_version = v;
    }
  }
  // A new version may bring new options: show the launcher once, keeping the tick.
  bool show = !have_config || ask || seen_version != ZOIDS_VERSION || REXCVAR_GET(launcher);
#ifdef _WIN32
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) show = true;
  if (show) {
    if (!ShowLauncher(opt, ask)) {
      REXLOG_INFO("zoids launcher: closed, not starting the game");
      ExitProcess(0);
    }
    std::ofstream out(path, std::ios::trunc);
    out << "display=" << opt.display << "\nscale=" << opt.scale << "\nvsync=" << opt.vsync
        << "\nask=" << (ask ? 1 : 0) << "\nversion=" << ZOIDS_VERSION << "\n";
  }
#else
  (void)show;
#endif
  REXLOG_INFO("zoids launcher: display={} scale={} vsync={} shown={} ({})", opt.display, opt.scale,
              opt.vsync, show, path.string());
  if (!Explicit("fullscreen")) SetCvar("fullscreen", opt.display == 1 ? "false" : "true");
  if (!Explicit("fullscreen_exclusive")) SetCvar("fullscreen_exclusive", opt.display == 2 ? "true" : "false");
  if (!Explicit("resolution_scale") && opt.scale > 1) SetCvar("resolution_scale", std::to_string(opt.scale));
  if (!Explicit("vsync") && !opt.vsync) SetCvar("vsync", "false");
}

void LogDisplaySettings() {
  REXLOG_INFO("zoids display: fullscreen={} fullscreen_exclusive={} resolution_scale={} vsync={}",
              rex::cvar::GetFlagByName("fullscreen"), rex::cvar::GetFlagByName("fullscreen_exclusive"),
              rex::cvar::GetFlagByName("resolution_scale"), rex::cvar::GetFlagByName("vsync"));
}

}  // namespace zoids
