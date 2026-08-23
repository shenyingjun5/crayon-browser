add_library(crayon-cef-shell-media-observer STATIC
  media_observer.cc
)
add_library(crayon::cef-shell-media-observer ALIAS crayon-cef-shell-media-observer)

target_include_directories(crayon-cef-shell-media-observer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)
target_compile_features(crayon-cef-shell-media-observer PUBLIC cxx_std_17)
set_target_properties(crayon-cef-shell-media-observer PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(crayon-cef-shell-media-observer
    PRIVATE /W4 /WX /permissive-
  )
else()
  target_compile_options(crayon-cef-shell-media-observer
    PRIVATE -Wall -Wextra -Wpedantic -Werror
  )
endif()

if(CRAYON_BUILD_TESTS)
  add_executable(crayon_media_observer_test
    media_observer_test.cc
  )
  target_compile_features(crayon_media_observer_test
    PRIVATE cxx_std_17)
  target_link_libraries(crayon_media_observer_test
    PRIVATE crayon::cef-shell-media-observer
  )
  if(MSVC)
    target_compile_options(crayon_media_observer_test
      PRIVATE /W4 /WX /permissive-
    )
  else()
    target_compile_options(crayon_media_observer_test
      PRIVATE -Wall -Wextra -Wpedantic -Werror
    )
  endif()
  add_test(NAME media_observer COMMAND crayon_core_client_supervisor_test)
endif()
