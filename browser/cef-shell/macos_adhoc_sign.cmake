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

if(EXISTS "${CRAYON_HELPER_MANIFEST}")
  file(STRINGS "${CRAYON_HELPER_MANIFEST}" helper_names)
  foreach(helper_name IN LISTS helper_names)
    get_filename_component(helper_dir "${CRAYON_APP_BUNDLE}" DIRECTORY)
    sign_bundle("${helper_dir}/${helper_name}.app")
  endforeach()
endif()

sign_bundle("${CRAYON_APP_BUNDLE}")
message(STATUS "Ad-hoc signed ${CRAYON_APP_BUNDLE} (+ helpers) for the macOS sandbox")
