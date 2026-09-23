#include "browser/window/alloy_tab_strip.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include "browser/window/alloy_chrome_palette.h"
#include "browser/window/alloy_icon.h"
#include "include/base/cef_callback.h"
#include "include/cef_color_ids.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::window {
namespace {

constexpr int kStripHeight = kTabStripHeightDip;
// tokens.json -> metrics.tabMinWidthDip / metrics.tabMaxWidthDip describe the
// whole TAB ROW, and the row is [indicator slot][title][close]. The title
// button's own bounds are therefore the row minus its fixed siblings and the
// two gaps, so the row still lands inside the token range.
constexpr int kTabRowMinimumWidth = 96;
constexpr int kTabRowMaximumWidth = 240;
constexpr int kIndicatorSlotWidth = kTabIndicatorSlotWidthDip;
constexpr int kCloseMinimumWidth = 32;
constexpr int kChildSpacing = 2;
constexpr int kTitleMinimumWidth =
    kTabRowMinimumWidth - kIndicatorSlotWidth - kCloseMinimumWidth -
    2 * kChildSpacing;
constexpr int kTitleMaximumWidth =
    kTabRowMaximumWidth - kIndicatorSlotWidth - kCloseMinimumWidth -
    2 * kChildSpacing;
constexpr std::size_t kMaximumTitleBytes = 4096;

class SurfaceDelegate final : public CefPanelDelegate {
public:
  explicit SurfaceDelegate(int minimum_width, bool active = true,
                           int preferred_width = 0, int reserved_width = 0)
      : minimum_width_(minimum_width), active_(active),
        preferred_width_(preferred_width > 0 ? preferred_width : minimum_width),
        reserved_width_(reserved_width) {}

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    int width = preferred_width_;
    if (reserved_width_ > 0 && view->GetParentView()) {
      auto parent = view->GetParentView()->AsPanel();
      if (parent && parent->GetChildViewCount() > 1 &&
          parent->GetSize().width > 0) {
        const int count = static_cast<int>(parent->GetChildViewCount()) - 1;
        const int available =
            parent->GetSize().width - reserved_width_ - count * kChildSpacing;
        width = std::clamp(available / count, minimum_width_, preferred_width_);
      }
    }
    return CefSize(width, kStripHeight);
  }

  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(minimum_width_, kStripHeight);
  }

  CefSize GetMaximumSize(CefRefPtr<CefView>) override {
    return preferred_width_ > minimum_width_
               ? CefSize(preferred_width_, kStripHeight)
               : CefSize();
  }

  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(active_ ? chrome_palette::kActiveTabBackground
                                     : chrome_palette::kTabStripBackground);
  }

private:
  const int minimum_width_;
  const bool active_;
  const int preferred_width_;
  const int reserved_width_;
  IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};

// PLT-SHELL-24M2FIX-C4: the leading slot of a tab. It exists so the page
// indicator (loading spinner) has a place of its own: without it the title
// starts at the tab's left edge and the native indicator would be drawn on top
// of the first characters. The slot is reserved for every tab, loading or not,
// so the title never shifts when a load starts or ends.
//
// Sized fixed (not flexed) and repainted from OnThemeChanged for the same
// reason as SurfaceDelegate: CefView::SetBackgroundColor is reset by every
// theme change (cef_view.h).
class IndicatorSlotDelegate final : public CefPanelDelegate {
public:
  explicit IndicatorSlotDelegate(std::uint32_t background)
      : background_(background) {}

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(kIndicatorSlotWidth, kStripHeight);
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(kIndicatorSlotWidth, kStripHeight);
  }
  CefSize GetMaximumSize(CefRefPtr<CefView>) override {
    return CefSize(kIndicatorSlotWidth, kStripHeight);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(background_);
  }

private:
  const std::uint32_t background_;
  IMPLEMENT_REFCOUNTING(IndicatorSlotDelegate);
};

} // namespace

struct AlloyTabStrip::State final : std::enable_shared_from_this<State> {
  struct Binding final {
    TabId tab_id;
    CefRefPtr<CefPanel> row;
    CefRefPtr<CefPanel> indicator;
    CefRefPtr<CefLabelButton> activate;
    CefRefPtr<CefLabelButton> close;
    bool active = false;
    bool loading = false;
  };

  class ButtonDelegate final : public CefButtonDelegate {
  public:
    ButtonDelegate(std::weak_ptr<State> state, std::uint32_t background)
        : state_(std::move(state)), background_(background) {}

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
      ReleaseAlloyIconFocus(button);
      if (auto state = state_.lock()) {
        state->Dispatch(button);
      }
    }

    // PLT-SHELL-24M2FIX-C2: the tab color has to be applied HERE.
    // CefView::SetBackgroundColor is documented as "automatically reset when
    // CefViewDelegate::OnThemeChanged is called" (cef_view.h), so a color set
    // once at construction never survives to the first paint — which is why
    // every tab, active and inactive, rendered as the same default white and
    // the strip read as one solid bar. SurfaceDelegate above exists for the
    // same reason on the strip panel.
    void OnThemeChanged(CefRefPtr<CefView> view) override {
      view->SetBackgroundColor(background_);
    }

