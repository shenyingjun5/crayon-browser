cmake_minimum_required(VERSION 3.21)

project(crayon_browser VERSION 0.1.0 LANGUAGES CXX)

option(CRAYON_BUILD_TESTS "Build Crayon browser contract tests" OFF)
option(CRAYON_ENABLE_CEF "Integrate an offline CEF Standard distribution" OFF)
option(CRAYON_BUILD_CEF_SHELL "Build the platform CEF browser shell" ON)
set(CRAYON_CEF_ROOT "" CACHE STRING
    "Absolute path to an extracted CEF distribution")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING
      "Supported multi-config build types" FORCE)
endif()

# The official Windows CEF bootstrap and wrapper use the static MSVC runtime.
# Apply the same runtime before adding product libraries so shared UI/engine
# objects can be linked into the CEF client DLL without mixed-CRT ownership.
if(MSVC AND CRAYON_ENABLE_CEF)
  set(CMAKE_MSVC_RUNTIME_LIBRARY
      "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CRAYON_ENGINE_API_BUILD_TESTS "${CRAYON_BUILD_TESTS}" CACHE BOOL
    "Build browser engine API contract tests" FORCE)
if(CRAYON_BUILD_TESTS)
  enable_testing()
  add_test(
    NAME cef_distribution_contract
    COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts/cef_distribution_contract.cmake")
  add_test(
    NAME cef_build_graph_contract
    COMMAND "${CMAKE_COMMAND}" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts/cef_build_graph_contract.cmake")
  set_tests_properties(cef_distribution_contract cef_build_graph_contract
                       PROPERTIES LABELS "cef;contract")
endif()
add_subdirectory(browser/engine-api)
add_subdirectory(browser/downloads)
add_subdirectory(browser/bookmarks)
add_subdirectory(browser/history)
add_subdirectory(browser/preferences)
add_subdirectory(browser/privacy/standard)
add_subdirectory(browser/privacy/strict)
add_subdirectory(browser/shared-ui/shell)
add_subdirectory(browser/shared-ui/omnibox/core)
add_subdirectory(browser/shared-ui/omnibox/provider)
add_subdirectory(browser/shared-ui/navigation)
add_subdirectory(browser/shared-ui/tabs/basic)
add_subdirectory(browser/shared-ui/tabs/advanced)
add_subdirectory(browser/shared-ui/windows)
add_subdirectory(browser/shared-ui/downloads)
add_subdirectory(browser/shared-ui/bookmarks)
add_subdirectory(browser/shared-ui/history)
add_subdirectory(browser/shared-ui/settings)
add_subdirectory(browser/shared-ui/page-tools)
add_subdirectory(browser/shared-ui/context-menu)
add_subdirectory(browser/session)
add_subdirectory(browser/shared-ui/profiles)
add_subdirectory(browser/shared-ui/chrome)
add_subdirectory(browser/shared-ui/features/cast)
add_subdirectory(browser/shared-ui/agent-confirm)
add_subdirectory(browser/shared-ui/capability-route)
add_subdirectory(tests/e2e/desktop/browser)
add_subdirectory(browser/cef-shell/src/ipc)
add_subdirectory(browser/cef-shell/src/browser/core_client)
add_subdirectory(browser/cef-shell/src/renderer/media_observer)
add_subdirectory(browser/cef-shell/src/browser/input_proof)
add_subdirectory(browser/cef-shell/src/browser/network_observer)
add_subdirectory(browser/cef-shell/src/browser/observation_gateway)
add_subdirectory(browser/shared-ui/site-controls)
add_subdirectory(browser/shared-ui/new-tab)

if(CRAYON_ENABLE_CEF)
  if(APPLE)
    # The CEF wrapper adds .mm sources on macOS, so OBJCXX must be enabled
    # before crayon_integrate_cef() calls add_subdirectory(libcef_dll).
    enable_language(OBJCXX)
  endif()
  include(cmake/cef/IntegrateCef.cmake)
  crayon_integrate_cef()
  if((WIN32 OR APPLE) AND CRAYON_BUILD_CEF_SHELL)
    add_subdirectory(browser/cef-shell)
  endif()
endif()
