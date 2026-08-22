add_library(crayon-browser-profiles-view STATIC
  src/profile_picker.cc
)
add_library(crayon::browser-profiles-view ALIAS crayon-browser-profiles-view)

target_include_directories(crayon-browser-profiles-view
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-profiles-view PUBLIC cxx_std_17)
set_target_properties(crayon-browser-profiles-view PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-profiles-view
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-profiles-view
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon-browser-profiles-view_test
    tests/profile_picker_contract_test.cc
  )
  target_compile_features(crayon-browser-profiles-view_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon-browser-profiles-view_test
    PRIVATE crayon::browser-profiles-view
  )
  if(MSVC)
    target_compile_options(crayon-browser-profiles-view_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon-browser-profiles-view_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME profile_picker_contract_test.cc COMMAND crayon-browser-profiles-view_test)
endif()
