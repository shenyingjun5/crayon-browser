#ifndef CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_BROWSER_NEW_TAB_NEW_TAB_PAGE_H_
#define CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_BROWSER_NEW_TAB_NEW_TAB_PAGE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::browser_new_tab {

inline constexpr char kNewTabScheme[] = "crayon";
inline constexpr char kNewTabHost[] = "newtab";
inline constexpr char kNewTabUrl[] = "crayon://newtab/";
inline constexpr char kNewTabStylesheetUrl[] = "crayon://newtab/styles.css";
inline constexpr std::uint32_t kShortcutConfigSchemaVersion = 1;
inline constexpr std::size_t kMaximumShortcutCount = 12;

enum class NewTabProfileMode { kRegular, kIncognito };

enum class NewTabConfigStatus {
  kAccepted,
  kUnsupportedVersion,
  kTooManyEntries,
  kInvalidEntry,
  kDuplicateId,
};

struct ShortcutEntry {
  std::string id;
  std::string title;
  std::string url;
};

struct ShortcutConfig {
  std::uint32_t schema_version = kShortcutConfigSchemaVersion;
  std::vector<ShortcutEntry> entries;
};

struct NewTabPageModel {
  NewTabProfileMode profile_mode = NewTabProfileMode::kRegular;
  NewTabConfigStatus config_status = NewTabConfigStatus::kAccepted;
  std::vector<ShortcutEntry> shortcuts;
};

struct NewTabPageStrings {
  std::string language;
  std::string document_title;
  std::string regular_heading;
  std::string incognito_heading;
  std::string regular_description;
  std::string incognito_description;
  std::string omnibox_hint;
  std::string shortcuts_heading;
  std::string empty_shortcuts;
  std::string config_error;
};

struct NewTabRequestParts {
  std::string method;
  std::string scheme;
  std::string host;
  std::string path;
  bool has_credentials = false;
  bool has_port = false;
  bool has_query = false;
  bool has_fragment = false;
};

enum class NewTabResourceKind {
  kDocument,
  kStylesheet,
  kNotFound,
  kMethodNotAllowed,
  kRejected,
};

struct NewTabRoute {
  NewTabResourceKind kind = NewTabResourceKind::kRejected;
  int status_code = 0;
  bool include_body = false;
};

NewTabPageModel BuildNewTabPageModel(NewTabProfileMode profile_mode,
                                     const ShortcutConfig& config);
NewTabRoute ClassifyNewTabRequest(const NewTabRequestParts& request);
std::string RenderNewTabDocument(const NewTabPageModel& model,
                                 const NewTabPageStrings& strings);
std::string RenderNewTabStylesheet();

}  // namespace crayon::browser_new_tab

#endif  // CRAYON_BROWSER_SHARED_UI_NEW_TAB_INCLUDE_CRAYON_BROWSER_NEW_TAB_NEW_TAB_PAGE_H_
