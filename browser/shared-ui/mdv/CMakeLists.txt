set(MDV_ICON_DECLARATIONS "")
function(crayon_embed_mdv_icon symbol file_name)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/design/icons/${file_name}" icon_svg)
  string(REPLACE " xmlns=\"http://www.w3.org/2000/svg\"" "" icon_svg "${icon_svg}")
  string(APPEND MDV_ICON_DECLARATIONS
    "inline constexpr char ${symbol}[] = R\"MDVSVG(${icon_svg})MDVSVG\";\n")
  set(MDV_ICON_DECLARATIONS "${MDV_ICON_DECLARATIONS}" PARENT_SCOPE)
endfunction()

crayon_embed_mdv_icon(kViewSource "view-source.svg")
crayon_embed_mdv_icon(kViewPreview "view-preview.svg")
crayon_embed_mdv_icon(kViewSplit "view-split.svg")
crayon_embed_mdv_icon(kHeading1 "format-heading-1.svg")
crayon_embed_mdv_icon(kHeading2 "format-heading-2.svg")
crayon_embed_mdv_icon(kHeading3 "format-heading-3.svg")
crayon_embed_mdv_icon(kBold "format-bold.svg")
crayon_embed_mdv_icon(kItalic "format-italic.svg")
crayon_embed_mdv_icon(kStrike "format-strike.svg")
crayon_embed_mdv_icon(kInlineCode "format-inline-code.svg")
crayon_embed_mdv_icon(kBulletList "block-bullet-list.svg")
crayon_embed_mdv_icon(kOrderedList "block-ordered-list.svg")
crayon_embed_mdv_icon(kTaskList "block-task-list.svg")
crayon_embed_mdv_icon(kQuote "block-quote.svg")
crayon_embed_mdv_icon(kCodeBlock "block-code.svg")
crayon_embed_mdv_icon(kTable "block-table.svg")
crayon_embed_mdv_icon(kLink "block-link.svg")
crayon_embed_mdv_icon(kDivider "block-divider.svg")
crayon_embed_mdv_icon(kStructure "structure-menu.svg")
crayon_embed_mdv_icon(kOutdent "structure-outdent.svg")
crayon_embed_mdv_icon(kIndent "structure-indent.svg")
crayon_embed_mdv_icon(kAlignDefault "structure-align-default.svg")
crayon_embed_mdv_icon(kAlignLeft "structure-align-left.svg")
crayon_embed_mdv_icon(kAlignCenter "structure-align-center.svg")
crayon_embed_mdv_icon(kAlignRight "structure-align-right.svg")

set(mdv_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${mdv_generated_dir}")
configure_file(src/mdv_icons_generated.h.in
               "${mdv_generated_dir}/mdv_icons_generated.h" @ONLY)

add_library(crayon-browser-mdv STATIC
  src/mdv_viewer.cc
  src/mdv_entry_guard.cc
  src/mdv_edit.cc
  src/mdv_save.cc
  src/mdv_page.cc
  src/mdv_images.cc
  src/mdv_transform.cc
)
add_library(crayon::browser-mdv ALIAS crayon-browser-mdv)

target_include_directories(crayon-browser-mdv
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    "${mdv_generated_dir}"
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

  add_executable(crayon_browser_mdv_entry_guard_test
    tests/mdv_entry_guard_test.cc
  )
  target_compile_features(crayon_browser_mdv_entry_guard_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_entry_guard_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_entry_guard_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_entry_guard_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_entry_guard COMMAND crayon_browser_mdv_entry_guard_test)

  add_executable(crayon_browser_mdv_edit_test
    tests/mdv_edit_test.cc
  )
  target_compile_features(crayon_browser_mdv_edit_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_edit_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_edit_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_edit_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_edit COMMAND crayon_browser_mdv_edit_test)

  add_executable(crayon_browser_mdv_save_test
    tests/mdv_save_test.cc
  )
  target_compile_features(crayon_browser_mdv_save_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_save_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_save_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_save_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_save COMMAND crayon_browser_mdv_save_test)

  add_executable(crayon_browser_mdv_page_test
    tests/mdv_page_test.cc
  )
  target_compile_features(crayon_browser_mdv_page_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_page_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_page_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_page_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_page COMMAND crayon_browser_mdv_page_test)

  add_executable(crayon_browser_mdv_images_test
    tests/mdv_images_test.cc
  )
  target_compile_features(crayon_browser_mdv_images_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_images_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_images_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_images_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_images COMMAND crayon_browser_mdv_images_test)

  add_executable(crayon_browser_mdv_transform_test
    tests/mdv_transform_test.cc
  )
  target_compile_features(crayon_browser_mdv_transform_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_mdv_transform_test
    PRIVATE crayon::browser-mdv
  )
  if(MSVC)
    target_compile_options(crayon_browser_mdv_transform_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_mdv_transform_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME mdv_transform COMMAND crayon_browser_mdv_transform_test)
endif()
