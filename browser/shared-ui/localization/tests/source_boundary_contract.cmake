if(NOT DEFINED CRAYON_LOCALIZATION_SOURCE)
  message(FATAL_ERROR "CRAYON_LOCALIZATION_SOURCE is required")
endif()

set(localization_files
    "${CRAYON_LOCALIZATION_SOURCE}/include/crayon/browser_localization/locale_catalog.h"
    "${CRAYON_LOCALIZATION_SOURCE}/include/crayon/browser_localization/locale_snapshot.h"
    "${CRAYON_LOCALIZATION_SOURCE}/src/locale_catalog.cc"
    "${CRAYON_LOCALIZATION_SOURCE}/src/locale_snapshot.cc")
set(forbidden_patterns
    "windows\\.h"
    "AppKit"
    "CoreFoundation"
    "CFLocale"
    "NSLocale"
    "GetUserPreferredUILanguages"
    "Cef[A-Za-z]"
    "std::filesystem"
    "std::ifstream"
    "std::fstream"
    "getenv\\(")

foreach(file_path IN LISTS localization_files)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "localization source is missing: ${file_path}")
  endif()
  file(READ "${file_path}" source_text)
  foreach(pattern IN LISTS forbidden_patterns)
    if(source_text MATCHES "${pattern}")
      message(FATAL_ERROR
              "platform/runtime-IO token '${pattern}' is forbidden in ${file_path}")
    endif()
  endforeach()
endforeach()

message(STATUS "localization source boundary contract passed")
