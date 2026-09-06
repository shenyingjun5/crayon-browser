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
     "${CRAYON_CEF_SHELL_SOURCE}/src/process/windows/bootstrap_entry.cc"
     windows_bootstrap)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/context/profile_context_factory.cc"
     profile_context_factory)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/new_tab/cef_new_tab_handler.cc"
     new_tab_handler)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_handler.cc"
     mdv_handler)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/content_host_process_win.cc"
     windows_content_host)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/media_host_process_win.cc"
     windows_media_host)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/cast_chrome_win.cc"
     windows_cast_chrome)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/trusted_input_monitor_win.cc"
     windows_trusted_input)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/media_host/cast_shell_controller.cc"
     cast_shell_controller)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/media_host/cast_entry_surface.cc"
     cast_entry_surface)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/media_host/alloy_cast_controller.cc"
     alloy_cast_controller)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/alloy_cast_overlay_win.cc"
     alloy_cast_overlay)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/alloy_product_host_win.cc"
     alloy_product_host)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/alloy_product_host_win.h"
     alloy_product_host_header)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/alloy_tab_controller.cc"
     alloy_tab_controller)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/alloy_interactions.cc"
     alloy_interactions)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/alloy_tab_strip.cc"
     alloy_tab_strip)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/alloy_builtin_content.cc"
     alloy_builtin_content)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/window/alloy_page_markdown.cc"
     alloy_page_markdown)
file(READ
     "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_entries.cc"
     mdv_entries)
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
        "page_markdown_strings_valid"
        "cast_strings_valid")
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
        "src/browser/media_host/cast_shell_controller.cc"
        "src/windows/cast_chrome_win.cc"
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
        "media_host_->Consume"
        "CastShellController"
        "CastChromeWin"
        "TrustedInputMonitorWin"
        "DrainPlanning"
        "alloy_product_host_->TickCast")
  string(FIND "${windows_app}" "${required_media_app_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows BrowserApp is missing PLT-W05a token ${required_media_app_token}")
  endif()
endforeach()
foreach(required_cast_chrome_token
        "SetWindowSubclass"
        "TOOLTIPS_CLASSW"
        "WM_DPICHANGED_AFTERPARENT"
        "LBS_NOTIFY"
        "UpdatePicker"
        "EnableWindow"
        "RemoveWindowSubclass")
  string(FIND "${windows_cast_chrome}" "${required_cast_chrome_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Cast chrome is missing lifecycle/UI token ${required_cast_chrome_token}")
  endif()
endforeach()
foreach(required_trusted_input_token
        "SetWindowsHookExW"
        "WH_MOUSE_LL"
        "WindowFromPoint"
        "GetCurrentProcessId"
        "LLMHF_INJECTED"
        "LLMHF_LOWER_IL_INJECTED"
        "WM_LBUTTONDOWN"
        "WM_RBUTTONDOWN"
        "UnhookWindowsHookEx")
  string(FIND "${windows_trusted_input}" "${required_trusted_input_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows trusted input adapter is missing ${required_trusted_input_token}")
  endif()
endforeach()
foreach(required_cast_controller_token
        "OnNavigation"
        "OnPageClosed"
        "OnHostUnavailable"
        "Shutdown"
        "DiscoveryAction::kRefresh"
        "RequestStop"
        "NotifySessionEnded")
  string(FIND "${cast_shell_controller}" "${required_cast_controller_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Shared Cast controller is missing lifecycle token ${required_cast_controller_token}")
  endif()
endforeach()
foreach(forbidden_cast_entry_token
        "chrome_location_bar"
        "ChromeLocationBar"
        "GetChromeToolbar"
        "CEF_CTT_LOCATION"
        "SuspendLocation"
        "RestoreLocation")
  string(FIND "${cast_entry_surface}" "${forbidden_cast_entry_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy Cast entry contains LOCATION dependency ${forbidden_cast_entry_token}")
  endif()
endforeach()
foreach(required_interaction_token
        "CefMenuButton::CreateMenuButton"
        "HandleAccelerator"
        "OnBeforeContextMenu"
        "OnContextMenuDismissed"
        "OnDragEnter"
        "chrome://credits/"
        "kAboutBrowserUrl")
  string(FIND "${alloy_interactions}" "${required_interaction_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy interactions are missing ${required_interaction_token}")
  endif()
endforeach()
foreach(required_incognito_interaction_token
        "privacy.incognito"
        "kOpenIncognito"
        "EVENTFLAG_SHIFT_DOWN")
  string(FIND "${alloy_interactions}" "${required_incognito_interaction_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy interactions are missing incognito token ${required_incognito_interaction_token}")
  endif()
