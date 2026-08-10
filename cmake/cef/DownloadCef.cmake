include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CefDistribution.cmake")

set(CRAYON_CEF_DOWNLOAD_TIMEOUT_SECONDS 1800)
set(CRAYON_CEF_DOWNLOAD_INACTIVITY_TIMEOUT_SECONDS 120)
set(CRAYON_CEF_DOWNLOAD_LOCK_TIMEOUT_SECONDS 60)
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

function(crayon_cef_verify_archive)
  cmake_parse_arguments(ARG "" "ARCHIVE;EXPECTED_SHA1;OUT_VALID;OUT_REASON" "" ${ARGN})
  foreach(required_argument ARCHIVE EXPECTED_SHA1 OUT_VALID OUT_REASON)
    if(NOT ARG_${required_argument})
      message(FATAL_ERROR "crayon_cef_verify_archive requires ${required_argument}")
    endif()
  endforeach()

  if(NOT EXISTS "${ARG_ARCHIVE}")
    set(${ARG_OUT_VALID} FALSE PARENT_SCOPE)
    set(${ARG_OUT_REASON} "CEF archive does not exist: ${ARG_ARCHIVE}" PARENT_SCOPE)
    return()
  endif()

  file(SHA1 "${ARG_ARCHIVE}" actual_sha1)
  if(NOT actual_sha1 STREQUAL ARG_EXPECTED_SHA1)
    set(${ARG_OUT_VALID} FALSE PARENT_SCOPE)
    set(${ARG_OUT_REASON}
        "CEF archive SHA-1 mismatch: expected ${ARG_EXPECTED_SHA1}, got ${actual_sha1}"
        PARENT_SCOPE)
    return()
  endif()

  set(${ARG_OUT_VALID} TRUE PARENT_SCOPE)
  set(${ARG_OUT_REASON} "" PARENT_SCOPE)
endfunction()

function(crayon_cef_download_archive)
  cmake_parse_arguments(ARG "" "PLATFORM;CACHE_DIR;OUT_ARCHIVE" "" ${ARGN})
  foreach(required_argument PLATFORM CACHE_DIR OUT_ARCHIVE)
    if(NOT ARG_${required_argument})
      message(FATAL_ERROR "crayon_cef_download_archive requires ${required_argument}")
    endif()
  endforeach()

  crayon_cef_distribution(
    PLATFORM "${ARG_PLATFORM}"
    OUT_ARCHIVE archive_name
    OUT_URL archive_url
    OUT_SHA1 expected_sha1)

  get_filename_component(cache_dir "${ARG_CACHE_DIR}" ABSOLUTE)
  file(MAKE_DIRECTORY "${cache_dir}")
  file(
    LOCK "${cache_dir}/.crayon-cef-download.lock"
    GUARD FUNCTION
    TIMEOUT "${CRAYON_CEF_DOWNLOAD_LOCK_TIMEOUT_SECONDS}"
    RESULT_VARIABLE lock_result)
  if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Unable to lock CEF cache '${cache_dir}': ${lock_result}")
  endif()
  set(archive_path "${cache_dir}/${archive_name}")
  set(partial_path "${archive_path}.partial")

  if(EXISTS "${archive_path}")
    crayon_cef_verify_archive(
      ARCHIVE "${archive_path}"
      EXPECTED_SHA1 "${expected_sha1}"
      OUT_VALID archive_valid
      OUT_REASON archive_reason)
    if(NOT archive_valid)
      message(FATAL_ERROR "${archive_reason}; remove or quarantine the invalid cache entry")
    endif()
    message(STATUS "Using verified cached CEF archive: ${archive_path}")
    set(${ARG_OUT_ARCHIVE} "${archive_path}" PARENT_SCOPE)
    return()
  endif()

  file(REMOVE "${partial_path}")
  message(STATUS "Downloading CEF ${CRAYON_CEF_VERSION} ${ARG_PLATFORM} Standard archive")
  file(
    DOWNLOAD "${archive_url}" "${partial_path}"
    TLS_VERIFY ON
    TIMEOUT "${CRAYON_CEF_DOWNLOAD_TIMEOUT_SECONDS}"
    INACTIVITY_TIMEOUT "${CRAYON_CEF_DOWNLOAD_INACTIVITY_TIMEOUT_SECONDS}"
    STATUS download_status
    SHOW_PROGRESS)
  list(GET download_status 0 download_code)
  list(GET download_status 1 download_message)
  if(NOT download_code EQUAL 0)
    file(REMOVE "${partial_path}")
    message(FATAL_ERROR "CEF download failed (${download_code}): ${download_message}")
  endif()

  crayon_cef_verify_archive(
    ARCHIVE "${partial_path}"
    EXPECTED_SHA1 "${expected_sha1}"
    OUT_VALID partial_valid
    OUT_REASON partial_reason)
  if(NOT partial_valid)
    file(REMOVE "${partial_path}")
    message(FATAL_ERROR "${partial_reason}")
  endif()

  file(RENAME "${partial_path}" "${archive_path}" RESULT rename_result)
  if(rename_result)
    file(REMOVE "${partial_path}")
    message(FATAL_ERROR "Unable to finalize CEF archive: ${rename_result}")
  endif()
  set(${ARG_OUT_ARCHIVE} "${archive_path}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(DEFINED CRAYON_CEF_LOCAL_ROOT)
    crayon_cef_validate_root(
      ROOT "${CRAYON_CEF_LOCAL_ROOT}"
      OUT_VALID root_valid
      OUT_REASON root_reason)
    if(NOT root_valid)
      message(FATAL_ERROR "${root_reason}")
    endif()
    message(STATUS "Validated offline CEF root: ${CRAYON_CEF_LOCAL_ROOT}")
  else()
    if(NOT DEFINED CRAYON_CEF_PLATFORM OR NOT DEFINED CRAYON_CEF_CACHE_DIR)
      message(FATAL_ERROR
              "Set CRAYON_CEF_PLATFORM and CRAYON_CEF_CACHE_DIR, or set CRAYON_CEF_LOCAL_ROOT")
    endif()
    crayon_cef_download_archive(
      PLATFORM "${CRAYON_CEF_PLATFORM}"
      CACHE_DIR "${CRAYON_CEF_CACHE_DIR}"
      OUT_ARCHIVE downloaded_archive)
    message(STATUS "CEF archive ready: ${downloaded_archive}")
  endif()
endif()
