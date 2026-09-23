#include "macos/alloy_toolbar_mac.h"
#include "macos/alloy_titlebar_mac.h"

#include <utility>

#include "browser/window/alloy_chrome_palette.h"
#include "browser/window/alloy_icon.h"
#include "include/views/cef_menu_button.h"
#include "browser/window/alloy_search_engines.h"
#include "macos/application_menu_mac.h"
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

// PLT-SHELL-24M2FIX-C7: the toolbar's trailing menu. It forwards the chosen
// command id straight to the app, which maps it onto the same
// ApplicationCommand handler the macOS application menu uses - the menu is a
// second entry point to one command set, never a second implementation.
class ToolbarMenuDelegate final : public CefMenuModelDelegate {
 public:
  explicit ToolbarMenuDelegate(std::function<void(int)> command)
      : command_(std::move(command)) {}

  void ExecuteCommand(CefRefPtr<CefMenuModel> /*model*/,
                      int command_id,
                      cef_event_flags_t /*event_flags*/) override {
    if (command_) command_(command_id);
  }

 private:
  std::function<void(int)> command_;
  IMPLEMENT_REFCOUNTING(ToolbarMenuDelegate);
};

// Menu items, in order. Only commands the shell actually implements are listed:
// an entry that opens nothing would be worse than its absence.
constexpr int kToolbarMenuCommandIds[] = {
    static_cast<int>(ApplicationCommand::kNewTab),
    static_cast<int>(ApplicationCommand::kCloseTab),
    static_cast<int>(ApplicationCommand::kReload),
    static_cast<int>(ApplicationCommand::kBack),
    static_cast<int>(ApplicationCommand::kForward),
    // PLT-SHELL-24M2FIX-C11: settings opens the Chromium own settings page, so
    // the entry stays listed and now has a real destination.
    static_cast<int>(ApplicationCommand::kSettings)};

constexpr const char* kToolbarMenuLabelKeys[] = {
    "tabs.new",   "tabs.close", "nav.reload",
    "nav.back",   "nav.forward", "settings.title"};

constexpr int kToolbarMenuButtonWidth = 36;
// Command id of the trailing menu button itself (not of a menu item).
constexpr int kTrailingMenuCommandId = 0x7b40;

class ToolbarMenuButtonDelegate final : public CefMenuButtonDelegate {
 public:
  ToolbarMenuButtonDelegate(std::function<void(int)> command,
                            std::function<std::string(const char*)> label)
      : command_(std::move(command)), label_(std::move(label)) {}

  // CefButtonDelegate declares OnButtonPressed pure; this control's action is
  // the menu itself, so the plain press has nothing to do.
  void OnButtonPressed(CefRefPtr<CefButton> button) override {
    window::ReleaseAlloyIconFocus(button);
  }

  void OnMenuButtonPressed(
      CefRefPtr<CefMenuButton> button,
      const CefPoint& screen_point,
      CefRefPtr<CefMenuButtonPressedLock> /*pressed_lock*/) override {
    window::ReleaseAlloyIconFocus(button);
    if (!button) {
      return;
    }
    auto model =
        CefMenuModel::CreateMenuModel(new ToolbarMenuDelegate(command_));
    if (!model) {
      return;
    }
    constexpr std::size_t kCount =
        sizeof(kToolbarMenuCommandIds) / sizeof(kToolbarMenuCommandIds[0]);
    for (std::size_t index = 0; index < kCount; ++index) {
      model->AddItem(kToolbarMenuCommandIds[index],
                     label_(kToolbarMenuLabelKeys[index]));
    }
    // Top-left anchor: the button sits at the trailing end of the toolbar, so
    // the menu drops from its left edge and stays inside the window.
    button->ShowMenu(model, screen_point, CEF_MENU_ANCHOR_TOPLEFT);
  }

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(window::chrome_palette::kToolbarBackground);
  }

 private:
  std::function<void(int)> command_;
  std::function<std::string(const char*)> label_;
  IMPLEMENT_REFCOUNTING(ToolbarMenuButtonDelegate);
};

}  // namespace

