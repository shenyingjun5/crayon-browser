add_library(crayon-browser-product-strings STATIC
  src/product_strings.cc
)
add_library(crayon::browser-product-strings ALIAS crayon-browser-product-strings)

target_include_directories(crayon-browser-product-strings
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(crayon-browser-product-strings
  PUBLIC
    crayon::browser-localization
    crayon::browser-mdv
    crayon::browser-new-tab
)
target_compile_features(crayon-browser-product-strings PUBLIC cxx_std_17)
set_target_properties(crayon-browser-product-strings PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-product-strings
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-product-strings
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_product_strings_test
    tests/product_strings_test.cc
  )
  target_link_libraries(crayon_browser_product_strings_test
    PRIVATE crayon::browser-product-strings
  )
  target_compile_features(crayon_browser_product_strings_test PRIVATE cxx_std_17)
  if(MSVC)
    target_compile_options(crayon_browser_product_strings_test
      PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_browser_product_strings_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME browser_product_strings_contract
           COMMAND crayon_browser_product_strings_test)
  set_tests_properties(browser_product_strings_contract
                       PROPERTIES LABELS "browser;localization;contract")
endif()
