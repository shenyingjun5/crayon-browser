add_library(crayon-browser-privacy-strict STATIC
  src/strict_mode.cc
)
add_library(crayon::browser-privacy-strict ALIAS crayon-browser-privacy-strict)

target_include_directories(crayon-browser-privacy-strict
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-privacy-strict PUBLIC cxx_std_17)
set_target_properties(crayon-browser-privacy-strict PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-privacy-strict
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-privacy-strict
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_privacy_strict_test
    tests/strict_mode_contract_test.cc
  )
  target_compile_features(crayon_browser_privacy_strict_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_privacy_strict_test
    PRIVATE crayon::browser-privacy-strict
  )
  if(MSVC)
    target_compile_options(crayon_browser_privacy_strict_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_privacy_strict_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME privacy_strict_contract
           COMMAND crayon_browser_privacy_strict_test)
  set_tests_properties(privacy_strict_contract
                       PROPERTIES LABELS "browser;privacy;contract")
endif()
