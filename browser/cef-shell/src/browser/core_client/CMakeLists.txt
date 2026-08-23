add_library(crayon-cef-shell-core-client STATIC
  core_client_supervisor.cc
)
add_library(crayon::cef-shell-core-client ALIAS crayon-cef-shell-core-client)

target_include_directories(crayon-cef-shell-core-client
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_compile_features(crayon-cef-shell-core-client PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-core-client PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-core-client
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-core-client
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_core_client_supervisor_test
    core_client_supervisor_test.cc
  )
  target_compile_features(crayon_core_client_supervisor_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_core_client_supervisor_test
    PRIVATE crayon::cef-shell-core-client
  )
  if(MSVC)
    target_compile_options(crayon_core_client_supervisor_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_core_client_supervisor_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME core_client_supervisor COMMAND crayon_core_client_supervisor_test)
endif()
