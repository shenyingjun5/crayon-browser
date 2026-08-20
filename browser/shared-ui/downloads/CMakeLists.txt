add_library(crayon-browser-downloads-view STATIC
  src/download_shelf_state_machine.cc
)
add_library(crayon::browser-downloads-view ALIAS crayon-browser-downloads-view)

target_include_directories(crayon-browser-downloads-view
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-downloads-view PUBLIC cxx_std_17)
set_target_properties(crayon-browser-downloads-view PROPERTIES
  CXX_EXTENSIONS OFF
)
target_link_libraries(crayon-browser-downloads-view
  PUBLIC crayon::browser-downloads
)

if(MSVC)
  target_compile_options(crayon-browser-downloads-view
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-downloads-view
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_downloads_view_test
    tests/download_shelf_contract_test.cc
  )
  target_compile_features(crayon_browser_downloads_view_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_downloads_view_test
    PRIVATE crayon::browser-downloads-view
  )
  if(MSVC)
    target_compile_options(crayon_browser_downloads_view_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_downloads_view_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME download_shelf_contract
           COMMAND crayon_browser_downloads_view_test)
  set_tests_properties(download_shelf_contract
                       PROPERTIES LABELS "browser;downloads;contract")
endif()
