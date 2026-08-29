function(crayon_engine_api_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

add_executable(browser-engine-contract
  contract_test.cc
  fake_browser_engine.cc
)
target_link_libraries(browser-engine-contract PRIVATE crayon::browser-engine-api)
crayon_engine_api_enable_warnings(browser-engine-contract)
add_test(NAME browser_engine_contract COMMAND browser-engine-contract)

add_executable(browser-engine-headers
  headers/adapter.cc
  headers/browser_engine.cc
  headers/event_sink.cc
  headers/ids.cc
  headers/main.cc
  headers/result.cc
  headers/snapshot.cc
  headers/types.cc
)
target_link_libraries(browser-engine-headers PRIVATE crayon::browser-engine-api)
crayon_engine_api_enable_warnings(browser-engine-headers)
add_test(NAME browser_engine_headers_compile COMMAND browser-engine-headers)

add_test(
  NAME browser_engine_forbidden_api_scan
  COMMAND ${CMAKE_COMMAND}
    -DENGINE_API_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
    -P ${CMAKE_CURRENT_SOURCE_DIR}/forbidden_api_scan.cmake
)
