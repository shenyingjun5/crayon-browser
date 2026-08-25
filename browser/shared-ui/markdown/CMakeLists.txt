add_library(crayon-browser-markdown STATIC
  src/markdown_render.cc
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c.c
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c-html.c
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/entity.c
)
add_library(crayon::browser-markdown ALIAS crayon-browser-markdown)

target_include_directories(crayon-browser-markdown
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/md4c/src
)
target_compile_features(crayon-browser-markdown PUBLIC cxx_std_17)
set_target_properties(crayon-browser-markdown PROPERTIES
  CXX_EXTENSIONS OFF
)

# Vendored md4c is C99; keep its warnings at the project level without
# forcing C++ flags onto it. Vendored upstream sources must not be edited,
# so MSVC warning level/warnings-as-errors are relaxed per file instead.
set_source_files_properties(
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c.c
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c-html.c
  ${PROJECT_SOURCE_DIR}/third_party/md4c/src/entity.c
  PROPERTIES LANGUAGE C)
if(MSVC)
  set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c.c
    ${PROJECT_SOURCE_DIR}/third_party/md4c/src/md4c-html.c
    ${PROJECT_SOURCE_DIR}/third_party/md4c/src/entity.c
    PROPERTIES COMPILE_OPTIONS "/W3;/WX-")
endif()

if(MSVC)
  target_compile_options(crayon-browser-markdown
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-markdown
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_markdown_test
    tests/markdown_render_test.cc
  )
  target_compile_features(crayon_browser_markdown_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_markdown_test
    PRIVATE crayon::browser-markdown
  )
  if(MSVC)
    target_compile_options(crayon_browser_markdown_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_markdown_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME markdown_render COMMAND crayon_browser_markdown_test)
endif()
