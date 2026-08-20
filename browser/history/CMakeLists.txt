add_library(crayon-browser-history STATIC
  src/history_codec.cc
  src/history_store.cc
)
add_library(crayon::browser-history ALIAS crayon-browser-history)

target_include_directories(crayon-browser-history
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-history PUBLIC cxx_std_17)
set_target_properties(crayon-browser-history PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-history
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-history
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_history_test
    tests/history_store_test.cc
  )
  target_compile_features(crayon_browser_history_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_history_test
    PRIVATE crayon::browser-history
  )
  if(MSVC)
    target_compile_options(crayon_browser_history_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_history_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME history_contract
           COMMAND crayon_browser_history_test)
  set_tests_properties(history_contract
                       PROPERTIES LABELS "browser;history;contract")
endif()
