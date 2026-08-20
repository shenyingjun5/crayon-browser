add_library(crayon-browser-preferences STATIC
  src/preference_codec.cc
  src/preference_store.cc
)
add_library(crayon::browser-preferences ALIAS crayon-browser-preferences)

target_include_directories(crayon-browser-preferences
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-preferences PUBLIC cxx_std_17)
set_target_properties(crayon-browser-preferences PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-preferences
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-preferences
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_preferences_test
    tests/preference_store_test.cc
  )
  target_compile_features(crayon_browser_preferences_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_preferences_test
    PRIVATE crayon::browser-preferences
  )
  if(MSVC)
    target_compile_options(crayon_browser_preferences_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_preferences_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME preferences_contract
           COMMAND crayon_browser_preferences_test)
  set_tests_properties(preferences_contract
                       PROPERTIES LABELS "browser;preferences;contract")
endif()
