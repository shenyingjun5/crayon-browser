add_library(crayon-browser-site-controls STATIC
  src/site_controls_state_machine.cc
  src/permission_prompt_queue.cc
)
add_library(crayon::browser-site-controls ALIAS crayon-browser-site-controls)

target_include_directories(crayon-browser-site-controls
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-site-controls
  PUBLIC crayon::browser-navigation
)
target_compile_features(crayon-browser-site-controls PUBLIC cxx_std_17)
set_target_properties(crayon-browser-site-controls PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-site-controls
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-site-controls
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_site_controls_test
    tests/site_controls_contract_test.cc
  )
  target_compile_features(crayon_browser_site_controls_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_site_controls_test
    PRIVATE crayon::browser-site-controls
  )
  if(MSVC)
    target_compile_options(crayon_browser_site_controls_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_site_controls_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME site_controls_contract
           COMMAND crayon_browser_site_controls_test)
  set_tests_properties(site_controls_contract
                       PROPERTIES LABELS "browser;site-controls;contract")
endif()
