// PLT-SHELL-17M: real-CEF macOS probe for the Alloy interaction entry
// migration. Proves the shared AlloyInteractions surface, the AppKit
// native menu dispatch through AlloyMenuBridgeMac, and the real
// MdvEntryController gates (context menu, single-`.md` drag, transient
// fencing) inside one Alloy window. Automation never selects a file in
// the native open dialog; the open seam is proven by command routing.
#import <Cocoa/Cocoa.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alloy_interactions_mac_probe.h"

#include "browser/branding/about_destination.h"
#include "browser/mdv/cef_mdv_entries.h"
#include "browser/mdv/cef_mdv_handler.h"
#include "browser/window/alloy_interactions.h"
#include "crayon/browser_localization/locale_snapshot.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_menu_model.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "macos/alloy_menu_bridge_mac.h"

namespace {

using crayon::browser::cef_shell::macos::AlloyMenuBridgeMac;
using crayon::browser::cef_shell::macos::ApplicationCommand;
using crayon::browser::cef_shell::macos::ApplicationMenuMac;
using crayon::browser::cef_shell::mdv::MdvEntryController;
using crayon::browser::cef_shell::mdv::MdvRuntimeState;
using crayon::browser::cef_shell::window::AlloyInteractions;
using crayon::browser::cef_shell::window::AlloyMainCommand;
using crayon::browser::localization::AppLocale;
using crayon::browser::localization::SnapshotFor;
using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv::MdvPageStrings;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 400;
// The MDV entry controller registers its viewer item at MENU_ID_USER_FIRST.
constexpr int kMdvViewerCommandId = MENU_ID_USER_FIRST;

std::filesystem::path MakeTempDir() {
  std::error_code error;
  auto dir = std::filesystem::temp_directory_path(error) /
             ("crayon-interactions-mac-" +
              std::to_string(static_cast<long long>(getpid())));
  std::filesystem::create_directories(dir, error);
  return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out << bytes;
}

std::string FileUrl(const std::filesystem::path& path) {
  return "file://" + path.string();
}

/// Context-menu params with a configurable link/frame URL so the real
/// entry-gate augment can be driven from trusted test input.
class ProbeContextMenuParams final : public CefContextMenuParams {
 public:
  explicit ProbeContextMenuParams(std::string link_url,
                                  std::string frame_url)
      : link_url_(std::move(link_url)), frame_url_(std::move(frame_url)) {}

  int GetXCoord() override { return 40; }
  int GetYCoord() override { return 40; }
  TypeFlags GetTypeFlags() override { return CM_TYPEFLAG_PAGE; }
  CefString GetLinkUrl() override { return link_url_; }
  CefString GetUnfilteredLinkUrl() override { return link_url_; }
  CefString GetSourceUrl() override { return {}; }
  bool HasImageContents() override { return false; }
  CefString GetTitleText() override { return {}; }
  CefString GetPageUrl() override { return "about:blank"; }
  CefString GetFrameUrl() override { return frame_url_; }
  CefString GetFrameCharset() override { return "UTF-8"; }
  MediaType GetMediaType() override { return CM_MEDIATYPE_NONE; }
  MediaStateFlags GetMediaStateFlags() override { return CM_MEDIAFLAG_NONE; }
  CefString GetSelectionText() override { return {}; }
  CefString GetMisspelledWord() override { return {}; }
  bool GetDictionarySuggestions(std::vector<CefString>&) override {
    return false;
  }
  bool IsEditable() override { return false; }
  bool IsSpellCheckEnabled() override { return false; }
  EditStateFlags GetEditStateFlags() override { return CM_EDITFLAG_NONE; }
  bool IsCustomMenu() override { return false; }

 private:
  std::string link_url_;
  std::string frame_url_;

  IMPLEMENT_REFCOUNTING(ProbeContextMenuParams);
  DISALLOW_COPY_AND_ASSIGN(ProbeContextMenuParams);
};

class ProbeMenuModelDelegate final : public CefMenuModelDelegate {
 public:
  ProbeMenuModelDelegate() = default;
  void ExecuteCommand(CefRefPtr<CefMenuModel>, int,
                      cef_event_flags_t) override {}

