# Profile context contract: verify source structure and invariants.
#
# Usage:
#   cmake -DCRAYON_CEF_SHELL_SOURCE=<path> -P profile_context_contract.cmake

if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE)
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must be defined")
endif()

set(_source_dir "${CRAYON_CEF_SHELL_SOURCE}/src/browser/context")

foreach(_file
    profile_context_factory.h
    profile_context_factory.cc
    profile_id_validator.h
    profile_id_validator.cc)
  set(_path "${_source_dir}/${_file}")
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "Missing context file: ${_path}")
  endif()
endforeach()

# Verify that profile_id_validator.h does not include CEF headers.
# Use [^\n]* so the match stays on a single line.
file(READ "${_source_dir}/profile_id_validator.h" _validator_h)
if(_validator_h MATCHES "#include[^\n]*cef" OR
   _validator_h MATCHES "#include[^\n]*Cef")
  message(FATAL_ERROR
          "profile_id_validator.h must not depend on CEF types")
endif()

# Verify that the factory header enforces UI-thread requirements.
file(READ "${_source_dir}/profile_context_factory.h" _factory_h)
if(NOT _factory_h MATCHES "CEF_REQUIRE_UI_THREAD")
  message(FATAL_ERROR
          "ProfileContextFactory header must document CEF UI thread requirement")
endif()

# Verify that Profile ID never appears literally in cache path helpers.
# Reject direct concatenation like: path += "profiles/" + profile_id
file(READ "${_source_dir}/profile_id_validator.cc" _validator_cc)
if(_validator_cc MATCHES "profiles/\"[^\n]*\\+ *profile_id")
  message(FATAL_ERROR
          "profile_id_validator.cc must not embed profile_id literally in path")
endif()

message(STATUS "Profile context contract passed")
