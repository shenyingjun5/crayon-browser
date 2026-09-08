#include "browser/window/alloy_navigation.h"

#include <limits>
#include <map>
#include <utility>

#include "browser/window/alloy_icon.h"
#include "crayon/browser_navigation/navigation_controller.h"
#include "include/cef_parser.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_panel_delegate.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr int kBarHeight = 48;
constexpr int kButtonWidth = 36;
constexpr int kIdentityWidth = 36;
constexpr int kChildSpacing = 2;
constexpr std::size_t kMaximumCachedBrowsers = 64;

class SurfaceDelegate final : public CefPanelDelegate {
public:
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(kButtonWidth * 3 + kIdentityWidth + kChildSpacing * 3,
                   kBarHeight);
  }

private:
  IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};

bool IsAllowedNavigationUrl(const std::string &value) {
  if (value.empty() || value.size() > 2048 ||
      !AlloyOmnibox::SafeDisplayText(value)) {
    return false;
  }
  CefURLParts parts;
  if (!CefParseURL(value, parts)) {
    return false;
  }
  const std::string scheme = CefString(&parts.scheme).ToString();
  const bool allowed = scheme == "http" || scheme == "https" ||
                       scheme == "crayon" || scheme == "about" ||
                       scheme == "file";
  return allowed && CefString(&parts.username).empty() &&
         CefString(&parts.password).empty();
}

} // namespace

struct AlloyNavigation::State final : std::enable_shared_from_this<State> {
  enum class Command { kBack, kForward, kReloadStop };

  struct CachedBrowserState final {
    std::string address;
    browser_navigation::SiteIdentity identity =
        browser_navigation::SiteIdentity::kUnknown;
    std::uint64_t navigation_id = 0;
    std::uint64_t next_navigation_id = 1;
    bool completion_succeeded = false;
    bool navigation_failed = false;
    bool loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
  };

  class ButtonDelegate final : public CefButtonDelegate {
  public:
    ButtonDelegate(std::weak_ptr<State> state, Command command)
        : state_(std::move(state)), command_(command) {}

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
      ReleaseAlloyIconFocus(button);
      if (auto state = state_.lock()) {
        state->Dispatch(command_);
      }
    }

