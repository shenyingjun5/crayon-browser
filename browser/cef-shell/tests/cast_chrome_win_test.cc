#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windows/cast_chrome_win.h"

#include <windows.h>

#include <iostream>
#include <iterator>
#include <string>

#include "crayon/browser_cast_view/cast_ui_coordinator.h"

namespace {

namespace cast = crayon::browser_cast_view;
namespace windows = crayon::browser::cef_shell::windows;

#define CHECK_CAST(condition)                                             \
  do {                                                                    \
    if (!(condition)) {                                                   \
      std::cerr << "check failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                  \
      return false;                                                       \
    }                                                                     \
  } while (false)

void PumpMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

HWND FindThreadWindow(const wchar_t* title) {
  struct Search final {
    const wchar_t* title;
    HWND result = nullptr;
  } search{title};
  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND window, LPARAM value) -> BOOL {
        auto* search = reinterpret_cast<Search*>(value);
        wchar_t title[128]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (std::wstring(title) == search->title) {
          search->result = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.result;
}

HWND FindChild(HWND parent, const wchar_t* class_name, const wchar_t* text) {
  HWND child = nullptr;
  while ((child = FindWindowExW(parent, child, class_name, nullptr)) !=
         nullptr) {
    wchar_t value[128]{};
    GetWindowTextW(child, value, static_cast<int>(std::size(value)));
    if (std::wstring(value) == text) return child;
  }
  return nullptr;
}

bool CastChromeLifecycle() {
  HWND root = CreateWindowExW(0, L"STATIC", L"Cast Chrome Test",
                              WS_OVERLAPPEDWINDOW, 0, 0, 900, 640, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
  CHECK_CAST(root != nullptr);
  ShowWindow(root, SW_SHOWNA);

  cast::CastUiCoordinator coordinator;
  coordinator.SetPageActive(true);
  coordinator.SetMediaPresent(true);
  coordinator.SetBrowserVerifiedEligible(true);
  int activations = 0;
  int refreshes = 0;
  int cancellations = 0;
  std::string selected;
  windows::CastChromeWin chrome(
      {L"Select receiver", L"Stop casting", L"Cast to device", L"No receivers",
       L"Cast", L"Refresh", L"Cancel"},
      {[&] {
         ++activations;
         if (coordinator.active_session_generation())
           return coordinator.RequestStop().has_value();
         return coordinator.OpenPicker().has_value();
       },
       [&] {
         ++refreshes;
         return true;
       },
       [&] {
         ++cancellations;
         coordinator.CancelPicker();
       },
       [&](const std::string& device_id) {
         selected = device_id;
         return true;
       }});

  CHECK_CAST(chrome.AttachWindow(7, root));
  chrome.SetActiveWindow(7);
  chrome.Render(coordinator);
  PumpMessages();
  HWND cast_button = FindChild(root, L"BUTTON", L"Select receiver");
  CHECK_CAST(cast_button != nullptr);
  CHECK_CAST(IsWindowVisible(cast_button));
  CHECK_CAST(IsWindowEnabled(cast_button));

  SendMessageW(cast_button, BM_CLICK, 0, 0);
  PumpMessages();
  CHECK_CAST(activations == 1);
  HWND picker = FindThreadWindow(L"Cast to device");
  CHECK_CAST(picker != nullptr && IsWindowVisible(picker));
  HWND list = FindWindowExW(picker, nullptr, L"LISTBOX", nullptr);
  CHECK_CAST(list != nullptr);
  CHECK_CAST(SendMessageW(list, LB_GETCOUNT, 0, 0) == 0);
  HWND empty = FindChild(picker, L"STATIC", L"No receivers");
  CHECK_CAST(empty != nullptr && IsWindowVisible(empty));
  HWND select = FindChild(picker, L"BUTTON", L"Cast");
  CHECK_CAST(select != nullptr && !IsWindowEnabled(select));

  CHECK_CAST(coordinator.ReplaceReceivers(
      {{"receiver_a", "Living room", true},
       {"receiver_b", "\xe4\xb9\xa6\xe6\x88\xbf", false}}));
  chrome.Render(coordinator);
  PumpMessages();
  CHECK_CAST(SendMessageW(list, LB_GETCOUNT, 0, 0) == 2);
  CHECK_CAST(!IsWindowVisible(empty));
  CHECK_CAST(IsWindowEnabled(select));

  HWND refresh = FindChild(picker, L"BUTTON", L"Refresh");
  CHECK_CAST(refresh != nullptr);
  SendMessageW(refresh, BM_CLICK, 0, 0);
  CHECK_CAST(refreshes == 1);
  SendMessageW(list, LB_SETCURSEL, 1, 0);
  SendMessageW(select, BM_CLICK, 0, 0);
  CHECK_CAST(selected == "receiver_b");

  SendMessageW(picker, WM_CLOSE, 0, 0);
  CHECK_CAST(cancellations == 1);
  CHECK_CAST(!IsWindowVisible(picker));

  CHECK_CAST(coordinator.OpenPicker().has_value());
  CHECK_CAST(coordinator.ApplyPolicyOutcome(cast::PolicyOutcome::kDirect));
  CHECK_CAST(coordinator.NotifySessionStarted(9));
  chrome.Render(coordinator);
  PumpMessages();
  wchar_t label[64]{};
  GetWindowTextW(cast_button, label, static_cast<int>(std::size(label)));
  CHECK_CAST(std::wstring(label) == L"Stop casting");
  CHECK_CAST(!IsWindowVisible(picker));
  SendMessageW(cast_button, BM_CLICK, 0, 0);
  CHECK_CAST(activations == 2);

  chrome.DetachWindow(7);
  CHECK_CAST(!IsWindow(cast_button));
  chrome.Close();
  DestroyWindow(root);
  PumpMessages();
  return true;
}

}  // namespace

int main() {
  if (!CastChromeLifecycle()) return 1;
  std::cout << "cast_chrome_win_test passed\n";
  return 0;
}
