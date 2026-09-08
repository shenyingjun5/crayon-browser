#include "browser/window/alloy_interactions.h"

#include <optional>
#include <utility>

#include "browser/branding/about_destination.h"
#include "browser/window/alloy_icon.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr char kLicensesUrl[] = "chrome://credits/";
constexpr int kOpenMarkdownId = 0xcb01;
constexpr int kOpenIncognitoId = 0xcb02;
constexpr int kCopyId = 0xcb03;
constexpr int kPasteId = 0xcb04;
constexpr int kAboutId = 0xcb05;
constexpr int kLicensesId = 0xcb06;
constexpr int kTogglePinId = 0xcb07;
constexpr int kDuplicateTabId = 0xcb08;
constexpr int kToggleMuteId = 0xcb09;
constexpr int kToggleGroupId = 0xcb0a;
constexpr int kToggleBookmarkBarId = 0xcb0b;
constexpr int kTabSearchMenuId = 0xcb0c;
constexpr std::size_t kMaximumSearchEntries = 32;
constexpr std::size_t kMaximumVisibleBookmarkButtons = 8;
constexpr int kControlO = 'O';
constexpr int kControlN = 'N';
constexpr int kControlC = 'C';
constexpr int kControlV = 'V';
constexpr int kToolbarButtonWidth = 36;
constexpr int kToolbarHeight = 48;

std::optional<AlloyMainCommand> CommandForId(int id) {
  switch (id) {
  case kOpenMarkdownId:
    return AlloyMainCommand::kOpenMarkdown;
  case kOpenIncognitoId:
    return AlloyMainCommand::kOpenIncognito;
  case kCopyId:
    return AlloyMainCommand::kCopy;
  case kPasteId:
    return AlloyMainCommand::kPaste;
  case kAboutId:
    return AlloyMainCommand::kAbout;
  case kLicensesId:
    return AlloyMainCommand::kLicenses;
  case kTogglePinId:
    return AlloyMainCommand::kTogglePin;
  case kDuplicateTabId:
    return AlloyMainCommand::kDuplicateTab;
  case kToggleMuteId:
    return AlloyMainCommand::kToggleMute;
  case kToggleGroupId:
    return AlloyMainCommand::kToggleGroup;
  case kToggleBookmarkBarId:
    return AlloyMainCommand::kToggleBookmarkBar;
  default:
    return std::nullopt;
  }
}

} // namespace

AlloyInteractions::AlloyInteractions(localization::LocaleSnapshot locale,
                                     Callbacks callbacks)
    : locale_(std::move(locale)), callbacks_(std::move(callbacks)) {
  CEF_REQUIRE_UI_THREAD();
}

bool AlloyInteractions::Attach(CefRefPtr<CefWindow> window,
                               CefRefPtr<CefBrowserView> view,
                               CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefPanel> toolbar) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || window_ || !window || !view || !browser || !toolbar ||
      !window->IsValid() || !view->IsValid() || !toolbar->IsValid() ||
      browser->GetIdentifier() <= 0 ||
      browser->GetHost()->GetRuntimeStyle() != CEF_RUNTIME_STYLE_ALLOY ||
      !view->GetWindow() || !toolbar->GetWindow() ||
      !view->GetWindow()->IsSame(window) ||
      !toolbar->GetWindow()->IsSame(window)) {
    return false;
  }
  const std::string menu_label = String("app.menu");
  if (menu_label.empty() || String("menu.open_markdown").empty() ||
      String("privacy.incognito").empty() || String("menu.copy").empty() ||
      String("menu.paste").empty() || String("app.about").empty() ||
      String("menu.licenses").empty() || String("tabs.pin").empty() ||
      String("tabs.unpin").empty() || String("tabs.duplicate").empty() ||
      String("tabs.mute").empty() || String("tabs.unmute").empty() ||
      String("tabs.add_group").empty() || String("tabs.remove_group").empty() ||
      String("tabs.search").empty() || String("bookmarks.add_page").empty() ||
      String("bookmarks.remove_page").empty() ||
      String("bookmarks.show_bar").empty() ||
      String("bookmarks.hide_bar").empty()) {
    return false;
  }
  window_ = std::move(window);
  view_ = std::move(view);
  browser_ = std::move(browser);
  toolbar_ = std::move(toolbar);
  bookmark_button_ = CefLabelButton::CreateLabelButton(this, {});
  if (!bookmark_button_) {
    Shutdown();
    return false;
  }
  bookmark_button_->SetID(AlloyInteractions::kBookmarkButtonId);
  bookmark_button_->SetFocusable(true);
  bookmark_button_->SetMinimumSize(
      CefSize(kToolbarButtonWidth, kToolbarHeight));
  bookmark_button_->SetMaximumSize(
      CefSize(kToolbarButtonWidth, kToolbarHeight));
  toolbar_->AddChildView(bookmark_button_);
  menu_button_ = CefMenuButton::CreateMenuButton(this, {});
  if (!menu_button_) {
    Shutdown();
    return false;
  }
  menu_button_->SetID(kMenuButtonId);
  menu_button_->SetAccessibleName(menu_label);
  menu_button_->SetTooltipText(menu_label);
  menu_button_->SetMinimumSize(CefSize(kToolbarButtonWidth, kToolbarHeight));
  menu_button_->SetMaximumSize(CefSize(kToolbarButtonWidth, kToolbarHeight));
  if (!ApplyAlloyIcon(menu_button_, AlloyIcon::kMenu, menu_label)) {
    Shutdown();
    return false;
  }
  toolbar_->AddChildView(menu_button_);
  if (!RefreshDailyControls()) {
    Shutdown();
    return false;
  }
  toolbar_->Layout();
  return true;
}

