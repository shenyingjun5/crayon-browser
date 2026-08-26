# MDV-08 contract: the crayon://mdv handler lives in its own adapter
# directory, serves only the three fixed in-memory framework resources,
# issues the shared CSP constant, and never reads files or exposes paths.
if(NOT DEFINED CRAYON_CEF_SHELL_SOURCE)
  message(FATAL_ERROR "CRAYON_CEF_SHELL_SOURCE must point at browser/cef-shell")
endif()

set(handler_cc "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_handler.cc")
set(handler_h "${CRAYON_CEF_SHELL_SOURCE}/src/browser/mdv/cef_mdv_handler.h")

foreach(file IN ITEMS "${handler_cc}" "${handler_h}")
  if(NOT EXISTS "${file}")
    message(FATAL_ERROR "mdv handler missing: ${file}")
  endif()
endforeach()

file(READ "${handler_h}" header_text)
file(READ "${handler_cc}" impl_text)

# Registration entry point exists and takes injected strings.
string(FIND "${header_text}" "RegisterMdvSchemeHandlerFactory(MdvPageStrings strings)" hit)
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

# The fixture is compile-time content; no filesystem or network IO.
foreach(forbidden IN ITEMS "std::ifstream" "fopen" "CreateFileW(\"" "WinHttp" "URLDownload")
  string(FIND "${impl_text}" "${forbidden}" hit)
  if(NOT hit EQUAL -1)
    message(FATAL_ERROR "handler must not contain ${forbidden}")
  endif()
endforeach()

# The route classifier gates every request; no direct body serving.
string(FIND "${impl_text}" "ClassifyMdvRequest" hit)
if(hit EQUAL -1)
  message(FATAL_ERROR "handler must route through ClassifyMdvRequest")
endif()

message(STATUS "mdv_handler_contract: OK")
