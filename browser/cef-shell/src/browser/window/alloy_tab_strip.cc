#include "browser/window/alloy_tab_strip.h"

#include <unordered_set>
#include <utility>
#include <vector>

#include "browser/window/alloy_icon.h"
#include "include/cef_color_ids.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel_delegate.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr int kStripHeight = 40;
constexpr int kTabMinimumWidth = 62;
constexpr int kTabMaximumWidth = 208;
constexpr int kCloseMinimumWidth = 32;
constexpr int kChildSpacing = 2;

class SurfaceDelegate final : public CefPanelDelegate {
public:
  explicit SurfaceDelegate(int minimum_width, bool active = true)
      : minimum_width_(minimum_width), active_(active) {}

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(minimum_width_, kStripHeight);
  }

  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(minimum_width_, kStripHeight);
  }

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(view->GetThemeColor(
        active_ ? CEF_ColorTabBackgroundActiveFrameActive
                : CEF_ColorTabBackgroundInactiveFrameActive));
  }

private:
  const int minimum_width_;
  const bool active_;
  IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};

} // namespace

struct AlloyTabStrip::State final : std::enable_shared_from_this<State> {
  struct Binding final {
    TabId tab_id;
    CefRefPtr<CefLabelButton> activate;
    CefRefPtr<CefLabelButton> close;
  };

  class ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(std::weak_ptr<State> state)
        : state_(std::move(state)) {}

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
      ReleaseAlloyIconFocus(button);
      if (auto state = state_.lock()) {
        state->Dispatch(button);
      }
    }

  private:
    std::weak_ptr<State> state_;
    IMPLEMENT_REFCOUNTING(ButtonDelegate);
  };

  State(Strings strings_value, Callbacks callbacks_value)
      : strings(std::move(strings_value)),
        callbacks(std::move(callbacks_value)) {}

  void Initialize() {
    panel = CefPanel::CreatePanel(new SurfaceDelegate(kTabMinimumWidth));
    CefBoxLayoutSettings layout;
    layout.horizontal = true;
    layout.between_child_spacing = kChildSpacing;
    panel->SetToBoxLayout(layout);
  }

  CefRefPtr<CefLabelButton> Button(const std::string &label, int command_id,
                                   int minimum_width, bool icon_only = false) {
    auto button = CefLabelButton::CreateLabelButton(
        new ButtonDelegate(weak_from_this()),
        icon_only ? CefString() : CefString(label));
    button->SetID(command_id);
    button->SetFocusable(true);
    button->SetMinimumSize(CefSize(minimum_width, kStripHeight));
    button->SetTooltipText(label);
    button->SetAccessibleName(label);
    return button;
  }

  bool Sync(const TabModel &model, const std::vector<TabId> &order) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !panel ||
        model.size() > kMaximumTabsPerWindow || order.size() != model.size()) {
      return false;
    }

    std::unordered_set<TabId> unique;
    unique.reserve(order.size());
    for (const TabId tab_id : order) {
      if (!model.Find(tab_id) || !unique.insert(tab_id).second)
        return false;
    }

    panel->RemoveAllChildViews();
    bindings.clear();
    active_tab.reset();
    bindings.reserve(order.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
      const TabId tab_id = order[index];
      const TabSnapshot *snapshot = model.Find(tab_id);
      if (!snapshot) {
        panel->RemoveAllChildViews();
        bindings.clear();
        return false;
      }

      const bool is_active = model.active_tab() == tab_id;
      auto row = CefPanel::CreatePanel(new SurfaceDelegate(
          kTabMinimumWidth + kCloseMinimumWidth + kChildSpacing, is_active));
      CefBoxLayoutSettings row_layout;
      row_layout.horizontal = true;
      row_layout.between_child_spacing = kChildSpacing;
      row->SetToBoxLayout(row_layout);
      const std::string title =
          strings.tab_fallback + " " + std::to_string(index + 1);
      auto activate_button =
          Button(title, kActivateCommandBase + static_cast<int>(index),
                 kTabMinimumWidth);
      activate_button->SetMaximumSize(CefSize(kTabMaximumWidth, kStripHeight));
      activate_button->SetEnabled(snapshot->lifecycle !=
                                  TabLifecycle::kClosing);
      if (is_active) {
        active_tab = tab_id;
      }
      auto close_button =
          Button(strings.close_tab, kCloseCommandBase + static_cast<int>(index),
                 kCloseMinimumWidth, true);
      if (!ApplyAlloyIcon(close_button, AlloyIcon::kTabClose,
                          strings.close_tab)) {
        return false;
      }
      close_button->SetEnabled(snapshot->lifecycle != TabLifecycle::kClosing);
      row->AddChildView(activate_button);
      row->AddChildView(close_button);
      panel->AddChildView(row);
      bindings.push_back({tab_id, activate_button, close_button});
    }

    new_button =
        Button(strings.new_tab, kNewTabCommandId, kCloseMinimumWidth, true);
    if (!ApplyAlloyIcon(new_button, AlloyIcon::kTabNew, strings.new_tab)) {
      return false;
    }
    new_button->SetEnabled(model.size() < kMaximumTabsPerWindow);
    panel->AddChildView(new_button);
    panel->Layout();
    return true;
  }

  void Dispatch(CefRefPtr<CefButton> sender) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !sender || !sender->IsEnabled()) {
      return;
    }
    dispatching = true;
    if (new_button && new_button->IsSame(sender)) {
      if (callbacks.new_tab) {
        callbacks.new_tab();
      }
      dispatching = false;
      return;
    }
    for (const auto &binding : bindings) {
      if (binding.activate->IsSame(sender)) {
        if (active_tab == binding.tab_id) {
          dispatching = false;
          return;
        }
        if (callbacks.activate_tab) {
          callbacks.activate_tab(binding.tab_id);
        }
        dispatching = false;
        return;
      }
      if (binding.close->IsSame(sender)) {
        if (callbacks.close_tab) {
          callbacks.close_tab(binding.tab_id);
        }
        dispatching = false;
        return;
      }
    }
    dispatching = false;
  }

  bool Shutdown() {
    CEF_REQUIRE_UI_THREAD();
    if (!active) {
      return true;
    }
    if (dispatching) {
      return false;
    }
    active = false;
    callbacks = {};
    active_tab.reset();
    bindings.clear();
    new_button = nullptr;
    if (panel) {
      panel->RemoveAllChildViews();
      panel = nullptr;
    }
    return true;
  }

  Strings strings;
  Callbacks callbacks;
  CefRefPtr<CefPanel> panel;
  CefRefPtr<CefLabelButton> new_button;
  std::vector<Binding> bindings;
  std::optional<TabId> active_tab;
  bool active = true;
  bool dispatching = false;
};

