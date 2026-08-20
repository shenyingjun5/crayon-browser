add_library(crayon-browser-settings-view STATIC
  src/settings_page_state_machine.cc
)
add_library(crayon::browser-settings-view ALIAS crayon-browser-settings-view)

target_include_directories(crayon-browser-settings-view
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-settings-view PUBLIC cxx_std_17)
set_target_properties(crayon-browser-settings-view PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-settings-view
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-settings-view
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_settings_view_test
    tests/settings_page_contract_test.cc
  )
  target_compile_features(crayon_browser_settings_view_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_settings_view_test
    PRIVATE crayon::browser-settings-view
  )
  if(MSVC)
    target_compile_options(crayon_browser_settings_view_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_settings_view_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME settings_page_contract
           COMMAND crayon_browser_settings_view_test)
  set_tests_properties(settings_page_contract
                       PROPERTIES LABELS "browser;settings;contract")
endif()
