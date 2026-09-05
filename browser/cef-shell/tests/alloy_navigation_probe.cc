#include "alloy_navigation_probe.h"

#include <windows.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "browser/permission/cef_download_handler.h"
#include "browser/permission/permission_store.h"
#include "browser/window/alloy_bookmarks.h"
#include "browser/window/alloy_downloads.h"
#include "browser/window/alloy_history.h"
#include "browser/window/alloy_navigation.h"
#include "include/base/cef_callback.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_display.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_closure_task.h"

namespace {

using crayon::browser::cef_shell::permission::CefDownloadHandlerAdapter;
using crayon::browser::cef_shell::permission::PermissionDecision;
using crayon::browser::cef_shell::permission::PermissionKind;
using crayon::browser::cef_shell::window::AlloyBookmarkResult;
using crayon::browser::cef_shell::window::AlloyBookmarks;
using crayon::browser::cef_shell::window::AlloyDownloads;
using crayon::browser::cef_shell::window::AlloyHistory;
using crayon::browser::cef_shell::window::AlloyHistoryResult;
using crayon::browser::cef_shell::window::AlloyNavigation;
using crayon::browser::cef_shell::window::BookmarkOpenTarget;
using crayon::browser::cef_shell::window::OmniboxSubmission;
using crayon::browser::cef_shell::window::OmniboxSubmissionKind;
using crayon::browser_engine::ProfileId;
using crayon::browser_navigation::SiteIdentity;

constexpr int kPollMilliseconds = 20;
constexpr int kMaximumChecks = 600;

bool WindowsPathExists(const std::string &path) {
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::optional<std::string> CreateVerifiedDownloadDirectory() {
  char temporary_path[MAX_PATH]{};
  if (GetTempPathA(MAX_PATH, temporary_path) == 0)
    return std::nullopt;
  char unique_path[MAX_PATH]{};
  if (GetTempFileNameA(temporary_path, "cay", 0, unique_path) == 0) {
    return std::nullopt;
  }
  if (!DeleteFileA(unique_path) || !CreateDirectoryA(unique_path, nullptr)) {
    return std::nullopt;
  }
  return std::string(unique_path);
}

std::string Origin(const std::string &url) {
  const std::size_t scheme = url.find("://");
  const std::size_t slash = scheme == std::string::npos
                                ? std::string::npos
                                : url.find('/', scheme + 3);
  return slash == std::string::npos ? url : url.substr(0, slash);
}

bool EndsWith(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
             0;
}

bool IsCertificateOrSslError(cef_errorcode_t error) {
  const int value = static_cast<int>(error);
  return value == -107 || (value <= -200 && value >= -299);
}

class AlloyNavigationProbe final : public CefApp,
                                   public CefBrowserProcessHandler,
                                   public CefClient,
                                   public CefLifeSpanHandler,
                                   public CefLoadHandler,
                                   public CefDisplayHandler,
                                   public CefBrowserViewDelegate,
                                   public CefWindowDelegate {
public:
  AlloyNavigationProbe(std::string fixture_url,
                       std::shared_ptr<AlloyNavigationProbeResult> result)
      : fixture_url_(std::move(fixture_url)), origin_(Origin(fixture_url_)),
        result_(std::move(result)) {}

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
    return download_handler_;
  }

  void
  OnBeforeCommandLineProcessing(const CefString &,
                                CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("disable-background-networking");
    command->AppendSwitch("disable-component-update");
    command->AppendSwitch("disable-default-apps");
    command->AppendSwitch("disable-sync");
    command->AppendSwitch("no-proxy-server");
  }

  void OnContextInitialized() override {
    navigation_ = std::make_unique<AlloyNavigation>(
        AlloyNavigation::Strings{"Back", "Forward", "Reload", "Stop", "Unknown",
                                 "Secure", "Checking", "Not secure", "Local",
                                 "Certificate error"},
        AlloyNavigation::Callbacks{[this](const std::string &address) {
          displayed_address_ = address;
        }});
    const auto profile = ProfileId::TryCreate("alloy-navigation-probe");
    if (!profile) {
      Finish(false, "bookmark-profile");
      return;
    }
    bookmarks_ = std::make_unique<AlloyBookmarks>(
        *profile, AlloyBookmarks::Callbacks{
                      [this](const std::string &url) {
                        return navigation_->Navigate(
                            {OmniboxSubmissionKind::kNavigateUrl, url});
                      },
                      {}});
    history_ = std::make_unique<AlloyHistory>(
        *profile, false,
        AlloyHistory::Callbacks{[this](const std::string &url) {
          if (!foreign_browser_ || !foreign_browser_->GetMainFrame()) {
            return false;
          }
          foreign_browser_->GetMainFrame()->LoadURL(url);
          return true;
        }});
    download_directory_ = CreateVerifiedDownloadDirectory();
    if (!download_directory_) {
      Finish(false, "download-directory");
      return;
    }
    permission_store_.Record(origin_, PermissionKind::kDownload,
                             PermissionDecision::kAllowSession);
    downloads_ = std::make_unique<AlloyDownloads>(
        *download_directory_, WindowsPathExists,
        AlloyDownloads::Callbacks{
            [this](std::uint64_t id, const std::string &path) {
              return download_handler_ &&
                     download_handler_->ConfirmPending(id, path);
            },
            [this](std::uint64_t id) {
              return download_handler_ && download_handler_->DiscardPending(id);
            },
            [this](std::uint64_t id) {
              return download_handler_ && download_handler_->Pause(id);
            },
            [this](std::uint64_t id) {
              return download_handler_ && download_handler_->Resume(id);
            },
            [this](std::uint64_t id) {
              return download_handler_ && download_handler_->Cancel(id);
            },
            [this](const std::string &path) {
              const DWORD attributes = GetFileAttributesA(path.c_str());
              if (attributes == INVALID_FILE_ATTRIBUTES ||
                  (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                return false;
              }
              downloaded_path_ = path;
              return true;
            }});
    download_handler_ =
        new CefDownloadHandlerAdapter(&permission_store_, downloads_.get());
    CefBrowserSettings settings;
    primary_view_ = CefBrowserView::CreateBrowserView(
        this, "about:blank", settings, nullptr, nullptr, this);
    foreign_view_ = CefBrowserView::CreateBrowserView(
        this, "about:blank", settings, nullptr, nullptr, this);
    if (!primary_view_ || !foreign_view_) {
      Finish(false, "create-view");
      return;
    }
    CefWindow::CreateTopLevelWindow(this);
  }

  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window_ = window;
    CefBoxLayoutSettings settings;
    auto layout = window_->SetToBoxLayout(settings);
    window_->AddChildView(navigation_->panel());
    window_->AddChildView(primary_view_);
    window_->AddChildView(foreign_view_);
    layout->SetFlexForView(primary_view_, 1);
    foreign_view_->SetVisible(false);
    window_->SetTitle("Crayon Alloy Navigation Probe");
    window_->SetSize(CefSize(900, 600));
    window_->Layout();
    window_->Show();
    window_->Activate();
    ScheduleCheck();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return finished_; }

  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    result_->window_closed = true;
    result_->behavior_passed =
        passed_ && result_->real_navigation_passed &&
        result_->identity_passed && result_->fencing_passed &&
        result_->bookmark_passed && result_->history_passed &&
        result_->download_passed;
    std::cout << "alloy_navigation_windows passed=" << result_->behavior_passed
              << " real_navigation=" << result_->real_navigation_passed
              << " identity=" << result_->identity_passed
              << " fencing=" << result_->fencing_passed << std::endl;
    navigation_.reset();
    bookmarks_.reset();
    history_.reset();
    downloads_.reset();
    download_handler_ = nullptr;
    if (!downloaded_path_.empty()) {
      static_cast<void>(DeleteFileA(downloaded_path_.c_str()));
    }
    if (download_directory_) {
      static_cast<void>(RemoveDirectoryA(download_directory_->c_str()));
    }
    primary_view_ = nullptr;
    foreign_view_ = nullptr;
    window_ = nullptr;
    CefQuitMessageLoop();
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView> view,
                        CefRefPtr<CefBrowser> browser) override {
    if (primary_view_ && view->IsSame(primary_view_)) {
      primary_browser_ = browser;
      primary_browser_id_ = browser->GetIdentifier();
      if (!navigation_->Bind("navigation-probe", browser)) {
        Finish(false, "bind");
      }
    } else if (foreign_view_ && view->IsSame(foreign_view_)) {
      foreign_browser_ = browser;
      foreign_browser_id_ = browser->GetIdentifier();
    } else {
      Finish(false, "unknown-browser");
    }
  }

  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser> browser) override {
    if (browser && browser->GetIdentifier() == primary_browser_id_) {
      primary_browser_ = nullptr;
      primary_view_ = nullptr;
      ++closed_browsers_;
    } else if (browser && browser->GetIdentifier() == foreign_browser_id_) {
      foreign_browser_ = nullptr;
      foreign_view_ = nullptr;
      ++closed_browsers_;
    }
    if (finished_ && closed_browsers_ == 2 && window_) {
      window_->Close();
    }
  }

  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString &url) override {
    if (frame->IsMain() && primary_browser_ &&
        browser->GetIdentifier() == primary_browser_->GetIdentifier()) {
      navigation_->OnAddressChange(browser, url.ToString());
      if (bookmarks_) {
        static_cast<void>(bookmarks_->RefreshForUrl(url.ToString()));
      }
    }
  }

  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward) override {
    if (primary_browser_ &&
        browser->GetIdentifier() == primary_browser_->GetIdentifier()) {
      navigation_->OnLoadingStateChange(browser, is_loading, can_go_back,
                                        can_go_forward);
    }
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                 int) override {
    if (frame->IsMain() && primary_browser_ &&
        browser->GetIdentifier() == primary_browser_->GetIdentifier()) {
      navigation_->OnLoadEnd(browser, frame->GetURL());
    } else if (frame->IsMain() && foreign_browser_ &&
               browser->GetIdentifier() == foreign_browser_->GetIdentifier()) {
      foreign_loaded_url_ = frame->GetURL();
    }
  }

  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode error_code, const CefString &,
                   const CefString &failed_url) override {
    if (frame->IsMain() && primary_browser_ &&
        browser->GetIdentifier() == primary_browser_->GetIdentifier()) {
      const bool certificate_error = IsCertificateOrSslError(error_code);
      navigation_->OnLoadError(browser, failed_url.ToString(),
                               certificate_error);
      if (certificate_error) {
        saw_certificate_error_ = true;
        std::cout << "alloy_navigation_windows expected_ssl_error="
                  << static_cast<int>(error_code) << std::endl;
      }
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString &title) override {
    if (primary_browser_ &&
        browser->GetIdentifier() == primary_browser_->GetIdentifier() &&
        title == "Secure") {
      page_claimed_secure_ = true;
    }
  }

private:
  bool Click(CefRefPtr<CefView> view) {
    if (!window_ || !view || !view->IsDrawn() || !view->IsEnabled()) {
      return false;
    }
    window_->Activate();
    const CefRect bounds = view->GetBoundsInScreen();
    const CefPoint point = CefDisplay::ConvertScreenPointToPixels(
        CefPoint(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2));
    if (!SetCursorPos(point.x, point.y)) {
      return false;
    }
    INPUT input[2]{};
    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(2, input, sizeof(INPUT)) == 2;
  }

  void ScheduleCheck() {
    CefPostDelayedTask(TID_UI,
                       base::BindOnce(&AlloyNavigationProbe::Check,
                                      CefRefPtr<AlloyNavigationProbe>(this)),
                       kPollMilliseconds);
  }

  void Check() {
    if (finished_) {
      return;
    }
    if (++checks_ > kMaximumChecks) {
      Finish(false, "timeout");
      return;
    }
    if (logged_stage_ != stage_) {
      logged_stage_ = stage_;
      std::cout << "alloy_navigation_windows stage=" << stage_ << std::endl;
    }
    if (!primary_browser_ || !foreign_browser_) {
      ScheduleCheck();
      return;
    }
    if (stage_ == 0) {
      result_->fencing_passed =
          !navigation_->OnAddressChange(foreign_browser_,
                                        "https://forged.test/") &&
          navigation_->site_identity() != SiteIdentity::kSecure &&
          !navigation_->Navigate(
              OmniboxSubmission{OmniboxSubmissionKind::kNavigateUrl,
                                "https://user:pass@example.test/"});
      if (!result_->fencing_passed ||
          !navigation_->Navigate(OmniboxSubmission{
              OmniboxSubmissionKind::kNavigateUrl, fixture_url_})) {
        Finish(false, "start-navigation");
        return;
      }
      ++stage_;
    } else if (stage_ == 1) {
      if (navigation_->is_loading() || displayed_address_ != fixture_url_) {
        ScheduleCheck();
        return;
      }
      if (!navigation_->Navigate(
              OmniboxSubmission{OmniboxSubmissionKind::kNavigateUrl,
                                origin_ + "/nav-redirect"})) {
        Finish(false, "redirect-navigation");
        return;
      }
      ++stage_;
    } else if (stage_ == 2) {
      if (navigation_->is_loading() ||
          !EndsWith(displayed_address_, "/nav-final.html")) {
        ScheduleCheck();
        return;
      }
      if (navigation_->site_identity() != SiteIdentity::kInsecure ||
          navigation_->displayed_identity() != "Not secure" ||
          !page_claimed_secure_ ||
          navigation_->OnLoadEnd(primary_browser_, origin_ + "/stale") ||
          navigation_->site_identity() != SiteIdentity::kInsecure ||
          !Click(navigation_->back_button())) {
        std::cout << "alloy_navigation_windows redirect_identity="
                  << static_cast<int>(navigation_->site_identity())
                  << " label=" << navigation_->displayed_identity()
                  << " page_claim=" << page_claimed_secure_
                  << " back_enabled=" << navigation_->back_button()->IsEnabled()
                  << " back_drawn=" << navigation_->back_button()->IsDrawn()
                  << std::endl;
        Finish(false, "redirect-or-back");
        return;
      }
      ++stage_;
    } else if (stage_ == 3) {
      if (navigation_->is_loading() || displayed_address_ != fixture_url_) {
        ScheduleCheck();
        return;
      }
      if (navigation_->site_identity() != SiteIdentity::kInsecure ||
          !Click(navigation_->forward_button())) {
        Finish(false, "back-or-forward");
        return;
      }
      ++stage_;
    } else if (stage_ == 4) {
      if (navigation_->is_loading() ||
          !EndsWith(displayed_address_, "/nav-final.html")) {
        ScheduleCheck();
        return;
      }
      reloaded_navigation_id_ = navigation_->navigation_id();
      if (!Click(navigation_->reload_stop_button())) {
        Finish(false, "reload-click");
        return;
      }
      ++stage_;
    } else if (stage_ == 5) {
      if (navigation_->is_loading() ||
          navigation_->navigation_id() <= reloaded_navigation_id_) {
        ScheduleCheck();
        return;
      }
      if (!navigation_->Navigate(OmniboxSubmission{
              OmniboxSubmissionKind::kNavigateUrl, origin_ + "/nav-slow"})) {
        Finish(false, "slow-navigation");
        return;
      }
      ++stage_;
    } else if (stage_ == 6) {
      if (!navigation_->is_loading()) {
        ScheduleCheck();
        return;
      }
      if (!Click(navigation_->reload_stop_button())) {
        Finish(false, "stop-click");
        return;
      }
      ++stage_;
    } else if (stage_ == 7) {
      if (navigation_->is_loading()) {
        ScheduleCheck();
        return;
      }
      bookmark_id_ = bookmarks_->AddCurrentPage("Fixture", fixture_url_);
      if (!bookmark_id_ ||
          bookmarks_->Open(*bookmark_id_, BookmarkOpenTarget::kCurrentTab) !=
              AlloyBookmarkResult::kSuccess) {
        Finish(false, "bookmark-open");
        return;
      }
      ++stage_;
    } else if (stage_ == 8) {
      if (navigation_->is_loading() || displayed_address_ != fixture_url_) {
        ScheduleCheck();
        return;
      }
      result_->bookmark_passed = bookmarks_->bar().current_page_starred() &&
                                 bookmarks_->Search("fixture") ==
                                     std::vector<std::uint64_t>{*bookmark_id_};
      if (!result_->bookmark_passed) {
        Finish(false, "bookmark-readback");
        return;
      }
      if (!history_->BeginNavigation(1) ||
          history_->CommitNavigation(1, fixture_url_, "Fixture", 100) !=
              AlloyHistoryResult::kSuccess ||
          history_->RecordClosedTab(fixture_url_, "Fixture", 101) !=
              AlloyHistoryResult::kSuccess ||
          history_->RestoreRecentlyClosed() != AlloyHistoryResult::kSuccess) {
        Finish(false, "history-restore");
        return;
      }
      ++stage_;
    } else if (stage_ == 9) {
      if (foreign_loaded_url_ != fixture_url_) {
        ScheduleCheck();
        return;
      }
      result_->history_passed =
          history_->store().entries().size() == 1 &&
          history_->store().recently_closed_count() == 0 &&
          history_->Search("fixture") && history_->view().entries().size() == 1;
      if (!result_->history_passed) {
        Finish(false, "history-readback");
        return;
      }
      primary_browser_->GetMainFrame()->LoadURL(origin_ + "/download-safe");
      ++stage_;
    } else if (stage_ == 10) {
      if (downloads_->shelf().items().empty() ||
          downloads_->shelf().items().front().state !=
              crayon::browser_downloads::DownloadState::kCompleted) {
        ScheduleCheck();
        return;
      }
      const std::uint64_t id = downloads_->shelf().items().front().download_id;
      result_->download_passed =
          downloads_->OpenLocation(id) && !downloaded_path_.empty();
      if (!result_->download_passed) {
        Finish(false, "download-readback");
        return;
      }
      std::string https_url = origin_;
      https_url.replace(0, 4, "https");
      if (!navigation_->Navigate(OmniboxSubmission{
              OmniboxSubmissionKind::kNavigateUrl, https_url + "/"})) {
        Finish(false, "ssl-navigation");
        return;
      }
      ++stage_;
    } else if (stage_ == 11) {
      if (!saw_certificate_error_) {
        ScheduleCheck();
        return;
      }
      result_->identity_passed =
          navigation_->site_identity() == SiteIdentity::kCertificateError &&
          navigation_->displayed_identity() == "Certificate error";
      std::cout << "alloy_navigation_windows final_identity="
                << static_cast<int>(navigation_->site_identity())
                << " label=" << navigation_->displayed_identity() << std::endl;
      window_->SetSize(CefSize(320, 400));
      window_->Layout();
      const bool narrow = navigation_->panel()->GetBounds().width > 0;
      window_->SetSize(CefSize(1000, 700));
      window_->Layout();
      result_->real_navigation_passed =
          narrow && navigation_->panel()->GetBounds().width > 0 &&
          page_claimed_secure_;
      Finish(result_->identity_passed && result_->real_navigation_passed,
             "complete");
      return;
    }
    ScheduleCheck();
  }

  void Finish(bool passed, const char *detail) {
    if (finished_) {
      return;
    }
    passed_ = passed;
    finished_ = true;
    std::cout << "alloy_navigation_windows detail=" << detail << std::endl;
    if (navigation_ && !navigation_->Shutdown()) {
      passed_ = false;
    }
    if (bookmarks_ && !bookmarks_->Shutdown()) {
      passed_ = false;
    }
    if (history_ && !history_->Shutdown()) {
      passed_ = false;
    }
    if (download_handler_) {
      download_handler_->Shutdown();
    }
    if (downloads_ && !downloads_->Shutdown()) {
      passed_ = false;
    }
    if (primary_browser_) {
      primary_browser_->GetHost()->CloseBrowser(true);
    }
    if (foreign_browser_) {
      foreign_browser_->GetHost()->CloseBrowser(true);
    }
    if (!primary_browser_ && !foreign_browser_ && window_) {
      window_->Close();
    }
  }

  const std::string fixture_url_;
  const std::string origin_;
  std::shared_ptr<AlloyNavigationProbeResult> result_;
  std::unique_ptr<AlloyNavigation> navigation_;
  std::unique_ptr<AlloyBookmarks> bookmarks_;
  std::unique_ptr<AlloyHistory> history_;
  std::unique_ptr<AlloyDownloads> downloads_;
  crayon::browser::cef_shell::permission::PermissionStore permission_store_;
  CefRefPtr<CefDownloadHandlerAdapter> download_handler_;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> primary_view_;
  CefRefPtr<CefBrowserView> foreign_view_;
  CefRefPtr<CefBrowser> primary_browser_;
  CefRefPtr<CefBrowser> foreign_browser_;
  std::string displayed_address_;
  std::string foreign_loaded_url_;
  std::uint64_t reloaded_navigation_id_ = 0;
  std::optional<std::uint64_t> bookmark_id_;
  std::optional<std::string> download_directory_;
  std::string downloaded_path_;
  int stage_ = 0;
  int logged_stage_ = -1;
  int checks_ = 0;
  int closed_browsers_ = 0;
  int primary_browser_id_ = 0;
  int foreign_browser_id_ = 0;
  bool page_claimed_secure_ = false;
  bool saw_certificate_error_ = false;
  bool passed_ = false;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(AlloyNavigationProbe);
};

} // namespace

CefRefPtr<CefApp>
CreateAlloyNavigationProbe(std::string fixture_url,
                           std::shared_ptr<AlloyNavigationProbeResult> result) {
  return new AlloyNavigationProbe(std::move(fixture_url), std::move(result));
}
