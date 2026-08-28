cmake_minimum_required(VERSION 3.21)

project(crayon_mdv_toolbar_design_contract NONE)

find_program(CRAYON_NODE_EXECUTABLE NAMES node REQUIRED)

enable_testing()
add_test(
  NAME mdv_toolbar_design_contract
  COMMAND "${CRAYON_NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify-icons.mjs"
)
add_test(
  NAME mdv_toolbar_design_rejection_contract
  COMMAND "${CRAYON_NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/negative-contract.mjs"
          --work-root "${CMAKE_CURRENT_BINARY_DIR}"
)
set_tests_properties(
  mdv_toolbar_design_contract mdv_toolbar_design_rejection_contract
  PROPERTIES LABELS "browser;mdv;design;contract"
)
