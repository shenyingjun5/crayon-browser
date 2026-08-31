add_library(crayon-cef-shell-ipc STATIC
  src/content_host_codec.cc
  src/ipc_channel_contract.cc
  src/media_host_codec.cc
)
add_library(crayon::cef-shell-ipc ALIAS crayon-cef-shell-ipc)

target_include_directories(crayon-cef-shell-ipc
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-cef-shell-ipc PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-ipc PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-ipc
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-ipc
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_cef_shell_ipc_test
    tests/content_host_codec_test.cc
    tests/ipc_channel_contract_test.cc
    tests/media_host_codec_test.cc
  )
  target_compile_definitions(crayon_cef_shell_ipc_test PRIVATE
    CRAYON_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
  )
  target_compile_features(crayon_cef_shell_ipc_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_cef_shell_ipc_test
    PRIVATE crayon::cef-shell-ipc
  )
  if(MSVC)
    target_compile_options(crayon_cef_shell_ipc_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_cef_shell_ipc_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME ipc_channel_contract COMMAND crayon_cef_shell_ipc_test)
endif()