  private:
    std::weak_ptr<State> state_;
    const Command command_;
    IMPLEMENT_REFCOUNTING(ButtonDelegate);
  };

  State(Strings strings_value, Callbacks callbacks_value)
      : strings(std::move(strings_value)),
        callbacks(std::move(callbacks_value)) {}

  void Initialize() {
    panel = CefPanel::CreatePanel(new SurfaceDelegate());
    CefBoxLayoutSettings layout;
    layout.horizontal = true;
    layout.between_child_spacing = kChildSpacing;
    panel->SetToBoxLayout(layout);
    back = Button(strings.back, Command::kBack);
    forward = Button(strings.forward, Command::kForward);
    reload_stop = Button(strings.reload, Command::kReloadStop);
    ApplyAlloyIcon(back, AlloyIcon::kBack, strings.back);
    ApplyAlloyIcon(forward, AlloyIcon::kForward, strings.forward);
    ApplyAlloyIcon(reload_stop, AlloyIcon::kReload, strings.reload);
    identity_label = CefLabelButton::CreateLabelButton(
        new ButtonDelegate(weak_from_this(), Command::kBack), {});
    identity_label->SetMinimumSize(CefSize(kIdentityWidth, kBarHeight));
    identity_label->SetAccessibleName(strings.identity_unknown);
    identity_label->SetFocusable(false);
    identity_label->SetEnabled(false);
    ApplyAlloyIcon(identity_label, AlloyIcon::kSiteInfo,
                   strings.identity_unknown);
    panel->AddChildView(back);
    panel->AddChildView(forward);
    panel->AddChildView(reload_stop);
    panel->AddChildView(identity_label);
    Sync();
  }

  CefRefPtr<CefLabelButton> Button(const std::string &label, Command command) {
    auto button = CefLabelButton::CreateLabelButton(
        new ButtonDelegate(weak_from_this(), command), {});
    button->SetMinimumSize(CefSize(kButtonWidth, kBarHeight));
    button->SetTooltipText(label);
    button->SetAccessibleName(label);
    button->SetFocusable(true);
    return button;
  }

  bool Matches(CefRefPtr<CefBrowser> candidate) const {
    return active && browser && candidate &&
           browser->GetIdentifier() == candidate->GetIdentifier();
  }

  void CacheCurrentBrowser() {
    if (!browser || tab_id.empty()) {
      return;
    }
    const int browser_id = browser->GetIdentifier();
    if (browser_states.find(browser_id) == browser_states.end() &&
        browser_states.size() >= kMaximumCachedBrowsers) {
      browser_states.erase(browser_states.begin());
    }
    browser_states[browser_id] =
        CachedBrowserState{address,
                           identity,
                           navigation.CurrentNavigationId(tab_id),
                           next_navigation_id,
                           completion_succeeded,
                           navigation_failed,
                           navigation.IsLoading(tab_id),
                           navigation.CanGoBack(tab_id),
                           navigation.CanGoForward(tab_id)};
  }

  bool RestoreCachedBrowser(int browser_id) {
    const auto found = browser_states.find(browser_id);
    if (found == browser_states.end()) {
      return false;
    }
    const CachedBrowserState &cached = found->second;
    address = cached.address;
    identity = cached.identity;
    next_navigation_id = cached.next_navigation_id;
    completion_succeeded = cached.completion_succeeded;
    navigation_failed = cached.navigation_failed;
    if (cached.navigation_id != 0) {
      navigation.OnNavigationStarted(tab_id, cached.navigation_id);
      if (!cached.loading) {
        if (cached.navigation_failed) {
          navigation.OnNavigationFailed(tab_id, cached.navigation_id);
        } else if (cached.completion_succeeded) {
          navigation.OnNavigationCompleted(tab_id, cached.navigation_id);
        }
      }
    }
    navigation.SetCanGoBack(tab_id, cached.can_go_back);
    navigation.SetCanGoForward(tab_id, cached.can_go_forward);
    if (!address.empty() && callbacks.address_changed) {
      callbacks.address_changed(address);
    }
    return true;
  }

  bool Bind(std::string new_tab_id, CefRefPtr<CefBrowser> new_browser) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || new_tab_id.empty() || !new_browser) {
      return false;
    }
    CacheCurrentBrowser();
    navigation.Shutdown();
    navigation = browser_navigation::NavigationController{};
    tab_id = std::move(new_tab_id);
    browser = std::move(new_browser);
    navigation.OnTabCreated(tab_id);
    next_navigation_id = 1;
    identity = browser_navigation::SiteIdentity::kUnknown;
    address.clear();
    completion_succeeded = false;
    navigation_failed = false;
    if (RestoreCachedBrowser(browser->GetIdentifier())) {
      Sync();
      return true;
    }
    const bool loading = browser->IsLoading();
    if (const auto frame = browser->GetMainFrame(); frame) {
      const auto visible_address =
          AlloyOmnibox::SafeDisplayText(frame->GetURL().ToString());
      if (visible_address && !visible_address->empty()) {
        address = *visible_address;
        const std::uint64_t navigation_id = next_navigation_id++;
        navigation.OnNavigationStarted(tab_id, navigation_id);
        completion_succeeded = !loading;
        identity = browser_navigation::EvaluateSiteIdentity(
            address, completion_succeeded, false);
        if (completion_succeeded) {
          navigation.OnNavigationCompleted(tab_id, navigation_id);
        }
        if (callbacks.address_changed) {
          callbacks.address_changed(address);
        }
      }
    }
    navigation.SetCanGoBack(tab_id, browser->CanGoBack());
    navigation.SetCanGoForward(tab_id, browser->CanGoForward());
    Sync();
    return true;
  }

  bool Navigate(const OmniboxSubmission &submission) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !browser ||
        (submission.kind != OmniboxSubmissionKind::kNavigateUrl &&
         submission.kind != OmniboxSubmissionKind::kSearchUrl) ||
        !IsAllowedNavigationUrl(submission.value)) {
      return false;
    }
    auto frame = browser->GetMainFrame();
    if (!frame) {
      return false;
    }
    frame->LoadURL(submission.value);
    return true;
  }

  bool GoBack() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !browser || !navigation.GoBack(tab_id)) {
      return false;
    }
    browser->GoBack();
    return true;
  }

  bool GoForward() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !browser || !navigation.GoForward(tab_id)) {
      return false;
    }
    browser->GoForward();
    return true;
  }

  bool ReloadOrStop() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !browser) {
      return false;
    }
    if (navigation.Stop(tab_id)) {
      browser->StopLoad();
      return true;
    }
    if (!navigation.Reload(tab_id)) {
      return false;
    }
    browser->Reload();
    return true;
  }

  bool AddressChange(CefRefPtr<CefBrowser> candidate, std::string new_address) {
    CEF_REQUIRE_UI_THREAD();
    if (!Matches(candidate)) {
      return false;
    }
    const auto safe = AlloyOmnibox::SafeDisplayText(std::move(new_address));
    if (!safe) {
      return false;
    }
    if (navigation_failed) {
      return false;
    }
    address = *safe;
    identity = browser_navigation::EvaluateSiteIdentity(address, false, false);
    if (callbacks.address_changed) {
      callbacks.address_changed(address);
    }
    Sync();
    return true;
  }

  bool LoadingStateChange(CefRefPtr<CefBrowser> candidate, bool is_loading,
                          bool can_go_back, bool can_go_forward) {
    CEF_REQUIRE_UI_THREAD();
    if (!Matches(candidate)) {
      return false;
    }
    const bool was_loading = navigation.IsLoading(tab_id);
    if (is_loading && !was_loading) {
      if (next_navigation_id == 0 ||
          next_navigation_id == std::numeric_limits<std::uint64_t>::max()) {
        return false;
      }
      navigation.OnNavigationStarted(tab_id, next_navigation_id++);
      completion_succeeded = false;
      navigation_failed = false;
      identity =
          browser_navigation::EvaluateSiteIdentity(address, false, false);
    }
    if (!is_loading && was_loading) {
      const std::uint64_t navigation_id =
          navigation.CurrentNavigationId(tab_id);
      if (completion_succeeded && !navigation_failed) {
        navigation.OnNavigationCompleted(tab_id, navigation_id);
      } else {
        navigation.OnNavigationFailed(tab_id, navigation_id);
      }
    }
    navigation.SetCanGoBack(tab_id, can_go_back);
    navigation.SetCanGoForward(tab_id, can_go_forward);
    Sync();
    return true;
  }

  bool LoadEnd(CefRefPtr<CefBrowser> candidate, std::string final_address) {
    CEF_REQUIRE_UI_THREAD();
    if (!Matches(candidate) || navigation.CurrentNavigationId(tab_id) == 0 ||
        navigation_failed) {
      return false;
    }
    const auto safe = AlloyOmnibox::SafeDisplayText(std::move(final_address));
    if (!safe) {
      return false;
    }
    if (*safe != address) {
      return false;
    }
    address = *safe;
    completion_succeeded = true;
    identity = browser_navigation::EvaluateSiteIdentity(address, true, false);
    if (callbacks.address_changed) {
      callbacks.address_changed(address);
    }
    Sync();
    return true;
  }

  bool LoadError(CefRefPtr<CefBrowser> candidate, std::string failed_address,
                 bool certificate_error) {
    CEF_REQUIRE_UI_THREAD();
    if (!Matches(candidate) || navigation.CurrentNavigationId(tab_id) == 0) {
      return false;
    }
    const auto safe = AlloyOmnibox::SafeDisplayText(std::move(failed_address));
    if (!safe) {
      return false;
    }
    address = *safe;
    navigation_failed = true;
    identity = browser_navigation::EvaluateSiteIdentity(address, false,
                                                        certificate_error);
    if (callbacks.address_changed) {
      callbacks.address_changed(address);
    }
    Sync();
    return true;
  }

  void Dispatch(Command command) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching) {
      return;
    }
    dispatching = true;
    switch (command) {
    case Command::kBack:
      static_cast<void>(GoBackDuringDispatch());
      break;
    case Command::kForward:
      static_cast<void>(GoForwardDuringDispatch());
      break;
    case Command::kReloadStop:
      static_cast<void>(ReloadOrStopDuringDispatch());
      break;
    }
    dispatching = false;
  }

  bool GoBackDuringDispatch() {
    if (!browser || !navigation.GoBack(tab_id)) {
      return false;
    }
    browser->GoBack();
    return true;
  }

  bool GoForwardDuringDispatch() {
    if (!browser || !navigation.GoForward(tab_id)) {
      return false;
    }
    browser->GoForward();
    return true;
  }

  bool ReloadOrStopDuringDispatch() {
    if (!browser) {
      return false;
    }
    if (navigation.Stop(tab_id)) {
      browser->StopLoad();
      return true;
    }
    if (!navigation.Reload(tab_id)) {
      return false;
    }
    browser->Reload();
    return true;
  }

  const std::string &IdentityText() const {
    switch (identity) {
    case browser_navigation::SiteIdentity::kSecure:
      return strings.identity_secure;
    case browser_navigation::SiteIdentity::kSecurePending:
      return strings.identity_pending;
    case browser_navigation::SiteIdentity::kInsecure:
      return strings.identity_insecure;
    case browser_navigation::SiteIdentity::kLocal:
      return strings.identity_local;
    case browser_navigation::SiteIdentity::kCertificateError:
    case browser_navigation::SiteIdentity::kDangerous:
      return strings.identity_error;
    case browser_navigation::SiteIdentity::kUnknown:
      return strings.identity_unknown;
    }
    return strings.identity_unknown;
  }

  void Sync() {
    const bool bound = active && browser;
    back->SetEnabled(bound && navigation.CanGoBack(tab_id));
    forward->SetEnabled(bound && navigation.CanGoForward(tab_id));
    const bool loading = bound && navigation.IsLoading(tab_id);
    ApplyAlloyIcon(reload_stop, loading ? AlloyIcon::kStop : AlloyIcon::kReload,
                   loading ? strings.stop : strings.reload);
    reload_stop->SetEnabled(bound && (loading || navigation.CanReload(tab_id)));
    ApplyAlloyIcon(identity_label, AlloyIcon::kSiteInfo, IdentityText());
    panel->Layout();
  }

  bool Shutdown() {
    CEF_REQUIRE_UI_THREAD();
    if (!active) {
      return true;
    }
    if (dispatching) {
      return false;
    }
    active = false;
    callbacks = {};
    navigation.Shutdown();
    browser = nullptr;
    tab_id.clear();
    browser_states.clear();
    if (panel) {
      panel->RemoveAllChildViews();
    }
    back = nullptr;
    forward = nullptr;
    reload_stop = nullptr;
    identity_label = nullptr;
    panel = nullptr;
    return true;
  }

  Strings strings;
  Callbacks callbacks;
  browser_navigation::NavigationController navigation;
  CefRefPtr<CefPanel> panel;
  CefRefPtr<CefLabelButton> back;
  CefRefPtr<CefLabelButton> forward;
  CefRefPtr<CefLabelButton> reload_stop;
  CefRefPtr<CefLabelButton> identity_label;
  CefRefPtr<CefBrowser> browser;
  std::string tab_id;
  std::string address;
  std::map<int, CachedBrowserState> browser_states;
  browser_navigation::SiteIdentity identity =
      browser_navigation::SiteIdentity::kUnknown;
  std::uint64_t next_navigation_id = 1;
  bool completion_succeeded = false;
  bool navigation_failed = false;
  bool active = true;
  bool dispatching = false;
};

