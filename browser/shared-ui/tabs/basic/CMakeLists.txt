add_library(crayon-browser-tabs-basic STATIC
  src/tab_strip_state_machine.cc
)
add_library(crayon::browser-tabs-basic ALIAS crayon-browser-tabs-basic)

target_include_directories(crayon-browser-tabs-basic
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-tabs-basic PUBLIC cxx_std_17)
set_target_properties(crayon-browser-tabs-basic PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-tabs-basic
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-tabs-basic
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_tabs_basic_test
    tests/tab_strip_contract_test.cc
  )
  target_compile_features(crayon_browser_tabs_basic_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_tabs_basic_test
    PRIVATE crayon::browser-tabs-basic
  )
  if(MSVC)
    target_compile_options(crayon_browser_tabs_basic_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_tabs_basic_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME tab_strip_contract
           COMMAND crayon_browser_tabs_basic_test)
  set_tests_properties(tab_strip_contract
                       PROPERTIES LABELS "browser;tabs;basic;contract")
endif()
