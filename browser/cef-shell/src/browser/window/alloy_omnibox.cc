#include "browser/window/alloy_omnibox.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_color_ids.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include "browser/window/alloy_chrome_decoration.h"
#include "browser/window/alloy_chrome_palette.h"
#include "browser/window/alloy_icon.h"

namespace crayon::browser::cef_shell::window {
namespace {

using browser_omnibox::IsValid;
using browser_omnibox::OmniboxInput;
using browser_omnibox::OmniboxParseResult;
using browser_omnibox::OmniboxState;
using browser_omnibox::OmniboxSuggestion;
using browser_omnibox::ParseOmniboxInput;

// PLT-SHELL-24M2FIX-C4-a: the address field is a pill, so its height is
// exactly twice the pill radius (tokens.json -> metrics.pillRadiusDip). The
// reference Chrome build measures 34 DIP tall with a 17 DIP radius; the token
// value is the single source here, and 2x18 keeps the end caps true
// semicircles at the same fraction of the navigation bar (36 of 48 DIP).
constexpr int kOmniboxHeight = 2 * kOmniboxPillRadiusDip;
constexpr int kSuggestionHeight = 36;
// PLT-SHELL-24M2FIX-C6: the bookmark control lives inside the pill's trailing
// end; metrics.minimumHitTargetDip keeps it a real target, and the pill's
// rounded end is cut by the native decoration, not by this view.
constexpr int kBookmarkButtonWidth = 32;
// Command id for the bookmark control, in the same closed range as the other
// chrome commands (tab strip uses 0x7a.., this one is deliberately outside it).
constexpr int kBookmarkCommandId = 0x7b20;
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
  // |pill| selects the address field fill: the field is a pill on the toolbar
  // surface, while the suggestion strip is a separate surface above it.
  explicit PanelDelegate(bool pill) : pill_(pill) {}

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(320, kOmniboxHeight);
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(160, kOmniboxHeight);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(pill_
                                 ? chrome_palette::kOmniboxBackground
                                 : view->GetThemeColor(
                                       CEF_ColorPrimaryBackground));
  }

private:
  const bool pill_;
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

    // PLT-SHELL-24M2FIX-C10: Chrome selects the whole address when the field
    // gains focus, so the next keystroke replaces it. Done here rather than in
    // Focus() because a click lands on the field directly and never goes
    // through the accessor.
    void OnFocus(CefRefPtr<CefView>) override {
      if (auto state = state_.lock()) {
        state->OnFieldFocusChanged(true);
      }
    }

    void OnBlur(CefRefPtr<CefView>) override {
      if (auto state = state_.lock()) {
        state->OnFieldFocusChanged(false);
      }
    }

