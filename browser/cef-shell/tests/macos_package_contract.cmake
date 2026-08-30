foreach(required_argument
        CRAYON_APP_BUNDLE
        CRAYON_HELPER_MANIFEST
        CRAYON_EXPECTED_ARCH
        CRAYON_MANAGED_ICON)
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
set(content_host
    "${CRAYON_APP_BUNDLE}/Contents/Helpers/crayon-content-host")
foreach(required_path
        main_executable
        main_plist
        bundled_icon
        cef_framework
        cef_framework_binary
        content_host)
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
assert_binary_arch("${content_host}")

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

# Do not round-trip the iconset with the host `iconutil`: macOS 26 can reject
# even an iconset it just extracted from a valid ICNS. The deterministic brand
# verifier owns PNG dimensions/alpha, ICNS chunk coverage and manifest hashes;
# this package contract owns byte-for-byte consumption of that verified ICNS.

function(assert_code_signed code_path)
  execute_process(
    COMMAND /usr/bin/codesign --verify "${code_path}"
    RESULT_VARIABLE codesign_result
    ERROR_VARIABLE codesign_error)
  if(NOT codesign_result EQUAL 0)
    message(FATAL_ERROR
            "Code signature verification failed for ${code_path}: ${codesign_error}")
  endif()
endfunction()

file(GLOB cef_dylibs LIST_DIRECTORIES false
     "${cef_framework}/Versions/A/Libraries/*.dylib")
list(SORT cef_dylibs)
if(NOT cef_dylibs)
  message(FATAL_ERROR "Bundled CEF framework contains no dylibs")
endif()
foreach(cef_dylib IN LISTS cef_dylibs)
  assert_code_signed("${cef_dylib}")
endforeach()
assert_code_signed("${cef_framework}")
foreach(helper_name IN LISTS helper_names)
  assert_code_signed(
    "${CRAYON_APP_BUNDLE}/Contents/Frameworks/${helper_name}.app")
endforeach()
assert_code_signed("${content_host}")
assert_code_signed("${CRAYON_APP_BUNDLE}")

foreach(locale en zh-Hans)
  set(localized_info
      "${CRAYON_APP_BUNDLE}/Contents/Resources/${locale}.lproj/InfoPlist.strings")
  if(NOT EXISTS "${localized_info}")
    message(FATAL_ERROR "Localized InfoPlist.strings is missing: ${localized_info}")
  endif()
  set(localized_mdv
      "${CRAYON_APP_BUNDLE}/Contents/Resources/${locale}.lproj/Localizable.strings")
  if(NOT EXISTS "${localized_mdv}")
    message(FATAL_ERROR "MDV Localizable.strings is missing: ${localized_mdv}")
  endif()
endforeach()

message(STATUS "macOS CEF shell package contract passed")
