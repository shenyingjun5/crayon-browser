add_library(crayon-browser-windows STATIC
  src/popup_policy.cc
  src/window_state_machine.cc
)
add_library(crayon::browser-windows ALIAS crayon-browser-windows)

target_include_directories(crayon-browser-windows
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-windows PUBLIC cxx_std_17)
set_target_properties(crayon-browser-windows PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-windows
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-windows
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_windows_test
    tests/windows_contract_test.cc
  )
  target_compile_features(crayon_browser_windows_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_windows_test
    PRIVATE crayon::browser-windows
  )
  if(MSVC)
    target_compile_options(crayon_browser_windows_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_windows_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME windows_contract
           COMMAND crayon_browser_windows_test)
  set_tests_properties(windows_contract
                       PROPERTIES LABELS "browser;windows;contract")
endif()
