cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED REPOSITORY_ROOT)
  message(FATAL_ERROR "REPOSITORY_ROOT is required")
endif()

include("${REPOSITORY_ROOT}/cmake/cef/CefDistribution.cmake")
crayon_cef_distribution(
  PLATFORM "unsupported-platform"
  OUT_ARCHIVE archive
  OUT_URL url
  OUT_SHA1 sha1)
