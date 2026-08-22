add_library(crayon-browser-page-tools STATIC
  src/page_tools.cc
)
add_library(crayon::browser-page-tools ALIAS crayon-browser-page-tools)

target_include_directories(crayon-browser-page-tools
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-page-tools PUBLIC cxx_std_17)
set_target_properties(crayon-browser-page-tools PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-page-tools
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-page-tools
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_page_tools_test
    tests/page_tools_contract_test.cc
  )
  target_compile_features(crayon_browser_page_tools_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_page_tools_test
    PRIVATE crayon::browser-page-tools
  )
  if(MSVC)
    target_compile_options(crayon_browser_page_tools_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_page_tools_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME page_tools_contract COMMAND crayon_browser_page_tools_test)
endif()
