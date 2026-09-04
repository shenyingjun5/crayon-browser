#include "browser/window/chrome_location_bar.h"

#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {

ChromeLocationBar::~ChromeLocationBar() {
  if (row_)
    Detach();
}

bool ChromeLocationBar::Attach(CefRefPtr<CefPanel> parent,
                               CefRefPtr<CefBrowserView> browser_view,
                               CefRefPtr<CefView> trailing_action) {
  CEF_REQUIRE_UI_THREAD();
  if (row_ || !parent || !browser_view || !trailing_action ||
      !parent->IsValid() || !browser_view->IsValid() ||
      !trailing_action->IsValid() || trailing_action->GetParentView())
    return false;
  auto browser_window = browser_view->GetWindow();
  auto parent_window = parent->GetWindow();
  if (!browser_window || !parent_window ||
      !browser_window->IsSame(parent_window))
    return false;
  auto location = browser_view->GetChromeToolbar();
  // GetChromeToolbar returns an already-attached borrowed Chrome view.
  // Adding that public view to our panel is the supported CEF LOCATION
  // pattern; the BrowserView owner must not lend it to another surface.
  if (!location || !location->IsValid())
    return false;

  parent_ = std::move(parent);
  browser_view_ = std::move(browser_view);
  action_ = std::move(trailing_action);
  row_ = CefPanel::CreatePanel(nullptr);
  CefBoxLayoutSettings settings;
  settings.horizontal = true;
  auto layout = row_->SetToBoxLayout(settings);
  location_ = std::move(location);
  row_->AddChildView(location_);
  row_->AddChildView(action_);
  layout->SetFlexForView(location_, 1);
  parent_->AddChildViewAt(row_, 0);
  return true;
}

void ChromeLocationBar::SuspendLocation() {
  CEF_REQUIRE_UI_THREAD();
  if (row_ && row_->IsValid() && location_ && location_->IsValid())
    row_->RemoveChildView(location_);
  location_ = nullptr;
}

bool ChromeLocationBar::RestoreLocation() {
  CEF_REQUIRE_UI_THREAD();
  if (location_attached())
    return true;
  if (!row_ || !row_->IsValid() || !parent_ || !parent_->IsValid() ||
      !row_->GetParentView() || !row_->GetParentView()->IsSame(parent_) ||
      !browser_view_ || !browser_view_->IsValid())
    return false;
  auto location = browser_view_->GetChromeToolbar();
  if (!location || !location->IsValid())
    return false;
  location_ = std::move(location);
  row_->AddChildViewAt(location_, 0);
  row_->GetLayout()->AsBoxLayout()->SetFlexForView(location_, 1);
  parent_->Layout();
  return true;
}

void ChromeLocationBar::Detach() {
  CEF_REQUIRE_UI_THREAD();
  SuspendLocation();
  // The caller may retain and reuse its action. Unparent it explicitly;
  // releasing our row reference alone does not sever its Views attachment.
  if (row_ && row_->IsValid() && action_ && action_->IsValid())
    row_->RemoveChildView(action_);
  if (parent_ && parent_->IsValid() && row_ && row_->IsValid())
    parent_->RemoveChildView(row_);
  row_ = nullptr;
  action_ = nullptr;
  browser_view_ = nullptr;
  parent_ = nullptr;
}

bool ChromeLocationBar::location_attached() const {
  CEF_REQUIRE_UI_THREAD();
  return location_ && location_->IsValid() && row_ && row_->IsValid() &&
         location_->GetParentView() && location_->GetParentView()->IsSame(row_);
}

CefRect ChromeLocationBar::LocationBoundsInScreen() const {
  CEF_REQUIRE_UI_THREAD();
  return location_attached() ? location_->GetBoundsInScreen() : CefRect();
}

CefRect ChromeLocationBar::ActionBoundsInScreen() const {
  CEF_REQUIRE_UI_THREAD();
  return action_ && action_->IsValid() ? action_->GetBoundsInScreen()
                                       : CefRect();
}

} // namespace crayon::browser::cef_shell::window
