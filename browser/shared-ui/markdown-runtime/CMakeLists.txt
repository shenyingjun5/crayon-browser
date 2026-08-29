set(katex_manifest "${PROJECT_SOURCE_DIR}/third_party/katex/manifest.json")
file(READ "${katex_manifest}" katex_manifest_json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${katex_manifest}")
string(JSON katex_schema GET "${katex_manifest_json}" schema)
string(JSON katex_package_name GET "${katex_manifest_json}" package name)
string(JSON katex_package_version GET "${katex_manifest_json}" package version)
string(JSON katex_package_license GET "${katex_manifest_json}" package license)
string(JSON katex_runtime_dependencies GET "${katex_manifest_json}" policy
       browserRuntimeDependencies)
string(JSON katex_font_count GET "${katex_manifest_json}" policy fontCount)
if(NOT katex_schema STREQUAL "crayon-katex-assets/v1" OR
   NOT katex_package_name STREQUAL "katex" OR
   NOT katex_package_version STREQUAL "0.18.4" OR
   NOT katex_package_license STREQUAL "MIT" OR
   NOT katex_runtime_dependencies EQUAL 0 OR NOT katex_font_count EQUAL 20)
  message(FATAL_ERROR "KaTeX manifest violates the MRT-07 lock")
endif()

set(KATEX_ASSET_DECLARATIONS "")
set(KATEX_ASSET_ROWS "")
set(katex_asset_index 0)
function(crayon_embed_katex_asset resource_id source_path content_type)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${source_path}")
  file(READ "${source_path}" asset_hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," asset_literals
         "${asset_hex}")
  set(asset_symbol "kKatexAsset${katex_asset_index}")
  string(APPEND KATEX_ASSET_DECLARATIONS
         "inline constexpr unsigned char ${asset_symbol}[] = {${asset_literals}};\n")
  string(APPEND KATEX_ASSET_ROWS
         "  {\"${resource_id}\", ${asset_symbol}, sizeof(${asset_symbol}), RuntimeAssetContentType::${content_type}},\n")
  math(EXPR katex_asset_index "${katex_asset_index} + 1")
  set(KATEX_ASSET_DECLARATIONS "${KATEX_ASSET_DECLARATIONS}" PARENT_SCOPE)
  set(KATEX_ASSET_ROWS "${KATEX_ASSET_ROWS}" PARENT_SCOPE)
  set(katex_asset_index "${katex_asset_index}" PARENT_SCOPE)
endfunction()

crayon_embed_katex_asset(
  adapter "${CMAKE_CURRENT_SOURCE_DIR}/assets/katex-adapter.js" kJavaScript)
string(JSON katex_file_count LENGTH "${katex_manifest_json}" files)
if(NOT katex_file_count EQUAL 23)
  message(FATAL_ERROR "KaTeX closure must contain exactly 23 locked files")
endif()
math(EXPR katex_file_last "${katex_file_count} - 1")
foreach(file_index RANGE 0 ${katex_file_last})
  string(JSON asset_path GET "${katex_manifest_json}" files ${file_index} path)
  string(JSON expected_bytes GET "${katex_manifest_json}" files ${file_index}
         bytes)
  string(JSON expected_sha256 GET "${katex_manifest_json}" files ${file_index}
         sha256)
  if(asset_path STREQUAL "LICENSE")
    continue()
  endif()
  set(source_path "${PROJECT_SOURCE_DIR}/third_party/katex/${asset_path}")
  file(SIZE "${source_path}" actual_bytes)
  file(SHA256 "${source_path}" actual_sha256)
  if(NOT actual_bytes EQUAL expected_bytes OR
     NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "KaTeX asset does not match manifest: ${asset_path}")
  endif()
  if(asset_path STREQUAL "assets/katex.mjs")
    set(resource_id katex)
    set(content_type kJavaScript)
  elseif(asset_path STREQUAL "assets/katex.min.css")
    set(resource_id stylesheet)
    set(content_type kCss)
  elseif(asset_path MATCHES "^assets/fonts/KaTeX_[A-Za-z0-9-]+\\.woff2$")
    string(REPLACE "assets/" "" resource_id "${asset_path}")
    set(content_type kFont)
  else()
    message(FATAL_ERROR "unknown KaTeX runtime asset: ${asset_path}")
  endif()
  crayon_embed_katex_asset("${resource_id}" "${source_path}" "${content_type}")
endforeach()
if(NOT katex_asset_index EQUAL 23)
  message(FATAL_ERROR "KaTeX embedded runtime closure must contain 23 assets")
endif()

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

set(mermaid_manifest
    "${PROJECT_SOURCE_DIR}/third_party/mermaid/manifest.json")
file(READ "${mermaid_manifest}" mermaid_manifest_json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${mermaid_manifest}")
string(JSON mermaid_schema GET "${mermaid_manifest_json}" schema)
string(JSON mermaid_package_name GET "${mermaid_manifest_json}" package name)
string(JSON mermaid_package_version GET "${mermaid_manifest_json}" package
       version)
string(JSON mermaid_package_license GET "${mermaid_manifest_json}" package
       license)
string(JSON mermaid_entry GET "${mermaid_manifest_json}" policy entry)
string(JSON mermaid_external GET "${mermaid_manifest_json}" policy
       externalImports)
string(JSON mermaid_network GET "${mermaid_manifest_json}" policy
       networkImports)
string(JSON mermaid_total_bytes GET "${mermaid_manifest_json}" policy
       totalBytes)
string(JSON mermaid_file_count LENGTH "${mermaid_manifest_json}" files)
if(NOT mermaid_schema STREQUAL "crayon-mermaid-assets/v1" OR
   NOT mermaid_package_name STREQUAL "mermaid" OR
   NOT mermaid_package_version STREQUAL "11.17.2" OR
   NOT mermaid_package_license STREQUAL "MIT" OR
   NOT mermaid_entry STREQUAL "mermaid.esm.min.mjs" OR
   NOT mermaid_external EQUAL 0 OR NOT mermaid_network EQUAL 0 OR
   NOT mermaid_file_count EQUAL 104 OR NOT mermaid_total_bytes EQUAL 3522090)
  message(FATAL_ERROR "Mermaid manifest violates the MDV-14 lock")
endif()

set(markdown_runtime_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${markdown_runtime_generated_dir}")
configure_file(
  src/highlight_assets_generated.h.in
  "${markdown_runtime_generated_dir}/highlight_assets_generated.h" @ONLY)
configure_file(
  src/katex_assets_generated.h.in
  "${markdown_runtime_generated_dir}/katex_assets_generated.h" @ONLY)

add_library(crayon-browser-markdown-runtime STATIC
  src/extension_registry.cc
  src/highlight_extension.cc
  src/katex_extension.cc
  src/mermaid_extension.cc
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

  add_executable(crayon_browser_markdown_runtime_katex_test
    tests/katex_extension_test.cc
  )
  target_link_libraries(crayon_browser_markdown_runtime_katex_test
    PRIVATE crayon::browser-markdown-runtime)
  target_compile_features(crayon_browser_markdown_runtime_katex_test
    PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_markdown_runtime_katex_test
      PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_browser_markdown_runtime_katex_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME markdown_runtime_katex
           COMMAND crayon_browser_markdown_runtime_katex_test)

  add_executable(crayon_browser_markdown_runtime_mermaid_test
    tests/mermaid_extension_test.cc
  )
  target_link_libraries(crayon_browser_markdown_runtime_mermaid_test
    PRIVATE crayon::browser-markdown-runtime)
  target_compile_features(crayon_browser_markdown_runtime_mermaid_test
    PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_markdown_runtime_mermaid_test
      PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_browser_markdown_runtime_mermaid_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME markdown_runtime_mermaid
           COMMAND crayon_browser_markdown_runtime_mermaid_test)
endif()
