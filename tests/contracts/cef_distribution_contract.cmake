cmake_minimum_required(VERSION 3.21)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${REPOSITORY_ROOT}/cmake/cef/CefDistribution.cmake")

function(assert_equal actual expected label)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(FATAL_ERROR "${label}: expected '${expected}', got '${actual}'")
  endif()
endfunction()

assert_equal("${CRAYON_CEF_VERSION}"
             "150.0.10+g8042e43+chromium-150.0.7871.101"
             "CEF version")
assert_equal("${CRAYON_CEF_DISTRIBUTION}" "standard" "CEF distribution")
assert_equal("${CRAYON_CEF_PLATFORM_KEYS}"
             "windows64;macosx64;macosarm64;linux64"
             "supported CEF platforms")

set(expected_windows64_sha1 "b5ae23cec83689ef9843951e182443cacbaff5af")
set(expected_macosx64_sha1 "17e14fe00415e01a79e8b6d7ecaad8a861f1b388")
set(expected_macosarm64_sha1 "2e77063444e3ca07aea2651b763d3c4248bf2543")
set(expected_linux64_sha1 "8ef7861df621ac9ce370ff30161e4c5ba5d7e7de")

foreach(platform IN LISTS CRAYON_CEF_PLATFORM_KEYS)
  crayon_cef_distribution(
    PLATFORM "${platform}"
    OUT_ARCHIVE archive
    OUT_URL url
    OUT_SHA1 sha1)

  string(LENGTH "${sha1}" sha1_length)
  if(NOT sha1_length EQUAL 40 OR NOT sha1 MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "${platform}: invalid SHA-1 '${sha1}'")
  endif()
  assert_equal("${sha1}" "${expected_${platform}_sha1}" "${platform} SHA-1")

  set(expected_archive
      "cef_binary_${CRAYON_CEF_VERSION}_${platform}.tar.bz2")
  assert_equal("${archive}" "${expected_archive}" "${platform} archive")
  if(NOT url MATCHES "^https://cef-builds\\.spotifycdn\\.com/")
    message(FATAL_ERROR "${platform}: download URL is not the official HTTPS origin: ${url}")
  endif()
  if(url MATCHES "\\+")
    message(FATAL_ERROR "${platform}: download URL contains an unescaped plus sign: ${url}")
  endif()
endforeach()

include("${REPOSITORY_ROOT}/cmake/cef/DownloadCef.cmake")

# Windows exports TEMP; macOS/Linux use TMPDIR. Fall back so the contract
# never tries to create directories at the filesystem root.
if(NOT DEFINED ENV{TEMP})
  if(DEFINED ENV{TMPDIR})
    set(ENV{TEMP} "$ENV{TMPDIR}")
  else()
    set(ENV{TEMP} "/tmp")
  endif()
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef contract_suffix)
file(TO_CMAKE_PATH "$ENV{TEMP}/crayon-cef-contract-${contract_suffix}" contract_tmp)
set(valid_root "${contract_tmp}/valid-root")
file(MAKE_DIRECTORY "${valid_root}/include" "${valid_root}/cmake" "${valid_root}/libcef_dll")
file(WRITE
  "${valid_root}/include/cef_version.h"
  "#define CEF_VERSION \"${CRAYON_CEF_VERSION}\"\n")
file(WRITE "${valid_root}/cmake/cef_variables.cmake" "fixture")
file(WRITE "${valid_root}/libcef_dll/CMakeLists.txt" "fixture")

crayon_cef_validate_root(ROOT "${valid_root}" OUT_VALID root_valid OUT_REASON root_reason)
if(NOT root_valid)
  message(FATAL_ERROR "Valid offline CEF root was rejected: ${root_reason}")
endif()
crayon_cef_validate_root(
  ROOT "${contract_tmp}/missing-root"
  OUT_VALID missing_root_valid
  OUT_REASON missing_root_reason)
if(missing_root_valid OR missing_root_reason STREQUAL "")
  message(FATAL_ERROR "Missing offline CEF root must fail with a reason")
endif()

set(wrong_version_root "${contract_tmp}/wrong-version-root")
file(COPY "${valid_root}/" DESTINATION "${wrong_version_root}")
file(WRITE
  "${wrong_version_root}/include/cef_version.h"
  "#define CEF_VERSION \"0.0.0+wrong+chromium-0.0.0.0\"\n")
crayon_cef_validate_root(
  ROOT "${wrong_version_root}"
  OUT_VALID wrong_version_valid
  OUT_REASON wrong_version_reason)
if(wrong_version_valid OR NOT wrong_version_reason MATCHES "version does not match")
  message(FATAL_ERROR "Wrong offline CEF revision must fail with a stable reason")
endif()

set(fixture_archive "${contract_tmp}/fixture.tar.bz2")
file(WRITE "${fixture_archive}" "deterministic-cef-contract-fixture")
file(SHA1 "${fixture_archive}" fixture_sha1)
crayon_cef_verify_archive(
  ARCHIVE "${fixture_archive}"
  EXPECTED_SHA1 "${fixture_sha1}"
  OUT_VALID archive_valid
  OUT_REASON archive_reason)
if(NOT archive_valid)
  message(FATAL_ERROR "Valid cached archive was rejected: ${archive_reason}")
endif()
crayon_cef_verify_archive(
  ARCHIVE "${fixture_archive}"
  EXPECTED_SHA1 "0000000000000000000000000000000000000000"
  OUT_VALID bad_archive_valid
  OUT_REASON bad_archive_reason)
if(bad_archive_valid OR bad_archive_reason STREQUAL "")
  message(FATAL_ERROR "Hash mismatch must fail with a reason")
endif()

set(invalid_cache "${contract_tmp}/.cache/cef")
file(MAKE_DIRECTORY "${invalid_cache}")
crayon_cef_distribution(
  PLATFORM "windows64"
  OUT_ARCHIVE windows_archive
  OUT_URL windows_url
  OUT_SHA1 windows_sha1)
file(WRITE "${invalid_cache}/${windows_archive}" "invalid-cache-entry")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -DREPOSITORY_ROOT=${REPOSITORY_ROOT}
          -DCRAYON_CEF_PLATFORM=windows64
          -DCRAYON_CEF_CACHE_DIR=.cache/cef
          -P "${CMAKE_CURRENT_LIST_DIR}/cef_distribution_invalid_cache.cmake"
  WORKING_DIRECTORY "${contract_tmp}"
  RESULT_VARIABLE invalid_cache_result
  OUTPUT_VARIABLE invalid_cache_output
  ERROR_VARIABLE invalid_cache_error)
if(invalid_cache_result EQUAL 0)
  message(FATAL_ERROR "Invalid cached archive must fail")
endif()
set(invalid_cache_log "${invalid_cache_output}${invalid_cache_error}")
if(NOT invalid_cache_log MATCHES "SHA-1 mismatch")
  message(FATAL_ERROR
          "Relative cache path must reach hash validation; got: ${invalid_cache_log}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -DREPOSITORY_ROOT=${REPOSITORY_ROOT}
          -P "${CMAKE_CURRENT_LIST_DIR}/cef_distribution_invalid_platform.cmake"
  RESULT_VARIABLE invalid_platform_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(invalid_platform_result EQUAL 0)
  message(FATAL_ERROR "Unsupported CEF platform must fail")
endif()

file(REMOVE_RECURSE "${contract_tmp}")
message(STATUS "CEF distribution contract passed")
