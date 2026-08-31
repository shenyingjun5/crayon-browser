add_library(crayon-cef-shell-observation-gateway STATIC
  observation_gateway.cc
)
add_library(crayon::cef-shell-observation-gateway ALIAS crayon-cef-shell-observation-gateway)

target_include_directories(crayon-cef-shell-observation-gateway
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_compile_features(crayon-cef-shell-observation-gateway PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-observation-gateway PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-observation-gateway
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-observation-gateway
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_observation_gateway_test
    observation_gateway_test.cc
  )
  target_compile_features(crayon_observation_gateway_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_observation_gateway_test
    PRIVATE crayon::cef-shell-observation-gateway
  )
  if(MSVC)
    target_compile_options(crayon_observation_gateway_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_observation_gateway_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME observation_gateway COMMAND crayon_observation_gateway_test)
endif()
