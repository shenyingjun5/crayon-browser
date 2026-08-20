add_library(crayon-browser-navigation STATIC
  src/navigation_controller.cc
  src/site_identity.cc
)
add_library(crayon::browser-navigation ALIAS crayon-browser-navigation)

target_include_directories(crayon-browser-navigation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-navigation PUBLIC cxx_std_17)
set_target_properties(crayon-browser-navigation PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-navigation
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-navigation
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_navigation_test
    tests/navigation_contract_test.cc
  )
  target_compile_features(crayon_browser_navigation_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_navigation_test
    PRIVATE crayon::browser-navigation
  )
  if(MSVC)
    target_compile_options(crayon_browser_navigation_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_navigation_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME navigation_contract
           COMMAND crayon_browser_navigation_test)
  set_tests_properties(navigation_contract
                       PROPERTIES LABELS "browser;navigation;contract")
endif()