endforeach()
foreach(forbidden_interaction_token
        "IDC_OPEN_FILE"
        "ExecuteChromeCommand"
        "javascript:")
  string(FIND "${alloy_interactions}" "${forbidden_interaction_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy interactions contain forbidden Chrome/page token ${forbidden_interaction_token}")
  endif()
endforeach()
foreach(required_alloy_product_profile_token
        "ProfileContextFactory"
        "InitializeDefaultProfileContext"
        "CefPostTask"
        "GetGlobalContext"
        "AdoptGlobalContext"
        "SetRequestContext"
        "ReleaseRequestContextsForShutdown"
        "CreateTemporaryContext"
        "OnRequestContextInitialized"
        "CefRefPtr<AlloyProductHostWin> self"
        "register_incognito_content"
        "NewTabProfileMode::kIncognito"
        "GetCachePath().empty()"
        "pending_incognito_contexts_"
        "CreateIncognitoWindow")
  string(FIND
         "${alloy_product_host}${alloy_product_host_header}${windows_app}"
         "${required_alloy_product_profile_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product is missing 24W2b2b token ${required_alloy_product_profile_token}")
  endif()
endforeach()
foreach(required_profile_bootstrap_token
        "CSIDL_LOCAL_APPDATA"
        "settings.root_cache_path"
        "settings.persist_session_cookies"
        "BuildProfileCacheRoot"
        "profile_cache_root_utf8"
        "app->PrepareForCefShutdown()"
        "app = nullptr")
  string(FIND "${windows_bootstrap}" "${required_profile_bootstrap_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows bootstrap is missing profile root token ${required_profile_bootstrap_token}")
  endif()
endforeach()
foreach(required_context_scheme_token
        "request_context->RegisterSchemeHandlerFactory")
  string(FIND
         "${new_tab_handler}${mdv_handler}"
         "${required_context_scheme_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Per-profile built-in scheme registration is missing ${required_context_scheme_token}")
  endif()
endforeach()
foreach(required_alloy_session_token
        "AlloySessionRestore::LoadCheckpoint"
        "AlloySessionRestore::SaveCheckpoint"
        "pending_session_restore_"
        "CompleteSessionRestore"
        "pending_restored_browsers_"
        "OnRestoredBrowserReady"
        "FailSessionRestore"
        "restoring_window_ids_"
        "session_restore_failed_"
        "session_checkpoint_pending_"
        "kSessionRestoreDelayMilliseconds")
  string(FIND
         "${alloy_product_host}${alloy_product_host_header}"
         "${required_alloy_session_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product is missing 24W2b2c session token ${required_alloy_session_token}")
  endif()
endforeach()
foreach(required_alloy_session_bootstrap_token
        "CEF-Alloy-v1"
        "alloy-session-v2"
        "no-startup-window")
  string(FIND
         "${windows_bootstrap}${windows_app}"
         "${required_alloy_session_bootstrap_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows bootstrap is missing 24W2b2c token ${required_alloy_session_bootstrap_token}")
  endif()
endforeach()
foreach(required_alloy_daily_data_token
        "AlloyBookmarks"
        "AlloyHistory"
        "AlloyDownloads"
        "InitializeDailyState"
        "CommitHistoryNavigation"
        "RecordRecentlyClosed"
        "LoadFromFile"
        "SaveToFile"
        "daily_data_load_failed_"
        "downloads_.get()")
  string(FIND
         "${alloy_product_host}${alloy_product_host_header}"
         "${required_alloy_daily_data_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product is missing 24W2b3a token ${required_alloy_daily_data_token}")
  endif()
endforeach()
foreach(required_alloy_daily_surface_token
        "kTogglePin"
        "kDuplicateTab"
        "kToggleMute"
        "kToggleGroup"
        "kToggleBookmarkBar"
        "tab_search_entries"
        "toggle_current_bookmark"
        "kMaximumVisibleBookmarkButtons")
  string(FIND
         "${alloy_interactions}${alloy_product_host}${alloy_product_host_header}"
         "${required_alloy_daily_surface_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product is missing 24W2b3b1 surface token ${required_alloy_daily_surface_token}")
  endif()
endforeach()
foreach(required_alloy_advanced_order_token
        "advanced_ordered_tabs()"
        "std::unordered_set<TabId> unique"
        "order.size() != model.size()")
  string(FIND "${alloy_product_host}${alloy_tab_strip}"
         "${required_alloy_advanced_order_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product is missing 24W2b3b1 order token ${required_alloy_advanced_order_token}")
  endif()
