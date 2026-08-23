add_library(crayon-browser-agent-confirm STATIC
  src/agent_confirm.cc
)
add_library(crayon::browser-agent-confirm ALIAS crayon-browser-agent-confirm)

target_include_directories(crayon-browser-agent-confirm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(crayon-browser-agent-confirm PUBLIC cxx_std_17)
set_target_properties(crayon-browser-agent-confirm PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-browser-agent-confirm
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-browser-agent-confirm
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_browser_agent_confirm_test
    tests/agent_confirm_test.cc
  )
  target_compile_features(crayon_browser_agent_confirm_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_browser_agent_confirm_test
    PRIVATE crayon::browser-agent-confirm
  )
  if(MSVC)
    target_compile_options(crayon_browser_agent_confirm_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_browser_agent_confirm_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME agent_confirm COMMAND crayon_browser_agent_confirm_test)
  set_tests_properties(agent_confirm
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
  set_tests_properties(agent_confirm
                       PROPERTIES ENVIRONMENT "CRAYON_REPO_ROOT=${PROJECT_SOURCE_DIR}")
endif()
