# Contract test: verify permission module structure and invariants.
#
# Usage:
#   cmake -DCRAYON_CEF_SHELL_SOURCE=<path> -P permission_contract.cmake

if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE)
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must be defined")
endif()

set(SOURCE_DIR "${CRAYON_CEF_SHELL_SOURCE}")

# 1. Core permission files exist.
foreach(file
    src/browser/permission/permission_kind.h
    src/browser/permission/permission_decision.h
    src/browser/permission/site_origin.h
    src/browser/permission/site_origin.cc
    src/browser/permission/permission_store.h
    src/browser/permission/permission_store.cc)
  set(full_path "${SOURCE_DIR}/${file}")
  if(NOT EXISTS "${full_path}")
    message(FATAL_ERROR "Missing permission file: ${file}")
  endif()
endforeach()

# 2. CEF adapter files exist.
foreach(file
    src/browser/permission/cef_permission_handler.h
    src/browser/permission/cef_permission_handler.cc
    src/browser/permission/cef_download_handler.h
    src/browser/permission/cef_download_handler.cc)
  set(full_path "${SOURCE_DIR}/${file}")
  if(NOT EXISTS "${full_path}")
    message(FATAL_ERROR "Missing CEF permission adapter file: ${file}")
  endif()
endforeach()

# 3. Pure C++17 files must not include CEF headers.
foreach(file
    src/browser/permission/site_origin.cc
    src/browser/permission/permission_store.cc)
  set(full_path "${SOURCE_DIR}/${file}")
  file(STRINGS "${full_path}" lines)
  foreach(line IN LISTS lines)
    if(line MATCHES "#include.*cef")
      message(FATAL_ERROR "Pure C++17 file ${file} includes CEF header: ${line}")
    endif()
  endforeach()
endforeach()

# 4. PermissionStore must use shared_mutex (thread-safe Query).
set(store_path "${SOURCE_DIR}/src/browser/permission/permission_store.h")
file(STRINGS "${store_path}" store_lines)
set(has_shared_mutex FALSE)
foreach(line IN LISTS store_lines)
  if(line MATCHES "shared_mutex")
    set(has_shared_mutex TRUE)
  endif()
endforeach()
if(NOT has_shared_mutex)
  message(FATAL_ERROR "PermissionStore must use shared_mutex for thread safety")
endif()

# 5. TabController must reference PermissionStore (integration check).
set(tc_path "${SOURCE_DIR}/src/browser/window/tab_controller.h")
file(STRINGS "${tc_path}" tc_lines)
set(has_permission_store_ref FALSE)
foreach(line IN LISTS tc_lines)
  if(line MATCHES "PermissionStore")
    set(has_permission_store_ref TRUE)
  endif()
endforeach()
if(NOT has_permission_store_ref)
  message(FATAL_ERROR "TabController must reference PermissionStore")
endif()

message(STATUS "permission_contract: all checks passed")
