#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_NAVIGATION_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_NAVIGATION_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "browser/window/alloy_omnibox.h"
#include "crayon/browser_navigation/site_identity.h"
#include "include/cef_browser.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"

namespace crayon::browser::cef_shell::window {

// Browser-process adapter and CEF Views projection for one active Alloy tab.
// Only trusted main-frame CEF callbacks may update address or site identity.
class AlloyNavigation final {
public:
  struct Strings final {
    std::string back;
    std::string forward;
    std::string reload;
    std::string stop;
    std::string identity_unknown;
    std::string identity_secure;
    std::string identity_pending;
    std::string identity_insecure;
    std::string identity_local;
    std::string identity_error;
  };

  struct Callbacks final {
    std::function<void(const std::string &)> address_changed;
  };

  AlloyNavigation(Strings strings, Callbacks callbacks);
  ~AlloyNavigation();

  AlloyNavigation(const AlloyNavigation &) = delete;
  AlloyNavigation &operator=(const AlloyNavigation &) = delete;

  CefRefPtr<CefPanel> panel() const;
  CefRefPtr<CefLabelButton> back_button() const;
  CefRefPtr<CefLabelButton> forward_button() const;
  CefRefPtr<CefLabelButton> reload_stop_button() const;

  bool Bind(std::string tab_id, CefRefPtr<CefBrowser> browser);
  bool Navigate(const OmniboxSubmission &submission);
  bool GoBack();
  bool GoForward();
  bool ReloadOrStop();

  bool OnAddressChange(CefRefPtr<CefBrowser> browser, std::string address);
  bool OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward);
  bool OnLoadEnd(CefRefPtr<CefBrowser> browser, std::string address);
  bool OnLoadError(CefRefPtr<CefBrowser> browser, std::string address,
                   bool certificate_error);
  bool Shutdown();

  bool active() const noexcept;
  bool is_loading() const noexcept;
  std::uint64_t navigation_id() const noexcept;
  browser_navigation::SiteIdentity site_identity() const noexcept;
  std::string displayed_identity() const;

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_NAVIGATION_H_
