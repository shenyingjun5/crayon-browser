add_library(crayon-browser-bookmarks STATIC
  src/bookmark_codec.cc
  src/bookmark_store.cc
)
add_library(crayon::browser-bookmarks ALIAS crayon-browser-bookmarks)

target_include_directories(crayon-browser-bookmarks
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-bookmarks PUBLIC cxx_std_17)
set_target_properties(crayon-browser-bookmarks PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-bookmarks
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-bookmarks
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_bookmarks_test
    tests/bookmark_store_test.cc
  )
  target_compile_features(crayon_browser_bookmarks_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_bookmarks_test
    PRIVATE crayon::browser-bookmarks
  )
  if(MSVC)
    target_compile_options(crayon_browser_bookmarks_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_bookmarks_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME bookmarks_contract
           COMMAND crayon_browser_bookmarks_test)
  set_tests_properties(bookmarks_contract
                       PROPERTIES LABELS "browser;bookmarks;contract")
endif()
