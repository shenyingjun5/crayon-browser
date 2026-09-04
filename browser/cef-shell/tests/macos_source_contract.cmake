if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

set(macos_source_root "${CRAYON_CEF_SHELL_SOURCE}/src/macos")
set(macos_resource_root "${CRAYON_CEF_SHELL_SOURCE}/resources/macos")
cmake_path(ABSOLUTE_PATH CRAYON_CEF_SHELL_SOURCE
           BASE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" NORMALIZE
           OUTPUT_VARIABLE cef_shell_source_absolute)
cmake_path(GET cef_shell_source_absolute PARENT_PATH browser_root)
cmake_path(GET browser_root PARENT_PATH project_root)
set(generated_macos_resource_root
    "${project_root}/browser/shared-ui/localization/generated/macos")
set(required_files
    "${macos_source_root}/app.h"
    "${macos_source_root}/app.cc"
    "${macos_source_root}/main_mac.mm"
    "${macos_source_root}/process_helper_mac.cc"
    "${CRAYON_CEF_SHELL_SOURCE}/src/process/macos/ui_language_mac.h"
    "${CRAYON_CEF_SHELL_SOURCE}/src/process/macos/ui_language_mac.cc"
    "${CRAYON_CEF_SHELL_SOURCE}/src/process/macos/ui_language_mac.mm"
    "${macos_resource_root}/Info.plist.in"
    "${macos_resource_root}/helper-Info.plist.in"
    "${generated_macos_resource_root}/en.lproj/InfoPlist.strings"
    "${generated_macos_resource_root}/en.lproj/Localizable.strings"
    "${generated_macos_resource_root}/zh-Hans.lproj/InfoPlist.strings"
    "${generated_macos_resource_root}/zh-Hans.lproj/Localizable.strings"
    "${generated_macos_resource_root}/zh-Hant.lproj/InfoPlist.strings"
    "${generated_macos_resource_root}/zh-Hant.lproj/Localizable.strings")
foreach(required_file IN LISTS required_files)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "macOS CEF shell file is missing: ${required_file}")
  endif()
endforeach()

file(READ "${macos_source_root}/app.cc" app_source)
file(READ "${macos_source_root}/main_mac.mm" main_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/process/macos/ui_language_mac.mm"
     ui_language_source)
file(READ "${macos_source_root}/process_helper_mac.cc" helper_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/CMakeLists.txt" cmake_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/macos_adhoc_sign.cmake" signing_source)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/tests/macos_package_contract.cmake"
     package_contract_source)
file(READ "${macos_resource_root}/Info.plist.in" main_plist)
file(READ "${macos_resource_root}/helper-Info.plist.in" helper_plist)

# The macOS product (not just a portable test or Windows target) must link
# the shared window policy consumed by TabController's popup routing.
string(REGEX MATCH
       "target_link_libraries\\(\\$\\{CRAYON_BROWSER_TARGET\\}[ \r\n]+PRIVATE libcef_dll_wrapper[^)]*\\)"
       macos_product_links "${cmake_source}")
string(FIND "${macos_product_links}" "crayon::browser-windows" window_policy_index)
if(window_policy_index EQUAL -1)
  message(FATAL_ERROR "macOS product must link the shared window policy")
endif()
string(FIND "${app_source}" "#include \"macos/media_host_process_mac.h\""
       media_host_header_index)
if(media_host_header_index EQUAL -1)
  message(FATAL_ERROR "macOS app must include its concrete media host transport")
endif()
string(REGEX MATCH
       "target_link_libraries\\([ \r\n]*crayon_page_snapshot_cef_integration_test[^)]*\\)"
       macos_integration_links "${cmake_source}")
string(FIND "${macos_integration_links}" "crayon::browser-windows"
       integration_policy_index)
if(integration_policy_index EQUAL -1)
  message(FATAL_ERROR "macOS integration target must link the shared window policy")
endif()
file(READ "${CRAYON_CEF_SHELL_SOURCE}/tests/page_snapshot_cef_integration_mac.mm"
     integration_source)
string(FIND "${integration_source}" "#include \"macos/media_host_process_mac.h\""
       integration_transport_index)
if(integration_transport_index EQUAL -1)
  message(FATAL_ERROR "macOS integration must include its concrete media host transport")
endif()

string(REGEX MATCHALL "crayon://newtab" initial_urls "${app_source}")
list(LENGTH initial_urls initial_url_count)
if(NOT initial_url_count EQUAL 1)
  message(FATAL_ERROR "macOS shell must contain exactly one crayon://newtab URL")
endif()

