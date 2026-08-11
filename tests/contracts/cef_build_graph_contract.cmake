cmake_minimum_required(VERSION 3.21)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(run_configure label expected_success cef_enabled cef_root)
  set(build_dir "${contract_tmp}/build-${label}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${REPOSITORY_ROOT}"
            -B "${build_dir}"
            -G Ninja
            "-DCRAYON_BUILD_TESTS=ON"
            "-DCRAYON_ENABLE_CEF=${cef_enabled}"
            "-DCRAYON_BUILD_CEF_SHELL=OFF"
            "-DCRAYON_CEF_ROOT=${cef_root}"
    WORKING_DIRECTORY "${contract_tmp}"
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
  set(configure_log "${configure_output}${configure_error}")
  if(expected_success)
    if(NOT configure_result EQUAL 0)
      message(FATAL_ERROR "${label} configure failed: ${configure_log}")
    endif()
  elseif(configure_result EQUAL 0)
    message(FATAL_ERROR "${label} configure unexpectedly succeeded")
  endif()
  set("${label}_build_dir" "${build_dir}" PARENT_SCOPE)
  set("${label}_log" "${configure_log}" PARENT_SCOPE)
endfunction()

function(copy_fixture destination)
  file(COPY "${valid_root}/" DESTINATION "${destination}")
endfunction()

function(assert_preset list_name preset_name)
  string(JSON preset_count LENGTH "${presets_json}" "${list_name}")
  math(EXPR last_preset_index "${preset_count} - 1")
  foreach(preset_index RANGE 0 ${last_preset_index})
    string(JSON candidate_name GET "${presets_json}" "${list_name}"
           ${preset_index} name)
    if(candidate_name STREQUAL preset_name)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "Missing ${list_name} entry '${preset_name}'")
endfunction()

file(READ "${REPOSITORY_ROOT}/CMakePresets.json" presets_json)
string(JSON preset_version GET "${presets_json}" version)
if(preset_version LESS 3)
  message(FATAL_ERROR "CMakePresets.json schema version is too old")
endif()
foreach(required_preset
        engine-api
        windows-cef-debug
        macos-x64-cef-debug
        macos-arm64-cef-debug)
  assert_preset(configurePresets "${required_preset}")
  assert_preset(buildPresets "${required_preset}")
  assert_preset(testPresets "${required_preset}")
endforeach()
string(FIND "${presets_json}" ".cache/build/\${presetName}" binary_dir_index)
if(binary_dir_index EQUAL -1)
  message(FATAL_ERROR "Preset binaryDir must stay under .cache/build")
endif()
if(presets_json MATCHES "[A-Za-z]:[/\\\\]" OR
   presets_json MATCHES "(/Users/|/home/)")
  message(FATAL_ERROR "CMakePresets.json contains a machine-specific path")
endif()

foreach(build_graph_file
        "${REPOSITORY_ROOT}/CMakeLists.txt"
        "${REPOSITORY_ROOT}/CMakePresets.json"
        "${REPOSITORY_ROOT}/cmake/cef/CefRoot.cmake"
        "${REPOSITORY_ROOT}/cmake/cef/IntegrateCef.cmake")
  file(READ "${build_graph_file}" build_graph_contents)
  foreach(forbidden_token
          "file(DOWNLOAD"
          "FetchContent"
          "ExternalProject"
          "150.0.10+g8042e43"
          "b5ae23cec83689ef")
    string(FIND "${build_graph_contents}" "${forbidden_token}" forbidden_index)
    if(NOT forbidden_index EQUAL -1)
      message(FATAL_ERROR
              "Build graph contains forbidden token '${forbidden_token}' in ${build_graph_file}")
    endif()
  endforeach()
endforeach()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef contract_suffix)
set(contract_tmp
    "${REPOSITORY_ROOT}/.cache/build/contracts/cef-build-graph-${contract_suffix}")
set(valid_root "${contract_tmp}/valid-cef")
file(MAKE_DIRECTORY
     "${valid_root}/include"
     "${valid_root}/cmake"
     "${valid_root}/libcef_dll")
