add_library(crayon-browser-session STATIC
  src/session_restore.cc
)
add_library(crayon::browser-session ALIAS crayon-browser-session)

target_include_directories(crayon-browser-session
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-session PUBLIC cxx_std_17)
set_target_properties(crayon-browser-session PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-session
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-session
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon-browser-session_test
    tests/session_restore_contract_test.cc
  )
  target_compile_features(crayon-browser-session_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon-browser-session_test
    PRIVATE crayon::browser-session
  )
  if(MSVC)
    target_compile_options(crayon-browser-session_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon-browser-session_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME session_restore_contract_test.cc COMMAND crayon-browser-session_test)
endif()
