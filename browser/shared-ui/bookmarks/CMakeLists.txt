add_library(crayon-browser-bookmarks-view STATIC
  src/bookmark_bar_state_machine.cc
)
add_library(crayon::browser-bookmarks-view ALIAS crayon-browser-bookmarks-view)

target_include_directories(crayon-browser-bookmarks-view
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-bookmarks-view PUBLIC cxx_std_17)
set_target_properties(crayon-browser-bookmarks-view PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-bookmarks-view
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-bookmarks-view
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_bookmarks_view_test
    tests/bookmark_bar_contract_test.cc
  )
  target_compile_features(crayon_browser_bookmarks_view_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_bookmarks_view_test
    PRIVATE crayon::browser-bookmarks-view
  )
  if(MSVC)
    target_compile_options(crayon_browser_bookmarks_view_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_bookmarks_view_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME bookmark_bar_contract
           COMMAND crayon_browser_bookmarks_view_test)
  set_tests_properties(bookmark_bar_contract
                       PROPERTIES LABELS "browser;bookmarks;contract")
endif()
