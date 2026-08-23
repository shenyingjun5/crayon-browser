add_library(crayon-browser-chrome STATIC
  src/chrome_toolbar.cc
)
add_library(crayon::browser-chrome ALIAS crayon-browser-chrome)

target_include_directories(crayon-browser-chrome
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-chrome PUBLIC cxx_std_17)
set_target_properties(crayon-browser-chrome PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-chrome
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-chrome
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_chrome_test
    tests/chrome_contract_test.cc
  )
  target_compile_features(crayon_browser_chrome_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_chrome_test
    PRIVATE crayon::browser-chrome
  )
  if(MSVC)
    target_compile_options(crayon_browser_chrome_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_chrome_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME chrome_contract COMMAND crayon_browser_chrome_test)
  set_tests_properties(chrome_contract
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
  set_tests_properties(chrome_contract
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
endif()
