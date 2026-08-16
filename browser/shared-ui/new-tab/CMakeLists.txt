add_library(crayon-browser-new-tab STATIC
  src/new_tab_page.cc
)
add_library(crayon::browser-new-tab ALIAS crayon-browser-new-tab)

target_include_directories(crayon-browser-new-tab
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-new-tab PUBLIC cxx_std_17)
set_target_properties(crayon-browser-new-tab PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-new-tab
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-new-tab
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_new_tab_test
    tests/new_tab_contract_test.cc
  )
  target_compile_features(crayon_browser_new_tab_test PRIVATE cxx_std_17)
  set_target_properties(crayon_browser_new_tab_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  target_link_libraries(crayon_browser_new_tab_test
    PRIVATE crayon::browser-new-tab
  )
  if(MSVC)
    target_compile_options(crayon_browser_new_tab_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_new_tab_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME browser_new_tab_contract
           COMMAND crayon_browser_new_tab_test)
  set_tests_properties(browser_new_tab_contract
                       PROPERTIES LABELS "browser;new-tab;contract")
endif()
