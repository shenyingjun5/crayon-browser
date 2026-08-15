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

message(STATUS "window adapter contract passed")