endforeach()
foreach(required_alloy_daily_data_bootstrap_token
        "ProductData"
        "Downloads"
        "bookmarks-v1"
        "history-v1"
        "WindowsProductPaths")
  string(FIND
         "${windows_bootstrap}${windows_app}"
         "${required_alloy_daily_data_bootstrap_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows bootstrap is missing 24W2b3a token ${required_alloy_daily_data_bootstrap_token}")
  endif()
endforeach()
foreach(required_serial_close_token
        "RequestNextClose(false)"
        "if (!force_close)")
  string(FIND
         "${alloy_product_host}${alloy_tab_controller}"
         "${required_serial_close_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy normal-close path is missing ${required_serial_close_token}")
  endif()
endforeach()
foreach(required_builtin_token
        "RegisterAlloyBuiltinContentFactories"
        "CefMessageRouterBrowserSide::Create"
        "InterceptWhileDirty"
        "CancelTransientEntries"
        "OnRenderProcessTerminated")
  string(FIND "${alloy_builtin_content}" "${required_builtin_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy built-in content is missing ${required_builtin_token}")
  endif()
endforeach()
foreach(forbidden_builtin_token
        "ExecuteChromeCommand"
        "IDC_"
        "javascript:")
  string(FIND "${alloy_builtin_content}" "${forbidden_builtin_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy built-in content contains forbidden token ${forbidden_builtin_token}")
  endif()
endforeach()
foreach(required_alloy_cast_token
        "RequestPlayerPage"
        "RequestDevicePage"
        "RequestDraft"
        "RequestResolveCastCode"
        "RequestStopCast"
        "RequestControlCast"
        "AdvanceNavigation"
        "CloseTab")
  string(FIND "${alloy_cast_controller}" "${required_alloy_cast_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy Cast controller is missing ${required_alloy_cast_token}")
  endif()
endforeach()
foreach(forbidden_alloy_cast_token
        "RequestStartCast"
        "page_url"
        "media_url"
        "Authorization"
        "Cookie"
        "ExecuteJavaScript"
        "Cast-SDK")
  string(FIND "${alloy_cast_controller}" "${forbidden_alloy_cast_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy Cast controller contains forbidden token ${forbidden_alloy_cast_token}")
  endif()
endforeach()
foreach(required_alloy_overlay_token
        "CreateWindowExW"
        "SetWindowSubclass"
        "SetWindowPos"
        "TTM_ADDTOOLW"
        "kCastSelectionPageSize"
        "PlaceOverlay"
        "kFirstControlId"
        "BN_CLICKED")
  string(FIND "${alloy_cast_overlay}" "${required_alloy_overlay_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy Cast overlay is missing ${required_alloy_overlay_token}")
  endif()
endforeach()
foreach(required_alloy_product_cast_token
        "media_observation_bridge_"
        "GetResourceRequestHandler"
        "DrainMediaObservations"
        "NoteTrustedUserInput"
        "AlloyCastController"
        "CastEntrySurface"
        "AlloyCastOverlayWin"
        "TickCast")
  string(FIND "${alloy_product_host}" "${required_alloy_product_cast_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product host is missing 24W2 token ${required_alloy_product_cast_token}")
  endif()
endforeach()
foreach(required_alloy_product_security_token
        "AlloySiteControls"
        "GetPermissionHandler"
        "GetDownloadHandler"
        "OnCertificateError"
        "OnRequestMediaAccessPermission"
        "OnShowPermissionPrompt"
        "OnBeforeDownload"
        "ProductResourceHandlerWin"
        "GetFirstPartyForCookies"
        "trusted_input_generation_"
        "kExternalProtocolInputLifetimeMilliseconds"
        "allow_os_execution = false"
        "MessageBoxW")
  string(FIND "${alloy_product_host}${alloy_product_host_header}"
         "${required_alloy_product_security_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product host is missing 24W2b1 token ${required_alloy_product_security_token}")
  endif()
endforeach()
foreach(required_alloy_product_popup_token
        "OnBeforePopup"
        "coordinator_->RequestPopup"
        "CreatePopupWindow"
        "CefWindow::CreateTopLevelWindow"
        "pending_popup_windows_"
        "OwnerWindowIdForBrowser"
        "ControllerForBrowser"
        "GetRequestContext"
        "dependencies_.request_context")
  string(FIND "${alloy_product_host}${alloy_product_host_header}"
         "${required_alloy_product_popup_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy product host is missing 24W2b2a token ${required_alloy_product_popup_token}")
  endif()
