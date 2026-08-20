add_library(crayon-browser-downloads STATIC
  src/download_item.cc
  src/download_path.cc
)
add_library(crayon::browser-downloads ALIAS crayon-browser-downloads)

target_include_directories(crayon-browser-downloads
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-downloads PUBLIC cxx_std_17)
set_target_properties(crayon-browser-downloads PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-downloads
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-downloads
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_downloads_domain_test
    tests/download_domain_test.cc
  )
  target_compile_features(crayon_browser_downloads_domain_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_downloads_domain_test
    PRIVATE crayon::browser-downloads
  )
  if(MSVC)
    target_compile_options(crayon_browser_downloads_domain_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_downloads_domain_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME download_domain_contract
           COMMAND crayon_browser_downloads_domain_test)
  set_tests_properties(download_domain_contract
                       PROPERTIES LABELS "browser;downloads;contract")
endif()
