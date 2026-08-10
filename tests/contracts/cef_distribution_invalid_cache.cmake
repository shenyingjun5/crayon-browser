cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED REPOSITORY_ROOT)
  message(FATAL_ERROR "REPOSITORY_ROOT is required")
endif()

include("${REPOSITORY_ROOT}/cmake/cef/DownloadCef.cmake")
crayon_cef_download_archive(
  PLATFORM "${CRAYON_CEF_PLATFORM}"
  CACHE_DIR "${CRAYON_CEF_CACHE_DIR}"
  OUT_ARCHIVE downloaded_archive)
