add_library(crayon-cef-shell-network-observer STATIC
  network_observer.cc
)
add_library(crayon::cef-shell-network-observer ALIAS crayon-cef-shell-network-observer)

target_include_directories(crayon-cef-shell-network-observer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_compile_features(crayon-cef-shell-network-observer PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-network-observer PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-network-observer
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-network-observer
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_network_observer_test
    network_observer_test.cc
  )
  target_compile_features(crayon_network_observer_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_network_observer_test
    PRIVATE crayon::cef-shell-network-observer
  )
  if(MSVC)
    target_compile_options(crayon_network_observer_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_network_observer_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME network_observer COMMAND crayon_network_observer_test)
endif()