bool AlloyInteractions::RefreshDailyControls() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !toolbar_ || !toolbar_->IsValid() || !bookmark_button_ ||
      !bookmark_button_->IsValid()) {
    return false;
  }
  if (menu_open_)
    return true;
  const AlloyBookmarkCommandState state = callbacks_.bookmark_state
                                              ? callbacks_.bookmark_state()
                                              : AlloyBookmarkCommandState{};
  const std::string label =
      String(state.starred ? "bookmarks.remove_page" : "bookmarks.add_page");
  if (!ApplyAlloyIcon(bookmark_button_,
                      state.starred ? AlloyIcon::kBookmarkFilled
                                    : AlloyIcon::kBookmarkOutline,
                      label)) {
    return false;
  }
  bookmark_button_->SetEnabled(state.writable);

  for (const auto &button : bookmark_bar_buttons_) {
    if (button && button->IsValid() && button->GetParentView() &&
        button->GetParentView()->IsSame(toolbar_)) {
      toolbar_->RemoveChildView(button);
    }
  }
  bookmark_bar_buttons_.clear();
  bookmark_bar_ids_.clear();
  if (menu_button_ && menu_button_->IsValid() &&
      menu_button_->GetParentView() &&
      menu_button_->GetParentView()->IsSame(toolbar_)) {
    toolbar_->RemoveChildView(menu_button_);
  }
  if (state.bar_visible) {
    for (const auto &entry : state.entries) {
      if (bookmark_bar_ids_.size() >= kMaximumVisibleBookmarkButtons)
        break;
      if (entry.node_id == 0 || entry.label.empty())
        continue;
      auto button = CefLabelButton::CreateLabelButton(this, entry.label);
      if (!button)
        break;
      button->SetID(AlloyInteractions::kBookmarkBarCommandBase +
                    static_cast<int>(bookmark_bar_ids_.size()));
      button->SetFocusable(true);
      button->SetAccessibleName(entry.label);
      button->SetTooltipText(entry.label);
      toolbar_->AddChildView(button);
      bookmark_bar_buttons_.push_back(button);
      bookmark_bar_ids_.push_back(entry.node_id);
    }
  }
  if (menu_button_)
    toolbar_->AddChildView(menu_button_);
  toolbar_->Layout();
  return true;
}

bool AlloyInteractions::Execute(AlloyMainCommand command) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !browser_ || !browser_->GetMainFrame())
    return false;
  switch (command) {
  case AlloyMainCommand::kOpenMarkdown:
    return callbacks_.open_markdown && callbacks_.open_markdown(browser_);
  case AlloyMainCommand::kOpenIncognito:
    return callbacks_.open_incognito && callbacks_.open_incognito();
  case AlloyMainCommand::kCopy:
    browser_->GetMainFrame()->Copy();
    return true;
  case AlloyMainCommand::kPaste:
    browser_->GetMainFrame()->Paste();
    return true;
  case AlloyMainCommand::kAbout:
    return callbacks_.navigate &&
           callbacks_.navigate(browser_, branding::kAboutBrowserUrl);
  case AlloyMainCommand::kLicenses:
    return callbacks_.navigate && callbacks_.navigate(browser_, kLicensesUrl);
  case AlloyMainCommand::kTogglePin:
  case AlloyMainCommand::kDuplicateTab:
  case AlloyMainCommand::kToggleMute:
  case AlloyMainCommand::kToggleGroup:
  case AlloyMainCommand::kToggleBookmarkBar:
    return callbacks_.daily_command && callbacks_.daily_command(command);
  }
  return false;
}

