include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CefDistribution.cmake")

set(CRAYON_CEF_VERSION_HEADER_MAX_BYTES 65536)
set(CRAYON_CEF_ROOT_REQUIRED_PATHS
    "include/cef_version.h;cmake/cef_variables.cmake;libcef_dll/CMakeLists.txt")

function(crayon_cef_validate_root)
  cmake_parse_arguments(ARG "" "ROOT;OUT_VALID;OUT_REASON" "" ${ARGN})
  foreach(required_argument ROOT OUT_VALID OUT_REASON)
    if(NOT ARG_${required_argument})
      message(FATAL_ERROR "crayon_cef_validate_root requires ${required_argument}")
    endif()
  endforeach()

  get_filename_component(root "${ARG_ROOT}" ABSOLUTE)
  if(NOT IS_DIRECTORY "${root}")
    set(${ARG_OUT_VALID} FALSE PARENT_SCOPE)
    set(${ARG_OUT_REASON} "CEF root is not a directory: ${root}" PARENT_SCOPE)
    return()
  endif()

  foreach(required_path IN LISTS CRAYON_CEF_ROOT_REQUIRED_PATHS)
    if(NOT EXISTS "${root}/${required_path}")
      set(${ARG_OUT_VALID} FALSE PARENT_SCOPE)
      set(${ARG_OUT_REASON} "CEF root is missing ${required_path}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  file(
    READ "${root}/include/cef_version.h"
    version_header
    LIMIT "${CRAYON_CEF_VERSION_HEADER_MAX_BYTES}")
  set(expected_version_macro "#define CEF_VERSION \"${CRAYON_CEF_VERSION}\"")
  string(FIND "${version_header}" "${expected_version_macro}" version_macro_index)
  if(version_macro_index EQUAL -1)
    set(${ARG_OUT_VALID} FALSE PARENT_SCOPE)
    set(${ARG_OUT_REASON}
        "CEF root version does not match ${CRAYON_CEF_VERSION}"
        PARENT_SCOPE)
    return()
  endif()

  set(${ARG_OUT_VALID} TRUE PARENT_SCOPE)
  set(${ARG_OUT_REASON} "" PARENT_SCOPE)
endfunction()