endforeach()
string(FIND "${alloy_product_host}"
       "browser_engine::ContentPurpose::kWeb" product_popup_web_purpose)
string(FIND "${alloy_product_host}"
       "CefLifeSpanHandler::WindowOpenDisposition" product_popup_disposition)
string(FIND "${alloy_product_host}" "no_javascript_access = false"
       unsafe_popup_javascript_override)
if(product_popup_web_purpose EQUAL -1 OR product_popup_disposition EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product popup must use the bounded web-purpose lifecycle")
endif()
if(NOT unsafe_popup_javascript_override EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product popup must not override CEF JavaScript isolation")
endif()
string(FIND "${alloy_product_host}" "allow_os_execution = true"
       unsafe_protocol_execution)
if(NOT unsafe_protocol_execution EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product host must not directly allow CEF protocol execution")
endif()
string(FIND "${windows_app}" "tab_controller_->DrainMediaObservations"
       legacy_product_media_drain)
if(NOT legacy_product_media_drain EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product must not drain media observations from the legacy Chrome controller")
endif()
foreach(forbidden_alloy_overlay_token
        "ExecuteJavaScript"
        "RequestStartCast"
        "StartCast"
        "Authorization"
        "Cookie"
        "http://"
        "https://")
  string(FIND "${alloy_cast_overlay}" "${forbidden_alloy_overlay_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Windows Alloy Cast overlay contains forbidden token ${forbidden_alloy_overlay_token}")
  endif()
endforeach()
foreach(required_page_markdown_token
        "PageMarkdownSnapshotHost"
        "StartSnapshot"
        "AdvanceNavigation"
        "RendererGone"
        "CloseBrowser"
        "ShutDown"
        "OnBuiltinContextMenuCommand")
  string(FIND "${alloy_page_markdown}" "${required_page_markdown_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy page Markdown is missing ${required_page_markdown_token}")
  endif()
endforeach()
foreach(forbidden_page_markdown_token
        "ExecuteChromeCommand"
        "IDC_"
        "javascript:"
        "GetSource"
        "GetText")
  string(FIND "${alloy_page_markdown}" "${forbidden_page_markdown_token}"
         token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "Alloy page Markdown contains forbidden token ${forbidden_page_markdown_token}")
  endif()
endforeach()
foreach(required_mdv_entry_token
        "HandleOpenFileCommand"
        "CancelTransientEntries")
  string(FIND "${mdv_entries}" "${required_mdv_entry_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "MDV entries must expose ${required_mdv_entry_token}")
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

# The active interaction surface owns a CefBrowserView. It must be shut down
# before requesting browser close, not from the asynchronous DoClose release
# callback, or the final Alloy tab either stays alive or tears down views while
# CEF is already dispatching its close sequence.
string(FIND "${alloy_product_host}"
       "bool AlloyProductHostWin::PrepareActiveChromeForClose"
       prepare_close_start)
if(prepare_close_start EQUAL -1)
  message(FATAL_ERROR "Windows Alloy product close preparation is missing")
endif()
string(SUBSTRING "${alloy_product_host}" ${prepare_close_start} -1
       alloy_product_close_tail)
string(FIND "${alloy_product_close_tail}" "interactions_->Shutdown();"
       interaction_shutdown)
string(FIND "${alloy_product_host}"
       "const bool active = PrepareActiveChromeForClose(id);"
       tab_close_prepare)
string(FIND "${alloy_product_host}"
       "const bool prepared = active && PrepareActiveChromeForClose(*active);"
       window_close_prepare)
string(FIND "${alloy_product_host}"
       "coordinator_->BeginCloseWindow(kPrimaryWindowId, force_close)"
       window_close_request)
string(FIND "${alloy_product_host}"
       "ShutdownChromeForWindowClose();\n    window_->Close();"
       window_chrome_shutdown)
string(FIND "${alloy_product_host}"
       "value == -107 || (value <= -200 && value >= -299)"
       ssl_error_classification)
if(interaction_shutdown EQUAL -1 OR tab_close_prepare EQUAL -1 OR
   window_close_prepare EQUAL -1 OR window_close_request EQUAL -1 OR
   window_close_request LESS window_close_prepare OR
   window_chrome_shutdown EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product close must release active UI owners before requesting browser close")
endif()
if(ssl_error_classification EQUAL -1)
  message(FATAL_ERROR
          "Windows Alloy product navigation must classify SSL protocol and certificate errors")
endif()

message(STATUS "Windows CEF shell source contract passed")
