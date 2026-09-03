add_library(crayon-browser-localization STATIC
  src/locale_catalog.cc
  src/locale_snapshot.cc
)
add_library(crayon::browser-localization ALIAS crayon-browser-localization)

target_include_directories(crayon-browser-localization
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(crayon-browser-localization PUBLIC cxx_std_17)
set_target_properties(crayon-browser-localization PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-localization
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-localization
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_localization_test
    tests/locale_snapshot_test.cc
  )
  target_link_libraries(crayon_browser_localization_test
    PRIVATE crayon::browser-localization
  )
  target_compile_features(crayon_browser_localization_test PRIVATE cxx_std_17)

  add_executable(crayon_browser_localization_header_test
    tests/public_header_compile.cc
  )
  target_include_directories(crayon_browser_localization_header_test
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_compile_features(crayon_browser_localization_header_test PRIVATE cxx_std_17)

  foreach(target_name
          crayon_browser_localization_test
          crayon_browser_localization_header_test)
    set_target_properties(${target_name} PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
    )
    if(MSVC)
      target_compile_options(${target_name} PRIVATE /W4 /WX /permissive-)
    else()
      target_compile_options(${target_name}
        PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()

  add_test(NAME browser_localization_contract
           COMMAND crayon_browser_localization_test)
  add_test(NAME browser_localization_public_header
           COMMAND crayon_browser_localization_header_test)
  add_test(
    NAME browser_localization_source_boundary
    COMMAND "${CMAKE_COMMAND}"
            "-DCRAYON_LOCALIZATION_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_boundary_contract.cmake")
  set_tests_properties(browser_localization_contract
                       browser_localization_public_header
                       browser_localization_source_boundary
                       PROPERTIES LABELS "browser;localization;contract")
endif()
