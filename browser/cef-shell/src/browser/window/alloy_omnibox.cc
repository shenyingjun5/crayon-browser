#include "browser/window/alloy_omnibox.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

#include "include/cef_color_ids.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

using browser_omnibox::IsValid;
using browser_omnibox::OmniboxInput;
using browser_omnibox::OmniboxParseResult;
using browser_omnibox::OmniboxState;
using browser_omnibox::OmniboxSuggestion;
using browser_omnibox::ParseOmniboxInput;

constexpr int kOmniboxHeight = 40;
constexpr int kSuggestionHeight = 36;
constexpr std::size_t kMaxSuggestionTitleBytes = 512;
constexpr int kEnterKey = 13;
constexpr int kEscapeKey = 27;
constexpr int kUpKey = 38;
constexpr int kDownKey = 40;

bool HasControlCharacter(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20 || byte == 0x7f;
  });
}

std::size_t AuthorityBegin(std::string_view value) {
  const std::size_t delimiter = value.find("://");
  return delimiter == std::string_view::npos ? std::string_view::npos
                                             : delimiter + 3;
}

bool HasCredentials(std::string_view value) {
  const std::size_t begin = AuthorityBegin(value);
  if (begin == std::string_view::npos)
    return false;
  const std::size_t end = value.find_first_of("/?#", begin);
  const std::size_t at = value.find('@', begin);
  return at != std::string_view::npos &&
         (end == std::string_view::npos || at < end);
}

bool HasExplicitScheme(std::string_view value) {
  const std::size_t colon = value.find(':');
  if (colon == std::string_view::npos || colon == 0)
    return false;
  return std::all_of(
      value.begin(), value.begin() + static_cast<std::ptrdiff_t>(colon),
      [](char character) {
        const unsigned char byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) || character == '+' || character == '-' ||
               character == '.';
      });
}

bool SafeSuggestion(const OmniboxSuggestion &suggestion) {
  return IsValid(suggestion.source) && !suggestion.title.empty() &&
         suggestion.title.size() <= kMaxSuggestionTitleBytes &&
         suggestion.url_or_query.size() <=
             browser_omnibox::kMaxOmniboxInputBytes &&
         !HasControlCharacter(suggestion.title) &&
         !HasControlCharacter(suggestion.url_or_query);
}

class PanelDelegate final : public CefPanelDelegate {
public:
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(320, kOmniboxHeight);
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(160, kOmniboxHeight);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(view->GetThemeColor(CEF_ColorPrimaryBackground));
  }

private:
  IMPLEMENT_REFCOUNTING(PanelDelegate);
};

} // namespace

struct AlloyOmnibox::State final : std::enable_shared_from_this<State> {
  class TextDelegate final : public CefTextfieldDelegate {
  public:
    explicit TextDelegate(std::weak_ptr<State> state)
        : state_(std::move(state)) {}

    bool OnKeyEvent(CefRefPtr<CefTextfield>,
                    const CefKeyEvent &event) override {
      if (event.type != KEYEVENT_RAWKEYDOWN && event.type != KEYEVENT_KEYDOWN) {
        return false;
      }
      if (auto state = state_.lock()) {
        return state->OnKey(event.windows_key_code);
      }
      return false;
    }

    void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override {
      if (auto state = state_.lock()) {
        if (state->setting_text)
          return;
        state->Edit(textfield->GetText().ToString(), false);
      }
    }

  private:
    std::weak_ptr<State> state_;
    IMPLEMENT_REFCOUNTING(TextDelegate);
  };

  class SuggestionDelegate final : public CefButtonDelegate {
  public:
    explicit SuggestionDelegate(std::weak_ptr<State> state)
        : state_(std::move(state)) {}
    void OnButtonPressed(CefRefPtr<CefButton> button) override {
      if (auto state = state_.lock()) {
        state->OnSuggestionPressed(button);
      }
    }

  private:
    std::weak_ptr<State> state_;
    IMPLEMENT_REFCOUNTING(SuggestionDelegate);
  };

  struct SuggestionBinding final {
    std::size_t index;
    CefRefPtr<CefLabelButton> button;
  };

  State(Strings strings_value, Callbacks callbacks_value,
        browser_privacy::PrivacyDefaults privacy_value,
        browser_omnibox_provider::SearchProviderSet provider_value)
      : strings(std::move(strings_value)),
        callbacks(std::move(callbacks_value)), privacy(privacy_value),
        providers(std::move(provider_value)) {}

