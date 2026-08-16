add_library(crayon-browser-shared-shell STATIC
  src/command_registry.cc
  src/shell_state.cc
)
add_library(crayon::browser-shared-shell ALIAS crayon-browser-shared-shell)

target_include_directories(crayon-browser-shared-shell
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-shared-shell PUBLIC cxx_std_17)
target_link_libraries(crayon-browser-shared-shell
  PUBLIC crayon::browser-engine-api
)
set_target_properties(crayon-browser-shared-shell PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-shared-shell
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-shared-shell
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_shared_shell_test
    tests/shell_contract_test.cc
  )
  target_compile_features(crayon_browser_shared_shell_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_shared_shell_test
    PRIVATE crayon::browser-shared-shell
  )
  if(MSVC)
    target_compile_options(crayon_browser_shared_shell_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_shared_shell_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME browser_shared_shell_contract
           COMMAND crayon_browser_shared_shell_test)
  set_tests_properties(browser_shared_shell_contract
                       PROPERTIES LABELS "browser;shell;contract")
endif()