AlloyToolbarMac::AlloyToolbarMac(localization::LocaleSnapshot locale,
                                 Callbacks callbacks) {
  // Kept for the trailing menu, which resolves its labels lazily.
  locale_snapshot_ = locale;
  menu_command_ = std::move(callbacks.menu_command);
  tab_strip_ = std::make_unique<window::AlloyTabStrip>(
      window::AlloyTabStrip::Strings{Localized(locale.locale, "tabs.new"),
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
          }},
      // PLT-SHELL-24M2FIX-C5: the strip starts where the reference build's
      // first tab does, not where the window controls end.
      titlebar::kTabStripLeadingInset);
  omnibox_ = std::make_unique<window::AlloyOmnibox>(
      window::AlloyOmnibox::Strings{
          Localized(locale.locale, "address.placeholder"),
          Localized(locale.locale, "omnibox.edit"),
          Localized(locale.locale, "omnibox.notice.no_search_provider"),
          Localized(locale.locale, "omnibox.notice.blocked"),
          Localized(locale.locale, "omnibox.notice.load_failed"),
          // PLT-SHELL-24M2FIX-C6: the bookmark control's two accessible names
          // already exist for the Windows shell's bookmark UI.
          Localized(locale.locale, "bookmarks.add_page"),
          Localized(locale.locale, "bookmarks.remove_page")},
      window::AlloyOmnibox::Callbacks{
          {},
          [this](const window::OmniboxSubmission& submission) {
            if (navigation_) {
              static_cast<void>(navigation_->Navigate(submission));
            }
          },
          {},
          [toggle = std::move(callbacks.toggle_bookmark)] {
            if (toggle) toggle();
          },
          [focus = std::move(callbacks.omnibox_focus_changed)] {
            if (focus) focus();
          }},
      browser_privacy::DefaultPrivacyDefaults(),
      // PLT-SHELL-24M2FIX-C8: input that is not a URL must produce the default
      // engine's result page. Product decision recorded in
      // window/alloy_search_engines.h (Baidu default, Google alternative).
      window::DefaultSearchProviders());
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
  // PLT-SHELL-24M2FIX-C: band colors come from the shared palette (design
  // tokens), not per-file literals that drift from the tokens.
  toolbar_->SetBackgroundColor(window::chrome_palette::kToolbarBackground);
  CefBoxLayoutSettings toolbar_settings;
  toolbar_settings.horizontal = true;
  // PLT-SHELL-24M2FIX-C4-a: the reference build leaves the address field short
  // of the window edge instead of running it into the frame.
  toolbar_settings.inside_border_insets.right =
      window::kChromeTrailingInsetDip;
  auto toolbar_layout = toolbar_->SetToBoxLayout(toolbar_settings);
  // PLT-SHELL-24M2FIX-C4-a: the pill is centred in the navigation bar by a
  // holder with symmetric vertical insets. Setting the toolbar's cross-axis
  // alignment instead would also re-size the navigation panel, whose preferred
  // height is empty and which relies on its minimum height.
  omnibox_holder_ = CefPanel::CreatePanel(nullptr);
  omnibox_holder_->SetBackgroundColor(window::chrome_palette::kToolbarBackground);
  CefBoxLayoutSettings holder_settings;
  holder_settings.horizontal = true;
  holder_settings.inside_border_insets.top = window::kOmniboxPillMarginDip;
  holder_settings.inside_border_insets.bottom = window::kOmniboxPillMarginDip;
  auto holder_layout = omnibox_holder_->SetToBoxLayout(holder_settings);
  omnibox_holder_->AddChildView(omnibox_->panel());
  holder_layout->SetFlexForView(omnibox_->panel(), 1);
  toolbar_->AddChildView(navigation_->panel());
  toolbar_->AddChildView(omnibox_holder_);
  toolbar_layout->SetFlexForView(omnibox_holder_, 1);
}

