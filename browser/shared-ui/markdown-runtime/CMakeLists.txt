set(highlight_manifest
    "${PROJECT_SOURCE_DIR}/third_party/highlight/manifest.json")
file(READ "${highlight_manifest}" highlight_manifest_json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${highlight_manifest}")

string(JSON highlight_schema GET "${highlight_manifest_json}" schema)
string(JSON highlight_package_name GET "${highlight_manifest_json}" package
       name)
string(JSON highlight_package_version GET "${highlight_manifest_json}"
       package version)
string(JSON highlight_package_license GET "${highlight_manifest_json}"
       package license)
string(JSON highlight_auto_detect GET "${highlight_manifest_json}" policy
       autoDetect)
string(JSON highlight_runtime_dependencies GET "${highlight_manifest_json}"
       policy runtimeDependencies)
if(NOT highlight_schema STREQUAL "crayon-highlight-assets/v1" OR
   NOT highlight_package_name STREQUAL "@highlightjs/cdn-assets" OR
   NOT highlight_package_version STREQUAL "11.12.0" OR
   NOT highlight_package_license STREQUAL "BSD-3-Clause" OR
   highlight_auto_detect OR
   NOT highlight_runtime_dependencies EQUAL 0)
  message(FATAL_ERROR "highlight.js manifest violates the MRT-05 lock")
endif()

set(HIGHLIGHT_ASSET_DECLARATIONS "")
set(HIGHLIGHT_ASSET_ROWS "")
set(highlight_asset_index 0)
function(crayon_embed_highlight_asset resource_id source_path)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${source_path}")
  file(READ "${source_path}" asset_bytes)
  string(LENGTH "${asset_bytes}" asset_size)
  set(asset_literals "")
  set(asset_offset 0)
  while(asset_offset LESS asset_size)
    math(EXPR asset_remaining "${asset_size} - ${asset_offset}")
    if(asset_remaining GREATER 8192)
      set(asset_chunk_size 8192)
    else()
      set(asset_chunk_size "${asset_remaining}")
    endif()
    string(SUBSTRING "${asset_bytes}" ${asset_offset} ${asset_chunk_size}
                     asset_chunk)
    string(APPEND asset_literals "R\"CRAYON(${asset_chunk})CRAYON\"\n")
    math(EXPR asset_offset "${asset_offset} + ${asset_chunk_size}")
  endwhile()
  set(asset_symbol "kHighlightAsset${highlight_asset_index}")
  string(APPEND HIGHLIGHT_ASSET_DECLARATIONS
         "inline constexpr char ${asset_symbol}[] =\n${asset_literals};\n")
  string(APPEND HIGHLIGHT_ASSET_ROWS
         "  {\"${resource_id}\", ${asset_symbol}, sizeof(${asset_symbol}) - 1},\n")
  math(EXPR highlight_asset_index "${highlight_asset_index} + 1")
  set(HIGHLIGHT_ASSET_DECLARATIONS "${HIGHLIGHT_ASSET_DECLARATIONS}"
      PARENT_SCOPE)
  set(HIGHLIGHT_ASSET_ROWS "${HIGHLIGHT_ASSET_ROWS}" PARENT_SCOPE)
  set(highlight_asset_index "${highlight_asset_index}" PARENT_SCOPE)
endfunction()

crayon_embed_highlight_asset(
  adapter "${CMAKE_CURRENT_SOURCE_DIR}/assets/highlight-adapter.js")
crayon_embed_highlight_asset(
  core "${PROJECT_SOURCE_DIR}/third_party/highlight/assets/core.min.js")

set(HIGHLIGHT_LANGUAGE_ROWS "")
string(JSON highlight_language_count LENGTH "${highlight_manifest_json}"
       languages)
if(NOT highlight_language_count EQUAL 25)
  message(FATAL_ERROR "highlight.js language allowlist must contain 25 entries")
endif()
math(EXPR highlight_language_last "${highlight_language_count} - 1")
foreach(language_index RANGE 0 ${highlight_language_last})
  string(JSON language_id GET "${highlight_manifest_json}" languages
         ${language_index} id)
  set(language_aliases "")
  string(JSON language_alias_count LENGTH "${highlight_manifest_json}"
         languages ${language_index} aliases)
  math(EXPR language_alias_last "${language_alias_count} - 1")
  foreach(alias_index RANGE 0 ${language_alias_last})
    string(JSON language_alias GET "${highlight_manifest_json}" languages
           ${language_index} aliases ${alias_index})
    if(language_aliases)
      string(APPEND language_aliases ",")
    endif()
    string(APPEND language_aliases "${language_alias}")
  endforeach()
  set(language_dependencies "")
  string(JSON language_dependency_count LENGTH "${highlight_manifest_json}"
         languages ${language_index} dependencies)
  if(language_dependency_count GREATER 0)
    math(EXPR language_dependency_last "${language_dependency_count} - 1")
    foreach(dependency_index RANGE 0 ${language_dependency_last})
      string(JSON language_dependency GET "${highlight_manifest_json}"
             languages ${language_index} dependencies ${dependency_index})
      if(language_dependencies)
        string(APPEND language_dependencies ",")
      endif()
      string(APPEND language_dependencies "${language_dependency}")
    endforeach()
  endif()
  string(APPEND HIGHLIGHT_LANGUAGE_ROWS
         "  {\"${language_id}\", \"${language_aliases}\", \"${language_dependencies}\"},\n")
  crayon_embed_highlight_asset(
    "${language_id}"
    "${PROJECT_SOURCE_DIR}/third_party/highlight/assets/languages/${language_id}.min.js")
endforeach()

set(HIGHLIGHT_PLAINTEXT_ALIASES "")
string(JSON highlight_plaintext_count LENGTH "${highlight_manifest_json}"
       plaintextAliases)
math(EXPR highlight_plaintext_last "${highlight_plaintext_count} - 1")
foreach(alias_index RANGE 0 ${highlight_plaintext_last})
  string(JSON plaintext_alias GET "${highlight_manifest_json}"
         plaintextAliases ${alias_index})
  if(HIGHLIGHT_PLAINTEXT_ALIASES)
    string(APPEND HIGHLIGHT_PLAINTEXT_ALIASES ",")
  endif()
  string(APPEND HIGHLIGHT_PLAINTEXT_ALIASES "${plaintext_alias}")
endforeach()

set(markdown_runtime_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${markdown_runtime_generated_dir}")
configure_file(
  src/highlight_assets_generated.h.in
  "${markdown_runtime_generated_dir}/highlight_assets_generated.h" @ONLY)

add_library(crayon-browser-markdown-runtime STATIC
  src/extension_registry.cc
  src/highlight_extension.cc
  src/runtime_assets.cc
  src/runtime_session.cc
)
add_library(crayon::browser-markdown-runtime ALIAS
            crayon-browser-markdown-runtime)

target_include_directories(crayon-browser-markdown-runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    "${markdown_runtime_generated_dir}"
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

  add_executable(crayon_browser_markdown_runtime_highlight_test
    tests/highlight_extension_test.cc
  )
  target_link_libraries(crayon_browser_markdown_runtime_highlight_test
    PRIVATE crayon::browser-markdown-runtime)
  target_compile_features(crayon_browser_markdown_runtime_highlight_test
    PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_markdown_runtime_highlight_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_markdown_runtime_highlight_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME markdown_runtime_highlight
           COMMAND crayon_browser_markdown_runtime_highlight_test)
endif()