  private:
    std::weak_ptr<State> state_;
    const std::uint32_t background_;
    IMPLEMENT_REFCOUNTING(ButtonDelegate);
  };

  State(Strings strings_value, Callbacks callbacks_value, int leading_inset)
      : strings(std::move(strings_value)),
        callbacks(std::move(callbacks_value)),
        leading_inset(leading_inset) {}

  void Initialize() {
    // PLT-SHELL-24M2FIX-C: the strip panel is the band background, not a tab.
    // SurfaceDelegate's OnThemeChanged repaints with the ACTIVE tab color when
    // active_ is true, which silently overwrote the strip token set below —
    // measured via `alloy_cast_toolbar_mac`: tab_strip_view bg=0xffffffff
    // (white) instead of the token 0xffe8eef8.
    panel = CefPanel::CreatePanel(
        new SurfaceDelegate(kTabRowMinimumWidth, false));
    panel->SetBackgroundColor(chrome_palette::kTabStripBackground);
    CefBoxLayoutSettings layout;
    layout.horizontal = true;
    layout.between_child_spacing = kChildSpacing;
    layout.inside_border_insets.left = leading_inset;
    strip_layout = panel->SetToBoxLayout(layout);
  }

  CefRefPtr<CefLabelButton> Button(const std::string &label, int command_id,
                                   int minimum_width, std::uint32_t background,
                                   bool icon_only = false) {
    auto button = CefLabelButton::CreateLabelButton(
        new ButtonDelegate(weak_from_this(), background),
        icon_only ? CefString() : CefString(label));
    button->SetID(command_id);
    button->SetFocusable(true);
    button->SetMinimumSize(CefSize(minimum_width, kStripHeight));
    button->SetTooltipText(label);
    button->SetAccessibleName(label);
    return button;
  }

  std::string Title(TabId id, std::size_t index) const {
    const std::string value = callbacks.title ? callbacks.title(id) : std::string{};
    if (!value.empty() && value.size() <= kMaximumTitleBytes) return value;
    return strings.tab_fallback + " " + std::to_string(index + 1);
  }

  bool RefreshTitles() {
    CEF_REQUIRE_UI_THREAD();
    if (!active) return false;
    for (std::size_t index = 0; index < bindings.size(); ++index) {
      auto& binding = bindings[index];
      const auto title = Title(binding.tab_id, index);
      binding.activate->SetText(title);
      binding.activate->SetTooltipText(title);
      binding.activate->SetAccessibleName(title);
    }
    if (panel) panel->Layout();
    return true;
  }

  bool Sync(const TabModel &model, const std::vector<TabId> &order) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || !panel ||
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
      // The color must reach the paint through ButtonDelegate::OnThemeChanged:
      // a bare SetBackgroundColor here is reset by the next theme change.
      const std::uint32_t tab_background =
          is_active ? chrome_palette::kActiveTabBackground
                    : chrome_palette::kTabStripBackground;
      auto row = CefPanel::CreatePanel(new SurfaceDelegate(
          kTabRowMinimumWidth, is_active, kTabRowMaximumWidth,
          leading_inset + kCloseMinimumWidth));
      CefBoxLayoutSettings row_layout;
      row_layout.horizontal = true;
      row_layout.between_child_spacing = kChildSpacing;
      auto tab_layout = row->SetToBoxLayout(row_layout);
      // Decorative: the slot itself is not an accessibility element, so the
      // indicator never announces itself. Loading state belongs to the tab.
      auto indicator = CefPanel::CreatePanel(
          new IndicatorSlotDelegate(tab_background));
      const std::string title = Title(tab_id, index);
      auto activate_button =
          Button(title, kActivateCommandBase + static_cast<int>(index),
                 kTitleMinimumWidth, tab_background);
      activate_button->SetMaximumSize(CefSize(kTitleMaximumWidth, kStripHeight));
      activate_button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
      activate_button->SetEnabled(snapshot->lifecycle !=
                                  TabLifecycle::kClosing);
      if (is_active) {
        active_tab = tab_id;
      }
      // A text glyph remains visible and receives pointer events on inactive
      // macOS tabs; the icon-only CEF button did neither in the product window.
      auto close_button =
          Button("×", kCloseCommandBase + static_cast<int>(index),
                 kCloseMinimumWidth, tab_background);
      close_button->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
      close_button->SetTooltipText(strings.close_tab);
      close_button->SetAccessibleName(strings.close_tab);
      close_button->SetEnabled(snapshot->lifecycle != TabLifecycle::kClosing);
      row->AddChildView(indicator);
      row->AddChildView(activate_button);
      tab_layout->SetFlexForView(activate_button, 1);
      row->AddChildView(close_button);
      panel->AddChildView(row);
      // BoxLayout flex ignores maximum size and stretches even a single tab
      // across the window. Preferred size distributes the available width;
      // keep row flex at zero so spare space remains a draggable titlebar.
      bindings.push_back({tab_id, row, indicator, activate_button, close_button,
                          is_active, snapshot->loading});
    }

    new_button =
        Button(strings.new_tab, kNewTabCommandId, kCloseMinimumWidth,
               // PLT-SHELL-24M2FIX-C2: the new-tab affordance belongs to the
               // BAND, not to a tab. Left at the default it rendered 0xffffff
               // and merged into the row of white tabs.
               chrome_palette::kTabStripBackground, true);
    if (!ApplyAlloyIcon(new_button, AlloyIcon::kTabNew, strings.new_tab)) {
      return false;
    }
    new_button->SetEnabled(model.size() < kMaximumTabsPerWindow);
    panel->AddChildView(new_button);
    panel->Layout();
    return true;
  }

  // PLT-SHELL-24M2FIX-C4: the only channel that tells the native chrome
  // decoration where to cut corners and draw the loading indicator. The strip
  // owns the tab rows, so it owns this geometry; convert to window coordinates
  // here rather than letting the window host re-derive it from the panel's
  // children (which also contain the new-tab button).
  //
  // Fail-closed: if any row cannot resolve its window origin, no decoration is
  // published at all. A partial list would leave the decoration drawing
  // corners for a tab that has moved or gone.
  std::vector<TabDecoration> Decoration() const {
    CEF_REQUIRE_UI_THREAD();
    std::vector<TabDecoration> result;
    if (!active || !panel) {
      return result;
    }
    result.reserve(bindings.size());
    for (const auto &binding : bindings) {
      if (!binding.row || !binding.indicator) {
        return {};
      }
      CefPoint row_origin;
      CefPoint indicator_origin;
      if (!binding.row->ConvertPointToWindow(row_origin) ||
          !binding.indicator->ConvertPointToWindow(indicator_origin)) {
        return {};
      }
      const CefSize row_size = binding.row->GetSize();
      const CefSize indicator_size = binding.indicator->GetSize();
      TabDecoration item;
      item.bounds =
          CefRect(row_origin.x, row_origin.y, row_size.width, row_size.height);
      item.indicator =
          CefRect(indicator_origin.x, indicator_origin.y,
                  indicator_size.width, indicator_size.height);
      item.active = binding.active;
      item.loading = binding.loading;
      result.push_back(item);
    }
    return result;
  }

  void Dispatch(CefRefPtr<CefButton> sender) {
    CEF_REQUIRE_UI_THREAD();
    if (!active || dispatching || !sender || !sender->IsEnabled()) {
      return;
    }
    // Resolve the action before invoking external code: synchronous model
    // projection may replace every binding, including the sender itself.
    std::function<void()> action;
    if (new_button && new_button->IsSame(sender)) {
      action = callbacks.new_tab;
    } else {
      for (const auto &binding : bindings) {
        const TabId id = binding.tab_id;
        if (binding.activate->IsSame(sender)) {
          if (active_tab != id && callbacks.activate_tab) {
            action = [callback = callbacks.activate_tab, id] { callback(id); };
          }
          break;
        }
        if (binding.close->IsSame(sender)) {
          if (callbacks.close_tab) {
            action = [callback = callbacks.close_tab, id] { callback(id); };
          }
          break;
        }
      }
    }
    if (!action)
      return;
    // CEF still owns the pressed button's native event stack. Projecting the
    // new model there destroys the sender mid-dispatch. Run the resolved intent
    // on the next UI turn; only keep a weak owner across shutdown.
    CefPostTask(
        TID_UI,
        CefCreateClosureTask(base::BindOnce(
            [](std::weak_ptr<State> weak, std::function<void()> action) {
              const auto state = weak.lock();
              if (!state || !state->active || state->dispatching)
                return;
              state->dispatching = true;
              action();
              state->dispatching = false;
            },
            weak_from_this(), std::move(action))));
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
    strip_layout = nullptr;
    return true;
  }

  Strings strings;
  Callbacks callbacks;
  CefRefPtr<CefPanel> panel;
  CefRefPtr<CefBoxLayout> strip_layout;
  CefRefPtr<CefLabelButton> new_button;
  std::vector<Binding> bindings;
  std::optional<TabId> active_tab;
  bool active = true;
  bool dispatching = false;
  int leading_inset = 0;
};

AlloyTabStrip::AlloyTabStrip(Strings strings, Callbacks callbacks,
                             int leading_inset)
    : state_(std::make_shared<State>(std::move(strings),
                                     std::move(callbacks), leading_inset)) {
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

bool AlloyTabStrip::RefreshTitles() { return state_ && state_->RefreshTitles(); }

std::vector<TabDecoration> AlloyTabStrip::decoration() const {
  return state_ ? state_->Decoration() : std::vector<TabDecoration>{};
}

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
