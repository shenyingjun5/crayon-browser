if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

set(window_root "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window")
foreach(required_file
        "${window_root}/tab_model.h"
        "${window_root}/tab_model.cc"
        "${window_root}/tab_controller.h"
        "${window_root}/tab_controller.cc")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "window module file is missing: ${required_file}")
  endif()
endforeach()

file(READ "${window_root}/tab_model.h" model_header)
file(READ "${window_root}/tab_model.cc" model_source)
file(READ "${window_root}/tab_controller.h" controller_header)
file(READ "${window_root}/tab_controller.cc" controller_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/app.h" windows_app_header)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/app.cc" windows_app_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/shell_command_adapter.cc"
     windows_shell_command_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/CMakeLists.txt" shell_cmake)

# The state model must stay free of CEF and platform types so it can be tested
# and reviewed without an engine.
foreach(model_contents "${model_header}" "${model_source}")
  foreach(forbidden_model_token "include/cef" "CefRefPtr" "HWND" "NSView")
    string(FIND "${model_contents}" "${forbidden_model_token}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "tab model must not reference '${forbidden_model_token}'")
    endif()
  endforeach()
endforeach()

# The adapter must keep every mutation on the CEF UI thread, close browsers
# asynchronously, and normalize life span/display/load/crash callbacks.
foreach(required_token
        "CEF_REQUIRE_UI_THREAD"
        "CreateBrowser"
        "CEF_RUNTIME_STYLE_CHROME"
        "TryCloseBrowser"
        "OnAfterCreated"
        "OnBeforeClose"
        "OnRenderProcessTerminated"
        "OnLoadingStateChange"
        "OnAddressChange"
        "OnGotFocus"
        "CefQuitMessageLoop"
        "SetZoomLevel")
  string(FIND "${controller_source}" "${required_token}" token_index)
  string(FIND "${controller_header}" "${required_token}" header_token_index)
  if(token_index EQUAL -1 AND header_token_index EQUAL -1)
    message(FATAL_ERROR "tab controller is missing ${required_token}")
  endif()
endforeach()

string(FIND "${controller_source}" "CEF_RUNTIME_STYLE_CHROME" style_index)
if(style_index EQUAL -1)
  message(FATAL_ERROR "tab controller must create Chrome-style browser windows")
endif()

foreach(forbidden_token
        "http://"
        "https://"
        "CreateBrowserSync"
        "no_sandbox"
        "Fake"
        "Mock")
  foreach(source_text "${controller_source}" "${controller_header}")
    string(FIND "${source_text}" "${forbidden_token}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "tab controller contains forbidden token '${forbidden_token}'")
    endif()
  endforeach()
endforeach()

# Windows must use the same controller as macOS. Chrome UI-created tabs and
# windows must also resolve to the normalized client, while platform branding
# remains owned by the Windows adapter.
foreach(required_windows_token
        "window::TabController"
        "GetDefaultClient"
        "CreateMainWindow"
        "brand_icons_valid")
  string(FIND "${windows_app_header}" "${required_windows_token}"
         header_token_index)
  string(FIND "${windows_app_source}" "${required_windows_token}"
         source_token_index)
  if(header_token_index EQUAL -1 AND source_token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows app is missing shared controller token ${required_windows_token}")
  endif()
endforeach()

foreach(required_shell_token
        "cef_id_for_command_id_name"
        "CommandOrigin::kNativeChrome"
        "ObserveChromeCommand"
        "SetBrowsersClosedCallback")
  string(FIND
         "${windows_app_header};${windows_app_source};${windows_shell_command_source};${controller_header};${controller_source}"
         "${required_shell_token}" shell_token_index)
  if(shell_token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows shell command adapter is missing ${required_shell_token}")
  endif()
endforeach()

# Runtime callbacks touch state that is owned by the CEF UI thread. Install
# them from OnContextInitialized, after CEF establishes that thread and before
# the first browser can emit an event.
string(FIND "${windows_app_source}" "void BrowserApp::OnContextInitialized()"
       context_initialized_index)
string(FIND "${windows_app_source}" "SetChromeCommandCallback"
       chrome_callback_index)
string(FIND "${windows_app_source}" "SetBrowsersClosedCallback"
       closed_callback_index)
string(FIND "${windows_app_source}" "CreateMainWindow" create_window_index)
if(context_initialized_index EQUAL -1 OR chrome_callback_index EQUAL -1 OR
   closed_callback_index EQUAL -1 OR create_window_index EQUAL -1 OR
   chrome_callback_index LESS context_initialized_index OR
   closed_callback_index LESS context_initialized_index OR
   chrome_callback_index GREATER create_window_index OR
   closed_callback_index GREATER create_window_index)
  message(FATAL_ERROR
          "Windows shell callbacks must be installed on the CEF UI thread before CreateMainWindow")
endif()

string(REGEX MATCH "command_id[ \t\r\n]*==[ \t\r\n]*[0-9]+"
             hardcoded_command_id "${windows_shell_command_source}")
if(hardcoded_command_id)
  message(FATAL_ERROR "Windows shell adapter hard-codes a Chrome command ID")
endif()

foreach(required_windows_source
        "src/browser/window/tab_controller.cc"
        "src/browser/window/tab_model.cc")
  string(FIND "${shell_cmake}" "${required_windows_source}" source_index)
  if(source_index EQUAL -1)
    message(FATAL_ERROR
            "Windows CMake graph is missing ${required_windows_source}")
  endif()
endforeach()

foreach(forbidden_windows_token
        "CEF_RUNTIME_STYLE_ALLOY"
        "class BrowserClient")
  foreach(windows_text "${windows_app_header}" "${windows_app_source}")
    string(FIND "${windows_text}" "${forbidden_windows_token}"
           forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "Windows app retains legacy token '${forbidden_windows_token}'")
    endif()
  endforeach()
endforeach()

file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/browser/branding/about_browser.h"
     about_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/browser/branding/about_destination.h"
     about_destination)
string(APPEND about_source "${about_destination}")
foreach(token "IDS_ABOUT" "IDS_ABOUT_MAC" "app.about" "https://www.zknowai.com/"
              "cef_id_for_pack_string_name")
  string(FIND "${about_source}" "${token}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "About branding is missing ${token}")
  endif()
endforeach()
foreach(platform windows macos)
  file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/${platform}/app.h" app_header)
  file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/${platform}/app.cc" app_source)
  if(NOT app_header MATCHES "GetResourceBundleHandler" OR
     NOT app_source MATCHES "AboutBrowserResources.*locale_snapshot.locale")
    message(FATAL_ERROR "${platform} must wire About resources to the locale snapshot")
  endif()
endforeach()
foreach(token "cef_id_for_command_id_name(\"IDC_ABOUT\")"
              "kAboutCommandId > 0" "branding::kAboutBrowserUrl")
  string(FIND "${controller_source}" "${token}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "About command routing is missing ${token}")
  endif()
endforeach()

message(STATUS "window adapter contract passed")
