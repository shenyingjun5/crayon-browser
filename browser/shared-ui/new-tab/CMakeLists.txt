function(crayon_read_locale_string output json key)
  string(JSON value ERROR_VARIABLE json_error GET "${json}" "${key}")
  if(json_error)
    message(FATAL_ERROR "Missing new-tab locale key ${key}: ${json_error}")
  endif()
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  set(${output} "${value}" PARENT_SCOPE)
endfunction()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../locales/en-US.json" crayon_en_locale)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../locales/zh-CN.json" crayon_zh_locale)
foreach(locale_key
        title search_placeholder shortcuts_heading private_heading
        private_description cast_label)
  crayon_read_locale_string("CRAYON_EN_${locale_key}" "${crayon_en_locale}"
                            "newtab.${locale_key}")
  crayon_read_locale_string("CRAYON_ZH_${locale_key}" "${crayon_zh_locale}"
                            "newtab.${locale_key}")
endforeach()

configure_file(
  include/crayon/new_tab/localized_strings.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/crayon/new_tab/localized_strings.h"
  @ONLY
)

add_library(crayon-browser-new-tab STATIC
  src/new_tab.cc
)
add_library(crayon::browser-new-tab ALIAS crayon-browser-new-tab)

target_include_directories(crayon-browser-new-tab
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
)
target_compile_features(crayon-browser-new-tab PUBLIC cxx_std_17)
set_target_properties(crayon-browser-new-tab PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(crayon-browser-new-tab
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-new-tab
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_new_tab_test
    tests/new_tab_contract_test.cc
  )
  target_compile_features(crayon_browser_new_tab_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_new_tab_test
    PRIVATE crayon::browser-new-tab
  )
  if(MSVC)
    target_compile_options(crayon_browser_new_tab_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_new_tab_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME browser_new_tab_contract
           COMMAND crayon_browser_new_tab_test)
  set_tests_properties(browser_new_tab_contract
                       PROPERTIES LABELS "browser;new-tab;contract")
endif()