bool AlloyInteractions::HandleAccelerator(int windows_key_code,
                                          cef_event_flags_t modifiers) {
  CEF_REQUIRE_UI_THREAD();
  const auto forbidden = static_cast<cef_event_flags_t>(EVENTFLAG_ALT_DOWN |
                                                        EVENTFLAG_COMMAND_DOWN);
  if (!active_ || (modifiers & EVENTFLAG_CONTROL_DOWN) == 0 ||
      (modifiers & forbidden) != 0)
    return false;
  const bool shift = (modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  if (shift) {
    return windows_key_code == kControlN &&
           Execute(AlloyMainCommand::kOpenIncognito);
  }
  switch (windows_key_code) {
  case kControlO:
    return Execute(AlloyMainCommand::kOpenMarkdown);
  case kControlC:
    return Execute(AlloyMainCommand::kCopy);
  case kControlV:
    return Execute(AlloyMainCommand::kPaste);
  default:
    return false;
  }
}

bool AlloyInteractions::OnNavigation(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!IsCurrent(browser))
    return false;
  context_menu_active_ = false;
  if (menu_open_ && window_)
    window_->SendKeyPress(27, 0);
  if (callbacks_.cancel_transient)
    callbacks_.cancel_transient();
  return active_;
}

CefRefPtr<CefView> AlloyInteractions::GetView(int view_id) const {
  CEF_REQUIRE_UI_THREAD();
  if (!active_)
    return nullptr;
  if (menu_button_ && menu_button_->GetID() == view_id)
    return menu_button_;
  if (bookmark_button_ && bookmark_button_->GetID() == view_id) {
    return bookmark_button_;
  }
  for (const auto &button : bookmark_bar_buttons_) {
    if (button && button->GetID() == view_id)
      return button;
  }
  return nullptr;
}

void AlloyInteractions::OnMenuButtonPressed(
    CefRefPtr<CefMenuButton> menu_button, const CefPoint &screen_point,
    CefRefPtr<CefMenuButtonPressedLock>) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !menu_button_ || !menu_button ||
      !menu_button->IsSame(menu_button_))
    return;
  ReleaseAlloyIconFocus(menu_button);
  menu_model_ = CefMenuModel::CreateMenuModel(this);
  if (!menu_model_)
    return;
  menu_model_->AddItem(kOpenMarkdownId, String("menu.open_markdown"));
  menu_model_->AddItem(kOpenIncognitoId, String("privacy.incognito"));
  menu_model_->AddSeparator();
  const AlloyDailyCommandState state = callbacks_.daily_state
                                           ? callbacks_.daily_state()
                                           : AlloyDailyCommandState{};
  menu_model_->AddItem(kTogglePinId,
                       String(state.pinned ? "tabs.unpin" : "tabs.pin"));
  menu_model_->AddItem(kDuplicateTabId, String("tabs.duplicate"));
  menu_model_->SetEnabled(kDuplicateTabId, state.can_duplicate);
  menu_model_->AddItem(kToggleMuteId,
                       String(state.muted ? "tabs.unmute" : "tabs.mute"));
  menu_model_->AddItem(
      kToggleGroupId,
      String(state.grouped ? "tabs.remove_group" : "tabs.add_group"));
  search_tab_ids_.clear();
  auto search_menu =
      menu_model_->AddSubMenu(kTabSearchMenuId, String("tabs.search"));
  const auto search_entries = callbacks_.tab_search_entries
                                  ? callbacks_.tab_search_entries()
                                  : std::vector<AlloyTabSearchEntry>{};
  if (search_menu) {
    for (const auto &entry : search_entries) {
      if (search_tab_ids_.size() >= kMaximumSearchEntries)
        break;
      if (entry.tab_id == 0 || entry.label.empty())
        continue;
      const int command_id = AlloyInteractions::kTabSearchCommandBase +
                             static_cast<int>(search_tab_ids_.size());
      search_menu->AddItem(command_id, entry.label);
      search_menu->SetEnabled(command_id, !entry.active);
      search_tab_ids_.push_back(entry.tab_id);
    }
  }
  menu_model_->SetEnabled(kTabSearchMenuId, !search_tab_ids_.empty());
  menu_model_->AddItem(kToggleBookmarkBarId,
                       String(state.bookmark_bar_visible
                                  ? "bookmarks.hide_bar"
                                  : "bookmarks.show_bar"));
  menu_model_->AddSeparator();
  menu_model_->AddItem(kCopyId, String("menu.copy"));
  menu_model_->AddItem(kPasteId, String("menu.paste"));
  menu_model_->AddSeparator();
  menu_model_->AddItem(kAboutId, String("app.about"));
  menu_model_->AddItem(kLicensesId, String("menu.licenses"));
  menu_open_ = true;
  menu_button->ShowMenu(menu_model_, screen_point, CEF_MENU_ANCHOR_TOPRIGHT);
}

