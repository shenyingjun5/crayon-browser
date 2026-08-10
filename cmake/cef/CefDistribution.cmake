include_guard(GLOBAL)

set(CRAYON_CEF_VERSION "150.0.10+g8042e43+chromium-150.0.7871.101")
set(CRAYON_CEF_DISTRIBUTION "standard")
set(CRAYON_CEF_DOWNLOAD_ORIGIN "https://cef-builds.spotifycdn.com")
set(CRAYON_CEF_PLATFORM_KEYS "windows64;macosx64;macosarm64;linux64")

set(CRAYON_CEF_SHA1_windows64 "b5ae23cec83689ef9843951e182443cacbaff5af")
set(CRAYON_CEF_SHA1_macosx64 "17e14fe00415e01a79e8b6d7ecaad8a861f1b388")
set(CRAYON_CEF_SHA1_macosarm64 "2e77063444e3ca07aea2651b763d3c4248bf2543")
set(CRAYON_CEF_SHA1_linux64 "8ef7861df621ac9ce370ff30161e4c5ba5d7e7de")

function(crayon_cef_distribution)
  cmake_parse_arguments(
    ARG
    ""
    "PLATFORM;OUT_ARCHIVE;OUT_URL;OUT_SHA1"
    ""
    ${ARGN})

  foreach(required_argument PLATFORM OUT_ARCHIVE OUT_URL OUT_SHA1)
    if(NOT ARG_${required_argument})
      message(FATAL_ERROR "crayon_cef_distribution requires ${required_argument}")
    endif()
  endforeach()

  list(FIND CRAYON_CEF_PLATFORM_KEYS "${ARG_PLATFORM}" platform_index)
  if(platform_index EQUAL -1)
    message(FATAL_ERROR
            "Unsupported CEF platform '${ARG_PLATFORM}'. Allowed values: ${CRAYON_CEF_PLATFORM_KEYS}")
  endif()

  set(archive "cef_binary_${CRAYON_CEF_VERSION}_${ARG_PLATFORM}.tar.bz2")
  string(REPLACE "+" "%2B" encoded_archive "${archive}")
  set(sha1 "${CRAYON_CEF_SHA1_${ARG_PLATFORM}}")

  set(${ARG_OUT_ARCHIVE} "${archive}" PARENT_SCOPE)
  set(${ARG_OUT_URL} "${CRAYON_CEF_DOWNLOAD_ORIGIN}/${encoded_archive}" PARENT_SCOPE)
  set(${ARG_OUT_SHA1} "${sha1}" PARENT_SCOPE)
endfunction()
