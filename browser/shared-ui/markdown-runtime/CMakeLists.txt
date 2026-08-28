add_library(crayon-browser-markdown-runtime STATIC
  src/extension_registry.cc
  src/runtime_assets.cc
  src/runtime_session.cc
)
add_library(crayon::browser-markdown-runtime ALIAS
            crayon-browser-markdown-runtime)

target_include_directories(crayon-browser-markdown-runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-markdown-runtime
  PUBLIC crayon::browser-markdown)
target_compile_features(crayon-browser-markdown-runtime PUBLIC cxx_std_17)
set_target_properties(crayon-browser-markdown-runtime PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-markdown-runtime
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-markdown-runtime
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_markdown_runtime_registry_test
    tests/extension_registry_test.cc
  )
  target_link_libraries(crayon_browser_markdown_runtime_registry_test
    PRIVATE crayon::browser-markdown-runtime)
  target_compile_features(crayon_browser_markdown_runtime_registry_test
    PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_markdown_runtime_registry_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_markdown_runtime_registry_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME markdown_runtime_registry
           COMMAND crayon_browser_markdown_runtime_registry_test)

  add_executable(crayon_browser_markdown_runtime_lifecycle_test
    tests/runtime_lifecycle_test.cc
  )
  target_link_libraries(crayon_browser_markdown_runtime_lifecycle_test
    PRIVATE crayon::browser-markdown-runtime)
  target_compile_features(crayon_browser_markdown_runtime_lifecycle_test
    PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_markdown_runtime_lifecycle_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_markdown_runtime_lifecycle_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME markdown_runtime_lifecycle
           COMMAND crayon_browser_markdown_runtime_lifecycle_test)
endif()
