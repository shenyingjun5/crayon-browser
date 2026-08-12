if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE OR
   NOT IS_DIRECTORY "${CRAYON_CEF_SHELL_SOURCE}")
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must name the shell source root")
endif()

file(GLOB_RECURSE production_files
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/*.cc"
     "${CRAYON_CEF_SHELL_SOURCE}/src/windows/*.h")
if(NOT production_files)
  message(FATAL_ERROR "CEF shell production sources are missing")
endif()

set(initial_url_count 0)
foreach(production_file IN LISTS production_files)
  file(READ "${production_file}" contents)
  string(REGEX MATCHALL "about:blank" initial_urls "${contents}")
  list(LENGTH initial_urls file_initial_url_count)
  math(EXPR initial_url_count "${initial_url_count} + ${file_initial_url_count}")
  foreach(forbidden_token
          "http://"
          "https://"
          "file(DOWNLOAD"
          "FetchContent"
          "ExternalProject"
          "CastSdk"
          "Cast-SDK"
          "Relay"
          "WebRTC"
          "GetDisplayMedia"
          "DesktopCapturer"
          "Fake"
          "Mock")
    string(FIND "${contents}" "${forbidden_token}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "Production shell contains forbidden token '${forbidden_token}' in ${production_file}")
    endif()
  endforeach()
endforeach()

if(NOT initial_url_count EQUAL 1)
  message(FATAL_ERROR "Production shell must contain exactly one about:blank URL")
endif()

message(STATUS "Windows CEF shell source contract passed")
