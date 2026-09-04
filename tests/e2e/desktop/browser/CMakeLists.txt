# CEF-14: desktop browser E2E smoke harness.
#
# ctest always runs the deterministic selfcheck (redaction vectors,
# process expectations). The full app smoke runs on demand:
#   ctest -R browser_e2e_smoke_app
# with -DCRAYON_E2E_APP_BUNDLE=<path to CrayonBrowser.app> configured
# (macOS first; the harness refuses to run without it).
cmake_minimum_required(VERSION 3.21)

find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_FOUND)
  message(STATUS "Python3 not found: browser E2E harness disabled")
  return()
endif()

set(CRAYON_BROWSER_SMOKE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/run_smoke.py")
set(CRAYON_LOCALIZATION_PROBE_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/run_localization_probe.py")

add_test(
  NAME browser_e2e_smoke_selfcheck
  COMMAND "${Python3_EXECUTABLE}" "${CRAYON_BROWSER_SMOKE_SCRIPT}" selfcheck)
set_tests_properties(browser_e2e_smoke_selfcheck
                     PROPERTIES LABELS "e2e;browser;smoke")

add_test(
  NAME browser_localization_probe_selfcheck
  COMMAND "${Python3_EXECUTABLE}" "${CRAYON_LOCALIZATION_PROBE_SCRIPT}"
          --self-check)
set_tests_properties(browser_localization_probe_selfcheck
                     PROPERTIES LABELS "e2e;browser;localization;contract")

add_test(
  NAME page_snapshot_fixture_platform_contract
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/page_snapshot_fixture_test.py")
set_tests_properties(page_snapshot_fixture_platform_contract
                     PROPERTIES LABELS "e2e;browser;content;contract")

if(DEFINED CRAYON_E2E_APP_BUNDLE)
  if(NOT EXISTS "${CRAYON_E2E_APP_BUNDLE}")
    message(FATAL_ERROR "CRAYON_E2E_APP_BUNDLE does not exist: ${CRAYON_E2E_APP_BUNDLE}")
  endif()
  add_test(
    NAME browser_e2e_smoke_app
    COMMAND "${Python3_EXECUTABLE}" "${CRAYON_BROWSER_SMOKE_SCRIPT}" smoke
            "--bundle=${CRAYON_E2E_APP_BUNDLE}"
            "--out=${CMAKE_BINARY_DIR}/e2e-browser-report.json")
  set_tests_properties(browser_e2e_smoke_app
                       PROPERTIES LABELS "e2e;browser;smoke;device")
endif()
