#include "windows/alloy_cast_overlay_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows SDK requires windows.h before commctrl.h.
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <utility>

namespace crayon::browser::cef_shell::windows {
namespace {

using browser_cast_view::CastOverlayBounds;

constexpr UINT_PTR kRootSubclassId = 0x435241594f56ULL;
constexpr double kMaximumViewportDip = 32768.0;
constexpr double kMinimumScale = 0.25;
constexpr double kMaximumScale = 8.0;

bool IsDescendant(HWND root, HWND child) {
  return root && child &&
         (root == child ||
          (IsChild(root, child) && GetAncestor(child, GA_ROOT) == root));
}

void Hide(HWND window) {
  if (window) ShowWindow(window, SW_HIDE);
}

}  // namespace

struct AlloyCastOverlayWin::Impl final {
  struct Control final {
    HWND button = nullptr;
    HWND tooltip = nullptr;
    AlloyCastOverlayObservation observation;
  };

  Impl(std::wstring name, Clock now, Activate callback)
      : accessible_name(std::move(name)),
        clock(std::move(now)),
        activate(std::move(callback)) {}

  static LRESULT CALLBACK RootProc(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam, UINT_PTR,
                                  DWORD_PTR reference) {
    auto* self = reinterpret_cast<Impl*>(reference);
    if (!self) return DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
      const int id = LOWORD(wparam);
      if (id >= kFirstControlId &&
          id < kFirstControlId +
                   static_cast<int>(browser_cast_view::kCastSelectionPageSize)) {
        self->ActivateControl(static_cast<std::size_t>(id - kFirstControlId),
                              reinterpret_cast<HWND>(lparam));
        return 0;
      }
    } else if (message == WM_ACTIVATE) {
      self->window_active = LOWORD(wparam) != WA_INACTIVE;
      self->Render();
    } else if (message == WM_SIZE || message == WM_DPICHANGED ||
               message == WM_WINDOWPOSCHANGED) {
      self->Render();
    } else if (message == WM_NCDESTROY) {
      self->root = nullptr;
      self->browser = nullptr;
      for (auto& control : self->controls) {
        control.button = nullptr;
        if (control.tooltip) DestroyWindow(control.tooltip);
        control.tooltip = nullptr;
      }
      self->controls.clear();
      self->observations.clear();
      self->visible = 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
  }

  bool Attach(HWND root_window, HWND browser_window) {
    if (root || !IsWindow(root_window) || !IsWindow(browser_window) ||
        !IsDescendant(root_window, browser_window) || accessible_name.empty() ||
        !clock || !activate) {
      return false;
    }
    INITCOMMONCONTROLSEX controls_init{sizeof(controls_init), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls_init) ||
        !SetWindowSubclass(root_window, RootProc, kRootSubclassId,
                           reinterpret_cast<DWORD_PTR>(this))) {
      return false;
    }
    root = root_window;
    browser = browser_window;
    owner_thread = GetCurrentThreadId();
    window_active = GetForegroundWindow() == root || GetActiveWindow() == root;
    return true;
  }

  bool OnOwnerThread() const {
    return owner_thread != 0 && GetCurrentThreadId() == owner_thread;
  }