 private:
  IMPLEMENT_REFCOUNTING(ProbeMenuModelDelegate);
  DISALLOW_COPY_AND_ASSIGN(ProbeMenuModelDelegate);
};

class Probe final : public CefApp,
                    public CefBrowserProcessHandler,
                    public CefClient,
                    public CefLifeSpanHandler,
                    public CefLoadHandler,
                    public CefBrowserViewDelegate,
                    public CefWindowDelegate {
 public:
  explicit Probe(std::shared_ptr<AlloyInteractionsMacProbeResult> result)
      : result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return interactions_;
  }
  CefRefPtr<CefDragHandler> GetDragHandler() override { return interactions_; }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(this, "about:blank", settings,
                                              nullptr, nullptr, this);
    CefWindow::CreateTopLevelWindow(this);
    Schedule();
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser>) override {
    result_->browser_closed = true;
    browser_ = nullptr;
    if (finished_ && window_) {
      window_->Close();
    }
  }

  bool DoClose(CefRefPtr<CefBrowser>) override {
    // The browser accepted the close; re-issue the window close on the
    // next UI turn so CanClose can succeed.
    ReCloseWindow();
    return false;
  }

  void ReCloseWindow() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(
                           [](CefRefPtr<Probe> probe) {
                             if (probe->window_) {
                               probe->window_->Close();
                             }
                           },
                           CefRefPtr<Probe>(this)),
                       0);
  }

  void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                 int) override {
    if (frame->IsMain()) {
      loaded_ = true;
    }
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings column;
    column.horizontal = false;
    auto window_layout = window_->SetToBoxLayout(column);
    toolbar_ = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings row;
    row.horizontal = true;
    toolbar_->SetToBoxLayout(row);
    window_->AddChildView(toolbar_);
    window_->AddChildView(view_);
    window_layout->SetFlexForView(view_, 1);
    window_->SetTitle("Crayon Alloy Interactions Mac Probe");
    window_->SetSize(CefSize(720, 480));
    window_->Layout();
    window_->Show();
  }

  bool CanClose(CefRefPtr<CefWindow>) override {
    // Canonical CEF close dance: the first window close attempt issues
    // TryCloseBrowser (which fires DoClose and reports false); DoClose
    // re-issues the window close, and the second attempt succeeds once the
    // browsers are closing/closed.
    if (!finished_) {
      return false;
    }
    if (browser_ && browser_->GetHost() &&
        !browser_->GetHost()->TryCloseBrowser()) {
      return false;
    }
    return true;
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    interactions_ = nullptr;
    bridge_.reset();
    menu_.reset();
    entry_.reset();
    state_.reset();
    toolbar_ = nullptr;
    view_ = nullptr;
    window_ = nullptr;
    result_->window_closed = true;
    std::error_code cleanup_error;
    std::filesystem::remove_all(temp_dir_, cleanup_error);
    CefQuitMessageLoop();
  }

 private:
  void Attach() {
    std::error_code error;
    temp_dir_ = MakeTempDir();
    const auto markdown_path = temp_dir_ / "fixture.md";
    const auto text_path = temp_dir_ / "fixture.txt";
    WriteFile(markdown_path, "# 探针\n\n受控入口 fixture\n");
    WriteFile(text_path, "not markdown\n");
    markdown_url_ = FileUrl(markdown_path);
    markdown_path_ = markdown_path.string();
    text_path_ = text_path.string();

    MdvPageStrings strings;
    strings.document_title = "蜡笔文档";
    strings.label_open_in_viewer = "在查看器中打开";
    strings.status_not_markdown = "不是可打开的 Markdown 文档";
    strings.status_too_large = "文件超过大小上限";
    strings.status_invalid_utf8 = "文件不是有效的 UTF-8 编码";
    strings.status_render_policy = "渲染策略拦截";
    state_ = std::make_shared<MdvRuntimeState>(MdvPageSnapshot{});
    entry_ = std::make_shared<MdvEntryController>(state_, strings);
    entry_->SetDocumentLoadedCallback(
        [this](CefRefPtr<CefBrowser>, const std::string& path_utf8,
               const std::string&, std::uint64_t, std::uint64_t) {
          loaded_paths_.push_back(path_utf8);
        });

    AlloyInteractions::Callbacks callbacks;
    callbacks.open_markdown = [this](CefRefPtr<CefBrowser> browser) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier()) {
        return false;
      }
      ++open_seams_;
      return true;
    };
    callbacks.navigate = [this](CefRefPtr<CefBrowser> browser,
                                const std::string& url) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier()) {
        return false;
      }
      destinations_.push_back(url);
      return true;
    };
    callbacks.drag_enter = [this](CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefDragData> data,
                                  CefDragHandler::DragOperationsMask mask) {
      return entry_ && entry_->HandleDragEnter(browser, data, mask);
    };
    callbacks.augment_context_menu =
        [this](CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefContextMenuParams> params,
               CefRefPtr<CefMenuModel> model) {
          return entry_ && entry_->HandleContextMenuAugment(browser, params,
                                                            model);
        };
    callbacks.context_menu_command = [this](CefRefPtr<CefBrowser> browser,
                                            int command_id) {
      return entry_ && entry_->HandleContextMenuCommand(browser, command_id);
    };
    callbacks.cancel_transient = [this] {
      ++cancel_count_;
      if (entry_) {
        entry_->CancelTransientEntries();
      }
    };
    interactions_ = new AlloyInteractions(SnapshotFor(AppLocale::kZhCn),
                                          std::move(callbacks));
    result_->attach_passed = interactions_->Attach(window_, view_, browser_,
                                                   toolbar_);

    AlloyMenuBridgeMac::Owners owners;
    owners.active_browser = [this]() -> CefRefPtr<CefBrowser> {
      return shutting_down_ ? nullptr : browser_;
    };
    owners.open_markdown = [this](CefRefPtr<CefBrowser> browser) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier()) {
        return false;
      }
      ++open_seams_;
      return true;
    };
    owners.navigate = [this](CefRefPtr<CefBrowser> browser,
                             const std::string& url) {
      if (!browser_ || !browser ||
          browser->GetIdentifier() != browser_->GetIdentifier()) {
        return false;
      }
      destinations_.push_back(url);
      return true;
    };
    bridge_ = std::make_unique<AlloyMenuBridgeMac>(std::move(owners));

    auto labels = [](const char*) { return std::string("探针菜单"); };
    menu_ = std::make_unique<ApplicationMenuMac>(
        "Crayon Probe", labels, [this](ApplicationCommand command) {
          ++menu_dispatches_;
          static_cast<void>(bridge_->Execute(command));
        });
    result_->attach_passed =
        result_->attach_passed && menu_ && menu_->installed();
  }

  void Schedule() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&Probe::Check, CefRefPtr<Probe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_) {
      return;
    }
    if (++checks_ > kMaximumChecks) {
      std::cout << "alloy_interactions_mac_timeout stage=" << stage_
                << " loads=" << loaded_paths_.size()
                << " open_seams=" << open_seams_ << std::endl;
      Finish(false, "timeout");
      return;
    }
    if (!attached_) {
      if (!window_ || !browser_ || !loaded_) {
        Schedule();
        return;
      }
      Attach();
      if (!result_->attach_passed) {
        Finish(false, "attach");
        return;
      }
      attached_ = true;
      stage_ = 0;
      Schedule();
      return;
    }
    if (stage_ == 0) {
      // Real entry-gate context menu: a file:// link URL augments exactly
      // one viewer item; the command consumes the target and loads the
      // fixture through the gate.
      CefRefPtr<CefMenuModel> model =
          CefMenuModel::CreateMenuModel(new ProbeMenuModelDelegate());
      CefRefPtr<ProbeContextMenuParams> params =
          new ProbeContextMenuParams(markdown_url_, "about:blank");
      interactions_->OnBeforeContextMenu(browser_, browser_->GetMainFrame(),
                                         params, model);
      const int item_index =
          model ? model->GetIndexOf(kMdvViewerCommandId) : -1;
      const bool command = interactions_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), params, kMdvViewerCommandId,
          EVENTFLAG_NONE);
      interactions_->OnContextMenuDismissed(browser_,
                                            browser_->GetMainFrame());
      result_->context_passed =
          item_index >= 0 && command && loaded_paths_.size() == 1 &&
          loaded_paths_[0] == markdown_path_ &&
          !interactions_->context_menu_active();
      stage_ = 1;
      Schedule();
      return;
    }
    if (stage_ == 1) {
      // Real drag gate: single `.md` accepted, non-markdown and multi-file
      // rejected.
      // Real OS drops surface absolute paths as file names; mirror that
      // so the entry gate stats and loads the real fixture files.
      auto markdown = CefDragData::Create();
      markdown->AddFile(markdown_path_, markdown_path_);
      auto text = CefDragData::Create();
      text->AddFile(text_path_, text_path_);
      auto multiple = CefDragData::Create();
      multiple->AddFile(markdown_path_, markdown_path_);
      multiple->AddFile(text_path_, text_path_);
      result_->drag_passed =
          interactions_->OnDragEnter(browser_, markdown,
                                     DRAG_OPERATION_COPY) &&
          loaded_paths_.size() == 2 &&
          !interactions_->OnDragEnter(browser_, text, DRAG_OPERATION_COPY) &&
          !interactions_->OnDragEnter(browser_, multiple,
                                      DRAG_OPERATION_COPY) &&
          loaded_paths_.size() == 2;
      stage_ = 2;
      Schedule();
      return;
    }
    if (stage_ == 2) {
      // Closed-set commands through the bridge: clipboard on the current
      // main frame, the branding destination, and the open seam. Commands
      // without a wired owner (save, close-tab) are bounded no-ops.
      const bool open = bridge_->Execute(ApplicationCommand::kOpenFile);
      const bool about = bridge_->Execute(ApplicationCommand::kAbout);
      const bool unwired_save = bridge_->Execute(ApplicationCommand::kSave);
      const bool unwired_close =
          bridge_->Execute(ApplicationCommand::kCloseTab);
      // The AlloyInteractions closed set owns clipboard commands on the
      // current main frame (the AppKit Edit menu routes the same way).
      const bool copied = interactions_->Execute(AlloyMainCommand::kCopy);
      const bool pasted = interactions_->Execute(AlloyMainCommand::kPaste);
      result_->commands_passed =
          open && copied && pasted && about && !unwired_save &&
          !unwired_close && open_seams_ == 1 && destinations_.size() == 1 &&
          destinations_[0] ==
              crayon::browser::cef_shell::branding::kAboutBrowserUrl;
      stage_ = 3;
      Schedule();
      return;
    }
    if (stage_ == 3) {
      // Native AppKit menu entry: the real menu target dispatches the Open
      // File item into the same bridge seam (native selection/cancel stays
      // with the user; the panel is not driven by automation).
      NSMenu* main_menu = NSApp.mainMenu;
      NSMenuItem* open_item = nil;
      if (main_menu) {
        for (NSMenuItem* top_item in main_menu.itemArray) {
          if (!top_item.submenu) {
            continue;
          }
          for (NSMenuItem* item in top_item.submenu.itemArray) {
            if (item.tag == static_cast<NSInteger>(
                                ApplicationCommand::kOpenFile)) {
              open_item = item;
              break;
            }
          }
          if (open_item) {
            break;
          }
        }
      }
      if (open_item && open_item.target && open_item.action) {
        [open_item.target performSelector:open_item.action
                               withObject:open_item];
      }
      result_->native_menu_passed =
          menu_ && menu_->installed() && open_item != nil &&
          menu_dispatches_ == 1 && open_seams_ == 2;
      stage_ = 4;
      Schedule();
      return;
    }
    if (stage_ == 4) {
      // Navigation fencing: a pending context target is revoked and the
      // stale command can neither route nor re-enter.
      CefRefPtr<ProbeContextMenuParams> params =
          new ProbeContextMenuParams(markdown_url_, "about:blank");
      CefRefPtr<CefMenuModel> model =
          CefMenuModel::CreateMenuModel(new ProbeMenuModelDelegate());
      interactions_->OnBeforeContextMenu(browser_, browser_->GetMainFrame(),
                                         params, model);
      const bool augmented = model &&
                             model->GetIndexOf(kMdvViewerCommandId) >= 0;
      interactions_->OnNavigation(browser_);
      const bool stale_command = interactions_->OnContextMenuCommand(
          browser_, browser_->GetMainFrame(), params, kMdvViewerCommandId,
          EVENTFLAG_NONE);
      const bool stale_entry =
          entry_ && entry_->HandleContextMenuCommand(browser_,
                                                     kMdvViewerCommandId);
      result_->fencing_passed = augmented && cancel_count_ == 1 &&
                                !stale_command && !stale_entry &&
                                loaded_paths_.size() == 2;
      stage_ = 5;
      Schedule();
      return;
    }
    if (stage_ == 5) {
      // Shutdown fencing: idempotent teardown, view removal, and the
      // command-target constraint (no current browser -> reject).
      auto menu_view = interactions_->GetView(AlloyInteractions::kMenuButtonId);
      shutting_down_ = true;
      result_->shutdown_passed =
          interactions_->Shutdown() && interactions_->Shutdown() && menu_view &&
          menu_view->GetParentView() == nullptr && cancel_count_ == 2 &&
          !bridge_->Execute(ApplicationCommand::kOpenFile) &&
          !interactions_->Execute(AlloyMainCommand::kCopy);
      Finish(result_->attach_passed && result_->native_menu_passed &&
                 result_->context_passed && result_->drag_passed &&
                 result_->commands_passed && result_->fencing_passed &&
                 result_->shutdown_passed,
             "complete");
    }
  }

  void Finish(bool passed, const char* detail) {
    if (finished_) {
      return;
    }
    finished_ = true;
    std::cout << "alloy_interactions_mac passed=" << passed
              << " detail=" << detail
              << " attach=" << result_->attach_passed
              << " native_menu=" << result_->native_menu_passed
              << " context=" << result_->context_passed
              << " drag=" << result_->drag_passed
              << " commands=" << result_->commands_passed
              << " fencing=" << result_->fencing_passed
              << " shutdown=" << result_->shutdown_passed << std::endl;
    if (window_) {
      window_->Close();
    }
  }

  std::shared_ptr<AlloyInteractionsMacProbeResult> result_;
  CefRefPtr<CefBrowserView> view_;
  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> toolbar_;
  CefRefPtr<AlloyInteractions> interactions_;
  std::shared_ptr<MdvRuntimeState> state_;
  std::shared_ptr<MdvEntryController> entry_;
  std::unique_ptr<AlloyMenuBridgeMac> bridge_;
  std::unique_ptr<ApplicationMenuMac> menu_;
  std::filesystem::path temp_dir_;
  std::string markdown_url_;
  std::string markdown_path_;
  std::string text_path_;
  std::vector<std::string> loaded_paths_;
  std::vector<std::string> destinations_;
  int checks_ = 0;
  int stage_ = 0;
  int open_seams_ = 0;
  int menu_dispatches_ = 0;
  int cancel_count_ = 0;
  bool attached_ = false;
  bool loaded_ = false;
  bool finished_ = false;
  bool shutting_down_ = false;

  IMPLEMENT_REFCOUNTING(Probe);
  DISALLOW_COPY_AND_ASSIGN(Probe);
};

}  // namespace

CefRefPtr<CefApp> CreateAlloyInteractionsMacProbe(
    std::shared_ptr<AlloyInteractionsMacProbeResult> result) {
  return new Probe(std::move(result));
}
