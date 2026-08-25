add_library(crayon-browser-mdv STATIC
  src/mdv_viewer.cc
)
add_library(crayon::browser-mdv ALIAS crayon-browser-mdv)

target_include_directories(crayon-browser-mdv
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-mdv PUBLIC crayon::browser-markdown)
target_compile_features(crayon-browser-mdv PUBLIC cxx_std_17)
set_target_properties(crayon-browser-mdv PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-mdv
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-mdv
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_mdv_test
    tests/mdv_viewer_test.cc
  )
  target_compile_features(crayon_browser_mdv_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_viewer COMMAND crayon_browser_mdv_test)
endif()
