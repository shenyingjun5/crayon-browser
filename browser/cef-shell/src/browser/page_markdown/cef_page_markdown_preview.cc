#include "browser/page_markdown/cef_page_markdown_preview.h"

#include <utility>

#include "crayon/browser_mdv/mdv_viewer.h"
#include "include/cef_menu_model.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::page_markdown {
namespace {

constexpr int kPreviewCommandId = MENU_ID_USER_FIRST + 1;
constexpr int kCopyCommandId = MENU_ID_USER_FIRST + 2;
constexpr int kSaveAsCommandId = MENU_ID_USER_FIRST + 3;

bool IsHttpDocument(const CefString& frame_url) {
  const std::string url = frame_url.ToString();
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

bool IsMdvDocument(const std::string& url) {
  return url.rfind(std::string(crayon::browser_mdv::kMdvScheme) + "://" +
                       crayon::browser_mdv::kMdvHost + "/",
                   0) == 0;
}

bool IsMdvDocument(const CefString& frame_url) {
  return IsMdvDocument(frame_url.ToString());
}

}  // namespace

CefPageMarkdownPreviewController::CefPageMarkdownPreviewController(
    window::TabController* tabs,
    std::shared_ptr<mdv::MdvEditController> mdv_editing,
    PageMarkdownStrings strings,
    std::function<bool(const std::string&)> clipboard_write)
    : tabs_(tabs),
      mdv_editing_(std::move(mdv_editing)),
      strings_(std::move(strings)),
      clipboard_write_(std::move(clipboard_write)) {}

bool CefPageMarkdownPreviewController::HandleContextMenuAugment(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefFrame> main_frame = browser ? browser->GetMainFrame() : nullptr;
  if (!browser || !params || !model || !main_frame ||
      params->GetFrameUrl() != main_frame->GetURL()) {
    return false;
  }
  if (export_session_.CanExport(browser->GetIdentifier()) &&
      IsMdvDocument(params->GetFrameUrl()) && mdv_editing_ &&
      !strings_.copy_command.empty() && !strings_.save_as_command.empty()) {
    model->AddSeparator();
    model->AddItem(kCopyCommandId, strings_.copy_command);
    model->AddItem(kSaveAsCommandId, strings_.save_as_command);
    return true;
  }
  if (strings_.preview_command.empty() ||
      !IsHttpDocument(params->GetFrameUrl())) {
    return false;
  }
  model->AddSeparator();
  model->AddItem(kPreviewCommandId, strings_.preview_command);
  model->SetEnabled(kPreviewCommandId, !assembler_.active());
  return true;
}

bool CefPageMarkdownPreviewController::HandleContextMenuCommand(
    CefRefPtr<CefBrowser> browser, int command_id) {
  CEF_REQUIRE_UI_THREAD();
  if (command_id == kCopyCommandId) {
    CefRefPtr<CefFrame> main_frame =
        browser ? browser->GetMainFrame() : nullptr;
    if (!browser || !main_frame || !IsMdvDocument(main_frame->GetURL()) ||
        !mdv_editing_) {
      return true;
    }
    auto markdown = mdv_editing_->CurrentMarkdown(browser);
    const bool copied =
        markdown &&
        export_session_.Copy(browser->GetIdentifier(), *markdown,
                             clipboard_write_) == CopyMarkdownResult::kCopied;
    mdv_editing_->SetTransientStatus(
        browser, copied ? strings_.copied_status : strings_.copy_failed_status,
        copied);
    return true;
  }
  if (command_id == kSaveAsCommandId) {
    CefRefPtr<CefFrame> main_frame =
        browser ? browser->GetMainFrame() : nullptr;
    if (browser && main_frame && IsMdvDocument(main_frame->GetURL()) &&
        export_session_.CanExport(browser->GetIdentifier()) && mdv_editing_) {
      static_cast<void>(mdv_editing_->SaveAs(browser, "page.md",
                                             strings_.save_cancelled_status));
    }
    return true;
  }
  if (command_id != kPreviewCommandId) return false;
  if (!browser || assembler_.active() || !tabs_) return true;
  const window::TabSnapshot* tab =
      tabs_->model().FindByBrowser(browser->GetIdentifier());
  if (!tab || tab->navigation_generation == 0) return true;
  auto request = tabs_->StartPageSnapshot(browser);
  if (!request ||
      !assembler_.Begin(request->value(), "tab-" + std::to_string(tab->id),
                        tab->navigation_generation)) {
    return true;
  }
  browser_ = browser;
  export_session_.Invalidate();
  pending_preview_browser_id_ = -1;
  pending_preview_navigation_id_ = 0;
  request_id_ = *request;
  browser_id_ = browser->GetIdentifier();
  tab_id_ = tab->id;
  navigation_id_ = tab->navigation_generation;
  return true;
}

void CefPageMarkdownPreviewController::Tick(
    std::vector<::crayon::cef_shell::ipc::content_host::Message> replies,
    bool content_host_healthy) {
  CEF_REQUIRE_UI_THREAD();
  if (tabs_ && pending_preview_browser_id_ > 0) {
    const window::TabSnapshot* pending =
        tabs_->model().FindByBrowser(pending_preview_browser_id_);
    if (!pending) {
      pending_preview_browser_id_ = -1;
      pending_preview_navigation_id_ = 0;
    } else if (IsMdvDocument(pending->url)) {
      export_session_.Activate(pending_preview_browser_id_);
      pending_preview_browser_id_ = -1;
      pending_preview_navigation_id_ = 0;
    } else if (pending->navigation_generation !=
                   pending_preview_navigation_id_ &&
               !pending->loading) {
      pending_preview_browser_id_ = -1;
      pending_preview_navigation_id_ = 0;
    }
  }
  if (tabs_ && export_session_.browser_id() > 0) {
    const window::TabSnapshot* preview =
        tabs_->model().FindByBrowser(export_session_.browser_id());
    if (!preview || !IsMdvDocument(preview->url)) {
      export_session_.Invalidate();
    }
  }
  if (!assembler_.active()) return;
  if (!content_host_healthy || !SameNavigation()) {
    Reset();
    return;
  }
  for (const auto& reply : replies) {
    const PreviewAssemblyResult result = assembler_.Consume(reply);
    if (result == PreviewAssemblyResult::kRejected) {
      Reset();
      return;
    }
    if (result != PreviewAssemblyResult::kCompleted) continue;
    auto markdown = assembler_.TakeCompleted();
    if (!markdown || !browser_ || !mdv_editing_) {
      Reset();
      return;
    }
    mdv_editing_->OnDocumentLoaded(browser_, std::string{}, *markdown,
                                   markdown->size(), 0);
    pending_preview_browser_id_ = browser_->GetIdentifier();
    pending_preview_navigation_id_ = navigation_id_;
    browser_->GetMainFrame()->LoadURL(
        std::string(crayon::browser_mdv::kMdvScheme) + "://" +
        crayon::browser_mdv::kMdvHost + crayon::browser_mdv::kResourceAppHtml);
    Reset();
    return;
  }
}

void CefPageMarkdownPreviewController::Stop() {
  Reset();
  export_session_.Invalidate();
  pending_preview_browser_id_ = -1;
  pending_preview_navigation_id_ = 0;
}

void CefPageMarkdownPreviewController::Reset() {
  if (tabs_ && request_id_) {
    static_cast<void>(tabs_->CancelPageSnapshot(*request_id_));
  }
  assembler_.Cancel();
  browser_ = nullptr;
  request_id_.reset();
  browser_id_ = -1;
  tab_id_ = 0;
  navigation_id_ = 0;
}

bool CefPageMarkdownPreviewController::SameNavigation() const {
  if (!tabs_ || browser_id_ <= 0 || tab_id_ == 0 || navigation_id_ == 0) {
    return false;
  }
  const window::TabSnapshot* tab = tabs_->model().FindByBrowser(browser_id_);
  return tab && tab->id == tab_id_ &&
         tab->navigation_generation == navigation_id_ &&
         tab->lifecycle == window::TabLifecycle::kReady;
}

}  // namespace crayon::browser::cef_shell::page_markdown
