if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

file(GLOB_RECURSE production_files
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/*.cc"
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/*.h")
if(NOT production_files)
  message(FATAL_ERROR "CEF shell production sources are missing")
endif()

set(initial_url_count 0)
foreach(production_file IN LISTS production_files)
  file(READ "${production_file}" contents)
  string(REGEX MATCHALL "about:blank" initial_urls "${contents}")
  list(LENGTH initial_urls file_initial_url_count)
  math(EXPR initial_url_count "${initial_url_count} + ${file_initial_url_count}")
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
              "Production shell contains forbidden token '${forbidden_token}' in ${production_file}")
    endif()
  endforeach()
endforeach()

if(NOT initial_url_count EQUAL 1)
  message(FATAL_ERROR "Production shell must contain exactly one about:blank URL")
endif()

file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/main_win.cc" windows_main)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/process/windows/bootstrap_entry.cc"
     windows_bootstrap)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/CMakeLists.txt" shell_cmake)
foreach(required_token
        "CEF_BOOTSTRAP_EXPORT"
        "RunWinMain"
        "version_info")
  string(FIND "${windows_main}" "${required_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "Windows bootstrap entry is missing ${required_token}")
  endif()
endforeach()
foreach(forbidden_main_token "wWinMain" "CefInitialize" "CefRunMessageLoop")
  string(FIND "${windows_main}" "${forbidden_main_token}" token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows exported entry contains forbidden token ${forbidden_main_token}")
  endif()
endforeach()
foreach(required_bootstrap_token
        "CefExecuteProcess"
        "CefInitialize"
        "sandbox_info"
        "GetClientModule"
        "brand_icons_valid")
  string(FIND "${windows_bootstrap}" "${required_bootstrap_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows process bootstrap is missing ${required_bootstrap_token}")
  endif()
endforeach()
string(FIND "${windows_bootstrap}" "no_sandbox" no_sandbox_index)
if(NOT no_sandbox_index EQUAL -1)
  message(FATAL_ERROR "Windows product bootstrap must not disable sandbox")
endif()
foreach(required_cmake_token
        "Windows product builds require the CEF sandbox bootstrap"
        "COPY_SINGLE_FILE"
        "bootstrap.exe"
        "SET_LPAC_ACLS")
  string(FIND "${shell_cmake}" "${required_cmake_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "Windows sandbox CMake graph is missing ${required_cmake_token}")
  endif()
endforeach()

message(STATUS "Windows CEF shell source contract passed")
