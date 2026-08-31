#include "windows/cast_chrome_win.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows SDK requires windows.h before commctrl.h; keep this order stable.
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <algorithm>
#include <climits>
#include <map>
#include <utility>
#include <vector>

namespace crayon::browser::cef_shell::windows {
namespace {

using browser_cast_view::ReceiverOption;
using browser_chrome::CastButtonState;

constexpr wchar_t kPickerClass[] = L"CrayonCastReceiverPicker";
constexpr UINT_PTR kRootSubclassId = 0x435241594f4eULL;
constexpr int kCastButtonId = 0x5c01;
constexpr int kReceiverListId = 0x5c02;
constexpr int kSelectButtonId = 0x5c03;
constexpr int kRefreshButtonId = 0x5c04;
constexpr int kCancelButtonId = 0x5c05;

struct State;

struct Surface final {
  State* owner = nullptr;
  int browser_id = 0;
  HWND root = nullptr;
  HWND button = nullptr;
  HWND tooltip = nullptr;
  HWND picker = nullptr;
  HWND receiver_list = nullptr;
  HWND empty_label = nullptr;
  HWND select_button = nullptr;
  HWND refresh_button = nullptr;
  HWND cancel_button = nullptr;
  std::vector<ReceiverOption> receivers;
  CastButtonState state = CastButtonState::kHidden;
};

struct State final {
  CastChromeStrings strings;
  CastChromeCallbacks callbacks;
  std::map<int, Surface> surfaces;
  int active_browser_id = 0;
  bool closed = false;
};

std::wstring Text(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(std::min<std::size_t>(value.size(), INT_MAX)), nullptr,
      0);
  if (length <= 0) return L"\ufffd";
  std::wstring output(static_cast<std::size_t>(length), L'\0');
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), output.data(),
                             length) == length
             ? output
             : std::wstring(L"\ufffd");
}

int Scale(HWND window, int value) {
  const UINT dpi = window ? GetDpiForWindow(window) : USER_DEFAULT_SCREEN_DPI;
  return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

void SetFont(HWND control) {
  if (control) {
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
  }
}

void LayoutButton(Surface* surface) {
  if (!surface || !surface->root || !surface->button) return;
  RECT client{};
  if (!GetClientRect(surface->root, &client)) return;
  const int width = Scale(surface->root, 34);
  const int height = Scale(surface->root, 28);
  const int right_margin = Scale(surface->root, 148);
  const int top = Scale(surface->root, 7);
  SetWindowPos(
      surface->button, HWND_TOP,
      std::max(0, static_cast<int>(client.right) - right_margin - width), top,
      width, height, SWP_NOACTIVATE);
}

void DrawCastGlyph(const DRAWITEMSTRUCT& item) {
  RECT bounds = item.rcItem;
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  FillRect(item.hDC, &bounds, GetSysColorBrush(COLOR_BTNFACE));
  if (pressed) OffsetRect(&bounds, 1, 1);
  const COLORREF color = GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT);
  HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(item.hwndItem, 2)), color);
  HGDIOBJ old_pen = SelectObject(item.hDC, pen);
  HGDIOBJ old_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
  const int left = bounds.left + Scale(item.hwndItem, 8);
  const int top = bounds.top + Scale(item.hwndItem, 6);
  const int right = bounds.right - Scale(item.hwndItem, 7);
  const int bottom = bounds.bottom - Scale(item.hwndItem, 5);
  MoveToEx(item.hDC, left, top, nullptr);
  LineTo(item.hDC, right, top);
  LineTo(item.hDC, right, bottom);
  Arc(item.hDC, left - Scale(item.hwndItem, 8),
      bottom - Scale(item.hwndItem, 8), left + Scale(item.hwndItem, 9),
      bottom + Scale(item.hwndItem, 9), left, bottom - Scale(item.hwndItem, 8),
      left + Scale(item.hwndItem, 8), bottom);
  Arc(item.hDC, left - Scale(item.hwndItem, 3),
      bottom - Scale(item.hwndItem, 3), left + Scale(item.hwndItem, 4),
      bottom + Scale(item.hwndItem, 4), left, bottom - Scale(item.hwndItem, 3),
      left + Scale(item.hwndItem, 3), bottom);
  Ellipse(item.hDC, left - 1, bottom - 1, left + 2, bottom + 2);
  SelectObject(item.hDC, old_brush);
  SelectObject(item.hDC, old_pen);
  DeleteObject(pen);
  if ((item.itemState & ODS_FOCUS) != 0) DrawFocusRect(item.hDC, &bounds);
}

