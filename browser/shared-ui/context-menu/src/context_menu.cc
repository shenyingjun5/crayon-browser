#include "crayon/browser_context_menu/context_menu.h"

#include <algorithm>

namespace crayon::browser_context_menu {
namespace {

bool IsLowerAsciiAlpha(char c) { return c >= 'a' && c <= 'z'; }

/// Extracts the lowercase scheme prefix of `url` (empty when none).
std::string ExtractScheme(const std::string& url) {
  const std::size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) {
    return std::string();
  }
  std::string scheme;
  scheme.reserve(colon);
  for (std::size_t i = 0; i < colon; ++i) {
    const char c = url[i];
    if (!IsLowerAsciiAlpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') {
      return std::string();
    }
    scheme.push_back(c);
  }
  return scheme;
}

bool IsValidContextKind(ContextKind kind) noexcept {
  switch (kind) {
    case ContextKind::kPage:
    case ContextKind::kLink:
    case ContextKind::kImage:
    case ContextKind::kSelection:
      return true;
  }
  return false;
}

}  // namespace

bool IsAvailableIn(ContextCommand command, ContextKind kind) noexcept {
  switch (command) {
    case ContextCommand::kOpenLink:
    case ContextCommand::kOpenLinkInNewTab:
    case ContextCommand::kCopyLinkUrl:
    case ContextCommand::kCopyLinkText:
    case ContextCommand::kSaveLinkAs:
      return kind == ContextKind::kLink;
    case ContextCommand::kCopyImage:
    case ContextCommand::kCopyImageUrl:
    case ContextCommand::kDownloadImage:
      return kind == ContextKind::kImage;
    case ContextCommand::kCopySelection:
    case ContextCommand::kSearchSelection:
      return kind == ContextKind::kSelection;
    case ContextCommand::kPaste:
      return kind == ContextKind::kSelection || kind == ContextKind::kPage;
    case ContextCommand::kSavePageAs:
    case ContextCommand::kPrintPage:
      return kind == ContextKind::kPage;
  }
  return false;
}

bool IsOpenableScheme(const std::string& scheme) noexcept {
  return scheme == "http" || scheme == "https";
}

ContextUrlAction ValidateContextUrl(const std::string& url, std::string* scheme_out) {
  if (url.empty() || url.size() > kMaxContextUrlLen) {
    return ContextUrlAction::kMalformed;
  }
  const std::string scheme = ExtractScheme(url);
  if (scheme_out != nullptr) {
    *scheme_out = scheme;
  }
  if (scheme.empty()) {
    return ContextUrlAction::kMalformed;
  }
  if (!IsOpenableScheme(scheme)) {
    return ContextUrlAction::kSchemeRejected;
  }
  return ContextUrlAction::kAllowed;
}

bool ContextMenuController::Open(ContextKind kind) noexcept {
  if (!IsValidContextKind(kind)) {
    return false;
  }
  open_ = true;
  kind_ = kind;
  return true;
}

void ContextMenuController::Close() noexcept {
  open_ = false;
}

std::vector<ContextCommand> ContextMenuController::VisibleCommands() const {
  std::vector<ContextCommand> visible;
  if (!open_) {
    return visible;
  }
  const ContextCommand all[] = {
      ContextCommand::kOpenLink,      ContextCommand::kOpenLinkInNewTab,
      ContextCommand::kCopyLinkUrl,   ContextCommand::kCopyLinkText,
      ContextCommand::kSaveLinkAs,    ContextCommand::kCopyImage,
      ContextCommand::kCopyImageUrl,  ContextCommand::kDownloadImage,
      ContextCommand::kCopySelection, ContextCommand::kSearchSelection,
      ContextCommand::kPaste,         ContextCommand::kSavePageAs,
      ContextCommand::kPrintPage};
  for (ContextCommand command : all) {
    if (IsAvailableIn(command, kind_)) {
      visible.push_back(command);
    }
  }
  return visible;
}

bool ContextMenuController::Execute(ContextCommand command) noexcept {
  if (!open_ || !IsAvailableIn(command, kind_)) {
    return false;
  }
  last_command_ = command;
  return true;
}

bool ClipboardGuard::CopyText(const std::string& text, ActionSource source) {
  if (source != ActionSource::kUserCommand) {
    return false;
  }
  if (text.size() > kMaxClipboardTextLen) {
    return false;
  }
  pending_ = true;
  text_ = text;
  return true;
}

void ClipboardGuard::AcknowledgeWrite() noexcept {
  pending_ = false;
  text_.clear();
}

bool LocalFileEntryGuard::IsValidEntryName(const std::string& name) {
  if (name.empty() || name.size() > kMaxFileEntryLen || name[0] == '.') {
    return false;
  }
  if (name.find("..") != std::string::npos) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == ' ';
  });
}

bool LocalFileEntryGuard::RequestOpen(const std::string& name, ActionSource source) {
  if (pending_) {
    return false;
  }
  if (source != ActionSource::kUserCommand || !IsValidEntryName(name)) {
    return false;
  }
  pending_ = true;
  name_ = name;
  return true;
}

bool LocalFileEntryGuard::ConfirmOpen() {
  if (!pending_) {
    return false;
  }
  pending_ = false;
  return true;
}

void LocalFileEntryGuard::CancelOpen() noexcept {
  pending_ = false;
  name_.clear();
}

}  // namespace crayon::browser_context_menu
