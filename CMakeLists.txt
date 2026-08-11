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

if(CRAYON_ENABLE_CEF)
  include(cmake/cef/IntegrateCef.cmake)
  crayon_integrate_cef()
  if(WIN32 AND CRAYON_BUILD_CEF_SHELL)
    add_subdirectory(browser/cef-shell)
  endif()
endif()
