add_library(crayon-browser-cast-view STATIC
  src/cast_feature_view.cc
)
add_library(crayon::browser-cast-view ALIAS crayon-browser-cast-view)

target_include_directories(crayon-browser-cast-view
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-cast-view PUBLIC cxx_std_17)
set_target_properties(crayon-browser-cast-view PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-cast-view
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-cast-view
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_cast_view_test
    tests/cast_feature_view_test.cc
  )
  target_compile_features(crayon_browser_cast_view_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_cast_view_test
    PRIVATE crayon::browser-cast-view
  )
  if(MSVC)
    target_compile_options(crayon_browser_cast_view_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_cast_view_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME cast_feature_view COMMAND crayon_browser_cast_view_test)
  set_tests_properties(cast_feature_view
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
  set_tests_properties(cast_feature_view
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
endif()
