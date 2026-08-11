include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CefRoot.cmake")

macro(crayon_integrate_cef)
  if(ARGC GREATER 0)
    message(FATAL_ERROR "crayon_integrate_cef does not accept arguments")
  endif()
  if(NOT CRAYON_CEF_ROOT)
    message(FATAL_ERROR
            "CRAYON_CEF_ROOT is required when CRAYON_ENABLE_CEF=ON")
  endif()
  if(NOT IS_ABSOLUTE "${CRAYON_CEF_ROOT}")
    message(FATAL_ERROR "CRAYON_CEF_ROOT must be an absolute path")
  endif()

  crayon_cef_validate_root(
    ROOT "${CRAYON_CEF_ROOT}"
    OUT_VALID crayon_cef_root_valid
    OUT_REASON crayon_cef_root_reason)
  if(NOT crayon_cef_root_valid)
    message(FATAL_ERROR "${crayon_cef_root_reason}")
  endif()

  file(REAL_PATH "${CRAYON_CEF_ROOT}" crayon_cef_root_real)
  if(NOT EXISTS "${crayon_cef_root_real}/cmake/FindCEF.cmake")
    message(FATAL_ERROR "CEF root is missing cmake/FindCEF.cmake")
  endif()

  set(CEF_ROOT "${crayon_cef_root_real}")
  list(PREPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED MODULE)

  if(NOT DEFINED CEF_LIBCEF_DLL_WRAPPER_PATH OR
     NOT IS_DIRECTORY "${CEF_LIBCEF_DLL_WRAPPER_PATH}")
    message(FATAL_ERROR "CEF package did not provide a valid wrapper path")
  endif()
  file(REAL_PATH "${CEF_LIBCEF_DLL_WRAPPER_PATH}" crayon_cef_wrapper_real)
  file(REAL_PATH "${crayon_cef_root_real}/libcef_dll"
       crayon_cef_expected_wrapper_real)
  cmake_path(IS_PREFIX crayon_cef_root_real "${crayon_cef_wrapper_real}"
             NORMALIZE crayon_cef_wrapper_inside_root)
  if(NOT crayon_cef_wrapper_inside_root OR
     NOT crayon_cef_wrapper_real STREQUAL crayon_cef_expected_wrapper_real)
    message(FATAL_ERROR "CEF wrapper path must resolve to CRAYON_CEF_ROOT/libcef_dll")
  endif()

  add_subdirectory("${crayon_cef_wrapper_real}"
                   "${CMAKE_BINARY_DIR}/third_party/cef/libcef_dll_wrapper")
  if(NOT TARGET libcef_dll_wrapper)
    message(FATAL_ERROR "CEF wrapper did not define libcef_dll_wrapper")
  endif()

  set(CRAYON_CEF_INTEGRATED_ROOT "${crayon_cef_root_real}" CACHE INTERNAL
      "Validated CEF root used by this build")
  unset(crayon_cef_root_valid)
  unset(crayon_cef_root_reason)
  unset(crayon_cef_root_real)
  unset(crayon_cef_wrapper_real)
  unset(crayon_cef_expected_wrapper_real)
  unset(crayon_cef_wrapper_inside_root)
endmacro()
