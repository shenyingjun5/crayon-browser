add_library(crayon-browser-omnibox-provider STATIC
  src/search_provider.cc
)
add_library(crayon::browser-omnibox-provider ALIAS crayon-browser-omnibox-provider)

target_include_directories(crayon-browser-omnibox-provider
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-omnibox-provider PUBLIC cxx_std_17)
set_target_properties(crayon-browser-omnibox-provider PROPERTIES
  CXX_EXTENSIONS OFF
)
target_link_libraries(crayon-browser-omnibox-provider
  PUBLIC crayon::browser-privacy-standard
)

if(MSVC)
  target_compile_options(crayon-browser-omnibox-provider
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-omnibox-provider
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_omnibox_provider_test
    tests/provider_contract_test.cc
  )
  target_compile_features(crayon_browser_omnibox_provider_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_omnibox_provider_test
    PRIVATE crayon::browser-omnibox-provider
  )
  if(MSVC)
    target_compile_options(crayon_browser_omnibox_provider_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_omnibox_provider_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME omnibox_provider_contract
           COMMAND crayon_browser_omnibox_provider_test)
  set_tests_properties(omnibox_provider_contract
                       PROPERTIES LABELS "browser;omnibox;privacy;contract")
endif()
