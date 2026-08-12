foreach(required_argument
        CRAYON_APP_BUNDLE
        CRAYON_HELPER_MANIFEST
        CRAYON_EXPECTED_ARCH
        CRAYON_MANAGED_ICON
        CRAYON_MANAGED_ICONSET
        CRAYON_ICONUTIL_OUTPUT)
  if(NOT DEFINED ${required_argument} OR "${${required_argument}}" STREQUAL "")
    message(FATAL_ERROR "${required_argument} is required")
  endif()
endforeach()

if(NOT IS_DIRECTORY "${CRAYON_APP_BUNDLE}")
  message(FATAL_ERROR "macOS app bundle is missing: ${CRAYON_APP_BUNDLE}")
endif()
set(main_executable "${CRAYON_APP_BUNDLE}/Contents/MacOS/CrayonBrowser")
set(main_plist "${CRAYON_APP_BUNDLE}/Contents/Info.plist")
set(bundled_icon "${CRAYON_APP_BUNDLE}/Contents/Resources/app.icns")
set(cef_framework
    "${CRAYON_APP_BUNDLE}/Contents/Frameworks/Chromium Embedded Framework.framework")
set(cef_framework_binary
    "${cef_framework}/Chromium Embedded Framework")
foreach(required_path
        main_executable
        main_plist
        bundled_icon
        cef_framework
        cef_framework_binary)
  if(NOT EXISTS "${${required_path}}")
    message(FATAL_ERROR "macOS bundle path is missing: ${${required_path}}")
  endif()
endforeach()

function(assert_binary_arch binary_path)
  execute_process(
    COMMAND /usr/bin/lipo -archs "${binary_path}"
    RESULT_VARIABLE lipo_result
    OUTPUT_VARIABLE binary_arches
    ERROR_VARIABLE lipo_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT lipo_result EQUAL 0)
    message(FATAL_ERROR "lipo failed for ${binary_path}: ${lipo_error}")
  endif()
  string(REPLACE " " ";" binary_arch_list "${binary_arches}")
  list(FIND binary_arch_list "${CRAYON_EXPECTED_ARCH}" expected_arch_index)
  if(expected_arch_index EQUAL -1)
    message(FATAL_ERROR
            "${binary_path} does not contain ${CRAYON_EXPECTED_ARCH}: ${binary_arches}")
  endif()
endfunction()

assert_binary_arch("${main_executable}")
assert_binary_arch("${cef_framework_binary}")

if(NOT EXISTS "${CRAYON_HELPER_MANIFEST}")
  message(FATAL_ERROR "macOS helper manifest is missing")
endif()
file(STRINGS "${CRAYON_HELPER_MANIFEST}" helper_names)
list(LENGTH helper_names helper_count)
if(NOT helper_count EQUAL 5)
  message(FATAL_ERROR "Expected five CEF helper app variants, got ${helper_count}")
endif()
foreach(helper_name IN LISTS helper_names)
  set(helper_bundle
      "${CRAYON_APP_BUNDLE}/Contents/Frameworks/${helper_name}.app")
  set(helper_executable
      "${helper_bundle}/Contents/MacOS/${helper_name}")
  if(NOT EXISTS "${helper_executable}")
    message(FATAL_ERROR "CEF helper executable is missing: ${helper_executable}")
  endif()
  assert_binary_arch("${helper_executable}")
endforeach()

execute_process(
  COMMAND /usr/bin/plutil -extract CFBundleIdentifier raw -o - "${main_plist}"
  RESULT_VARIABLE plist_result
  OUTPUT_VARIABLE bundle_identifier
  ERROR_VARIABLE plist_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT plist_result EQUAL 0 OR
   NOT bundle_identifier STREQUAL "com.crayon.browser")
  message(FATAL_ERROR
          "Unexpected main bundle identifier '${bundle_identifier}': ${plist_error}")
endif()

file(SHA256 "${CRAYON_MANAGED_ICON}" managed_icon_sha256)
file(SHA256 "${bundled_icon}" bundled_icon_sha256)
if(NOT managed_icon_sha256 STREQUAL bundled_icon_sha256)
  message(FATAL_ERROR "Bundled app.icns differs from the managed brand asset")
endif()

file(REMOVE "${CRAYON_ICONUTIL_OUTPUT}")
execute_process(
  COMMAND /usr/bin/iconutil --convert icns
          --output "${CRAYON_ICONUTIL_OUTPUT}" "${CRAYON_MANAGED_ICONSET}"
  RESULT_VARIABLE iconutil_result
  ERROR_VARIABLE iconutil_error)
if(NOT iconutil_result EQUAL 0 OR NOT EXISTS "${CRAYON_ICONUTIL_OUTPUT}")
  message(FATAL_ERROR "iconutil validation failed: ${iconutil_error}")
endif()

foreach(locale en zh-Hans)
  set(localized_info
      "${CRAYON_APP_BUNDLE}/Contents/Resources/${locale}.lproj/InfoPlist.strings")
  if(NOT EXISTS "${localized_info}")
    message(FATAL_ERROR "Localized InfoPlist.strings is missing: ${localized_info}")
  endif()
endforeach()

message(STATUS "macOS CEF shell package contract passed")
