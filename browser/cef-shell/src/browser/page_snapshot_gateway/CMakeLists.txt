add_library(crayon-browser-page-snapshot-gateway STATIC
  page_snapshot_gateway.cc
)
add_library(crayon::browser-page-snapshot-gateway ALIAS crayon-browser-page-snapshot-gateway)
target_include_directories(crayon-browser-page-snapshot-gateway
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(crayon-browser-page-snapshot-gateway
  PUBLIC crayon::browser-engine-api)
target_compile_features(crayon-browser-page-snapshot-gateway PUBLIC cxx_std_17)
set_target_properties(crayon-browser-page-snapshot-gateway PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(crayon-browser-page-snapshot-gateway PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(crayon-browser-page-snapshot-gateway PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_page_snapshot_gateway_test page_snapshot_gateway_test.cc)
  target_link_libraries(crayon_page_snapshot_gateway_test
    PRIVATE crayon::browser-page-snapshot-gateway)
  target_compile_features(crayon_page_snapshot_gateway_test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_page_snapshot_gateway_test PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_page_snapshot_gateway_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME page_snapshot_gateway COMMAND crayon_page_snapshot_gateway_test)
endif()
