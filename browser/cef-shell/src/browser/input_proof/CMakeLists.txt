add_library(crayon-cef-shell-input-proof STATIC
  input_proof_gate.cc
)
add_library(crayon::cef-shell-input-proof ALIAS crayon-cef-shell-input-proof)

target_include_directories(crayon-cef-shell-input-proof
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_compile_features(crayon-cef-shell-input-proof PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-input-proof PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-input-proof
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-input-proof
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_input_proof_gate_test
    input_proof_gate_test.cc
  )
  target_compile_features(crayon_input_proof_gate_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_input_proof_gate_test
    PRIVATE crayon::cef-shell-input-proof
  )
  if(MSVC)
    target_compile_options(crayon_input_proof_gate_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_input_proof_gate_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME input_proof_gate COMMAND crayon_input_proof_gate_test)
endif()
