#include "browser/window/alloy_activity_surface.h"

#include <algorithm>
#include <utility>

#include "crayon/browser_localization/locale_catalog.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr std::size_t kMaximumHistoryMenuEntries = 10;
constexpr std::size_t kMaximumDownloadMenuEntries = 8;
constexpr int kEmptyHistoryId = MENU_ID_USER_FIRST + 102;
constexpr int kEmptyDownloadsId = MENU_ID_USER_FIRST + 103;
constexpr int kDownloadSubmenuBase = MENU_ID_USER_FIRST + 300;
constexpr int kDownloadStatusBase = MENU_ID_USER_FIRST + 400;

}  // namespace

AlloyActivitySurface::AlloyActivitySurface(
    localization::LocaleSnapshot locale, AlloyHistory* history,
    AlloyDownloads* downloads, Callbacks callbacks)
    : locale_(std::move(locale)),
      history_(history),
      downloads_(downloads),
      callbacks_(std::move(callbacks)) {
  CEF_REQUIRE_UI_THREAD();
}

bool AlloyActivitySurface::Attach(CefRefPtr<CefWindow> window,
                                  CefRefPtr<CefPanel> toolbar) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || window_ || !window || !toolbar || !history_ || !downloads_ ||
      !window->IsValid() || !toolbar->IsValid() || !toolbar->GetWindow() ||
      !toolbar->GetWindow()->IsSame(window)) {
    return false;
  }
  const std::string history_label = String("history.title");
  const std::string downloads_label = String("downloads.title");
  if (history_label.empty() || downloads_label.empty()) return false;
  window_ = std::move(window);
  toolbar_ = std::move(toolbar);
  history_button_ = CefMenuButton::CreateMenuButton(this, history_label);
  downloads_button_ = CefMenuButton::CreateMenuButton(this, downloads_label);
  if (!history_button_ || !downloads_button_) {
    Shutdown();
    return false;
  }
  history_button_->SetID(kHistoryButtonId);
  history_button_->SetAccessibleName(history_label);
  history_button_->SetTooltipText(history_label);
  downloads_button_->SetID(kDownloadsButtonId);
  downloads_button_->SetAccessibleName(downloads_label);
  downloads_button_->SetTooltipText(downloads_label);
  toolbar_->AddChildView(history_button_);
  toolbar_->AddChildView(downloads_button_);
  toolbar_->Layout();
  return true;
}

CefRefPtr<CefView> AlloyActivitySurface::GetView(int view_id) const {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return nullptr;
  if (history_button_ && history_button_->GetID() == view_id) {
    return history_button_;
  }
  if (downloads_button_ && downloads_button_->GetID() == view_id) {
    return downloads_button_;
  }
  return nullptr;
}

void AlloyActivitySurface::OnButtonPressed(CefRefPtr<CefButton>) {}

void AlloyActivitySurface::OnMenuButtonPressed(
    CefRefPtr<CefMenuButton> menu_button, const CefPoint& screen_point,
    CefRefPtr<CefMenuButtonPressedLock>) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_ || menu_open_ || !menu_button || !history_button_ ||
      !downloads_button_) {
    return;
  }
  menu_model_ = CefMenuModel::CreateMenuModel(this);
  if (!menu_model_) return;
  if (menu_button->IsSame(history_button_)) {
    BuildHistoryMenu();
  } else if (menu_button->IsSame(downloads_button_)) {
    BuildDownloadsMenu();
  } else {
    menu_model_ = nullptr;
    return;
  }
  menu_open_ = true;
  menu_button->ShowMenu(menu_model_, screen_point, CEF_MENU_ANCHOR_TOPRIGHT);
}

void AlloyActivitySurface::BuildHistoryMenu() {
  history_entry_ids_.clear();
  download_commands_.clear();
  menu_model_->AddItem(kRestoreClosedId, String("history.reopen_closed"));
  menu_model_->SetEnabled(
      kRestoreClosedId,
      history_ && history_->store().recently_closed_count() != 0);
  menu_model_->AddItem(kClearHistoryId, String("history.clear"));
  menu_model_->SetEnabled(kClearHistoryId,
                          history_ && !history_->store().entries().empty());
  menu_model_->AddSeparator();
  if (!history_) return;
  for (const auto& entry : history_->view().entries()) {
    if (history_entry_ids_.size() >= kMaximumHistoryMenuEntries) break;
    if (entry.entry_id == 0 || entry.display_title.empty()) continue;
    const int command_id = kHistoryEntryCommandBase +
                           static_cast<int>(history_entry_ids_.size());
    menu_model_->AddItem(command_id, entry.display_title);
    history_entry_ids_.push_back(entry.entry_id);
  }
  if (history_entry_ids_.empty()) {
    menu_model_->AddItem(kEmptyHistoryId, String("history.empty"));
    menu_model_->SetEnabled(kEmptyHistoryId, false);
  }
}