AlloyNavigation::AlloyNavigation(Strings strings, Callbacks callbacks)
    : state_(
          std::make_shared<State>(std::move(strings), std::move(callbacks))) {
  CEF_REQUIRE_UI_THREAD();
  state_->Initialize();
}

AlloyNavigation::~AlloyNavigation() {
  if (state_ && state_->active) {
    state_->Shutdown();
  }
}

CefRefPtr<CefPanel> AlloyNavigation::panel() const {
  return state_ ? state_->panel : nullptr;
}

CefRefPtr<CefLabelButton> AlloyNavigation::back_button() const {
  return state_ ? state_->back : nullptr;
}

CefRefPtr<CefLabelButton> AlloyNavigation::forward_button() const {
  return state_ ? state_->forward : nullptr;
}

CefRefPtr<CefLabelButton> AlloyNavigation::reload_stop_button() const {
  return state_ ? state_->reload_stop : nullptr;
}

bool AlloyNavigation::Bind(std::string tab_id, CefRefPtr<CefBrowser> browser) {
  return state_ && state_->Bind(std::move(tab_id), std::move(browser));
}

bool AlloyNavigation::Navigate(const OmniboxSubmission &submission) {
  return state_ && state_->Navigate(submission);
}

bool AlloyNavigation::GoBack() { return state_ && state_->GoBack(); }