    // The field's surface depends on focus, so a theme change has to re-apply
    // the color that matches the CURRENT state rather than the default one.
    void OnThemeChanged(CefRefPtr<CefView> view) override {
      if (auto state = state_.lock()) {
        view->SetBackgroundColor(state->FieldSurfaceColor());
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

  // PLT-SHELL-24M2FIX-C6: the bookmark control. It reports the press to the
  // owner (which owns the store and answers with SetBookmarked) and repaints
  // its own background with the pill colour, because a label button would
  // otherwise show the window theme's background as a lighter rectangle
  // inside the pill.
  class BookmarkDelegate final : public CefButtonDelegate {
  public:
    explicit BookmarkDelegate(std::weak_ptr<State> state)
        : state_(std::move(state)) {}

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
      ReleaseAlloyIconFocus(button);
      if (auto state = state_.lock()) {
        state->OnBookmarkPressed();
      }
    }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
      view->SetBackgroundColor(chrome_palette::kOmniboxBackground);
    }

  private:
    std::weak_ptr<State> state_;
    IMPLEMENT_REFCOUNTING(BookmarkDelegate);
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
    panel = CefPanel::CreatePanel(new PanelDelegate(/*pill=*/true));
    CefBoxLayoutSettings column;
    panel->SetToBoxLayout(column);
    textfield =
        CefTextfield::CreateTextfield(new TextDelegate(weak_from_this()));
    textfield->SetFocusable(true);
    textfield->SetPlaceholderText(strings.placeholder);
    textfield->SetAccessibleName(strings.accessible_name);
    // PLT-SHELL-24M2FIX-C6: the pill is [field][bookmark control] on one row,
    // with the suggestion strip below it. The row paints the pill colour itself
    // because a bare panel falls back to the window theme's primary background,
    // which would show as a lighter rectangle inside the pill.
    field_row = CefPanel::CreatePanel(new PanelDelegate(/*pill=*/true));
    CefBoxLayoutSettings field_layout;
    field_layout.horizontal = true;
    auto field_box = field_row->SetToBoxLayout(field_layout);
    field_row->AddChildView(textfield);
    field_box->SetFlexForView(textfield, 1);
    bookmark_button = CefLabelButton::CreateLabelButton(
        new BookmarkDelegate(weak_from_this()), CefString());
    bookmark_button->SetID(kBookmarkCommandId);
    bookmark_button->SetFocusable(true);
    bookmark_button->SetMinimumSize(CefSize(kBookmarkButtonWidth, kOmniboxHeight));
    field_row->AddChildView(bookmark_button);
    if (!SetBookmarked(false)) {
      bookmark_button = nullptr;
    }
    panel->AddChildView(field_row);
    suggestions_panel = CefPanel::CreatePanel(new PanelDelegate(/*pill=*/false));
    CefBoxLayoutSettings suggestions_layout;
    suggestions_panel->SetToBoxLayout(suggestions_layout);
    suggestions_panel->SetVisible(false);
    panel->AddChildView(suggestions_panel);
  }