void AlloyActivitySurface::BuildDownloadsMenu() {
  history_entry_ids_.clear();
  download_commands_.clear();
  if (!downloads_ || downloads_->shelf().items().empty()) {
    menu_model_->AddItem(kEmptyDownloadsId, String("downloads.empty"));
    menu_model_->SetEnabled(kEmptyDownloadsId, false);
    return;
  }
  std::size_t count = 0;
  for (const auto& item : downloads_->shelf().items()) {
    if (count >= kMaximumDownloadMenuEntries) break;
    if (item.download_id == 0 || item.display_name.empty()) continue;
    auto submenu = menu_model_->AddSubMenu(
        kDownloadSubmenuBase + static_cast<int>(count), item.display_name);
    if (!submenu) continue;
    const int status_id = kDownloadStatusBase + static_cast<int>(count);
    submenu->AddItem(status_id, DownloadLabel(item));
    submenu->SetEnabled(status_id, false);
    switch (item.state) {
      case browser_downloads::DownloadState::kPendingDangerConfirm:
        AddDownloadAction(submenu, item.download_id, DownloadAction::kKeep,
                          "downloads.keep");
        AddDownloadAction(submenu, item.download_id, DownloadAction::kDiscard,
                          "downloads.discard");
        break;
      case browser_downloads::DownloadState::kInProgress:
        AddDownloadAction(submenu, item.download_id, DownloadAction::kPause,
                          "downloads.pause");
        AddDownloadAction(submenu, item.download_id, DownloadAction::kCancel,
                          "downloads.cancel");
        break;
      case browser_downloads::DownloadState::kPaused:
        AddDownloadAction(submenu, item.download_id, DownloadAction::kResume,
                          "downloads.resume");
        AddDownloadAction(submenu, item.download_id, DownloadAction::kCancel,
                          "downloads.cancel");
        break;
      case browser_downloads::DownloadState::kCompleted:
        AddDownloadAction(submenu, item.download_id,
                          DownloadAction::kShowInFolder,
                          "downloads.show_in_folder");
        break;
      case browser_downloads::DownloadState::kFailed:
      case browser_downloads::DownloadState::kCancelled:
        break;
    }
    ++count;
  }
  if (count == 0) {
    menu_model_->AddItem(kEmptyDownloadsId, String("downloads.empty"));
    menu_model_->SetEnabled(kEmptyDownloadsId, false);
  }
}

void AlloyActivitySurface::AddDownloadAction(CefRefPtr<CefMenuModel> menu,
                                             std::uint64_t download_id,
                                             DownloadAction action,
                                             const char* label_key) {
  if (!menu || download_commands_.size() >= kMaximumDownloadMenuEntries * 2) {
    return;
  }
  const int command_id = kDownloadActionCommandBase +
                         static_cast<int>(download_commands_.size());
  menu->AddItem(command_id, String(label_key));
  download_commands_.push_back({download_id, action});
}

void AlloyActivitySurface::ExecuteCommand(CefRefPtr<CefMenuModel>,
                                          int command_id,
                                          cef_event_flags_t) {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return;
  if (command_id == kRestoreClosedId) {
    if (history_ && history_->RestoreRecentlyClosed() ==
                        AlloyHistoryResult::kSuccess &&
        callbacks_.persist_history) {
      static_cast<void>(callbacks_.persist_history());
    }
    return;
  }
  if (command_id == kClearHistoryId) {
    if (!history_ || !callbacks_.confirm_clear_history ||
        !callbacks_.confirm_clear_history()) {
      return;
    }
    const std::string previous = history_->Export();
    if (!history_->ClearAll()) return;
    if (!callbacks_.persist_history || !callbacks_.persist_history()) {
      static_cast<void>(history_->Import(previous));
    }
    return;
  }
  const int history_index = command_id - kHistoryEntryCommandBase;
  if (history_index >= 0 &&
      static_cast<std::size_t>(history_index) < history_entry_ids_.size()) {
    static_cast<void>(ExecuteHistoryEntry(
        static_cast<std::size_t>(history_index)));
    return;
  }
  const int download_index = command_id - kDownloadActionCommandBase;
  if (download_index >= 0 &&
      static_cast<std::size_t>(download_index) < download_commands_.size()) {
    static_cast<void>(ExecuteDownloadAction(
        static_cast<std::size_t>(download_index)));
  }
}

