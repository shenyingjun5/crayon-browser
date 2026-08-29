add_library(crayon-cef-page-snapshot-collector STATIC
  page_snapshot_collector.cc
)
add_library(crayon::cef-page-snapshot-collector ALIAS crayon-cef-page-snapshot-collector)
target_include_directories(crayon-cef-page-snapshot-collector
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(crayon-cef-page-snapshot-collector
  PUBLIC crayon::browser-engine-api)
target_compile_features(crayon-cef-page-snapshot-collector PUBLIC cxx_std_17)
set_target_properties(crayon-cef-page-snapshot-collector PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(crayon-cef-page-snapshot-collector PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(crayon-cef-page-snapshot-collector PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_page_snapshot_collector_test page_snapshot_collector_test.cc)
  target_link_libraries(crayon_page_snapshot_collector_test
    PRIVATE crayon::cef-page-snapshot-collector)
  target_compile_features(crayon_page_snapshot_collector_test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_page_snapshot_collector_test PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_page_snapshot_collector_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME page_snapshot_collector COMMAND crayon_page_snapshot_collector_test)
endif()
