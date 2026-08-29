cmake_minimum_required(VERSION 3.21)

project(crayon_browser_engine_api VERSION 0.1.0 LANGUAGES CXX)

option(CRAYON_ENGINE_API_BUILD_TESTS "Build browser engine API contract tests" OFF)

add_library(crayon-browser-engine-api STATIC
  src/snapshot.cc
  src/types.cc
)
add_library(crayon::browser-engine-api ALIAS crayon-browser-engine-api)

target_include_directories(crayon-browser-engine-api
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-engine-api PUBLIC cxx_std_17)
set_target_properties(crayon-browser-engine-api PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-engine-api PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(crayon-browser-engine-api PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CRAYON_ENGINE_API_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
