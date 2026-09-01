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
#include <charconv>
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
constexpr int kCastCodeEditId = 0x5c06;
constexpr int kCastCodeConnectId = 0x5c07;
constexpr int kPauseButtonId = 0x5c08;
constexpr int kSeekEditId = 0x5c09;
constexpr int kSeekButtonId = 0x5c0a;
constexpr WPARAM kMaxCastCodeLength = 32;
constexpr std::uint64_t kMaxSeekSeconds = 7ULL * 24 * 60 * 60;

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
  HWND cast_code_label = nullptr;
  HWND cast_code_edit = nullptr;
  HWND cast_code_connect = nullptr;
  HWND status_label = nullptr;
  HWND select_button = nullptr;
  HWND refresh_button = nullptr;
  HWND cancel_button = nullptr;
  HWND pause_button = nullptr;
  HWND seek_edit = nullptr;
  HWND seek_button = nullptr;
  std::vector<ReceiverOption> receivers;
  CastButtonState state = CastButtonState::kHidden;
  CastChromePresentation presentation;
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

std::optional<std::string> Utf8(HWND control) {
  if (!control) return std::nullopt;
  const int length = GetWindowTextLengthW(control);
  if (length <= 0) return std::nullopt;
  std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
  const int copied =
      GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
  if (copied <= 0) return std::nullopt;
  value.resize(static_cast<std::size_t>(copied));
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        value.data(), copied, nullptr, 0,
                                        nullptr, nullptr);
  if (bytes <= 0) return std::nullopt;
  std::string output(static_cast<std::size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), copied,
                          output.data(), bytes, nullptr, nullptr) != bytes) {
    return std::nullopt;
  }
  return output;
}

std::optional<std::uint64_t> SeekSeconds(HWND control) {
  const auto text = Utf8(control);
  if (!text || text->empty()) return std::nullopt;
  std::uint64_t value = 0;
  const auto parsed =
      std::from_chars(text->data(), text->data() + text->size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() ||
      value > kMaxSeekSeconds) {
    return std::nullopt;
  }
  return value;
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
  const int gap = Scale(surface->root, 4);
  const int seek_width = Scale(surface->root, 52);
  const int action_width = Scale(surface->root, 62);
  const int controls_right =
      std::max(0, static_cast<int>(client.right) - right_margin - width - gap);
  SetWindowPos(surface->seek_button, HWND_TOP,
               controls_right - action_width, top, action_width, height,
               SWP_NOACTIVATE);
  SetWindowPos(surface->seek_edit, HWND_TOP,
               controls_right - action_width - gap - seek_width, top,
               seek_width, height, SWP_NOACTIVATE);
  SetWindowPos(surface->pause_button, HWND_TOP,
               controls_right - action_width - gap - seek_width - gap -
                   action_width,
               top, action_width, height, SWP_NOACTIVATE);
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
  const auto& presentation = surface->presentation;
  EnableWindow(surface->cast_code_edit,
               presentation.cast_code_pending ? FALSE : TRUE);
  EnableWindow(surface->cast_code_connect,
               presentation.cast_code_pending ? FALSE : TRUE);
  const wchar_t* status = presentation.cast_code_failed
                              ? surface->owner->strings.cast_code_failed.c_str()
                          : presentation.cast_code_pending
                              ? surface->owner->strings.cast_code_connect.c_str()
                              : L"";
  SetWindowTextW(surface->status_label, status);
}