bool AlloyActivitySurface::ExecuteHistoryEntry(std::size_t index) {
  if (!history_ || index >= history_entry_ids_.size() ||
      !callbacks_.navigate_current) {
    return false;
  }
  const auto* entry = history_->store().Find(history_entry_ids_[index]);
  return entry && callbacks_.navigate_current(entry->url);
}

bool AlloyActivitySurface::ExecuteDownloadAction(std::size_t index) {
  if (!downloads_ || index >= download_commands_.size()) return false;
  const auto& command = download_commands_[index];
  switch (command.action) {
    case DownloadAction::kKeep: {
      const auto* item = downloads_->shelf().Find(command.download_id);
      return item && callbacks_.confirm_dangerous &&
             callbacks_.confirm_dangerous(item->display_name) &&
             downloads_->ConfirmDangerous(command.download_id);
    }
    case DownloadAction::kDiscard:
      return downloads_->DiscardDangerous(command.download_id);
    case DownloadAction::kPause:
      return downloads_->Pause(command.download_id);
    case DownloadAction::kResume:
      return downloads_->Resume(command.download_id);
    case DownloadAction::kCancel:
      return downloads_->Cancel(command.download_id);
    case DownloadAction::kShowInFolder:
      return downloads_->OpenLocation(command.download_id);
  }
  return false;
}

std::string AlloyActivitySurface::DownloadLabel(
    const browser_downloads_view::DownloadProjection& item) const {
  const char* key = "downloads.status.failed";
  switch (item.state) {
    case browser_downloads::DownloadState::kPendingDangerConfirm:
      key = "downloads.status.pending";
      break;
    case browser_downloads::DownloadState::kInProgress:
      key = "downloads.status.in_progress";
      break;
    case browser_downloads::DownloadState::kPaused:
      key = "downloads.status.paused";
      break;
    case browser_downloads::DownloadState::kCompleted:
      key = "downloads.status.completed";
      break;
    case browser_downloads::DownloadState::kFailed:
      break;
    case browser_downloads::DownloadState::kCancelled:
      key = "downloads.status.cancelled";
      break;
  }
  std::string label = String(key);
  if (item.state == browser_downloads::DownloadState::kInProgress ||
      item.state == browser_downloads::DownloadState::kPaused) {
    label += " " + std::to_string(item.percent) + "%";
  }
  return label;
}

void AlloyActivitySurface::MenuWillShow(CefRefPtr<CefMenuModel> model) {
  menu_open_ = active_ && menu_model_ && model && menu_model_.get() == model.get();
}

void AlloyActivitySurface::MenuClosed(CefRefPtr<CefMenuModel> model) {
  if (!menu_model_ || !model || menu_model_.get() != model.get()) return;
  menu_open_ = false;
  menu_model_ = nullptr;
  history_entry_ids_.clear();
  download_commands_.clear();
}

bool AlloyActivitySurface::Shutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!active_) return true;
  active_ = false;
  menu_open_ = false;
  menu_model_ = nullptr;
  history_entry_ids_.clear();
  download_commands_.clear();
  for (const auto& button : {history_button_, downloads_button_}) {
    if (toolbar_ && toolbar_->IsValid() && button && button->IsValid() &&
        button->GetParentView() && button->GetParentView()->IsSame(toolbar_)) {
      toolbar_->RemoveChildView(button);
    }
  }
  history_button_ = nullptr;
  downloads_button_ = nullptr;
  toolbar_ = nullptr;
  window_ = nullptr;
  callbacks_ = {};
  history_ = nullptr;
  downloads_ = nullptr;
  return true;
}

std::string AlloyActivitySurface::String(const char* key) const {
  const localization::LocaleCatalog catalog(locale_.locale);
  const auto value = catalog.Find(key ? key : "");
  return value ? std::string(*value) : std::string{};
}

}  // namespace crayon::browser::cef_shell::window
