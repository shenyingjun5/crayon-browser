add_library(crayon-browser-autofill-ui STATIC
  src/autofill_ui.cc
)
add_library(crayon::browser-autofill-ui ALIAS crayon-browser-autofill-ui)

target_include_directories(crayon-browser-autofill-ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-autofill-ui
  PUBLIC crayon::browser-autofill-address
)
target_compile_features(crayon-browser-autofill-ui PUBLIC cxx_std_17)
set_target_properties(crayon-browser-autofill-ui PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-autofill-ui
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-autofill-ui
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_autofill_ui_test
    tests/autofill_ui_test.cc
  )
  target_compile_features(crayon_browser_autofill_ui_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_autofill_ui_test
    PRIVATE crayon::browser-autofill-ui
  )
  if(MSVC)
    target_compile_options(crayon_browser_autofill_ui_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_autofill_ui_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME autofill_ui COMMAND crayon_browser_autofill_ui_test)
  set_tests_properties(autofill_ui
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
endif()
