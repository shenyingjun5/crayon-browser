#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "browser/window/tab_model.h"
#include "include/cef_client.h"

namespace crayon::browser::cef_shell::window {

class TabController;

// Normalizes CEF browser callbacks into TabModel state transitions. The
// controller is owned by the application and outlives every browser.
// All methods run on the CEF UI thread.
class WindowClient final : public CefClient,
                           public CefLifeSpanHandler,
                           public CefDisplayHandler,
                           public CefLoadHandler,
                           public CefRequestHandler,
                           public CefFocusHandler {
 public:
  explicit WindowClient(TabController* controller) : controller_(controller) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;
  void OnGotFocus(CefRefPtr<CefBrowser> browser) override;

 private:
  TabController* controller_;

  IMPLEMENT_REFCOUNTING(WindowClient);
  DISALLOW_COPY_AND_ASSIGN(WindowClient);
};

// Single owner of the browser window/tab lifecycle. Windows are Chrome-style
// browser windows created through CefBrowserHost::CreateBrowser, so the tab
// strip, omnibox and window chrome come from CEF itself; this controller only
// tracks state and executes commands. Shared by the Windows and macOS shells.
// All methods run on the CEF UI thread.
class TabController final : public CefBaseRefCounted {
 public:
  using BrowserCreatedCallback =
      std::function<void(CefRefPtr<CefBrowser> browser)>;

  explicit TabController(std::string initial_url,
                         BrowserCreatedCallback browser_created_callback = {});

  // Creates the first Chrome-style browser window. Returns false when CEF
  // rejected the browser creation.
  bool CreateMainWindow();

  // Commands. Unknown or closing tabs are ignored.
  void NewWindow();
  void CloseActiveTab();
  void GoBack();
  void GoForward();
  void Reload();
  void Stop();
  void ZoomIn();
  void ZoomOut();
  void ResetZoom();

  void CloseAllBrowsers(bool force_close);
  // Brings back a window after a Dock reopen: creates a fresh window when no
  // browser is alive, otherwise leaves existing windows untouched.
  void ShowMainWindow();

  const TabModel& model() const noexcept { return model_; }
  CefRefPtr<WindowClient> client() const { return client_; }

  // Normalized callbacks from WindowClient.
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosing(CefRefPtr<CefBrowser> browser);
  void OnBrowserFocused(CefRefPtr<CefBrowser> browser);
  void OnAddressUpdated(CefRefPtr<CefBrowser> browser, const std::string& url);
  void OnLoadingUpdated(CefRefPtr<CefBrowser> browser,
                        bool is_loading,
                        bool can_go_back,
                        bool can_go_forward);
  void OnRenderProcessGone(CefRefPtr<CefBrowser> browser);

 private:
  bool CreateBrowserWindow();
  CefRefPtr<CefBrowser> ActiveBrowser() const;
  void ApplyZoom(TabId tab_id);

  const std::string initial_url_;
  const BrowserCreatedCallback browser_created_callback_;
  TabModel model_;
  CefRefPtr<WindowClient> client_;
  std::map<int, CefRefPtr<CefBrowser>> browsers_;
  bool close_initiated_ = false;

  IMPLEMENT_REFCOUNTING(TabController);
  DISALLOW_COPY_AND_ASSIGN(TabController);
};

}  // namespace crayon::browser::cef_shell::window

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_TAB_CONTROLLER_H_
