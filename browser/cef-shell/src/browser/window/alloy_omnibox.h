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
#include "include/views/cef_label_button.h"
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
    /// PLT-SHELL-24M2FIX-B: shown when a search-query submission has no
    /// configured provider (the product ships none by design), so the input
    /// can reach no remote service.
    std::string no_search_provider_notice;
    /// Shown when a submission is blocked before it can reach the network.
    std::string blocked_notice;
    /// Shown when a main-frame navigation finished unsuccessfully: the shell
    /// has no error page of its own, so without this the address bar keeps the
    /// failed URL and the page area stays blank with no explanation.
    /// Platforms that leave it empty keep the previous silent behaviour.
    std::string load_failed_notice;
    /// PLT-SHELL-24M2FIX-C6: accessible name of the trailing bookmark control,
    /// in its un-bookmarked and bookmarked states.
    std::string bookmark_add;
    std::string bookmark_remove;
  };

  struct Callbacks final {
    std::function<void(std::uint64_t, const std::string &)> request_suggestions;
    std::function<void(const OmniboxSubmission &)> submit;
    std::function<void()> cancel;
    /// PLT-SHELL-24M2FIX-C6: the user pressed the bookmark control. The owner
    /// decides whether this adds or removes the current page's bookmark and
    /// reports the result back through SetBookmarked().
    std::function<void()> toggle_bookmark;
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
  /// PLT-SHELL-24M2FIX-C6: the bookmark control, exposed so callers (and
  /// probes) address it by name instead of by child index.
  CefRefPtr<CefLabelButton> bookmark_button() const;
  bool bookmarked() const noexcept;

  /// PLT-SHELL-24M2FIX-C8: replaces the provider set used for search-query
  /// submissions, so the engine selector can switch it at runtime. Refuses
  /// while a submission is being dispatched (the set is read on that path).
  bool SetSearchProviders(
      browser_omnibox_provider::SearchProviderSet providers);

  /// PLT-SHELL-24M2FIX-C6: reflects the current page's bookmark state on the
  /// trailing control (filled or outline glyph plus its accessible name). The
  /// owner calls this after every navigation and after each toggle, so the
  /// control never shows a state the store does not agree with.
  bool SetBookmarked(bool bookmarked);

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
  /// PLT-SHELL-24M2FIX-B: renders the load-failure notice regardless of the
  /// model state, for loads the user did not submit from the field (link,
  /// redirect, reload). Required because OnNavigationFinished only reports a
  /// failure it was itself waiting on. Returns true when a notice is visible.
  bool ShowLoadFailureNotice();
  bool Shutdown();

  bool active() const noexcept;
  std::uint64_t edit_generation() const noexcept;
  std::size_t suggestion_count() const noexcept;
  browser_omnibox::SuggestionIndex selected_suggestion() const noexcept;
  browser_omnibox::OmniboxState state() const noexcept;
  std::string displayed_text() const;
  /// PLT-SHELL-24M2FIX-B: the submission notice currently shown, or empty.
  std::string notice_text() const;

  static std::optional<std::string> SafeDisplayText(std::string address);

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace crayon::browser::cef_shell::window

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_OMNIBOX_H_
