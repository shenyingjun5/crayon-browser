add_library(crayon-browser-privacy-standard STATIC
  src/privacy_defaults.cc
)
add_library(crayon::browser-privacy-standard ALIAS crayon-browser-privacy-standard)

target_include_directories(crayon-browser-privacy-standard
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-privacy-standard PUBLIC cxx_std_17)
set_target_properties(crayon-browser-privacy-standard PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-privacy-standard
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-privacy-standard
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_privacy_standard_test
    tests/privacy_defaults_contract_test.cc
  )
  target_compile_features(crayon_browser_privacy_standard_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_privacy_standard_test
    PRIVATE crayon::browser-privacy-standard
  )
  if(MSVC)
    target_compile_options(crayon_browser_privacy_standard_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_privacy_standard_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME privacy_defaults_contract
           COMMAND crayon_browser_privacy_standard_test)
  set_tests_properties(privacy_defaults_contract
                       PROPERTIES LABELS "browser;privacy;contract")
endif()
