#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "browser/mdv/cef_mdv_editing.h"
#include "browser/page_markdown/page_markdown_preview.h"
#include "browser/window/tab_controller.h"

namespace crayon::browser::cef_shell::page_markdown {

struct PageMarkdownStrings final {
  std::string preview_command;
};

// UI-thread owner for the explicit context-menu request -> Markdown preview
// flow. Page content cannot invoke this controller.
class CefPageMarkdownPreviewController final {
 public:
  CefPageMarkdownPreviewController(
      window::TabController* tabs,
      std::shared_ptr<mdv::MdvEditController> mdv_editing,
      PageMarkdownStrings strings);

  bool HandleContextMenuAugment(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefContextMenuParams> params,
                                CefRefPtr<CefMenuModel> model);
  bool HandleContextMenuCommand(CefRefPtr<CefBrowser> browser, int command_id);
  void Tick(
      std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
      bool content_host_healthy);
  void Stop();

 private:
  void Reset();
  bool SameNavigation() const;

  window::TabController* tabs_;
  std::shared_ptr<mdv::MdvEditController> mdv_editing_;
  PageMarkdownStrings strings_;
  PageMarkdownPreviewAssembler assembler_;
  CefRefPtr<CefBrowser> browser_;
  std::optional<browser_engine::SnapshotRequestId> request_id_;
  int browser_id_ = -1;
  std::uint64_t tab_id_ = 0;
  std::uint64_t navigation_id_ = 0;
};

}  // namespace crayon::browser::cef_shell::page_markdown
