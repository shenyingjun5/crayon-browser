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
set(managed_new_tab_count 0)
foreach(production_file IN LISTS production_files)
  file(READ "${production_file}" contents)
  string(REGEX MATCHALL "about:blank" initial_urls "${contents}")
  list(LENGTH initial_urls file_initial_url_count)
  math(EXPR initial_url_count "${initial_url_count} + ${file_initial_url_count}")
  string(REGEX MATCHALL "kNewTabUrl" managed_new_tabs "${contents}")
  list(LENGTH managed_new_tabs file_managed_new_tab_count)
  math(EXPR managed_new_tab_count
       "${managed_new_tab_count} + ${file_managed_new_tab_count}")
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

if(NOT initial_url_count EQUAL 0)
  message(FATAL_ERROR "Windows production shell must not use about:blank as its start page")
endif()
if(managed_new_tab_count LESS 1)
  message(FATAL_ERROR "Windows production shell must use the managed new-tab URL")
endif()

file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/main_win.cc" windows_main)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/app.cc" windows_app)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/content_host_process_win.cc"
     windows_content_host)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/media_host_process_win.cc"
     windows_media_host)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/page_markdown_platform_win.cc"
     windows_page_markdown)
set(windows_markdown_dialog_path
    "${CRAYON_CEF_SHELL_SOURCE}/src/windows/markdown_file_dialog_win.cc")
if(NOT EXISTS "${windows_markdown_dialog_path}")
  message(FATAL_ERROR
          "Windows Markdown file-dialog adapter is missing")
endif()
file(READ "${windows_markdown_dialog_path}" windows_markdown_dialog)
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
        "brand_icons_valid"
        "page_markdown_strings_valid")
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
        "crayon_content_host_windows"
        "crayon-content-host.exe"
        "crayon-media-host.exe"
        "src/browser/media_host/media_host_adapter.cc"
        "src/windows/media_host_process_win.cc"
        "src/windows/content_host_adapter_win.cc"
        "src/browser/page_markdown/cef_page_markdown_preview.cc"
        "src/windows/markdown_file_dialog_win.cc"
        "SET_LPAC_ACLS")
  string(FIND "${shell_cmake}" "${required_cmake_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "Windows sandbox CMake graph is missing ${required_cmake_token}")
  endif()
endforeach()
foreach(required_app_token
        "HelperExecutablePath"
        "SetPageSnapshotObserver"
        "SetPageSnapshotAdmission"
        "SetFileDialogHandler"
        "CefPageMarkdownPreviewController"
        "CopyMarkdownToClipboard")
  string(FIND "${windows_app}" "${required_app_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows BrowserApp is missing CNT-20W1 token ${required_app_token}")
  endif()
endforeach()
foreach(required_media_app_token
        "SetMediaObservationLifecycleCallback"
        "SetMediaObservationEventsReadyCallback"
        "DrainMediaObservations"
        "TrustedPageUrl"
        "media_host_->Consume")
  string(FIND "${windows_app}" "${required_media_app_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows BrowserApp is missing PLT-W05a token ${required_media_app_token}")
  endif()
endforeach()
foreach(required_host_token
        "CoreClientSupervisor"
        "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE"
        "CREATE_NO_WINDOW"
        "PROC_THREAD_ATTRIBUTE_HANDLE_LIST"
        "EXTENDED_STARTUPINFO_PRESENT"
        "CancelSynchronousIo"
        "crayon-agent-"
        "kMaxFrames")
  string(FIND "${windows_content_host}" "${required_host_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows content host is missing lifecycle token ${required_host_token}")
  endif()
endforeach()
foreach(required_host_token
        "CoreClientSupervisor"
        "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE"
        "CREATE_NO_WINDOW"
        "PROC_THREAD_ATTRIBUTE_HANDLE_LIST"
        "EXTENDED_STARTUPINFO_PRESENT"
        "CancelSynchronousIo"
        "media-health-"
        "kMaxFrames")
  string(FIND "${windows_media_host}" "${required_host_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows media host is missing lifecycle token ${required_host_token}")
  endif()
endforeach()
string(FIND "${windows_page_markdown}" "CF_UNICODETEXT" unicode_clipboard)
if(unicode_clipboard EQUAL -1)
  message(FATAL_ERROR "Windows page Markdown clipboard must use CF_UNICODETEXT")
endif()
foreach(required_dialog_token
        "GetOpenFileNameW"
        "GetSaveFileNameW"
        "CefSetOSModalLoop"
        "OFN_DONTADDTORECENT"
        "callback->Continue"
        "callback->Cancel")
  string(FIND "${windows_markdown_dialog}" "${required_dialog_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Markdown file-dialog adapter is missing ${required_dialog_token}")
  endif()
endforeach()

message(STATUS "Windows CEF shell source contract passed")
