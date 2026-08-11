if(NOT DEFINED ENGINE_API_ROOT)
  message(FATAL_ERROR "ENGINE_API_ROOT is required")
endif()

file(GLOB_RECURSE production_files
  "${ENGINE_API_ROOT}/include/*.h"
  "${ENGINE_API_ROOT}/src/*.cc"
)

if(NOT production_files)
  message(FATAL_ERROR "no engine API production files found")
endif()

set(forbidden_tokens
  "Cef"
  "CEF"
  "ArkWeb"
  "windows.h"
  "Windows.h"
  "AppKit"
  "Cookie"
  "Authorization"
  "WebRTC"
  "JavaScript"
  "selector"
  "Cast"
  "cast-sdk"
  "cast_sdk"
  "Relay"
  "relay"
  "Fake"
  "Mock"
  "<thread>"
  "<future>"
  "<fstream>"
  "condition_variable"
  "socket"
  "asio"
)

foreach(file_path IN LISTS production_files)
  file(READ "${file_path}" contents)
  foreach(token IN LISTS forbidden_tokens)
    string(FIND "${contents}" "${token}" offset)
    if(NOT offset EQUAL -1)
      message(FATAL_ERROR "forbidden token '${token}' in ${file_path}")
    endif()
  endforeach()
endforeach()

message(STATUS "engine API production boundary scan passed")
