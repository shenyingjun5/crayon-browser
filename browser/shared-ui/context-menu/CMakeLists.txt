add_library(crayon-browser-context-menu STATIC
  src/context_menu.cc
)
add_library(crayon::browser-context-menu ALIAS crayon-browser-context-menu)

target_include_directories(crayon-browser-context-menu
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-context-menu PUBLIC cxx_std_17)
set_target_properties(crayon-browser-context-menu PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-context-menu
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-context-menu
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_context_menu_test
    tests/context_menu_contract_test.cc
  )
  target_compile_features(crayon_browser_context_menu_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_context_menu_test
    PRIVATE crayon::browser-context-menu
  )
  if(MSVC)
    target_compile_options(crayon_browser_context_menu_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_context_menu_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME context_menu_contract COMMAND crayon_browser_context_menu_test)
endif()
