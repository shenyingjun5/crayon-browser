#include "macos/alloy_toolbar_mac.h"

#include <utility>

#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "include/views/cef_box_layout.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::macos {
namespace {

std::string Localized(::crayon::browser::localization::AppLocale locale,
                      std::string_view key) {
  const auto value =
      ::crayon::browser::localization::LocaleCatalog(locale).Find(key);
  return value ? std::string(*value) : std::string{};
}

}  // namespace

AlloyToolbarMac::AlloyToolbarMac(localization::LocaleSnapshot locale,
                                 Callbacks callbacks) {
  tab_strip_ = std::make_unique<window::AlloyTabStrip>(
      window::AlloyTabStrip::Strings{
          Localized(locale.locale, "tabs.new"),
          Localized(locale.locale, "tabs.close"),
          Localized(locale.locale, "tabs.fallback")},
      window::AlloyTabStrip::Callbacks{
          [new_tab = std::move(callbacks.new_tab)] {
            if (new_tab) new_tab();
          },
          [activate = std::move(callbacks.activate_tab)](window::TabId id) {
            if (activate) activate(id);
          },
          [close = std::move(callbacks.close_tab)](window::TabId id) {
            if (close) close(id);
          },
          [title = std::move(callbacks.tab_title)](window::TabId id) {
            return title ? title(id) : std::string{};
          }});
  omnibox_ = std::make_unique<window::AlloyOmnibox>(
      window::AlloyOmnibox::Strings{
          Localized(locale.locale, "address.placeholder"),
          Localized(locale.locale, "omnibox.edit")},
      window::AlloyOmnibox::Callbacks{
          {},
          [this](const window::OmniboxSubmission& submission) {
            if (navigation_) {
              static_cast<void>(navigation_->Navigate(submission));
            }
          },
          {}},
      browser_privacy::DefaultPrivacyDefaults());
  navigation_ = std::make_unique<window::AlloyNavigation>(
      window::AlloyNavigation::Strings{
          Localized(locale.locale, "nav.back"),
          Localized(locale.locale, "nav.forward"),
          Localized(locale.locale, "nav.reload"),
          Localized(locale.locale, "nav.stop"),
          Localized(locale.locale, "nav.identity.unknown"),
          Localized(locale.locale, "nav.identity.secure"),
          Localized(locale.locale, "nav.identity.pending"),
          Localized(locale.locale, "nav.identity.insecure"),
          Localized(locale.locale, "nav.identity.local"),
          Localized(locale.locale, "nav.identity.error")},
      window::AlloyNavigation::Callbacks{[this](const std::string& address) {
        if (omnibox_) {
          static_cast<void>(omnibox_->SetAddress(address));
        }
      }});
  toolbar_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings toolbar_settings;
  toolbar_settings.horizontal = true;
  auto toolbar_layout = toolbar_->SetToBoxLayout(toolbar_settings);
  toolbar_->AddChildView(navigation_->panel());
  toolbar_->AddChildView(omnibox_->panel());
  toolbar_layout->SetFlexForView(omnibox_->panel(), 1);
}

AlloyToolbarMac::~AlloyToolbarMac() = default;

CefRefPtr<CefView> AlloyToolbarMac::tab_strip_view() const {
  return tab_strip_ ? tab_strip_->panel() : nullptr;
}

CefRefPtr<CefView> AlloyToolbarMac::toolbar_view() const { return toolbar_; }

bool AlloyToolbarMac::SyncTabs(const window::TabModel& model) {
  if (!tab_strip_) {
    return false;
  }
  const bool synced = tab_strip_->Sync(model);
  return tab_strip_->RefreshTitles() && synced;
}

bool AlloyToolbarMac::AttachBrowser(window::TabId tab_id,
                                    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!navigation_ || !browser) {
    return false;
  }
  bound_browser_ = std::move(browser);
  // Bind primes the address display from the main frame URL itself.
  return navigation_->Bind(std::to_string(tab_id), bound_browser_);
}

bool AlloyToolbarMac::OnTabUiUpdate(int browser_id, const std::string& url,
                                    bool is_loading, bool can_go_back,
                                    bool can_go_forward) {
  if (browser_id == 0 || !bound_browser_ ||
      bound_browser_->GetIdentifier() != browser_id) {
    return false;
  }
  static_cast<void>(navigation_->OnAddressChange(bound_browser_, url));
  static_cast<void>(navigation_->OnLoadingStateChange(
      bound_browser_, is_loading, can_go_back, can_go_forward));
  if (!is_loading && !url.empty()) {
    static_cast<void>(navigation_->OnLoadEnd(bound_browser_, url));
  }
  return true;
}

bool AlloyToolbarMac::SetAddress(std::string address) {
  return omnibox_ && omnibox_->SetAddress(std::move(address));
}

bool AlloyToolbarMac::FocusOmnibox() {
  CEF_REQUIRE_UI_THREAD();
  return omnibox_ && omnibox_->Focus();
}

void AlloyToolbarMac::Shutdown() {
  if (omnibox_) {
    static_cast<void>(omnibox_->Shutdown());
  }
  if (navigation_) {
    static_cast<void>(navigation_->Shutdown());
  }
  if (tab_strip_) {
    static_cast<void>(tab_strip_->Shutdown());
  }
}

}  // namespace crayon::browser::cef_shell::macos
