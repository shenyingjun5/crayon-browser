#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace crayon::browser_context_menu {

/// Maximum URL length accepted by the context guard, in bytes.
inline constexpr std::size_t kMaxContextUrlLen = 2048;
/// Maximum clipboard text length accepted for a user copy command.
inline constexpr std::size_t kMaxClipboardTextLen = 1'048'576;
/// Maximum local-file entry token length, in bytes.
inline constexpr std::size_t kMaxFileEntryLen = 255;

/// Closed context kinds driving menu minimization (UX-015).
enum class ContextKind { kPage = 0, kLink, kImage, kSelection };

/// Closed command set.  Each command is available in exactly the
/// contexts listed by `IsAvailableIn`; the menu shows only those.
enum class ContextCommand {
  kOpenLink = 0,
  kOpenLinkInNewTab,
  kCopyLinkUrl,
  kCopyLinkText,
  kSaveLinkAs,
  kCopyImage,
  kCopyImageUrl,
  kDownloadImage,
  kCopySelection,
  kSearchSelection,
  kPaste,
  kSavePageAs,
  kPrintPage
};

/// Availability matrix (context minimization): the closed source of
/// truth for which commands may appear on which context.
bool IsAvailableIn(ContextCommand command, ContextKind kind) noexcept;

/// Closed scheme classification for context URLs.
enum class ContextUrlAction { kAllowed, kSchemeRejected, kMalformed };

/// Reports whether `scheme` (ASCII, lowercase expected by caller) is in
/// the openable allow-list (`http`/`https` only).
bool IsOpenableScheme(const std::string& scheme) noexcept;

/// Validates a context URL for open/copy/download actions.  Rejects
/// dangerous schemes (`javascript:`, `data:`, `file:`, `vbscript:` and
/// anything outside the allow-list), oversize and empty inputs.
ContextUrlAction ValidateContextUrl(const std::string& url, std::string* scheme_out);

/// Menu view model.  Tracks the open context and dispatches commands;
/// invoking a command that the context does not offer is a stable
/// rejection (a page cannot reach commands the context hides).
/// Thread contract: single-threaded, UI thread only.
class ContextMenuController final {
 public:
  ContextMenuController() = default;

  /// Opens the menu for a context; rejects unknown enum values.
  bool Open(ContextKind kind) noexcept;
  /// Dismisses the menu; pending state cleared.
  void Close() noexcept;

  /// Returns the commands visible in the open context (minimized set).
  std::vector<ContextCommand> VisibleCommands() const;

  /// Attempts to execute a command; rejected when the menu is closed or
  /// the command is not part of the open context's minimized set.
  bool Execute(ContextCommand command) noexcept;

  bool open() const noexcept { return open_; }
  ContextKind kind() const noexcept { return kind_; }
  ContextCommand last_command() const noexcept { return last_command_; }

 private:
  bool open_{false};
  ContextKind kind_{ContextKind::kPage};
  ContextCommand last_command_{ContextCommand::kSavePageAs};
};

/// Closed origin of a clipboard or local-file action: only explicit
/// user commands may act; page-initiated actions are rejected.
enum class ActionSource { kUserCommand = 0, kPage };

/// Clipboard write guard (UX-015: 复制粘贴 with bounded size; page
/// cannot trigger writes).
/// Thread contract: single-threaded, UI thread only.
class ClipboardGuard final {
 public:
  /// A user-command copy of bounded text; anything else is rejected.
  bool CopyText(const std::string& text, ActionSource source);

  bool has_pending_write() const noexcept { return pending_; }
  const std::string& pending_text() const noexcept { return text_; }
  std::size_t pending_len() const noexcept { return text_.size(); }
  /// The platform layer acknowledges the write and clears the buffer.
  void AcknowledgeWrite() noexcept;

 private:
  bool pending_{false};
  std::string text_;
};

/// Controlled local-file entry (UX-015: 危险路径拒绝; hidden external
/// actions impossible).  A file entry token uses the closed charset
/// `[A-Za-z0-9._ -]` with no leading dot, no `..` and no separators;
/// opening requires an explicit user confirmation step.
/// Thread contract: single-threaded, UI thread only.
class LocalFileEntryGuard final {
 public:
  LocalFileEntryGuard() = default;

  /// Validates a file entry token (see class comment).
  static bool IsValidEntryName(const std::string& name);

  /// A page can never open local files; a user command starts the
  /// two-step confirm flow.
  bool RequestOpen(const std::string& name, ActionSource source);
  /// Confirms the pending entry (user clicked open).
  bool ConfirmOpen();
  /// Cancels the pending entry without side effects.
  void CancelOpen() noexcept;

  bool pending() const noexcept { return pending_; }
  const std::string& pending_name() const noexcept { return name_; }

 private:
  bool pending_{false};
  std::string name_;
};

}  // namespace crayon::browser_context_menu
