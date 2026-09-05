#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_OMNIBOX_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_OMNIBOX_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "crayon/browser_omnibox/omnibox_state.h"
#include "crayon/browser_omnibox_provider/search_provider.h"
#include "crayon/browser_privacy/privacy_defaults.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"

namespace crayon::browser::cef_shell::window {

enum class OmniboxSubmissionKind {
  kNavigateUrl = 0,
  kSearchUrl,
  kEmpty,
  kNoSearchProvider,
  kBlocked,
};

struct OmniboxSubmission final {
  OmniboxSubmissionKind kind;
  std::string value;
};

class AlloyOmnibox final {
public:
  struct Strings final {
    std::string placeholder;
    std::string accessible_name;
  };

  struct Callbacks final {
    std::function<void(std::uint64_t, const std::string &)> request_suggestions;
    std::function<void(const OmniboxSubmission &)> submit;
    std::function<void()> cancel;
  };

  AlloyOmnibox(
      Strings strings, Callbacks callbacks,
      browser_privacy::PrivacyDefaults privacy,
      browser_omnibox_provider::SearchProviderSet search_providers = {});
  ~AlloyOmnibox();

  AlloyOmnibox(const AlloyOmnibox &) = delete;
  AlloyOmnibox &operator=(const AlloyOmnibox &) = delete;

  CefRefPtr<CefPanel> panel() const;
  CefRefPtr<CefTextfield> textfield() const;

  bool Focus();
  bool Edit(std::string text);
  bool
  ApplySuggestions(std::uint64_t generation,
                   std::vector<browser_omnibox::OmniboxSuggestion> suggestions);
  bool SelectNextSuggestion();
  bool SelectPreviousSuggestion();
  bool Submit();
  bool Cancel();
  bool SetAddress(std::string address);
  bool OnNavigationFinished(bool succeeded, std::string address);
  bool Shutdown();

  bool active() const noexcept;
  std::uint64_t edit_generation() const noexcept;
  std::size_t suggestion_count() const noexcept;
  browser_omnibox::SuggestionIndex selected_suggestion() const noexcept;
  browser_omnibox::OmniboxState state() const noexcept;
  std::string displayed_text() const;

  static std::optional<std::string> SafeDisplayText(std::string address);

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_OMNIBOX_H_
