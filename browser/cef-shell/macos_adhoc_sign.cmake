# Ad-hoc signs the assembled helper bundles and the main app bundle so the
# macOS CEF sandbox can initialize (CEF-02M). Distribution signing and
# notarization are owned by PLT-M05.
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CRAYON_APP_BUNDLE OR NOT DEFINED CRAYON_HELPER_MANIFEST)
  message(FATAL_ERROR "macos_adhoc_sign requires CRAYON_APP_BUNDLE and CRAYON_HELPER_MANIFEST")
endif()

find_program(CODESIGN_EXECUTABLE NAMES codesign)
if(NOT CODESIGN_EXECUTABLE)
  message(FATAL_ERROR "codesign is required to sign the macOS sandbox bundles")
endif()

function(sign_bundle bundle)
  execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - "${bundle}"
    RESULT_VARIABLE sign_result)
  if(NOT sign_result EQUAL 0)
    message(FATAL_ERROR "codesign failed (${sign_result}) for ${bundle}")
  endif()
  execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --verify "${bundle}"
    RESULT_VARIABLE verify_result)
  if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "codesign verify failed (${verify_result}) for ${bundle}")
  endif()
endfunction()

# The helper manifest is generated at configure time; a missing
# manifest means the assembly contract broke and helpers would be
# silently unsigned — fail instead.
if(NOT EXISTS "${CRAYON_HELPER_MANIFEST}")
  message(FATAL_ERROR "helper manifest missing: ${CRAYON_HELPER_MANIFEST}")
endif()
file(STRINGS "${CRAYON_HELPER_MANIFEST}" helper_names)
set(cef_framework
    "${CRAYON_APP_BUNDLE}/Contents/Frameworks/Chromium Embedded Framework.framework")
if(NOT IS_DIRECTORY "${cef_framework}")
  message(FATAL_ERROR "CEF framework missing from app bundle: ${cef_framework}")
endif()

# CEF's arm64 distribution currently carries linker signatures while the x64
# distribution does not. Never depend on that upstream difference: sign every
# nested Mach-O explicitly, from the deepest dylibs outward, before sealing the
# framework and the containing app bundle.
file(GLOB cef_dylibs LIST_DIRECTORIES false
     "${cef_framework}/Versions/A/Libraries/*.dylib")
list(SORT cef_dylibs)
if(NOT cef_dylibs)
  message(FATAL_ERROR "CEF framework contains no dylibs to sign")
endif()
foreach(cef_dylib IN LISTS cef_dylibs)
  sign_bundle("${cef_dylib}")
endforeach()
sign_bundle("${cef_framework}")

foreach(helper_name IN LISTS helper_names)
  set(embedded_helper
      "${CRAYON_APP_BUNDLE}/Contents/Frameworks/${helper_name}.app")
  if(NOT IS_DIRECTORY "${embedded_helper}")
    message(FATAL_ERROR "embedded helper missing: ${embedded_helper}")
  endif()
  sign_bundle("${embedded_helper}")
endforeach()

sign_bundle("${CRAYON_APP_BUNDLE}")
message(STATUS
        "Ad-hoc signed ${CRAYON_APP_BUNDLE} (CEF dylibs/framework + embedded helpers) for the macOS sandbox")
