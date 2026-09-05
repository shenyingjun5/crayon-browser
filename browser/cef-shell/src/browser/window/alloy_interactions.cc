#include "browser/window/alloy_interactions.h"

#include <optional>
#include <utility>

#include "browser/branding/about_destination.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr char kLicensesUrl[] = "chrome://credits/";
constexpr int kOpenMarkdownId = 0xcb01;
constexpr int kCopyId = 0xcb02;
constexpr int kPasteId = 0xcb03;
constexpr int kAboutId = 0xcb04;
constexpr int kLicensesId = 0xcb05;
constexpr int kControlO = 'O';
constexpr int kControlC = 'C';
constexpr int kControlV = 'V';

std::optional<AlloyMainCommand> CommandForId(int id) {
  switch (id) {
  case kOpenMarkdownId:
    return AlloyMainCommand::kOpenMarkdown;
  case kCopyId:
    return AlloyMainCommand::kCopy;
  case kPasteId:
    return AlloyMainCommand::kPaste;
  case kAboutId:
    return AlloyMainCommand::kAbout;
  case kLicensesId:
    return AlloyMainCommand::kLicenses;
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
      String("menu.copy").empty() || String("menu.paste").empty() ||
      String("app.about").empty() || String("menu.licenses").empty()) {
    return false;
  }
  window_ = std::move(window);
  view_ = std::move(view);
  browser_ = std::move(browser);
  toolbar_ = std::move(toolbar);
  menu_button_ = CefMenuButton::CreateMenuButton(this, menu_label);
  if (!menu_button_) {
    Shutdown();
    return false;
  }
  menu_button_->SetID(kMenuButtonId);
  menu_button_->SetAccessibleName(menu_label);
  menu_button_->SetTooltipText(menu_label);
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
  }
  return false;
}

bool AlloyInteractions::HandleAccelerator(int windows_key_code,
                                          cef_event_flags_t modifiers) {
  CEF_REQUIRE_UI_THREAD();
  const auto forbidden = static_cast<cef_event_flags_t>(
      EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN | EVENTFLAG_SHIFT_DOWN);
  if (!active_ || (modifiers & EVENTFLAG_CONTROL_DOWN) == 0 ||
      (modifiers & forbidden) != 0)
    return false;
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
  return active_ && menu_button_ && menu_button_->GetID() == view_id
             ? menu_button_
             : nullptr;
}

void AlloyInteractions::OnMenuButtonPressed(
    CefRefPtr<CefMenuButton> menu_button, const CefPoint &screen_point,
    CefRefPtr<CefMenuButtonPressedLock>) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || !menu_button_ || !menu_button ||
      !menu_button->IsSame(menu_button_))
    return;
  menu_model_ = CefMenuModel::CreateMenuModel(this);
  if (!menu_model_)
    return;
  menu_model_->AddItem(kOpenMarkdownId, String("menu.open_markdown"));
  menu_model_->AddSeparator();
  menu_model_->AddItem(kCopyId, String("menu.copy"));
  menu_model_->AddItem(kPasteId, String("menu.paste"));
  menu_model_->AddSeparator();
  menu_model_->AddItem(kAboutId, String("app.about"));
  menu_model_->AddItem(kLicensesId, String("menu.licenses"));
  menu_open_ = true;
  menu_button->ShowMenu(menu_model_, screen_point,
                        CEF_MENU_ANCHOR_TOPRIGHT);
}

void AlloyInteractions::OnButtonPressed(CefRefPtr<CefButton>) {}

void AlloyInteractions::ExecuteCommand(CefRefPtr<CefMenuModel>, int command_id,
                                       cef_event_flags_t) {
  CEF_REQUIRE_UI_THREAD();
  const auto command = CommandForId(command_id);
  if (command)
    static_cast<void>(Execute(*command));
}

void AlloyInteractions::MenuWillShow(CefRefPtr<CefMenuModel> model) {
  menu_open_ = active_ && menu_model_ && model;
}

void AlloyInteractions::MenuClosed(CefRefPtr<CefMenuModel>) {
  if (menu_model_) {
    menu_open_ = false;
    menu_model_ = nullptr;
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

bool AlloyInteractions::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams>, int command_id, EventFlags) {
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
  if (callbacks_.cancel_transient)
    callbacks_.cancel_transient();
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