void LayoutPicker(Surface* surface) {
  if (!surface || !surface->picker) return;
  RECT client{};
  GetClientRect(surface->picker, &client);
  const int margin = Scale(surface->picker, 12);
  const int button_width = Scale(surface->picker, 82);
  const int button_height = Scale(surface->picker, 28);
  const int gap = Scale(surface->picker, 8);
  const int code_label_width = Scale(surface->picker, 62);
  const int connect_width = Scale(surface->picker, 82);
  const int code_height = Scale(surface->picker, 24);
  const int bottom = client.bottom - margin - button_height;
  SetWindowPos(surface->cast_code_label, nullptr, margin, margin,
               code_label_width, code_height, SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->cast_code_edit, nullptr, margin + code_label_width,
               margin, client.right - 3 * margin - code_label_width -
                           connect_width,
               code_height, SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->cast_code_connect, nullptr,
               client.right - margin - connect_width, margin, connect_width,
               code_height, SWP_NOZORDER | SWP_NOACTIVATE);
  const int status_top = margin + code_height + gap;
  SetWindowPos(surface->status_label, nullptr, margin, status_top,
               client.right - 2 * margin, code_height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  const int list_top = status_top + code_height;
  SetWindowPos(surface->receiver_list, nullptr, margin, list_top,
               client.right - 2 * margin, bottom - list_top - gap,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(surface->empty_label, nullptr, margin, list_top,
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
      if (command == kCastCodeConnectId) {
        const auto code = Utf8(surface->cast_code_edit);
        if (code && surface->owner->callbacks.connect_cast_code) {
          static_cast<void>(
              surface->owner->callbacks.connect_cast_code(*code));
        }
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
  surface->cast_code_label =
      Control(SS_LEFT, L"STATIC",
              surface->owner->strings.cast_code_label.c_str(), 0,
              surface->picker);
  surface->cast_code_edit =
      Control(WS_BORDER | ES_AUTOHSCROLL, L"EDIT", L"", kCastCodeEditId,
              surface->picker);
  surface->cast_code_connect = Control(
      BS_PUSHBUTTON, L"BUTTON",
      surface->owner->strings.cast_code_connect.c_str(), kCastCodeConnectId,
      surface->picker);
  surface->status_label =
      Control(SS_LEFT, L"STATIC", L"", 0, surface->picker);
  SendMessageW(surface->cast_code_edit, EM_SETLIMITTEXT,
               kMaxCastCodeLength, 0);
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
  if (!surface->cast_code_label || !surface->cast_code_edit ||
      !surface->cast_code_connect || !surface->status_label ||
      !surface->receiver_list || !surface->empty_label ||
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
      if (surface && HIWORD(wparam) == BN_CLICKED &&
          LOWORD(wparam) == kPauseButtonId &&
          surface->owner->callbacks.set_paused) {
        static_cast<void>(surface->owner->callbacks.set_paused(
            !surface->presentation.playback_paused));
        return 0;
      }
      if (surface && HIWORD(wparam) == BN_CLICKED &&
          LOWORD(wparam) == kSeekButtonId &&
          surface->owner->callbacks.seek) {
        const auto seconds = SeekSeconds(surface->seek_edit);
        if (seconds)
          static_cast<void>(surface->owner->callbacks.seek(*seconds));
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
        surface->pause_button = nullptr;
        surface->seek_edit = nullptr;
        surface->seek_button = nullptr;
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
  surface->pause_button =
      Control(BS_PUSHBUTTON, L"BUTTON",
              surface->owner->strings.playback_pause.c_str(), kPauseButtonId,
              surface->root);
  surface->seek_edit = Control(WS_BORDER | ES_NUMBER | ES_CENTER, L"EDIT",
                               L"0", kSeekEditId, surface->root);
  surface->seek_button =
      Control(BS_PUSHBUTTON, L"BUTTON",
              surface->owner->strings.playback_seek.c_str(), kSeekButtonId,
              surface->root);
  if (!surface->pause_button || !surface->seek_edit || !surface->seek_button)
    return false;
  SendMessageW(surface->seek_edit, EM_SETLIMITTEXT, 6, 0);
  SendMessageW(surface->seek_edit, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(
                   surface->owner->strings.playback_seconds.c_str()));
  ShowWindow(surface->pause_button, SW_HIDE);
  ShowWindow(surface->seek_edit, SW_HIDE);
  ShowWindow(surface->seek_button, SW_HIDE);
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
    if (surface.seek_button) DestroyWindow(surface.seek_button);
    if (surface.seek_edit) DestroyWindow(surface.seek_edit);
    if (surface.pause_button) DestroyWindow(surface.pause_button);
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
  if (surface.seek_button) DestroyWindow(surface.seek_button);
  if (surface.seek_edit) DestroyWindow(surface.seek_edit);
  if (surface.pause_button) DestroyWindow(surface.pause_button);
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
    ShowWindow(surface.pause_button, SW_HIDE);
    ShowWindow(surface.seek_edit, SW_HIDE);
    ShowWindow(surface.seek_button, SW_HIDE);
    HidePicker(&surface);
  }
}

void CastChromeWin::Render(
    const browser_cast_view::CastUiCoordinator& coordinator,
    CastChromePresentation presentation) {
  if (impl_->state.closed) return;
  for (auto& [id, surface] : impl_->state.surfaces) {
    const bool active = id == impl_->state.active_browser_id;
    surface.state = coordinator.button().state();
    const bool presentation_changed = surface.presentation != presentation;
    surface.presentation = presentation;
    if (surface.receivers != coordinator.receivers() ||
        presentation_changed) {
      surface.receivers = coordinator.receivers();
      UpdatePicker(&surface);
    }
    const bool visible = active && surface.state != CastButtonState::kHidden;
    ShowWindow(surface.button, visible ? SW_SHOWNA : SW_HIDE);
    EnableWindow(surface.button,
                 active && (surface.state == CastButtonState::kEligible ||
                            surface.state == CastButtonState::kCasting));
    const bool casting = surface.state == CastButtonState::kCasting;
    const bool controls_visible = active && casting;
    ShowWindow(surface.pause_button, controls_visible ? SW_SHOWNA : SW_HIDE);
    ShowWindow(surface.seek_edit, controls_visible ? SW_SHOWNA : SW_HIDE);
    ShowWindow(surface.seek_button, controls_visible ? SW_SHOWNA : SW_HIDE);
    EnableWindow(surface.pause_button,
                 controls_visible && !presentation.control_pending);
    EnableWindow(surface.seek_edit,
                 controls_visible && !presentation.control_pending);
    EnableWindow(surface.seek_button,
                 controls_visible && !presentation.control_pending);
    const std::wstring& pause_text =
        presentation.control_failed
            ? impl_->state.strings.playback_failed
        : presentation.playback_paused
            ? impl_->state.strings.playback_resume
            : impl_->state.strings.playback_pause;
    SetWindowTextW(surface.pause_button, pause_text.c_str());
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
