add_library(crayon-browser-omnibox-core STATIC
  src/omnibox_parser.cc
  src/omnibox_state.cc
)
add_library(crayon::browser-omnibox-core ALIAS crayon-browser-omnibox-core)

target_include_directories(crayon-browser-omnibox-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-omnibox-core PUBLIC cxx_std_17)
set_target_properties(crayon-browser-omnibox-core PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-omnibox-core
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-omnibox-core
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_omnibox_parser_test
    tests/omnibox_parser_test.cc
  )
  target_compile_features(crayon_browser_omnibox_parser_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_omnibox_parser_test
    PRIVATE crayon::browser-omnibox-core
  )

  add_executable(crayon_browser_omnibox_state_test
    tests/omnibox_state_test.cc
  )
  target_compile_features(crayon_browser_omnibox_state_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_omnibox_state_test
    PRIVATE crayon::browser-omnibox-core
  )

  if(MSVC)
    target_compile_options(crayon_browser_omnibox_parser_test
      PRIVATE /W4 /WX /permissive-
    )
    target_compile_options(crayon_browser_omnibox_state_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_omnibox_parser_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(crayon_browser_omnibox_state_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()

  add_test(NAME omnibox_parser_contract
           COMMAND crayon_browser_omnibox_parser_test)
  set_tests_properties(omnibox_parser_contract
                       PROPERTIES LABELS "browser;omnibox;core;contract")

  add_test(NAME omnibox_state_contract
           COMMAND crayon_browser_omnibox_state_test)
  set_tests_properties(omnibox_state_contract
                       PROPERTIES LABELS "browser;omnibox;core;contract")
endif()
