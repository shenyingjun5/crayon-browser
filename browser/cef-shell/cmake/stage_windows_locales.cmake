if(NOT DEFINED CRAYON_CEF_RESOURCE_DIR OR
   NOT DEFINED CRAYON_STAGE_OUTPUT_DIR OR
   NOT DEFINED CRAYON_STAGE_CONFIGURATION)
  message(FATAL_ERROR "Windows locale staging requires source, output and configuration")
endif()

cmake_path(ABSOLUTE_PATH CRAYON_CEF_RESOURCE_DIR NORMALIZE)
cmake_path(ABSOLUTE_PATH CRAYON_STAGE_OUTPUT_DIR NORMALIZE)
cmake_path(GET CRAYON_STAGE_OUTPUT_DIR ROOT_PATH crayon_output_root)
if(CRAYON_STAGE_OUTPUT_DIR STREQUAL "" OR
   CRAYON_STAGE_OUTPUT_DIR STREQUAL crayon_output_root OR
   NOT IS_DIRECTORY "${CRAYON_STAGE_OUTPUT_DIR}" OR
   IS_SYMLINK "${CRAYON_STAGE_OUTPUT_DIR}")
  message(FATAL_ERROR "Windows locale staging output is missing or unsafe")
endif()

set(crayon_source_dir "${CRAYON_CEF_RESOURCE_DIR}/locales")
set(crayon_destination_dir "${CRAYON_STAGE_OUTPUT_DIR}/locales")
if(NOT IS_DIRECTORY "${crayon_source_dir}" OR
   IS_SYMLINK "${crayon_source_dir}")
  message(FATAL_ERROR "CEF locale source is missing or unsafe")
endif()
if(crayon_source_dir STREQUAL crayon_destination_dir OR
   IS_SYMLINK "${crayon_destination_dir}")
  message(FATAL_ERROR "CEF locale destination is unsafe")
endif()

set(crayon_supported_locales en-US zh-CN zh-TW)
set(crayon_supported_gender_suffixes _FEMININE _MASCULINE _NEUTER)
set(crayon_release_files)
foreach(crayon_locale IN LISTS crayon_supported_locales)
  list(APPEND crayon_release_files "${crayon_locale}.pak")
  foreach(crayon_suffix IN LISTS crayon_supported_gender_suffixes)
    list(APPEND crayon_release_files "${crayon_locale}${crayon_suffix}.pak")
  endforeach()
endforeach()

foreach(crayon_file IN LISTS crayon_release_files)
  if(NOT EXISTS "${crayon_source_dir}/${crayon_file}" OR
     IS_DIRECTORY "${crayon_source_dir}/${crayon_file}" OR
     IS_SYMLINK "${crayon_source_dir}/${crayon_file}")
    message(FATAL_ERROR "Required CEF locale resource is missing or unsafe: ${crayon_file}")
  endif()
endforeach()

file(REMOVE_RECURSE "${crayon_destination_dir}")
file(MAKE_DIRECTORY "${crayon_destination_dir}")
if(CRAYON_STAGE_CONFIGURATION STREQUAL "Release")
  foreach(crayon_file IN LISTS crayon_release_files)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${crayon_source_dir}/${crayon_file}"
              "${crayon_destination_dir}/${crayon_file}"
      RESULT_VARIABLE crayon_copy_result)
    if(NOT crayon_copy_result EQUAL 0)
      message(FATAL_ERROR "Failed to stage CEF locale resource: ${crayon_file}")
    endif()
  endforeach()
else()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${crayon_source_dir}" "${crayon_destination_dir}"
    RESULT_VARIABLE crayon_copy_result)
  if(NOT crayon_copy_result EQUAL 0)
    message(FATAL_ERROR "Failed to stage Debug CEF locale resources")
  endif()
endif()