bool AlloyNavigation::GoForward() { return state_ && state_->GoForward(); }

bool AlloyNavigation::ReloadOrStop() {
  return state_ && state_->ReloadOrStop();
}

bool AlloyNavigation::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                      std::string address) {
  return state_ &&
         state_->AddressChange(std::move(browser), std::move(address));
}

bool AlloyNavigation::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                           bool is_loading, bool can_go_back,
                                           bool can_go_forward) {
  return state_ && state_->LoadingStateChange(std::move(browser), is_loading,
                                              can_go_back, can_go_forward);
}

bool AlloyNavigation::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                std::string address) {
  return state_ && state_->LoadEnd(std::move(browser), std::move(address));
}

bool AlloyNavigation::OnLoadError(CefRefPtr<CefBrowser> browser,
                                  std::string address, bool certificate_error) {
  return state_ && state_->LoadError(std::move(browser), std::move(address),
                                     certificate_error);
}

bool AlloyNavigation::Shutdown() { return !state_ || state_->Shutdown(); }

bool AlloyNavigation::active() const noexcept {
  return state_ && state_->active;
}

bool AlloyNavigation::is_loading() const noexcept {
  return state_ && state_->navigation.IsLoading(state_->tab_id);
}

std::uint64_t AlloyNavigation::navigation_id() const noexcept {
  return state_ ? state_->navigation.CurrentNavigationId(state_->tab_id) : 0;
}

browser_navigation::SiteIdentity
AlloyNavigation::site_identity() const noexcept {
  return state_ ? state_->identity : browser_navigation::SiteIdentity::kUnknown;
}

std::string AlloyNavigation::displayed_identity() const {
  return state_ ? state_->IdentityText() : std::string{};
}

} // namespace crayon::browser::cef_shell::window
