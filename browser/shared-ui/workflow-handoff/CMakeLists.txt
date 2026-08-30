add_library(crayon-browser-workflow-handoff STATIC
  src/workflow_handoff.cc
)
add_library(crayon::browser-workflow-handoff ALIAS crayon-browser-workflow-handoff)

target_include_directories(crayon-browser-workflow-handoff
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-workflow-handoff PUBLIC cxx_std_17)
set_target_properties(crayon-browser-workflow-handoff PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(crayon-browser-workflow-handoff PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(crayon-browser-workflow-handoff PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_workflow_handoff_test tests/workflow_handoff_test.cc)
  target_compile_features(crayon_browser_workflow_handoff_test PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_workflow_handoff_test PRIVATE crayon::browser-workflow-handoff)
  if(MSVC)
    target_compile_options(crayon_browser_workflow_handoff_test PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(crayon_browser_workflow_handoff_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME workflow_handoff COMMAND crayon_browser_workflow_handoff_test)
  set_tests_properties(workflow_handoff PROPERTIES
    ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
endif()
