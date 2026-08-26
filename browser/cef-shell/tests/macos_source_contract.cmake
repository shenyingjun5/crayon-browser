if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

set(macos_source_root "${CRAYON_CEF_SHELL_SOURCE}/src/macos")
set(macos_resource_root "${CRAYON_CEF_SHELL_SOURCE}/resources/macos")
set(required_files
    "${macos_source_root}/app.h"
    "${macos_source_root}/app.cc"
    "${macos_source_root}/main_mac.mm"
    "${macos_source_root}/process_helper_mac.cc"
    "${macos_resource_root}/Info.plist.in"
    "${macos_resource_root}/helper-Info.plist.in"
    "${macos_resource_root}/en.lproj/InfoPlist.strings"
    "${macos_resource_root}/zh-Hans.lproj/InfoPlist.strings")
foreach(required_file IN LISTS required_files)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "macOS CEF shell file is missing: ${required_file}")
  endif()
endforeach()

file(READ "${macos_source_root}/app.cc" app_source)
file(READ "${macos_source_root}/main_mac.mm" main_source)
file(READ "${macos_source_root}/process_helper_mac.cc" helper_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/CMakeLists.txt" cmake_source)
file(READ "${macos_resource_root}/Info.plist.in" main_plist)
file(READ "${macos_resource_root}/helper-Info.plist.in" helper_plist)

string(REGEX MATCHALL "crayon://newtab" initial_urls "${app_source}")
list(LENGTH initial_urls initial_url_count)
if(NOT initial_url_count EQUAL 1)
  message(FATAL_ERROR "macOS shell must contain exactly one crayon://newtab URL")
endif()

foreach(required_main_token
        "CefScopedLibraryLoader"
        "LoadInMain"
        "CefInitialize"
        "CefRunMessageLoop"
        "CefShutdown"
        "CEF_USE_SANDBOX"
        "CrayonApplication"
        "CrayonAppDelegate")
  string(FIND "${main_source}" "${required_main_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS main entry is missing ${required_main_token}")
  endif()
endforeach()
string(FIND "${main_source}" "CefExecuteProcess" main_execute_index)
if(NOT main_execute_index EQUAL -1)
  message(FATAL_ERROR "macOS main entry must not execute helper processes")
endif()

foreach(required_helper_token
        "LoadInHelper"
        "CefExecuteProcess"
        "CEF_USE_SANDBOX")
  string(FIND "${helper_source}" "${required_helper_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS helper entry is missing ${required_helper_token}")
  endif()
endforeach()
foreach(forbidden_helper_token
        "CefInitialize"
        "CefRunMessageLoop"
        "BrowserApp"
        "about:blank")
  string(FIND "${helper_source}" "${forbidden_helper_token}" token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR "macOS helper contains browser-process token ${forbidden_helper_token}")
  endif()
endforeach()

foreach(required_cmake_token
        "assets/brand/generated/macos/app.icns"
        "assets/brand/generated/macos/AppIcon.iconset"
        "CEF_HELPER_APP_SUFFIXES"
        "COPY_MAC_FRAMEWORK"
        "COPY_MAC_RESOURCES"
        "MACOSX_BUNDLE_INFO_PLIST")
  string(FIND "${cmake_source}" "${required_cmake_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS CMake graph is missing ${required_cmake_token}")
  endif()
endforeach()

foreach(plist_token
        "CFBundleIdentifier"
        "CFBundleExecutable"
        "CFBundleVersion"
        "LSMinimumSystemVersion")
  string(FIND "${main_plist}" "${plist_token}" main_plist_index)
  string(FIND "${helper_plist}" "${plist_token}" helper_plist_index)
  if(main_plist_index EQUAL -1 OR helper_plist_index EQUAL -1)
    message(FATAL_ERROR "macOS plist contract is missing ${plist_token}")
  endif()
endforeach()
string(FIND "${main_plist}" "<string>app.icns</string>" icon_index)
if(icon_index EQUAL -1)
  message(FATAL_ERROR "macOS main plist must reference app.icns")
endif()

file(GLOB_RECURSE production_files
     "${macos_source_root}/*.cc"
     "${macos_source_root}/*.h"
     "${macos_source_root}/*.mm")
foreach(production_file IN LISTS production_files)
  file(READ "${production_file}" contents)
  foreach(forbidden_token
          "http://"
          "https://"
          "file(DOWNLOAD"
          "FetchContent"
          "ExternalProject"
          "CastSdk"
          "Cast-SDK"
          "Relay"
          "WebRTC"
          "GetDisplayMedia"
          "DesktopCapturer"
          "Fake"
          "Mock")
    string(FIND "${contents}" "${forbidden_token}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "macOS production shell contains forbidden token '${forbidden_token}' in ${production_file}")
    endif()
  endforeach()
endforeach()

message(STATUS "macOS CEF shell source contract passed")
