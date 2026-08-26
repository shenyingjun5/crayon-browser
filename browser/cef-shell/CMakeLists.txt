if(NOT OS_WINDOWS AND NOT OS_MAC)
  message(FATAL_ERROR "browser/cef-shell supports Windows and macOS only")
endif()

set(CRAYON_BROWSER_TARGET crayon_browser)
SET_CEF_TARGET_OUT_DIR()

if(OS_WINDOWS)
  if(NOT USE_SANDBOX)
    message(FATAL_ERROR
            "Windows product builds require the CEF sandbox bootstrap")
  endif()
  set(CRAYON_WINDOWS_APP_ICON
      "${PROJECT_SOURCE_DIR}/assets/brand/generated/windows/app.ico")
  set(CRAYON_WINDOWS_APP_MANIFEST
      "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows/app.manifest")
  if(NOT EXISTS "${CRAYON_WINDOWS_APP_ICON}")
    message(FATAL_ERROR "Managed Windows app icon is missing")
  endif()

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  configure_file(
    resources/windows/app.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    @ONLY)

  set(crayon_browser_sources
      src/browser/context/profile_context_factory.cc
      src/browser/context/profile_context_factory.h
      src/browser/context/profile_id_validator.cc
      src/browser/context/profile_id_validator.h
      src/browser/mdv/cef_mdv_entries.cc
      src/browser/mdv/cef_mdv_entries.h
      src/browser/mdv/cef_mdv_handler.cc
      src/browser/mdv/cef_mdv_handler.h
      src/browser/new_tab/cef_new_tab_handler.cc
      src/browser/new_tab/cef_new_tab_handler.h
      src/browser/permission/cef_download_handler.cc
      src/browser/permission/cef_download_handler.h
      src/browser/permission/cef_permission_handler.cc
      src/browser/permission/cef_permission_handler.h
      src/browser/permission/permission_store.cc
      src/browser/permission/permission_store.h
      src/browser/permission/site_origin.cc
      src/browser/permission/site_origin.h
      src/browser/window/tab_controller.cc
      src/browser/window/tab_model.cc
      src/process/windows/bootstrap_entry.cc
      src/process/windows/bootstrap_entry.h
      src/windows/app.cc
      src/windows/app.h
      src/windows/main_win.cc
      src/windows/shell_command_adapter.cc
      src/windows/shell_command_adapter.h
      resources/windows/resource_ids.h
      "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")

  ADD_LOGICAL_TARGET(crayon_libcef "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  add_library(${CRAYON_BROWSER_TARGET} SHARED ${crayon_browser_sources})
  SET_LIBRARY_TARGET_PROPERTIES(${CRAYON_BROWSER_TARGET})
  target_compile_features(${CRAYON_BROWSER_TARGET} PRIVATE cxx_std_17)
  target_include_directories(
    ${CRAYON_BROWSER_TARGET}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows")
  target_compile_definitions(${CRAYON_BROWSER_TARGET}
                             PRIVATE UNICODE _UNICODE)
  add_dependencies(${CRAYON_BROWSER_TARGET} libcef_dll_wrapper)
  target_link_libraries(
    ${CRAYON_BROWSER_TARGET}
    PRIVATE crayon::browser-mdv crayon::browser-new-tab
            crayon::browser-shared-shell crayon_libcef libcef_dll_wrapper
            ${CEF_STANDARD_LIBS})
  set_target_properties(${CRAYON_BROWSER_TARGET}
                        PROPERTIES OUTPUT_NAME "CrayonBrowser")
  COPY_SINGLE_FILE(
    ${CRAYON_BROWSER_TARGET}
    "${CEF_BINARY_DIR}/bootstrap.exe"
    "${CEF_TARGET_OUT_DIR}/CrayonBrowser.exe")
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(
      ${CRAYON_BROWSER_TARGET}
      PROPERTIES VS_DEBUGGER_COMMAND
                 "$<TARGET_FILE_DIR:${CRAYON_BROWSER_TARGET}>/CrayonBrowser.exe")
  endif()

  COPY_FILES("${CRAYON_BROWSER_TARGET}" "${CEF_BINARY_FILES}"
             "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("${CRAYON_BROWSER_TARGET}" "${CEF_RESOURCE_FILES}"
             "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
  SET_LPAC_ACLS("${CRAYON_BROWSER_TARGET}")

  string(REPLACE ";" "\n" crayon_cef_runtime_manifest
                 "${CEF_BINARY_FILES};${CEF_RESOURCE_FILES}")
  file(GENERATE
       OUTPUT "${CEF_TARGET_OUT_DIR}/cef-runtime-files.txt"
       CONTENT "${crayon_cef_runtime_manifest}\n")

  if(CRAYON_BUILD_TESTS)
    add_library(crayon_macos_shell_portable_compile_contract OBJECT
                src/macos/app.cc)
    SET_LIBRARY_TARGET_PROPERTIES(crayon_macos_shell_portable_compile_contract)
    target_compile_features(crayon_macos_shell_portable_compile_contract
                            PRIVATE cxx_std_17)
    target_include_directories(
      crayon_macos_shell_portable_compile_contract
      PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

    add_executable(crayon_windows_shell_contract
                   tests/windows_shell_contract.cc)
    target_compile_features(crayon_windows_shell_contract PRIVATE cxx_std_17)
    target_compile_definitions(crayon_windows_shell_contract
                               PRIVATE UNICODE _UNICODE)
    target_include_directories(
      crayon_windows_shell_contract
      PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows")
    target_compile_options(crayon_windows_shell_contract PRIVATE /W4 /WX)
    add_dependencies(crayon_windows_shell_contract ${CRAYON_BROWSER_TARGET})
    add_test(
      NAME windows_cef_shell_package_contract
      COMMAND crayon_windows_shell_contract
              "$<TARGET_FILE_DIR:${CRAYON_BROWSER_TARGET}>/CrayonBrowser.exe"
              "$<TARGET_FILE:${CRAYON_BROWSER_TARGET}>"
              "$<TARGET_FILE_DIR:${CRAYON_BROWSER_TARGET}>/cef-runtime-files.txt")
    add_test(
      NAME windows_cef_shell_source_contract
      COMMAND "${CMAKE_COMMAND}"
              "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_contract.cmake")
    add_test(
      NAME macos_cef_shell_source_contract
      COMMAND "${CMAKE_COMMAND}"
              "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_source_contract.cmake")
    set_tests_properties(
      windows_cef_shell_package_contract windows_cef_shell_source_contract
      PROPERTIES LABELS "cef;shell;windows")
    set_tests_properties(macos_cef_shell_source_contract
                         PROPERTIES LABELS "cef;shell;macos;contract")
  endif()
endif()

if(OS_MAC)
  if(NOT USE_SANDBOX)
    message(FATAL_ERROR
            "macOS product builds require the CEF sandbox (CEF-02M)")
  endif()
  set(CRAYON_MACOS_EXECUTABLE "CrayonBrowser")
  set(CRAYON_MACOS_PRODUCT_NAME "CrayonBrowser")
  set(CRAYON_MACOS_BUNDLE_ID "com.crayon.browser")
  set(CRAYON_MACOS_APP_ICON
      "${PROJECT_SOURCE_DIR}/assets/brand/generated/macos/app.icns")
  set(CRAYON_MACOS_ICONSET
      "${PROJECT_SOURCE_DIR}/assets/brand/generated/macos/AppIcon.iconset")
  set(CRAYON_MACOS_HELPER_TARGET "crayon_browser_helper")
  set(CRAYON_MACOS_HELPER_OUTPUT "${CRAYON_MACOS_EXECUTABLE} Helper")
  foreach(required_brand_path CRAYON_MACOS_APP_ICON CRAYON_MACOS_ICONSET)
    if(NOT EXISTS "${${required_brand_path}}")
      message(FATAL_ERROR "Managed macOS brand asset is missing: ${${required_brand_path}}")
    endif()
  endforeach()

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(CRAYON_MACOS_VERSION "${PROJECT_VERSION}")
  configure_file(
    resources/macos/Info.plist.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist"
    @ONLY)

  set(crayon_macos_resources
      "${CRAYON_MACOS_APP_ICON}"
      resources/macos/en.lproj/InfoPlist.strings
      resources/macos/zh-Hans.lproj/InfoPlist.strings)
  set(crayon_macos_sources
      src/browser/context/profile_context_factory.cc
      src/browser/context/profile_context_factory.h
      src/browser/context/profile_id_validator.cc
      src/browser/context/profile_id_validator.h
      src/browser/permission/cef_download_handler.cc
      src/browser/permission/cef_download_handler.h
      src/browser/permission/cef_permission_handler.cc
      src/browser/permission/cef_permission_handler.h
      src/browser/permission/permission_store.cc
      src/browser/permission/permission_store.h
      src/browser/permission/site_origin.cc
      src/browser/permission/site_origin.h
      src/browser/window/tab_controller.cc
      src/browser/window/tab_model.cc
      src/macos/app.cc
      src/macos/app.h
      src/macos/main_mac.mm)
  set(crayon_macos_helper_sources src/macos/process_helper_mac.cc)
  set(CRAYON_MACOS_APP
      "${CEF_TARGET_OUT_DIR}/${CRAYON_MACOS_EXECUTABLE}.app")

  add_executable(${CRAYON_BROWSER_TARGET} MACOSX_BUNDLE
                 ${crayon_macos_sources} ${crayon_macos_resources})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CRAYON_BROWSER_TARGET})
  target_compile_features(${CRAYON_BROWSER_TARGET} PRIVATE cxx_std_17)
  # CEF's compile flags only target COMPILE_LANGUAGE:CXX, so Objective-C++
  # sources would fall back to C++17 while CEF 145+ headers require C++20.
  set_target_properties(${CRAYON_BROWSER_TARGET}
                        PROPERTIES OBJCXX_STANDARD 20
                                   OBJCXX_STANDARD_REQUIRED ON)
  set_source_files_properties(src/macos/main_mac.mm
                              PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_include_directories(${CRAYON_BROWSER_TARGET}
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_dependencies(${CRAYON_BROWSER_TARGET} libcef_dll_wrapper)
  target_link_libraries(${CRAYON_BROWSER_TARGET}
                        PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
  set_target_properties(
    ${CRAYON_BROWSER_TARGET}
    PROPERTIES OUTPUT_NAME "${CRAYON_MACOS_EXECUTABLE}"
               MACOSX_BUNDLE_INFO_PLIST
               "${CMAKE_CURRENT_BINARY_DIR}/generated/Info.plist")

  COPY_MAC_FRAMEWORK("${CRAYON_BROWSER_TARGET}" "${CEF_BINARY_DIR}"
                     "${CRAYON_MACOS_APP}")

  set(crayon_macos_helper_names "")
  foreach(helper_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" helper_suffix_list "${helper_suffix_list}")
    list(GET helper_suffix_list 0 helper_name_suffix)
    list(GET helper_suffix_list 1 helper_target_suffix)
    list(GET helper_suffix_list 2 helper_plist_suffix)

    set(helper_target "${CRAYON_MACOS_HELPER_TARGET}${helper_target_suffix}")
    set(CRAYON_MACOS_HELPER_EXECUTABLE
        "${CRAYON_MACOS_HELPER_OUTPUT}${helper_name_suffix}")
    set(CRAYON_MACOS_HELPER_BUNDLE_ID
        "${CRAYON_MACOS_BUNDLE_ID}.helper${helper_plist_suffix}")
    set(helper_info_plist
        "${CMAKE_CURRENT_BINARY_DIR}/generated/helper-Info${helper_target_suffix}.plist")
    configure_file(resources/macos/helper-Info.plist.in
                   "${helper_info_plist}" @ONLY)

    add_executable(${helper_target} MACOSX_BUNDLE
                   ${crayon_macos_helper_sources})
    SET_EXECUTABLE_TARGET_PROPERTIES(${helper_target})
    target_compile_features(${helper_target} PRIVATE cxx_std_17)
    add_dependencies(${helper_target} libcef_dll_wrapper)
    target_link_libraries(${helper_target}
                          PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})
    set_target_properties(
      ${helper_target}
      PROPERTIES OUTPUT_NAME "${CRAYON_MACOS_HELPER_EXECUTABLE}"
                 MACOSX_BUNDLE_INFO_PLIST "${helper_info_plist}")
    add_dependencies(${CRAYON_BROWSER_TARGET} ${helper_target})
    add_custom_command(
      TARGET ${CRAYON_BROWSER_TARGET}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${CEF_TARGET_OUT_DIR}/${CRAYON_MACOS_HELPER_EXECUTABLE}.app"
              "${CRAYON_MACOS_APP}/Contents/Frameworks/${CRAYON_MACOS_HELPER_EXECUTABLE}.app"
      VERBATIM)
    list(APPEND crayon_macos_helper_names
         "${CRAYON_MACOS_HELPER_EXECUTABLE}")
  endforeach()

  set(crayon_macos_resource_prefixes
      "assets/brand/generated/macos/;resources/macos/")
  COPY_MAC_RESOURCES("${crayon_macos_resources}"
                     "${crayon_macos_resource_prefixes}"
                     "${CRAYON_BROWSER_TARGET}"
                     "${CMAKE_CURRENT_SOURCE_DIR}"
                     "${CRAYON_MACOS_APP}")

  string(REPLACE ";" "\n" crayon_macos_helper_manifest
                 "${crayon_macos_helper_names}")
  file(GENERATE
       OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/crayon-macos-helper-apps.txt"
       CONTENT "${crayon_macos_helper_manifest}\n")

  # The macOS sandbox requires signed bundles (ad-hoc for local builds;
  # distribution signing/notarization belongs to PLT-M05). Helpers must be
  # signed before the main app so the main signature covers them.
  add_custom_command(
    TARGET ${CRAYON_BROWSER_TARGET} POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" "-DCRAYON_APP_BUNDLE=${CRAYON_MACOS_APP}"
      "-DCRAYON_HELPER_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/crayon-macos-helper-apps.txt"
      "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/macos_adhoc_sign.cmake"
    COMMENT "Ad-hoc signing CrayonBrowser bundles for the macOS sandbox"
    VERBATIM)

  if(CRAYON_BUILD_TESTS)
    add_test(
      NAME macos_cef_shell_source_contract
      COMMAND "${CMAKE_COMMAND}"
              "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_source_contract.cmake")
    add_test(
      NAME macos_cef_shell_package_contract
      COMMAND "${CMAKE_COMMAND}"
              "-DCRAYON_APP_BUNDLE=${CRAYON_MACOS_APP}"
              "-DCRAYON_HELPER_MANIFEST=${CMAKE_CURRENT_BINARY_DIR}/crayon-macos-helper-apps.txt"
              "-DCRAYON_EXPECTED_ARCH=${PROJECT_ARCH}"
              "-DCRAYON_MANAGED_ICON=${CRAYON_MACOS_APP_ICON}"
              "-DCRAYON_MANAGED_ICONSET=${CRAYON_MACOS_ICONSET}"
              "-DCRAYON_ICONUTIL_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/iconutil-contract.icns"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/macos_package_contract.cmake")
    set_tests_properties(
      macos_cef_shell_source_contract macos_cef_shell_package_contract
      PROPERTIES LABELS "cef;shell;macos")
  endif()
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_window_state_model_test
                 tests/window_state_model_test.cc
                 src/browser/window/tab_model.cc)
  target_compile_features(crayon_window_state_model_test PRIVATE cxx_std_17)
  target_include_directories(crayon_window_state_model_test
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME window_state_model_test
           COMMAND crayon_window_state_model_test)
  add_test(
    NAME window_adapter_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/window_adapter_contract.cmake")
  add_test(
    NAME new_tab_adapter_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/new_tab_adapter_contract.cmake")
  set_tests_properties(window_state_model_test
                       PROPERTIES LABELS "cef;shell;window")
  set_tests_properties(window_adapter_contract
                       PROPERTIES LABELS "cef;shell;window;contract")
  set_tests_properties(new_tab_adapter_contract
                       PROPERTIES LABELS "cef;shell;new-tab;contract")

  add_test(
    NAME mdv_handler_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/mdv_handler_contract.cmake")
  set_tests_properties(mdv_handler_contract
                       PROPERTIES LABELS "cef;shell;mdv;contract")

  add_executable(crayon_profile_id_validator_test
                 tests/profile_id_validator_test.cc
                 src/browser/context/profile_id_validator.cc)
  target_compile_features(crayon_profile_id_validator_test PRIVATE cxx_std_17)
  target_include_directories(crayon_profile_id_validator_test
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME profile_id_validator_test
           COMMAND crayon_profile_id_validator_test)
  add_test(
    NAME profile_context_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/profile_context_contract.cmake")
  set_tests_properties(profile_id_validator_test
                       PROPERTIES LABELS "cef;shell;context")
  set_tests_properties(profile_context_contract
                       PROPERTIES LABELS "cef;shell;context;contract")

  add_executable(crayon_site_origin_test
                 tests/site_origin_test.cc
                 src/browser/permission/site_origin.cc)
  target_compile_features(crayon_site_origin_test PRIVATE cxx_std_17)
  target_include_directories(crayon_site_origin_test
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME site_origin_test COMMAND crayon_site_origin_test)

  add_executable(crayon_permission_store_test
                 tests/permission_store_test.cc
                 src/browser/permission/permission_store.cc)
  target_compile_features(crayon_permission_store_test PRIVATE cxx_std_17)
  target_include_directories(crayon_permission_store_test
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME permission_store_test COMMAND crayon_permission_store_test)

  add_test(
    NAME permission_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/permission_contract.cmake")
  set_tests_properties(site_origin_test
                       PROPERTIES LABELS "cef;shell;permission")
  set_tests_properties(permission_store_test
                       PROPERTIES LABELS "cef;shell;permission")
  set_tests_properties(permission_contract
                       PROPERTIES LABELS "cef;shell;permission;contract")
endif()

