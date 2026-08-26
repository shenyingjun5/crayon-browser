add_library(crayon-browser-autofill-address STATIC
  src/address_book.cc
)
add_library(crayon::browser-autofill-address ALIAS crayon-browser-autofill-address)

target_include_directories(crayon-browser-autofill-address
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-autofill-address PUBLIC cxx_std_17)
set_target_properties(crayon-browser-autofill-address PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-autofill-address
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-autofill-address
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_address_book_test
    tests/address_book_test.cc
  )
  target_compile_features(crayon_browser_address_book_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_address_book_test
    PRIVATE crayon::browser-autofill-address
  )
  if(MSVC)
    target_compile_options(crayon_browser_address_book_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_address_book_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME address_book COMMAND crayon_browser_address_book_test)
endif()
