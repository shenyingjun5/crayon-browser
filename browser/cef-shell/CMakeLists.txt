if(NOT OS_WINDOWS)
  message(FATAL_ERROR "browser/cef-shell currently supports Windows only")
endif()

set(CRAYON_BROWSER_TARGET crayon_browser)
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
    src/windows/app.cc
    src/windows/app.h
    src/windows/main_win.cc
    resources/windows/resource_ids.h
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")

SET_CEF_TARGET_OUT_DIR()
ADD_LOGICAL_TARGET(crayon_libcef "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

add_executable(${CRAYON_BROWSER_TARGET} WIN32 ${crayon_browser_sources})
SET_EXECUTABLE_TARGET_PROPERTIES(${CRAYON_BROWSER_TARGET})
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

COPY_FILES("${CRAYON_BROWSER_TARGET}" "${CEF_BINARY_FILES}"
           "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CRAYON_BROWSER_TARGET}" "${CEF_RESOURCE_FILES}"
           "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

string(REPLACE ";" "\n" crayon_cef_runtime_manifest
               "${CEF_BINARY_FILES};${CEF_RESOURCE_FILES}")
file(GENERATE
     OUTPUT "${CEF_TARGET_OUT_DIR}/cef-runtime-files.txt"
     CONTENT "${crayon_cef_runtime_manifest}\n")

if(CRAYON_BUILD_TESTS)
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
            "$<TARGET_FILE:${CRAYON_BROWSER_TARGET}>"
            "$<TARGET_FILE_DIR:${CRAYON_BROWSER_TARGET}>/cef-runtime-files.txt")
  add_test(
    NAME windows_cef_shell_source_contract
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_CEF_SHELL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_contract.cmake")
  set_tests_properties(
    windows_cef_shell_package_contract windows_cef_shell_source_contract
    PROPERTIES LABELS "cef;shell;windows")
endif()