void HidePicker(Surface* surface) {
  if (surface && surface->picker) ShowWindow(surface->picker, SW_HIDE);
}

void UpdatePicker(Surface* surface) {
  if (!surface || !surface->receiver_list) return;
  SendMessageW(surface->receiver_list, LB_RESETCONTENT, 0, 0);
  for (const auto& receiver : surface->receivers) {
    const std::wstring name = Text(receiver.display_name);
    SendMessageW(surface->receiver_list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name.c_str()));
  }
  const bool has_receivers = !surface->receivers.empty();
  if (has_receivers) {
    SendMessageW(surface->receiver_list, LB_SETCURSEL, 0, 0);
  }
  ShowWindow(surface->empty_label, has_receivers ? SW_HIDE : SW_SHOWNA);
  EnableWindow(surface->select_button, has_receivers ? TRUE : FALSE);
}

void LayoutPicker(Surface* surface) {
  if (!surface || !surface->picker) return;
  RECT client{};
  GetClientRect(surface->picker, &client);
  const int margin = Scale(surface->picker, 12);
  const int button_width = Scale(surface->picker, 82);
  const int button_height = Scale(surface->picker, 28);
  const int gap = Scale(surface->picker, 8);
  const int bottom = client.bottom - margin - button_height;
  SetWindowPos(surface->receiver_list, nullptr, margin, margin,
               client.right - 2 * margin, bottom - margin - gap,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->empty_label, nullptr, margin, margin,
               client.right - 2 * margin, Scale(surface->picker, 44),
               SWP_NOZORDER | SWP_NOACTIVATE);
  const int start = client.right - margin - 3 * button_width - 2 * gap;
  SetWindowPos(surface->select_button, nullptr, start, bottom, button_width,
               button_height, SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->refresh_button, nullptr, start + button_width + gap,
               bottom, button_width, button_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->cancel_button, nullptr,
               start + 2 * (button_width + gap), bottom, button_width,
               button_height, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK PickerProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  Surface* surface =
      reinterpret_cast<Surface*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    surface = static_cast<Surface*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(surface));
  }
  if (!surface) return DefWindowProcW(window, message, wparam, lparam);
  switch (message) {
    case WM_SIZE:
      LayoutPicker(surface);
      return 0;
    case WM_CLOSE:
      if (surface->owner->callbacks.cancel) surface->owner->callbacks.cancel();
      HidePicker(surface);
      return 0;
    case WM_COMMAND: {
      const int command = LOWORD(wparam);
      if (command == kCancelButtonId) {
        if (surface->owner->callbacks.cancel)
          surface->owner->callbacks.cancel();
        HidePicker(surface);
        return 0;
      }
      if (command == kRefreshButtonId) {
        if (surface->owner->callbacks.refresh)
          static_cast<void>(surface->owner->callbacks.refresh());
        return 0;
      }
      if (command == kSelectButtonId ||
          (command == kReceiverListId && HIWORD(wparam) == LBN_DBLCLK)) {
        const LRESULT selected =
            SendMessageW(surface->receiver_list, LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR &&
            static_cast<std::size_t>(selected) < surface->receivers.size() &&
            surface->owner->callbacks.select) {
          static_cast<void>(surface->owner->callbacks.select(
              surface->receivers[static_cast<std::size_t>(selected)]
                  .device_id));
        }
        return 0;
      }
      break;
    }
    case WM_NCDESTROY:
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      surface->picker = nullptr;
      return DefWindowProcW(window, message, wparam, lparam);
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterPickerClass() {
  WNDCLASSEXW window_class{sizeof(WNDCLASSEXW)};
  window_class.lpfnWndProc = PickerProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  window_class.lpszClassName = kPickerClass;
  if (RegisterClassExW(&window_class)) return true;
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND Control(DWORD style, const wchar_t* class_name, const wchar_t* text,
             int id, HWND parent) {
  HWND control = CreateWindowExW(
      0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  SetFont(control);
  return control;
}

bool EnsurePicker(Surface* surface) {
  if (surface->picker) return true;
  if (!RegisterPickerClass()) return false;
  surface->picker = CreateWindowExW(
      WS_EX_TOOLWINDOW, kPickerClass,
      surface->owner->strings.picker_title.c_str(),
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT,
      CW_USEDEFAULT, Scale(surface->root, 430), Scale(surface->root, 300),
      surface->root, nullptr, GetModuleHandleW(nullptr), surface);
  if (!surface->picker) return false;
  surface->receiver_list =
      Control(WS_BORDER | WS_VSCROLL | LBS_NOTIFY, L"LISTBOX", L"",
              kReceiverListId, surface->picker);
  surface->empty_label =
      Control(SS_CENTER, L"STATIC",
              surface->owner->strings.picker_empty.c_str(), 0, surface->picker);
  surface->select_button =
      Control(BS_DEFPUSHBUTTON, L"BUTTON",
              surface->owner->strings.picker_select.c_str(), kSelectButtonId,
              surface->picker);
  surface->refresh_button = Control(
      BS_PUSHBUTTON, L"BUTTON", surface->owner->strings.picker_refresh.c_str(),
      kRefreshButtonId, surface->picker);
  surface->cancel_button = Control(
      BS_PUSHBUTTON, L"BUTTON", surface->owner->strings.picker_cancel.c_str(),
      kCancelButtonId, surface->picker);
  if (!surface->receiver_list || !surface->empty_label ||
      !surface->select_button || !surface->refresh_button ||
      !surface->cancel_button) {
    DestroyWindow(surface->picker);
    surface->picker = nullptr;
    return false;
  }
  LayoutPicker(surface);
  UpdatePicker(surface);
  return true;
}

void PresentPicker(Surface* surface) {
  if (!surface || !EnsurePicker(surface)) return;
  if (IsWindowVisible(surface->picker)) return;
  UpdatePicker(surface);
  ShowWindow(surface->picker, SW_SHOWNORMAL);
  SetForegroundWindow(surface->picker);
}

void Activate(Surface* surface) {
  if (!surface || !surface->owner || surface->owner->closed ||
      surface->browser_id != surface->owner->active_browser_id ||
      !surface->owner->callbacks.activate) {
    return;
  }
  const bool was_casting = surface->state == CastButtonState::kCasting;
  if (surface->owner->callbacks.activate() && !was_casting)
    PresentPicker(surface);
}

LRESULT CALLBACK RootSubclassProc(HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam, UINT_PTR subclass_id,
                                  DWORD_PTR reference) {
  auto* surface = reinterpret_cast<Surface*>(reference);
  switch (message) {
    case WM_COMMAND:
      if (surface && reinterpret_cast<HWND>(lparam) == surface->button &&
          HIWORD(wparam) == BN_CLICKED) {
        Activate(surface);
        return 0;
      }
      break;
    case WM_DRAWITEM:
      if (surface &&
          reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)->hwndItem ==
              surface->button) {
        DrawCastGlyph(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        return TRUE;
      }
      break;
    case WM_SIZE:
    case WM_DPICHANGED_AFTERPARENT:
      LayoutButton(surface);
      break;
    case WM_NCDESTROY:
      if (surface) {
        if (surface->picker) DestroyWindow(surface->picker);
        surface->root = nullptr;
        surface->button = nullptr;
        surface->tooltip = nullptr;
      }
      RemoveWindowSubclass(window, RootSubclassProc, subclass_id);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

bool CreateSurfaceControls(Surface* surface) {
  surface->button = CreateWindowExW(
      0, L"BUTTON", surface->owner->strings.button_select.c_str(),
      WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, surface->root,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCastButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (!surface->button) return false;
  SetFont(surface->button);
  surface->tooltip = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, surface->root, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!surface->tooltip) return false;
  TOOLINFOW info{sizeof(TOOLINFOW)};
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = surface->root;
  info.uId = reinterpret_cast<UINT_PTR>(surface->button);
  info.lpszText =
      const_cast<wchar_t*>(surface->owner->strings.button_select.c_str());
  SendMessageW(surface->tooltip, TTM_ADDTOOLW, 0,
               reinterpret_cast<LPARAM>(&info));
  if (!SetWindowSubclass(surface->root, RootSubclassProc, kRootSubclassId,
                         reinterpret_cast<DWORD_PTR>(surface))) {
    return false;
  }
  LayoutButton(surface);
  return true;
}

void UpdateTooltip(Surface* surface, const std::wstring& text) {
  SetWindowTextW(surface->button, text.c_str());
  TOOLINFOW info{sizeof(TOOLINFOW)};
  info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  info.hwnd = surface->root;
  info.uId = reinterpret_cast<UINT_PTR>(surface->button);
  info.lpszText = const_cast<wchar_t*>(text.c_str());
  SendMessageW(surface->tooltip, TTM_UPDATETIPTEXTW, 0,
               reinterpret_cast<LPARAM>(&info));
}

}  // namespace

struct CastChromeWin::Impl final {
  Impl(CastChromeStrings strings, CastChromeCallbacks callbacks)
      : state{std::move(strings), std::move(callbacks)} {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),
                                  ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
  }

  State state;
};

CastChromeWin::CastChromeWin(CastChromeStrings strings,
                             CastChromeCallbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(strings), std::move(callbacks))) {}

CastChromeWin::~CastChromeWin() { Close(); }

bool CastChromeWin::AttachWindow(int browser_id, void* native_window) {
  if (browser_id <= 0 || !native_window || impl_->state.closed) return false;
  if (impl_->state.surfaces.find(browser_id) != impl_->state.surfaces.end())
    return true;
  auto [found, inserted] = impl_->state.surfaces.emplace(browser_id, Surface{});
  Surface& surface = found->second;
  surface.owner = &impl_->state;
  surface.browser_id = browser_id;
  surface.root = static_cast<HWND>(native_window);
  if (!inserted || !IsWindow(surface.root) ||
      !CreateSurfaceControls(&surface)) {
    if (surface.tooltip) DestroyWindow(surface.tooltip);
    if (surface.button) DestroyWindow(surface.button);
    impl_->state.surfaces.erase(found);
    return false;
  }
  return true;
}

void CastChromeWin::DetachWindow(int browser_id) {
  auto found = impl_->state.surfaces.find(browser_id);
  if (found == impl_->state.surfaces.end()) return;
  Surface& surface = found->second;
  if (surface.root)
    RemoveWindowSubclass(surface.root, RootSubclassProc, kRootSubclassId);
  if (surface.picker) DestroyWindow(surface.picker);
  if (surface.tooltip) DestroyWindow(surface.tooltip);
  if (surface.button) DestroyWindow(surface.button);
  impl_->state.surfaces.erase(found);
  if (impl_->state.active_browser_id == browser_id)
    impl_->state.active_browser_id = 0;
}

void CastChromeWin::SetActiveWindow(int browser_id) {
  if (impl_->state.closed ||
      impl_->state.surfaces.find(browser_id) == impl_->state.surfaces.end()) {
    return;
  }
  impl_->state.active_browser_id = browser_id;
  for (auto& [id, surface] : impl_->state.surfaces) {
    if (id == browser_id) continue;
    ShowWindow(surface.button, SW_HIDE);
    HidePicker(&surface);
  }
}

void CastChromeWin::Render(
    const browser_cast_view::CastUiCoordinator& coordinator) {
  if (impl_->state.closed) return;
  for (auto& [id, surface] : impl_->state.surfaces) {
    const bool active = id == impl_->state.active_browser_id;
    surface.state = coordinator.button().state();
    if (surface.receivers != coordinator.receivers()) {
      surface.receivers = coordinator.receivers();
      UpdatePicker(&surface);
    }
    const bool visible = active && surface.state != CastButtonState::kHidden;
    ShowWindow(surface.button, visible ? SW_SHOWNA : SW_HIDE);
    EnableWindow(surface.button,
                 active && (surface.state == CastButtonState::kEligible ||
                            surface.state == CastButtonState::kCasting));
    const bool casting = surface.state == CastButtonState::kCasting;
    UpdateTooltip(&surface, casting ? impl_->state.strings.button_stop
                                    : impl_->state.strings.button_select);
    if (!active || surface.state != CastButtonState::kSelecting) {
      HidePicker(&surface);
    } else {
      PresentPicker(&surface);
    }
  }
}

void CastChromeWin::Close() {
  if (!impl_ || impl_->state.closed) return;
  impl_->state.closed = true;
  std::vector<int> browser_ids;
  browser_ids.reserve(impl_->state.surfaces.size());
  for (const auto& [browser_id, surface] : impl_->state.surfaces) {
    static_cast<void>(surface);
    browser_ids.push_back(browser_id);
  }
  for (int browser_id : browser_ids) DetachWindow(browser_id);
  impl_->state.callbacks = {};
}

}  // namespace crayon::browser::cef_shell::windows