foreach(required_cast_token
        "strings.cast_code_label" "strings.playback_pause"
        "strings.playback_resume" "strings.playback_seek"
        "RequestResolveCastCode" "RequestControlCast"
        "ConnectCastCode" "SetPaused" "SeekSession"
        "CastChromePresentation(cast_shell_->presentation())")
  string(FIND "${app_source}" "${required_cast_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS app is missing Cast wiring ${required_cast_token}")
  endif()
endforeach()

foreach(required_mdv_token
        "RegisterMdvSchemeHandlerFactory"
        "MdvShortcutPlatform::kMacOS"
        "SetPageQueryHandler"
        "SetLocalEntryCommandHandler")
  string(FIND "${app_source}" "${required_mdv_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS app is missing MDV token ${required_mdv_token}")
  endif()
endforeach()

foreach(required_main_token
        "CefScopedLibraryLoader"
        "LoadInMain"
        "CefInitialize"
        "CefRunMessageLoop"
        "CefShutdown"
        "CEF_USE_SANDBOX"
        "CrayonApplication"
        "CrayonAppDelegate"
        "ResolveMacLocaleSnapshot"
        "ReadMacPreferredUiLanguages"
        "settings.locale"
        "settings.accept_language_list"
        "product_strings_valid")
  string(FIND "${main_source}" "${required_main_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS main entry is missing ${required_main_token}")
  endif()
endforeach()
foreach(required_locale_token
        "CFLocaleCopyPreferredLanguages"
        "kMaximumPreferredLocaleCount"
        "kMaximumPreferredLocaleBytes")
  string(FIND "${ui_language_source}" "${required_locale_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "macOS UI language adapter is missing ${required_locale_token}")
  endif()
endforeach()
foreach(forbidden_app_token
        "DefaultNewTabStrings"
        "DefaultMdvStrings"
        "CFBundleCopyLocalizedString"
        "PreferredLanguage")
  string(FIND "${app_source}" "${forbidden_app_token}" token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR
            "macOS app retains legacy localized string path ${forbidden_app_token}")
  endif()
endforeach()
string(FIND "${main_source}" "CefExecuteProcess" main_execute_index)
if(NOT main_execute_index EQUAL -1)
  message(FATAL_ERROR "macOS main entry must not execute helper processes")
endif()

foreach(required_helper_token
        "LoadInHelper"
        "CefExecuteProcess"
        "CreateNewTabProcessApp"
        "CEF_USE_SANDBOX")
  string(FIND "${helper_source}" "${required_helper_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS helper entry is missing ${required_helper_token}")
  endif()
endforeach()
foreach(forbidden_helper_token
        "CefInitialize"
        "CefRunMessageLoop"
        "CefExecuteProcess(main_args, nullptr"
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
        "LINK_DEPENDS"
        "crayon::browser-mdv"
        "crayon::browser-product-strings"
        "localization/generated/macos/zh-Hant.lproj"
        "MACOSX_BUNDLE_INFO_PLIST")
  string(FIND "${cmake_source}" "${required_cmake_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "macOS CMake graph is missing ${required_cmake_token}")
  endif()
endforeach()

file(READ "${CRAYON_CEF_SHELL_SOURCE}/../../CMakeLists.txt" root_cmake_source)
foreach(required_root_token
        "CRAYON_MACOS_DEPLOYMENT_TARGET"
        "CMAKE_OSX_DEPLOYMENT_TARGET")
  string(FIND "${root_cmake_source}" "${required_root_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "root CMake must seed ${required_root_token} before product targets")
  endif()
endforeach()

foreach(required_signing_token
        "Versions/A/Libraries/*.dylib"
        "sign_bundle(\"\${cef_framework}\")"
        "\${CRAYON_APP_BUNDLE}/Contents/Frameworks/\${helper_name}.app")
  string(FIND "${signing_source}" "${required_signing_token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "macOS signing graph is missing ${required_signing_token}")
  endif()
endforeach()
foreach(required_package_token
        "Bundled app.icns differs from the managed brand asset"
        "assert_code_signed")
  string(FIND "${package_contract_source}" "${required_package_token}"
         token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR
            "macOS package contract is missing ${required_package_token}")
  endif()
endforeach()
string(FIND "${package_contract_source}" "COMMAND /usr/bin/iconutil"
       iconutil_command_index)
if(NOT iconutil_command_index EQUAL -1)
  message(FATAL_ERROR
          "macOS package contract must not depend on host iconutil round-trips")
endif()

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
  # The shell controller may project the closed host route into the shared UI
  # state enum. Keep every other occurrence forbidden so this exception cannot
  # grow into a platform-owned transport implementation.
  if(production_file MATCHES "/cast_shell_controller\\.cc$")
    string(REPLACE "PolicyOutcome::kRelay" "" contents "${contents}")
  endif()
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
