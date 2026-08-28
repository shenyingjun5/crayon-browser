if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

set(adapter_header
    "${CRAYON_CEF_SHELL_SOURCE}/src/browser/new_tab/cef_new_tab_handler.h")
set(adapter_source
    "${CRAYON_CEF_SHELL_SOURCE}/src/browser/new_tab/cef_new_tab_handler.cc")
foreach(required_file "${adapter_header}" "${adapter_source}")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "new-tab CEF adapter file is missing: ${required_file}")
  endif()
endforeach()

file(READ "${adapter_header}" header_text)
file(READ "${adapter_source}" source_text)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/app.cc" windows_app_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/process/windows/bootstrap_entry.cc"
     bootstrap_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/tab_controller.cc"
     controller_source)

foreach(required_token
        "CEF_SCHEME_OPTION_STANDARD"
        "CEF_SCHEME_OPTION_SECURE"
        "CEF_SCHEME_OPTION_DISPLAY_ISOLATED"
        "CEF_SCHEME_OPTION_CORS_ENABLED"
        "CefRegisterSchemeHandlerFactory"
        "ClassifyNewTabRequest"
        "SetCharset"
        "Content-Security-Policy"
        "default-src 'none'"
        "style-src 'self'"
        "frame-ancestors 'none'"
        "Cache-Control"
        "no-store"
        "X-Content-Type-Options"
        "nosniff"
        "Referrer-Policy"
        "no-referrer")
  string(FIND "${header_text};${source_text}" "${required_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "new-tab CEF adapter is missing ${required_token}")
  endif()
endforeach()

foreach(forbidden_scheme_option
        "CEF_SCHEME_OPTION_CSP_BYPASSING"
        "CEF_SCHEME_OPTION_FETCH_ENABLED")
  string(FIND "${source_text}" "${forbidden_scheme_option}" option_index)
  if(NOT option_index EQUAL -1)
    message(FATAL_ERROR
            "new-tab scheme uses forbidden option ${forbidden_scheme_option}")
  endif()
endforeach()

foreach(forbidden_io_token
        "ifstream"
        "fopen"
        "CreateFile"
        "CefURLRequest"
        "CefDownloadItem"
        "WinHttp"
        "libcurl")
  string(FIND "${source_text}" "${forbidden_io_token}" io_index)
  if(NOT io_index EQUAL -1)
    message(FATAL_ERROR
            "new-tab adapter contains forbidden IO token ${forbidden_io_token}")
  endif()
endforeach()

string(FIND "${bootstrap_source}" "CreateNewTabProcessApp" child_app_index)
string(FIND "${bootstrap_source}" "CefExecuteProcess(main_args, child_app"
       execute_index)
if(child_app_index EQUAL -1 OR execute_index EQUAL -1)
  message(FATAL_ERROR
          "child processes must receive the same custom scheme registration app")
endif()

foreach(required_windows_token
        "RegisterNewTabSchemeHandlerFactory"
        "kNewTabUrl"
        "IDS_CRAYON_NEW_TAB_TITLE")
  string(FIND "${windows_app_source}" "${required_windows_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows app is missing new-tab token ${required_windows_token}")
  endif()
endforeach()

foreach(required_redirect_token
        "OnBeforeBrowse"
        "OnAddressChange"
        "OnBrowserCreated"
        "RedirectBuiltInNewTab(main_frame"
        "cef_id_for_command_id_name(\"IDC_NEW_TAB\")"
        "pending_new_tab_commands_"
        "chrome://newtab/"
        "new_tab_url_")
  string(FIND "${controller_source}" "${required_redirect_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "TabController is missing exact new-tab redirect token ${required_redirect_token}")
  endif()
endforeach()

message(STATUS "new-tab CEF adapter contract passed")