include("${REPOSITORY_ROOT}/cmake/cef/CefDistribution.cmake")
file(WRITE "${valid_root}/include/cef_version.h"
     "#define CEF_VERSION \"${CRAYON_CEF_VERSION}\"\n")
file(WRITE "${valid_root}/cmake/cef_variables.cmake" "# fixture\n")
file(WRITE "${valid_root}/cmake/FindCEF.cmake"
     "set(CEF_LIBCEF_DLL_WRAPPER_PATH \"\${CEF_ROOT}/libcef_dll\")\n")
file(WRITE "${valid_root}/libcef_dll/wrapper.cc" "int crayon_cef_fixture() { return 0; }\n")
file(WRITE "${valid_root}/libcef_dll/CMakeLists.txt"
     "add_library(libcef_dll_wrapper STATIC wrapper.cc)\n")

run_configure(fixture TRUE ON "${valid_root}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${fixture_build_dir}" --target libcef_dll_wrapper
  RESULT_VARIABLE wrapper_build_result
  OUTPUT_VARIABLE wrapper_build_output
  ERROR_VARIABLE wrapper_build_error)
if(NOT wrapper_build_result EQUAL 0)
  message(FATAL_ERROR
          "Fixture wrapper build failed: ${wrapper_build_output}${wrapper_build_error}")
endif()
file(READ "${fixture_build_dir}/CMakeCache.txt" fixture_cache)
if(NOT fixture_cache MATCHES "CRAYON_CEF_INTEGRATED_ROOT:INTERNAL=")
  message(FATAL_ERROR "Successful configure did not record the validated CEF root")
endif()
run_configure(fixture TRUE ON "${valid_root}")

run_configure(disabled_ignores_root TRUE OFF "relative/missing-root")
run_configure(missing_value FALSE ON "")
if(NOT missing_value_log MATCHES "CRAYON_CEF_ROOT is required")
  message(FATAL_ERROR "Missing root did not return the stable required error")
endif()
run_configure(relative_root FALSE ON "relative/cef-root")
if(NOT relative_root_log MATCHES "must be an absolute path")
  message(FATAL_ERROR "Relative root did not return the stable absolute-path error")
endif()
run_configure(missing_root FALSE ON "${contract_tmp}/missing-cef")
if(NOT missing_root_log MATCHES "not a directory")
  message(FATAL_ERROR "Missing root did not return the stable directory error")
endif()

set(wrong_version_root "${contract_tmp}/wrong-version-cef")
copy_fixture("${wrong_version_root}")
file(WRITE "${wrong_version_root}/include/cef_version.h"
     "#define CEF_VERSION \"wrong-version\"\n")
run_configure(wrong_version FALSE ON "${wrong_version_root}")
if(NOT wrong_version_log MATCHES "version does not match")
  message(FATAL_ERROR "Wrong version did not return the stable version error")
endif()

set(missing_find_root "${contract_tmp}/missing-find-cef")
copy_fixture("${missing_find_root}")
file(REMOVE "${missing_find_root}/cmake/FindCEF.cmake")
run_configure(missing_find FALSE ON "${missing_find_root}")
if(NOT missing_find_log MATCHES "missing cmake/FindCEF.cmake")
  message(FATAL_ERROR "Missing FindCEF did not return the stable package error")
endif()

set(missing_target_root "${contract_tmp}/missing-target-cef")
copy_fixture("${missing_target_root}")
file(WRITE "${missing_target_root}/libcef_dll/CMakeLists.txt"
     "add_library(not_the_cef_wrapper STATIC wrapper.cc)\n")
run_configure(missing_target FALSE ON "${missing_target_root}")
if(NOT missing_target_log MATCHES "did not define libcef_dll_wrapper")
  message(FATAL_ERROR "Missing wrapper target did not return the stable target error")
endif()

file(REMOVE_RECURSE "${contract_tmp}")
message(STATUS "CEF build graph contract passed")