  // PLT-SHELL-24M2FIX-C6: one control, two states. Chrome's star fills when the
  // page is bookmarked and offers the opposite action in its tooltip, so both
  // the glyph and the accessible name follow the state rather than a caption.
  bool SetBookmarked(bool value) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !bookmark_button) {
      return false;
    }
    const std::string label = value ? strings.bookmark_remove
                                    : strings.bookmark_add;
    if (!ApplyAlloyIcon(bookmark_button,
                        value ? AlloyIcon::kBookmarkFilled
                              : AlloyIcon::kBookmarkOutline,
                        label)) {
      return false;
    }
    bookmark_button->SetEnabled(true);
    bookmarked = value;
    return true;
  }

  bool Focus() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !textfield)
      return false;
    model.OnFocus();
    textfield->RequestFocus();
    return true;
  }

  // PLT-SHELL-24M2FIX-C8: the provider set is read by Submit(), so replacing it
  // mid-dispatch is refused rather than racing with that read.
  bool SetSearchProviders(
      browser_omnibox_provider::SearchProviderSet value) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching) {
      return false;
    }
    providers = std::move(value);
    return true;
  }

  // PLT-SHELL-24M2FIX-C10: focus is a chrome state, not just a textfield state.
  // Chrome selects the whole address on gain and lifts the field's surface, and
  // the native layer draws the ring on the pill because CEF would paint a
  // rectangle. All three follow from this one transition.
  void OnFieldFocusChanged(bool value) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || focused == value) {
      return;
    }
    focused = value;
    if (textfield) {
      textfield->SetBackgroundColor(FieldSurfaceColor());
      if (value) {
        // Select-all after the click has been dispatched: doing it inline loses
        // to CEF's own caret placement for the same event.
        CefPostTask(TID_UI,
                    CefCreateClosureTask(base::BindOnce(
                        [](std::weak_ptr<State> weak) {
                          const auto state = weak.lock();
                          if (!state || !state->active || !state->focused ||
                              !state->textfield) {
                            return;
                          }
                          state->textfield->SelectAll(false);
                        },
                        weak_from_this())));
      } else {
        textfield->ClearSelection();
      }
    }
    if (callbacks.focus_changed) {
      Dispatch([&] { callbacks.focus_changed(); });
    }
  }

  std::uint32_t FieldSurfaceColor() const {
    return focused ? chrome_palette::kOmniboxFocusedBackground
                   : chrome_palette::kOmniboxBackground;
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
    // A fresh edit supersedes any submission notice from the previous submit.
    notice.clear();
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
    notice.clear();
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

  // PLT-SHELL-24M2FIX-B: a submission that never reaches the network must not
  // look like a dead UI. The notice reuses the suggestion strip so no new
  // chrome surface is needed, and it registers no suggestion binding, so the
  // shared press handler can never act on it.
  void RenderNotice(const std::string &text) {
    CEF_REQUIRE_UI_THREAD();
    notice = text;
    if (!suggestions_panel || !panel) {
      return;
    }
    suggestions_panel->RemoveAllChildViews();
    suggestion_bindings.clear();
    if (text.empty()) {
      suggestions_panel->SetVisible(false);
      panel->Layout();
      return;
    }
    auto notice_button = CefLabelButton::CreateLabelButton(
        new SuggestionDelegate(weak_from_this()), text);
    // Informational, not actionable: disabled so it cannot read as a choice,
    // and unfocusable so Tab cannot land on it.
    notice_button->SetEnabled(false);
    notice_button->SetFocusable(false);
    notice_button->SetMinimumSize(CefSize(160, kSuggestionHeight));
    notice_button->SetAccessibleName(text);
    suggestions_panel->AddChildView(notice_button);
    suggestions_panel->SetVisible(true);
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

  // PLT-SHELL-24M2FIX-C6: the control does not decide anything itself. The
  // owner holds the store, so the intent leaves through the callback and the
  // state comes back through SetBookmarked(); a refused toggle therefore leaves
  // the glyph untouched instead of lying about the store.
  void OnBookmarkPressed() {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !callbacks.toggle_bookmark) {
      return;
    }
    Dispatch([&] { callbacks.toggle_bookmark(); });
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
        RenderNotice(strings.blocked_notice);
        break;
      case OmniboxParseResult::kValidUrl:
        if (HasCredentials(value)) {
          submission.kind = OmniboxSubmissionKind::kBlocked;
          model.OnNavigationFailed();
          RenderNotice(strings.blocked_notice);
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
          RenderNotice(strings.no_search_provider_notice);
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
    if (!succeeded) {
      // PLT-SHELL-24M2FIX-B: the shell owns no error page, so a failed load is
      // otherwise indistinguishable from an idle blank page. Rendered after the
      // field text so nothing later clears it. A successful navigation needs no
      // teardown here: Edit()/Submit() already superseded any earlier notice.
      RenderNotice(strings.load_failed_notice);
    }
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
    bookmark_button = nullptr;
    field_row = nullptr;
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
  CefRefPtr<CefPanel> field_row;
  CefRefPtr<CefTextfield> textfield;
  CefRefPtr<CefLabelButton> bookmark_button;
  CefRefPtr<CefPanel> suggestions_panel;
  std::vector<SuggestionBinding> suggestion_bindings;
  std::string accepted_text;
  bool bookmarked = false;
  bool focused = false;
  // PLT-SHELL-24M2FIX-B: text of the submission notice currently on screen.
  std::string notice;
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
CefRefPtr<CefLabelButton> AlloyOmnibox::bookmark_button() const {
  return state_ ? state_->bookmark_button : nullptr;
}
bool AlloyOmnibox::bookmarked() const noexcept {
  return state_ && state_->bookmarked;
}
bool AlloyOmnibox::focused() const noexcept {
  return state_ && state_->focused;
}
bool AlloyOmnibox::SetBookmarked(bool bookmarked) {
  return state_ && state_->SetBookmarked(bookmarked);
}
bool AlloyOmnibox::Focus() { return state_ && state_->Focus(); }
bool AlloyOmnibox::SetSearchProviders(
    browser_omnibox_provider::SearchProviderSet providers) {
  return state_ && state_->SetSearchProviders(std::move(providers));
}
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
bool AlloyOmnibox::ShowLoadFailureNotice() {
  if (!state_) {
    return false;
  }
  state_->RenderNotice(state_->strings.load_failed_notice);
  return !state_->strings.load_failed_notice.empty();
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

std::string AlloyOmnibox::notice_text() const {
  return state_ ? state_->notice : std::string{};
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