  void Initialize() {
    panel = CefPanel::CreatePanel(new PanelDelegate);
    CefBoxLayoutSettings column;
    panel->SetToBoxLayout(column);
    textfield =
        CefTextfield::CreateTextfield(new TextDelegate(weak_from_this()));
    textfield->SetFocusable(true);
    textfield->SetPlaceholderText(strings.placeholder);
    textfield->SetAccessibleName(strings.accessible_name);
    panel->AddChildView(textfield);
    suggestions_panel = CefPanel::CreatePanel(new PanelDelegate);
    CefBoxLayoutSettings suggestions_layout;
    suggestions_panel->SetToBoxLayout(suggestions_layout);
    suggestions_panel->SetVisible(false);
    panel->AddChildView(suggestions_panel);
  }

  bool Focus() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !textfield)
      return false;
    model.OnFocus();
    textfield->RequestFocus();
    return true;
  }

  void SetFieldText(const std::string &text) {
    setting_text = true;
    if (text.empty()) {
      if (!textfield->GetText().ToString().empty()) {
        textfield->SelectAll(false);
        textfield->ExecuteCommand(CEF_TFC_DELETE);
        textfield->ClearEditHistory();
      }
    } else {
      textfield->SetText(text);
    }
    setting_text = false;
  }

  bool Edit(std::string text, bool update_field) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !textfield)
      return false;
    const auto input = OmniboxInput::TryCreate(text);
    if (!input || HasControlCharacter(text)) {
      SetFieldText(accepted_text);
      textfield->ClearSelection();
      return false;
    }
    if (model.state() == OmniboxState::kIdle ||
        model.state() == OmniboxState::kCommitted) {
      model.OnFocus();
    }
    if (model.state() != OmniboxState::kEditing &&
        model.state() != OmniboxState::kSuggesting) {
      return false;
    }
    accepted_text = std::move(text);
    model.OnEdit(accepted_text);
    if (update_field) {
      SetFieldText(accepted_text);
      textfield->ClearSelection();
    }
    suggestions_panel->SetVisible(false);
    generation = generation == std::numeric_limits<std::uint64_t>::max()
                     ? 1
                     : generation + 1;
    if (generation == 0)
      generation = 1;
    pending_generation = generation;
    if (callbacks.request_suggestions) {
      Dispatch(
          [&] { callbacks.request_suggestions(generation, accepted_text); });
    }
    return true;
  }

  bool ApplySuggestions(std::uint64_t response_generation,
                        std::vector<OmniboxSuggestion> suggestions) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || response_generation == 0 ||
        response_generation != pending_generation ||
        (model.state() != OmniboxState::kEditing &&
         model.state() != OmniboxState::kSuggesting)) {
      return false;
    }
    suggestions.erase(
        std::remove_if(suggestions.begin(), suggestions.end(),
                       [](const auto &item) { return !SafeSuggestion(item); }),
        suggestions.end());
    if (suggestions.size() > browser_omnibox::kMaxSuggestions) {
      suggestions.resize(browser_omnibox::kMaxSuggestions);
    }
    model.OnSuggestionsUpdated(std::move(suggestions));
    pending_generation = 0;
    RenderSuggestions();
    return true;
  }

  void RenderSuggestions() {
    suggestions_panel->RemoveAllChildViews();
    suggestion_bindings.clear();
    const auto &suggestions = model.suggestions();
    suggestion_bindings.reserve(suggestions.size());
    for (std::size_t index = 0; index < suggestions.size(); ++index) {
      auto button = CefLabelButton::CreateLabelButton(
          new SuggestionDelegate(weak_from_this()), suggestions[index].title);
      button->SetFocusable(true);
      button->SetMinimumSize(CefSize(160, kSuggestionHeight));
      button->SetTooltipText(suggestions[index].title);
      button->SetAccessibleName(suggestions[index].title);
      suggestions_panel->AddChildView(button);
      suggestion_bindings.push_back({index, button});
    }
    suggestions_panel->SetVisible(!suggestions.empty());
    RenderSelection();
    panel->Layout();
  }

  void RenderSelection() {
    const auto selected = model.selected_index();
    for (const auto &binding : suggestion_bindings) {
      binding.button->SetState(selected == binding.index
                                   ? CEF_BUTTON_STATE_HOVERED
                                   : CEF_BUTTON_STATE_NORMAL);
    }
  }

  bool SelectNext() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !model.SelectNextSuggestion())
      return false;
    RenderSelection();
    return true;
  }

  bool SelectPrevious() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !model.SelectPreviousSuggestion())
      return false;
    RenderSelection();
    return true;
  }

  void OnSuggestionPressed(CefRefPtr<CefButton> sender) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !sender || !sender->IsEnabled())
      return;
    const auto found = std::find_if(
        suggestion_bindings.begin(), suggestion_bindings.end(),
        [&](const auto &binding) { return binding.button->IsSame(sender); });
    if (found == suggestion_bindings.end() ||
        found->index >= model.suggestions().size()) {
      return;
    }
    const std::string value = model.suggestions()[found->index].url_or_query;
    model.OnEdit(value);
    accepted_text = value;
    SetFieldText(value);
    Submit();
  }

  bool Submit() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching)
      return false;
    std::string value = model.current_text();
    if (const auto selected = model.selected_index();
        selected && *selected < model.suggestions().size()) {
      value = model.suggestions()[*selected].url_or_query;
    }
    const auto input = OmniboxInput::TryCreate(value);
    if (!input || HasControlCharacter(value))
      return false;
    model.OnSubmit();
    if (model.state() != OmniboxState::kLoading)
      return false;
    suggestions_panel->SetVisible(false);

    OmniboxSubmission submission{OmniboxSubmissionKind::kEmpty, {}};
    if (value.empty()) {
      model.OnNavigationFailed();
    } else {
      switch (ParseOmniboxInput(*input)) {
      case OmniboxParseResult::kDangerous:
        submission.kind = OmniboxSubmissionKind::kBlocked;
        model.OnNavigationFailed();
        break;
      case OmniboxParseResult::kValidUrl:
        if (HasCredentials(value)) {
          submission.kind = OmniboxSubmissionKind::kBlocked;
          model.OnNavigationFailed();
        } else {
          submission.kind = OmniboxSubmissionKind::kNavigateUrl;
          submission.value =
              HasExplicitScheme(value)
                  ? value
                  : browser_omnibox_provider::ResolveSchemelessUrl(value,
                                                                   privacy);
        }
        break;
      case OmniboxParseResult::kSearchQuery:
        if (const auto url = providers.BuildSearchUrl(value)) {
          submission.kind = OmniboxSubmissionKind::kSearchUrl;
          submission.value = *url;
        } else {
          submission.kind = OmniboxSubmissionKind::kNoSearchProvider;
          model.OnNavigationFailed();
        }
        break;
      }
    }
    if (callbacks.submit) {
      Dispatch([&] { callbacks.submit(submission); });
    }
    return true;
  }

  bool Cancel() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching)
      return false;
    if (model.state() != OmniboxState::kEditing &&
        model.state() != OmniboxState::kSuggesting &&
        model.state() != OmniboxState::kLoading) {
      return false;
    }
    model.OnCancel();
    pending_generation = 0;
    accepted_text = committed_display;
    SetFieldText(committed_display);
    textfield->ClearSelection();
    suggestions_panel->SetVisible(false);
    if (callbacks.cancel)
      Dispatch([&] { callbacks.cancel(); });
    return true;
  }

  bool SetAddress(std::string address) {
    CEF_REQUIRE_UI_THREAD();
    if (!active)
      return false;
    auto safe = AlloyOmnibox::SafeDisplayText(std::move(address));
    if (!safe)
      return false;
    committed_display = std::move(*safe);
    if (model.state() == OmniboxState::kIdle ||
        model.state() == OmniboxState::kCommitted) {
      accepted_text = committed_display;
      SetFieldText(committed_display);
    }
    return true;
  }

  bool NavigationFinished(bool succeeded, std::string address) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || model.state() != OmniboxState::kLoading)
      return false;
    auto safe = AlloyOmnibox::SafeDisplayText(std::move(address));
    if (!safe)
      return false;
    if (succeeded) {
      model.OnNavigationComplete();
    } else {
      model.OnNavigationFailed();
    }
    pending_generation = 0;
    committed_display = std::move(*safe);
    accepted_text = committed_display;
    SetFieldText(committed_display);
    return true;
  }

  bool OnKey(int key) {
    if (!active)
      return false;
    switch (key) {
    case kEnterKey:
      return Submit();
    case kEscapeKey:
      return Cancel();
    case kUpKey:
      return SelectPrevious();
    case kDownKey:
      return SelectNext();
    default:
      return false;
    }
  }

  template <typename Callback> void Dispatch(Callback callback) {
    dispatching = true;
    callback();
    dispatching = false;
  }

  bool Shutdown() {
    CEF_REQUIRE_UI_THREAD();
    if (!active)
      return true;
    if (dispatching)
      return false;
    active = false;
    model.Shutdown();
    callbacks = {};
    suggestion_bindings.clear();
    textfield = nullptr;
    suggestions_panel = nullptr;
    if (panel) {
      panel->RemoveAllChildViews();
      panel = nullptr;
    }
    return true;
  }

  Strings strings;
  Callbacks callbacks;
  browser_privacy::PrivacyDefaults privacy;
  browser_omnibox_provider::SearchProviderSet providers;
  browser_omnibox::OmniboxStateMachine model;
  CefRefPtr<CefPanel> panel;
  CefRefPtr<CefTextfield> textfield;
  CefRefPtr<CefPanel> suggestions_panel;
  std::vector<SuggestionBinding> suggestion_bindings;
  std::string accepted_text;
  std::string committed_display;
  std::uint64_t generation = 0;
  std::uint64_t pending_generation = 0;
  bool active = true;
  bool dispatching = false;
  bool setting_text = false;
};