  bool EnsureControl(std::size_t index) {
    if (index >= browser_cast_view::kCastSelectionPageSize || !root) return false;
    while (controls.size() <= index) controls.emplace_back();
    auto& control = controls[index];
    if (control.button && IsWindow(control.button)) return true;
    control.button = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY, L"BUTTON", accessible_name.c_str(),
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, root,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kFirstControlId + static_cast<int>(index))),
        GetModuleHandleW(nullptr), nullptr);
    if (!control.button) return false;
    SendMessageW(control.button, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
    control.tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, root, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    if (!control.tooltip) {
      DestroyWindow(control.button);
      control.button = nullptr;
      return false;
    }
    TOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = root;
    info.uId = reinterpret_cast<UINT_PTR>(control.button);
    info.lpszText = accessible_name.data();
    SendMessageW(control.tooltip, TTM_ADDTOOLW, 0,
                 reinterpret_cast<LPARAM>(&info));
    return true;
  }

  void HideAll() {
    for (auto& control : controls) Hide(control.button);
    visible = 0;
  }

  bool Place(const AlloyCastOverlayObservation& value,
             CastOverlayBounds* result) const {
    if (!result || !std::isfinite(value.viewport_width) ||
        !std::isfinite(value.viewport_height) || value.viewport_width <= 0 ||
        value.viewport_height <= 0 ||
        value.viewport_width > kMaximumViewportDip ||
        value.viewport_height > kMaximumViewportDip) {
      return false;
    }
    const auto placed = presentation.PlaceOverlay(
        value.anchor, static_cast<int>(std::ceil(value.viewport_width)),
        static_cast<int>(std::ceil(value.viewport_height)), clock());
    if (!placed) return false;
    *result = *placed;
    return true;
  }

  void Render() {
    HideAll();
    if (!OnOwnerThread() || !root || !browser || !window_active || dispatching ||
        presentation.PickerVisible() ||
        !IsWindowVisible(root) || !IsWindowVisible(browser) ||
        observations.empty() ||
        observations.size() > browser_cast_view::kCastSelectionPageSize) {
      return;
    }
    RECT browser_client{};
    if (!GetClientRect(browser, &browser_client) || browser_client.right <= 0 ||
        browser_client.bottom <= 0) {
      return;
    }
    POINT browser_origin{};
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(browser, root, &browser_origin, 1)) {
      const DWORD error = GetLastError();
      if (error != ERROR_SUCCESS) return;
    }
    for (std::size_t index = 0; index < observations.size(); ++index) {
      const auto& value = observations[index];
      for (std::size_t prior = 0; prior < index; ++prior) {
        if (observations[prior].anchor.media == value.anchor.media) {
          HideAll();
          return;
        }
      }
      CastOverlayBounds bounds{};
      if (!Place(value, &bounds)) {
        HideAll();
        return;
      }
      const double scale_x = browser_client.right / value.viewport_width;
      const double scale_y = browser_client.bottom / value.viewport_height;
      if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
          scale_x < kMinimumScale || scale_x > kMaximumScale ||
          scale_y < kMinimumScale || scale_y > kMaximumScale ||
          !EnsureControl(index)) {
        HideAll();
        return;
      }
      auto& control = controls[index];
      control.observation = value;
      const int x = browser_origin.x +
                    static_cast<int>(std::lround(bounds.x * scale_x));
      const int y = browser_origin.y +
                    static_cast<int>(std::lround(bounds.y * scale_y));
      const int width = std::max(1, static_cast<int>(std::lround(
                                        bounds.width * scale_x)));
      const int height = std::max(1, static_cast<int>(std::lround(
                                         bounds.height * scale_y)));
      if (!SetWindowPos(control.button, HWND_TOP, x, y, width, height,
                        SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        HideAll();
        return;
      }
      ++visible;
    }
  }

  void ActivateControl(std::size_t index, HWND source) {
    if (!OnOwnerThread() || dispatching || index >= controls.size() ||
        controls[index].button != source || !IsWindowVisible(source)) {
      return;
    }
    CastOverlayBounds ignored{};
    if (!window_active || !Place(controls[index].observation, &ignored)) {
      HideAll();
      return;
    }
    dispatching = true;
    const bool accepted = activate(controls[index].observation.anchor.media);
    dispatching = false;
    if (accepted) {
      observations.clear();
      HideAll();
    } else {
      Render();
    }
  }

  void Detach() {
    if (!root) return;
    if (!OnOwnerThread()) return;
    RemoveWindowSubclass(root, RootProc, kRootSubclassId);
    for (auto& control : controls) {
      if (control.tooltip) DestroyWindow(control.tooltip);
      if (control.button) DestroyWindow(control.button);
    }
    controls.clear();
    observations.clear();
    presentation.Clear();
    visible = 0;
    browser = nullptr;
    root = nullptr;
    owner_thread = 0;
    activate = {};
    clock = {};
  }

  std::wstring accessible_name;
  Clock clock;
  Activate activate;
  browser_cast_view::CastSelectionPresentation presentation;
  std::vector<AlloyCastOverlayObservation> observations;
  std::vector<Control> controls;
  HWND root = nullptr;
  HWND browser = nullptr;
  DWORD owner_thread = 0;
  std::size_t visible = 0;
  bool window_active = false;
  bool dispatching = false;
};

AlloyCastOverlayWin::AlloyCastOverlayWin(std::wstring accessible_name,
                                         Clock clock, Activate activate)
    : impl_(std::make_unique<Impl>(std::move(accessible_name),
                                   std::move(clock), std::move(activate))) {}

AlloyCastOverlayWin::~AlloyCastOverlayWin() { Detach(); }

bool AlloyCastOverlayWin::Attach(void* root_window, void* browser_window) {
  return impl_->Attach(static_cast<HWND>(root_window),
                       static_cast<HWND>(browser_window));
}

void AlloyCastOverlayWin::BindContext(
    browser_cast_view::CastViewContext context) {
  if (!impl_->OnOwnerThread()) return;
  impl_->observations.clear();
  impl_->HideAll();
  impl_->presentation.BindContext(std::move(context));
}

bool AlloyCastOverlayWin::Apply(
    browser_cast_view::CastSelectionSnapshot snapshot) {
  if (!impl_->OnOwnerThread()) return false;
  const bool accepted = impl_->presentation.Apply(std::move(snapshot));
  impl_->Render();
  return accepted;
}

void AlloyCastOverlayWin::SetObservations(
    std::vector<AlloyCastOverlayObservation> observations) {
  if (!impl_->OnOwnerThread()) return;
  impl_->observations = std::move(observations);
  impl_->Render();
}

void AlloyCastOverlayWin::Tick() {
  if (impl_->OnOwnerThread()) impl_->Render();
}

void AlloyCastOverlayWin::Invalidate() {
  if (!impl_->OnOwnerThread()) return;
  impl_->observations.clear();
  impl_->HideAll();
}

void AlloyCastOverlayWin::Detach() { impl_->Detach(); }

std::size_t AlloyCastOverlayWin::visible_count() const noexcept {
  return impl_->visible;
}

}  // namespace crayon::browser::cef_shell::windows
