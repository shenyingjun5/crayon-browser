# MDV-08 contract: the crayon://mdv handler lives in its own adapter
# directory, serves fixed in-memory framework/runtime resources,
# issues the shared CSP constant, and never reads files or exposes paths.
if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE)
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must point at browser/cef-shell")
endif()

set(handler_cc "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_handler.cc")
set(editing_cc "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_editing.cc")
set(entries_cc "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_entries.cc")
set(entries_h "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_entries.h")
set(handler_h "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_handler.h")

foreach(file IN ITEMS "${handler_cc}" "${handler_h}" "${entries_cc}" "${entries_h}" "${editing_cc}")
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "mdv handler/entries missing: ${file}")
  endif()
endforeach()

file(READ "${entries_cc}" entries_text)
string(FIND "${entries_text}" "GateLocalLoad" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "entry controller must route loads through GateLocalLoad")
endif()
string(FIND "${entries_text}" "RunFileDialog" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "entry controller must use RunFileDialog for E1")
endif()

file(READ "${editing_cc}" editing_text)
string(FIND "${editing_text}" "type == \"transform\"" transform_query_hit)
string(FIND "${editing_text}" "TransformMarkdownText" transform_api_hit)
string(FIND "${editing_text}" "Utf16OffsetToUtf8Byte" transform_offset_hit)
string(FIND "${editing_text}" "GetType(\"start\") != VTYPE_INT"
       transform_type_gate_hit)
if(transform_query_hit EQUAL -1 OR transform_api_hit EQUAL -1 OR
   transform_offset_hit EQUAL -1 OR transform_type_gate_hit EQUAL -1)
  message(FATAL_ERROR "mdvQuery must reuse the shared transform API with typed UTF-16 offset gating")
endif()
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_editing.h" editing_header)
string(FIND "${editing_header}${editing_text}" "MdvSaveController" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "editing controller must drive the MDV-06 save model")
endif()
string(FIND "${editing_text}" "ExecuteJavaScript" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "editing controller must push state to the page")
endif()

file(READ "${handler_h}" header_text)
file(READ "${handler_cc}" impl_text)

# Registration entry point exists and takes injected strings.
string(FIND "${header_text}" "RegisterMdvSchemeHandlerFactory(" hit)
string(FIND "${header_text}" "MdvRuntimeState" hit2)
if(hit2 EQUAL -1)
  message(FATAL_ERROR "handler must own the MdvRuntimeState snapshot store")
endif()
if(hit EQUAL -1)
  message(FATAL_ERROR "handler header must expose RegisterMdvSchemeHandlerFactory(MdvPageStrings strings)")
endif()

# CSP comes from the shared golden constant, not a local copy.
string(FIND "${impl_text}" "kMdvCsp" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "handler must issue the shared kMdvCsp constant")
endif()
string(FIND "${impl_text}" "Content-Security-Policy" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "handler must set Content-Security-Policy headers")
endif()

# MRT-06/MRT-08 runtime assets come only from immutable embedded catalogs and
# exact MDV routes; resource IDs cannot become filesystem or network paths.
foreach(required_text IN ITEMS
        "BuildHighlightAssetCatalog"
        "BuildKatexAssetCatalog"
        "FindCompatible"
        "runtime_resource_id"
        "ContentType::kJavaScript"
        "ContentType::kCss"
        "ContentType::kFont")
  string(FIND "${impl_text}" "${required_text}" hit)
  if(hit EQUAL -1)
    message(FATAL_ERROR "MDV handler lost runtime catalog gate: ${required_text}")
  endif()
endforeach()

# The fixture is compile-time content; no network IO and no arbitrary
# file access.  Bounded reads are only permitted inside the MDV-13
# validated local-image route (opaque /img/<index> tokens).
foreach(forbidden IN ITEMS "fopen" "CreateFileW(\"" "WinHttp" "URLDownload")
  string(FIND "${impl_text}" "${forbidden}" hit)
  if(NOT hit EQUAL -1)
    message(FATAL_ERROR "handler must not contain ${forbidden}")
  endif()
endforeach()
string(FIND "${impl_text}" "ReadImageBytes" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "handler must serve validated local images via ReadImageBytes")
endif()
string(FIND "${impl_text}" "kMaxLocalImageBytes" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "image reads must be bounded by kMaxLocalImageBytes")
endif()

# The route classifier gates every request; no direct body serving.
string(FIND "${impl_text}" "ClassifyMdvRequest" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "handler must route through ClassifyMdvRequest")
endif()

# The renderer process app must carry the mdvQuery binding; losing it
# silently disables all editing (MDV-11 regression class).
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/browser/new_tab/cef_new_tab_handler.cc" process_app_text)
string(FIND "${process_app_text}" "CefMessageRouterRendererSide" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "renderer process app lost the message router (mdvQuery binding)")
endif()
string(FIND "${process_app_text}" "CefRenderProcessHandler" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "renderer process app lost CefRenderProcessHandler")
endif()

# Windows cannot be executed on the macOS-first development host, but its
# resource surface must remain a closed compile-time triple: ID declaration,
# RC string and platform injection. The Windows x64 executable remains the
# authority for the final MDV-24 runtime gate.
file(READ "${CRAYON_CEF_SHELL_SOURCE}/resources/windows/resource_ids.h"
     windows_ids)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/resources/windows/app.rc.in"
     windows_rc)
file(READ "${CRAYON_CEF_SHELL_SOURCE}/src/windows/app.cc" windows_app)
foreach(resource IN ITEMS
        IDS_CRAYON_MDV_TOOL_HEADING1
        IDS_CRAYON_MDV_TOOL_HEADING2
        IDS_CRAYON_MDV_TOOL_HEADING3
        IDS_CRAYON_MDV_TOOL_STRUCTURE
        IDS_CRAYON_MDV_TOOL_INDENT
        IDS_CRAYON_MDV_TOOL_OUTDENT
        IDS_CRAYON_MDV_TOOL_ALIGN_DEFAULT
        IDS_CRAYON_MDV_TOOL_ALIGN_LEFT
        IDS_CRAYON_MDV_TOOL_ALIGN_CENTER
        IDS_CRAYON_MDV_TOOL_ALIGN_RIGHT
        IDS_CRAYON_MDV_TOOLTIP_VIEW
        IDS_CRAYON_MDV_TOOLTIP_MARKDOWN
        IDS_CRAYON_MDV_TOOLTIP_STRUCTURE
        IDS_CRAYON_MDV_TOOLTIP_TABLE_ALIGNMENT)
  foreach(surface IN ITEMS windows_ids windows_rc windows_app)
    string(FIND "${${surface}}" "${resource}" resource_hit)
    if(resource_hit EQUAL -1)
      message(FATAL_ERROR "${resource} is missing from ${surface}")
    endif()
  endforeach()
endforeach()
string(FIND "${windows_app}" "MdvShortcutPlatform::kWindows" platform_hit)
if(platform_hit EQUAL -1)
  message(FATAL_ERROR "Windows MDV must inject the kWindows shortcut profile")
endif()

message(STATUS "mdv_handler_contract: OK")