AlloyOmnibox::AlloyOmnibox(
    Strings strings, Callbacks callbacks,
    browser_privacy::PrivacyDefaults privacy,
    browser_omnibox_provider::SearchProviderSet search_providers)
    : state_(std::make_shared<State>(std::move(strings), std::move(callbacks),
                                     privacy, std::move(search_providers))) {
  CEF_REQUIRE_UI_THREAD();
  state_->Initialize();
}

AlloyOmnibox::~AlloyOmnibox() {
  if (state_ && state_->active)
    state_->Shutdown();
}

CefRefPtr<CefPanel> AlloyOmnibox::panel() const {
  return state_ ? state_->panel : nullptr;
}
CefRefPtr<CefTextfield> AlloyOmnibox::textfield() const {
  return state_ ? state_->textfield : nullptr;
}
bool AlloyOmnibox::Focus() { return state_ && state_->Focus(); }
bool AlloyOmnibox::Edit(std::string text) {
  return state_ && state_->Edit(std::move(text), true);
}
bool AlloyOmnibox::ApplySuggestions(
    std::uint64_t generation, std::vector<OmniboxSuggestion> suggestions) {
  return state_ && state_->ApplySuggestions(generation, std::move(suggestions));
}
bool AlloyOmnibox::SelectNextSuggestion() {
  return state_ && state_->SelectNext();
}
bool AlloyOmnibox::SelectPreviousSuggestion() {
  return state_ && state_->SelectPrevious();
}
bool AlloyOmnibox::Submit() { return state_ && state_->Submit(); }
bool AlloyOmnibox::Cancel() { return state_ && state_->Cancel(); }
bool AlloyOmnibox::SetAddress(std::string address) {
  return state_ && state_->SetAddress(std::move(address));
}
bool AlloyOmnibox::OnNavigationFinished(bool succeeded, std::string address) {
  return state_ && state_->NavigationFinished(succeeded, std::move(address));
}
bool AlloyOmnibox::Shutdown() { return !state_ || state_->Shutdown(); }
bool AlloyOmnibox::active() const noexcept { return state_ && state_->active; }
std::uint64_t AlloyOmnibox::edit_generation() const noexcept {
  return state_ ? state_->generation : 0;
}
std::size_t AlloyOmnibox::suggestion_count() const noexcept {
  return state_ ? state_->model.suggestions().size() : 0;
}
browser_omnibox::SuggestionIndex
AlloyOmnibox::selected_suggestion() const noexcept {
  return state_ ? state_->model.selected_index()
                : browser_omnibox::SuggestionIndex{};
}
OmniboxState AlloyOmnibox::state() const noexcept {
  return state_ ? state_->model.state() : OmniboxState::kIdle;
}
std::string AlloyOmnibox::displayed_text() const {
  return state_ && state_->textfield ? state_->textfield->GetText().ToString()
                                     : std::string{};
}

std::optional<std::string> AlloyOmnibox::SafeDisplayText(std::string address) {
  if (address.size() > browser_omnibox::kMaxOmniboxInputBytes ||
      HasControlCharacter(address)) {
    return std::nullopt;
  }
  if (const std::size_t begin = AuthorityBegin(address);
      begin != std::string::npos) {
    const std::size_t end = address.find_first_of("/?#", begin);
    const std::size_t at = address.find('@', begin);
    if (at != std::string::npos && (end == std::string::npos || at < end)) {
      address.erase(begin, at - begin + 1);
    }
  }
  std::string safe;
  safe.reserve(address.size());
  static constexpr char kHex[] = "0123456789ABCDEF";
  for (const unsigned char byte : address) {
    if (byte < 0x80) {
      safe.push_back(static_cast<char>(byte));
    } else {
      safe.push_back('%');
      safe.push_back(kHex[byte >> 4]);
      safe.push_back(kHex[byte & 0x0f]);
    }
  }
  if (safe.size() > browser_omnibox::kMaxOmniboxInputBytes)
    return std::nullopt;
  return safe;
}

} // namespace crayon::browser::cef_shell::window