void AlloyInteractions::OnButtonPressed(CefRefPtr<CefButton> button) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !button || !button->IsEnabled())
    return;
  ReleaseAlloyIconFocus(button);
  if (bookmark_button_ && bookmark_button_->IsSame(button)) {
    if (callbacks_.toggle_current_bookmark &&
        callbacks_.toggle_current_bookmark()) {
      static_cast<void>(RefreshDailyControls());
    }
    return;
  }
  for (std::size_t index = 0; index < bookmark_bar_buttons_.size(); ++index) {
    if (bookmark_bar_buttons_[index] &&
        bookmark_bar_buttons_[index]->IsSame(button)) {
      if (callbacks_.open_bookmark && index < bookmark_bar_ids_.size()) {
        static_cast<void>(callbacks_.open_bookmark(bookmark_bar_ids_[index]));
      }
      return;
    }
  }
}

void AlloyInteractions::ExecuteCommand(CefRefPtr<CefMenuModel>, int command_id,
                                       cef_event_flags_t) {
  CEF_REQUIRE_UI_THREAD();
  const auto command = CommandForId(command_id);
  if (command) {
    static_cast<void>(Execute(*command));
    return;
  }
  const int search_index =
      command_id - AlloyInteractions::kTabSearchCommandBase;
  if (search_index >= 0 &&
      static_cast<std::size_t>(search_index) < search_tab_ids_.size() &&
      callbacks_.activate_searched_tab) {
    static_cast<void>(callbacks_.activate_searched_tab(
        search_tab_ids_[static_cast<std::size_t>(search_index)]));
  }
}

void AlloyInteractions::MenuWillShow(CefRefPtr<CefMenuModel> model) {
  menu_open_ = active_ && menu_model_ && model;
}

void AlloyInteractions::MenuClosed(CefRefPtr<CefMenuModel>) {
  if (menu_model_) {
    menu_open_ = false;
    menu_model_ = nullptr;
    search_tab_ids_.clear();
    static_cast<void>(RefreshDailyControls());
  }
}

void AlloyInteractions::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  context_menu_active_ = false;
  if (IsCurrent(browser) && callbacks_.augment_context_menu && params &&
      model) {
    const bool augmented =
        callbacks_.augment_context_menu(browser, params, model);
    context_menu_active_ = active_ && augmented;
  }
}

bool AlloyInteractions::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame>,
                                             CefRefPtr<CefContextMenuParams>,
                                             int command_id, EventFlags) {
  CEF_REQUIRE_UI_THREAD();
  return context_menu_active_ && IsCurrent(browser) &&
         callbacks_.context_menu_command &&
         callbacks_.context_menu_command(browser, command_id);
}

void AlloyInteractions::OnContextMenuDismissed(CefRefPtr<CefBrowser>,
                                               CefRefPtr<CefFrame>) {
  context_menu_active_ = false;
}

bool AlloyInteractions::OnDragEnter(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefDragData> drag_data,
                                    DragOperationsMask mask) {
  CEF_REQUIRE_UI_THREAD();
  return IsCurrent(browser) && drag_data && callbacks_.drag_enter &&
         callbacks_.drag_enter(browser, drag_data, mask);
}

bool AlloyInteractions::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_)
    return true;
  active_ = false;
  context_menu_active_ = false;
  menu_open_ = false;
  menu_model_ = nullptr;
  search_tab_ids_.clear();
  if (callbacks_.cancel_transient)
    callbacks_.cancel_transient();
  for (const auto &button : bookmark_bar_buttons_) {
    if (toolbar_ && toolbar_->IsValid() && button && button->IsValid() &&
        button->GetParentView() && button->GetParentView()->IsSame(toolbar_)) {
      toolbar_->RemoveChildView(button);
    }
  }
  bookmark_bar_buttons_.clear();
  bookmark_bar_ids_.clear();
  if (toolbar_ && toolbar_->IsValid() && bookmark_button_ &&
      bookmark_button_->IsValid() && bookmark_button_->GetParentView() &&
      bookmark_button_->GetParentView()->IsSame(toolbar_)) {
    toolbar_->RemoveChildView(bookmark_button_);
  }
  bookmark_button_ = nullptr;
  if (toolbar_ && toolbar_->IsValid() && menu_button_ &&
      menu_button_->IsValid()) {
    const auto parent = menu_button_->GetParentView();
    if (parent && parent->IsSame(toolbar_))
      toolbar_->RemoveChildView(menu_button_);
  }
  menu_button_ = nullptr;
  toolbar_ = nullptr;
  browser_ = nullptr;
  view_ = nullptr;
  window_ = nullptr;
  callbacks_ = {};
  return true;
}

std::string AlloyInteractions::String(const char *key) const {
  const localization::LocaleCatalog catalog(locale_.locale);
  const auto value = catalog.Find(key);
  return value ? std::string(*value) : std::string();
}

bool AlloyInteractions::IsCurrent(CefRefPtr<CefBrowser> browser) const {
  return active_ && browser_ && browser &&
         browser->GetIdentifier() == browser_->GetIdentifier();
}

} // namespace crayon::browser::cef_shell::window
