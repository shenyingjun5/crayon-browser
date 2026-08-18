add_library(crayon-browser-tabs-advanced STATIC
  src/advanced_tab_strip_state_machine.cc
)
add_library(crayon::browser-tabs-advanced ALIAS crayon-browser-tabs-advanced)

target_include_directories(crayon-browser-tabs-advanced
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-tabs-advanced
  PUBLIC crayon::browser-tabs-basic
)
target_compile_features(crayon-browser-tabs-advanced PUBLIC cxx_std_17)
set_target_properties(crayon-browser-tabs-advanced PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-tabs-advanced
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-tabs-advanced
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_tabs_advanced_test
    tests/advanced_tab_strip_contract_test.cc
  )
  target_compile_features(crayon_browser_tabs_advanced_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_tabs_advanced_test
    PRIVATE crayon::browser-tabs-advanced
  )
  if(MSVC)
    target_compile_options(crayon_browser_tabs_advanced_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_tabs_advanced_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME advanced_tab_strip_contract
           COMMAND crayon_browser_tabs_advanced_test)
  set_tests_properties(advanced_tab_strip_contract
                       PROPERTIES LABELS "browser;tabs;advanced;contract")
endif()
