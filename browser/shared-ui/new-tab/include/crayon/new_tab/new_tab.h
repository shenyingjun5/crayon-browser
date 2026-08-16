#ifndef CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_NEW_TAB_NEW_TAB_H_
#define CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_NEW_TAB_NEW_TAB_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crayon::browser::new_tab {

inline constexpr std::string_view kNewTabUrl = "crayon://newtab/";
inline constexpr std::size_t kMaximumPinnedShortcuts = 12;
inline constexpr std::size_t kMaximumShortcutTitleBytes = 80;
inline constexpr std::size_t kMaximumShortcutUrlBytes = 2048;
inline constexpr std::size_t kMaximumLocalizedStringBytes = 256;
inline constexpr std::size_t kMaximumRenderedPageBytes = 64 * 1024;

enum class ProfileMode { kStandard, kPrivate };

struct ShortcutCandidate final {
  std::string title;
  std::string url;
};

struct PinnedShortcut final {
  std::string title;
  std::string url;
};

struct NewTabModel final {
  ProfileMode profile_mode = ProfileMode::kStandard;
  std::vector<PinnedShortcut> shortcuts;
  bool show_shortcuts = false;
  bool show_cast_entry = false;
};

struct NewTabStrings final {
  std::string language_tag;
  std::string page_title;
  std::string search_placeholder;
  std::string shortcuts_heading;
  std::string private_heading;
  std::string private_description;
  std::string cast_label;
};

enum class NewTabRequestKind { kReject, kGet, kHead };

struct NewTabResource final {
  std::string mime_type;
  std::string charset;
  std::string cache_control;
  std::string content_security_policy;
  std::string body;
};

NewTabModel BuildNewTabModel(
    ProfileMode profile_mode,
    const std::vector<ShortcutCandidate>& shortcut_candidates);

NewTabRequestKind ValidateNewTabRequest(std::string_view method,
                                        std::string_view url) noexcept;

std::optional<NewTabResource> BuildNewTabResource(
    NewTabRequestKind request_kind, const NewTabModel& model,
    const NewTabStrings& strings);

}  // namespace crayon::browser::new_tab

#endif  // CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_NEW_TAB_NEW_TAB_H_