AlloyToolbarMac::~AlloyToolbarMac() = default;

CefRefPtr<CefView> AlloyToolbarMac::tab_strip_view() const {
  return tab_strip_ ? tab_strip_->panel() : nullptr;
}

CefRefPtr<CefView> AlloyToolbarMac::toolbar_view() const { return toolbar_; }

CefRefPtr<CefView> AlloyToolbarMac::omnibox_view() const {
  return omnibox_ ? omnibox_->panel() : nullptr;
}

CefRefPtr<CefTextfield> AlloyToolbarMac::omnibox_textfield() const {
  return omnibox_ ? omnibox_->textfield() : nullptr;
}

CefRefPtr<CefLabelButton> AlloyToolbarMac::bookmark_button() const {
  return omnibox_ ? omnibox_->bookmark_button() : nullptr;
}

bool AlloyToolbarMac::SetBookmarked(bool bookmarked) {
  return omnibox_ && omnibox_->SetBookmarked(bookmarked);
}

bool AlloyToolbarMac::omnibox_focused() const {
  return omnibox_ && omnibox_->focused();
}

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
  if (omnibox_ && bound_browser_ &&
      bound_browser_->GetIdentifier() != browser->GetIdentifier()) {
    // An unfinished edit belongs to the previous tab, not the newly selected
    // page. Cancel it before Bind publishes that page's committed address.
    static_cast<void>(omnibox_->Cancel());
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

bool AlloyToolbarMac::OnTabLoadError(int browser_id, const std::string& url,
                                     bool certificate_error) {
  CEF_REQUIRE_UI_THREAD();
  if (browser_id == 0 || !bound_browser_ ||
      bound_browser_->GetIdentifier() != browser_id) {
    return false;
  }
  // Order mirrors the Windows shell: the navigation projection first, then
  // the omnibox, so the address bar lands on the final failure state.
  if (navigation_) {
    static_cast<void>(
        navigation_->OnLoadError(bound_browser_, url, certificate_error));
  }
  if (omnibox_) {
    // OnNavigationFinished reports true only when the omnibox model was itself
    // waiting on this navigation, i.e. the user submitted it from the field.
    // Loads driven by the page — link, redirect, reload — never enter that
    // state, so they take the explicit notice path instead; without it a failed
    // load leaves a blank page area with no explanation.
    if (!omnibox_->OnNavigationFinished(false, url)) {
      static_cast<void>(omnibox_->ShowLoadFailureNotice());
    }
  }
  return true;
}

bool AlloyToolbarMac::FocusOmnibox() {
  CEF_REQUIRE_UI_THREAD();
  return omnibox_ && omnibox_->Focus();
}

bool AlloyToolbarMac::NavigateToAddress(std::string url) {
  CEF_REQUIRE_UI_THREAD();
  if (!navigation_) {
    return false;
  }
  window::OmniboxSubmission submission;
  submission.kind = window::OmniboxSubmissionKind::kNavigateUrl;
  submission.value = std::move(url);
  return navigation_->Navigate(submission);
}

bool AlloyToolbarMac::EnsureTrailingMenuButton() {
  CEF_REQUIRE_UI_THREAD();
  if (!toolbar_) {
    return false;
  }
  if (!menu_button_) {
    // The delegate carries the locale and the callback by value, so it never
    // dereferences this toolbar after teardown.
    auto delegate = new ToolbarMenuButtonDelegate(
        menu_command_, [snapshot = locale_snapshot_](const char* key) {
          return Localized(snapshot.locale, key);
        });
    menu_button_ = CefMenuButton::CreateMenuButton(delegate, CefString());
    menu_button_->SetID(kTrailingMenuCommandId);
    menu_button_->SetFocusable(true);
    menu_button_->SetMinimumSize(
        CefSize(kToolbarMenuButtonWidth, window::kNavigationBarHeightDip));
    menu_button_->SetMaximumSize(
        CefSize(kToolbarMenuButtonWidth, window::kNavigationBarHeightDip));
    menu_button_->SetBackgroundColor(window::chrome_palette::kToolbarBackground);
    if (!window::ApplyAlloyIcon(menu_button_, window::AlloyIcon::kMenu,
                                // Existing key ("菜单"): an empty label would
                                // make ApplyAlloyIcon refuse and leave the
                                // button unmounted.
                                Localized(locale_snapshot_.locale,
                                          "app.menu"))) {
      menu_button_ = nullptr;
      return false;
    }
    menu_command_ids_.assign(std::begin(kToolbarMenuCommandIds),
                             std::end(kToolbarMenuCommandIds));
    toolbar_->AddChildView(menu_button_);
  }
  // The cast surface attaches its entry to this same row and may re-attach it,
  // so the menu button is re-appended whenever it is not already last. That is
  // what keeps "menu trails the cast entry" true without the toolbar knowing
  // when the cast surface runs.
  const std::size_t count = toolbar_->GetChildViewCount();
  if (count > 1 && !toolbar_->GetChildViewAt(count - 1)->IsSame(menu_button_)) {
    toolbar_->RemoveChildView(menu_button_);
    toolbar_->AddChildView(menu_button_);
  }
  toolbar_->Layout();
  return true;
}

CefRefPtr<CefMenuButton> AlloyToolbarMac::menu_button() const {
  return menu_button_;
}

std::vector<int> AlloyToolbarMac::menu_command_ids() const {
  return menu_command_ids_;
}

bool AlloyToolbarMac::SetSearchEngine(window::SearchEngine engine) {  CEF_REQUIRE_UI_THREAD();
  if (!omnibox_) {
    return false;
  }
  browser_omnibox_provider::SearchProviderSet providers;
  if (!providers.Add(window::SearchProviderFor(engine))) {
    return false;
  }
  if (!omnibox_->SetSearchProviders(std::move(providers))) {
    return false;
  }
  search_engine_ = engine;
  return true;
}

window::ChromeDecoration AlloyToolbarMac::decoration() const {
  CEF_REQUIRE_UI_THREAD();
  window::ChromeDecoration result;
  if (tab_strip_) {
    result.tabs = tab_strip_->decoration();
  }
  // An unresolvable omnibox keeps the pill rect empty: the decoration then cuts
  // no corners at all, rather than cutting them at stale coordinates.
  if (omnibox_ && omnibox_->panel()) {
    CefPoint origin;
    if (omnibox_->panel()->ConvertPointToWindow(origin)) {
      const CefSize size = omnibox_->panel()->GetSize();
      result.omnibox = CefRect(origin.x, origin.y, size.width, size.height);
      // PLT-SHELL-24M2FIX-C10: the native layer draws the focus ring on the
      // pill's outline, so it needs the focus state, not just the rect.
      result.omnibox_focused = omnibox_->focused();
      if (const CefRefPtr<CefTextfield> field = omnibox_->textfield()) {
        CefPoint field_origin;
        if (field->ConvertPointToWindow(field_origin)) {
          const CefSize field_size = field->GetSize();
          result.omnibox_field = CefRect(field_origin.x, field_origin.y,
                                         field_size.width, field_size.height);
        }
      }
    }
  }
  return result;
}

void AlloyToolbarMac::Shutdown() {
  // Release every CEF object reference before CEF teardown: wrappers still
  // alive at CefShutdown trip the Debug shutdown checker and leak the
  // underlying browser context in CEF's ImplManager. The window owns the
  // mounted views, so dropping our handles here cannot destroy live UI.
  bound_browser_ = nullptr;
  menu_button_ = nullptr;
  menu_model_ = nullptr;
  menu_command_ = {};
  omnibox_holder_ = nullptr;
  toolbar_ = nullptr;
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
