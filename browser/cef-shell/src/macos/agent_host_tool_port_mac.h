#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_TOOL_PORT_MAC_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_TOOL_PORT_MAC_H_

// AGT-12Cc2: ToolPort bridge that routes CAAP tool calls from the Rust
// serve thread onto the CEF UI thread via CefPostTask + a synchronous
// wait. Each tool maps to a TabController or page-data operation.

#include <functional>
#include <string>

namespace crayon::browser::cef_shell::macos {

struct ToolRequest {
  std::string tool;
  std::string params_json;
};

struct ToolResult {
  bool ok = false;
  std::string text;
  std::string error_code;
};

// Runs on the CEF UI thread: reads the active tab title via
// TabController/model.
using GetTitleFn = std::function<std::string()>;
// Runs on the CEF UI thread: reads the selection text.
using GetSelectionFn = std::function<std::string()>;
// Runs on the CEF UI thread: lists targets.
using ListTargetsFn = std::function<std::string()>;

// Routes a tool call onto the CEF UI thread and blocks the caller until
// the UI thread completes. Returns the result.
ToolResult ExecuteToolOnUIThread(
    const ToolRequest& request,
    const GetTitleFn& get_title,
    const GetSelectionFn& get_selection,
    const ListTargetsFn& list_targets);

}  // namespace crayon::browser::cef_shell::macos

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_MACOS_AGENT_HOST_TOOL_PORT_MAC_H_