AlloyTabStrip::AlloyTabStrip(Strings strings, Callbacks callbacks)
    : state_(
          std::make_shared<State>(std::move(strings), std::move(callbacks))) {
  CEF_REQUIRE_UI_THREAD();
  state_->Initialize();
}

AlloyTabStrip::~AlloyTabStrip() {
  if (state_ && state_->active) {
    state_->Shutdown();
  }
}

CefRefPtr<CefPanel> AlloyTabStrip::panel() const {
  return state_ ? state_->panel : nullptr;
}

bool AlloyTabStrip::Sync(const TabModel &model) {
  return Sync(model, model.ordered_tabs());
}

bool AlloyTabStrip::Sync(const TabModel &model,
                         const std::vector<TabId> &ordered_tabs) {
  return state_ && state_->Sync(model, ordered_tabs);
}

bool AlloyTabStrip::Shutdown() { return !state_ || state_->Shutdown(); }

bool AlloyTabStrip::active() const noexcept { return state_ && state_->active; }

std::size_t AlloyTabStrip::rendered_tab_count() const noexcept {
  return state_ ? state_->bindings.size() : 0;
}

std::optional<TabId>
AlloyTabStrip::rendered_tab_at(std::size_t index) const noexcept {
  if (!state_ || index >= state_->bindings.size()) {
    return std::nullopt;
  }
  return state_->bindings[index].tab_id;
}

std::optional<TabId> AlloyTabStrip::active_rendered_tab() const noexcept {
  return state_ ? state_->active_tab : std::nullopt;
}

bool AlloyTabStrip::new_tab_enabled() const noexcept {
  return state_ && state_->new_button && state_->new_button->IsEnabled();
}

} // namespace crayon::browser::cef_shell::window
