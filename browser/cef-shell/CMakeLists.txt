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
      src/process/windows/bootstrap_entry.cc
      src/process/windows/bootstrap_entry.h
      src/windows/app.cc
      src/windows/app.h
      src/windows/main_win.cc
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
    PRIVATE crayon_libcef libcef_dll_wrapper ${CEF_STANDARD_LIBS})
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
