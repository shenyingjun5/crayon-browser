#include "browser/media_host/cast_entry_surface.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "browser/window/chrome_location_bar.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "include/base/cef_callback.h"
#include "include/cef_color_ids.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_scroll_view.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell {
namespace {
using namespace browser_cast_view;
// Existing desktop design-token dimensions; native theme owns font and colors.
constexpr int kToolbarHeight = 48, kHitHeight = 36, kGap = 8;
constexpr int kEntryWidth = 96, kPanelWidth = 464, kPanelMinWidth = 320;
constexpr int kPanelHeight = 560, kPanelMinHeight = 280, kScrollHeight = 256;
constexpr int kScrollBarAllowance = 20;
constexpr int kStatusId = 0xca03, kCodeId = 0xca04;
constexpr int kEscapeKey = 27, kTabKey = 9;
constexpr int kEscapeCommand = 0xcac0, kTabCommand = 0xcac1,
              kBackTabCommand = 0xcac2;

class ButtonDelegate final : public CefButtonDelegate {
public:
  explicit ButtonDelegate(std::function<void(CefRefPtr<CefButton>)> callback)
      : callback_(std::move(callback)) {}
  void OnButtonPressed(CefRefPtr<CefButton> button) override {
    callback_(button);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(view->GetThemeColor(CEF_ColorPrimaryBackground));
  }

private:
  std::function<void(CefRefPtr<CefButton>)> callback_;
  IMPLEMENT_REFCOUNTING(ButtonDelegate);
};
class SurfaceDelegate final : public CefPanelDelegate {
public:
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    view->SetBackgroundColor(view->GetThemeColor(CEF_ColorPrimaryBackground));
  }

private:
  IMPLEMENT_REFCOUNTING(SurfaceDelegate);
};
class CodeDelegate final : public CefTextfieldDelegate {
public:
  explicit CodeDelegate(std::function<void()> changed)
      : changed_(std::move(changed)) {}
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(kPanelMinWidth - 2 * kGap, kHitHeight);
  }
  void OnAfterUserAction(CefRefPtr<CefTextfield> field) override {
    if (!changed_)
      return;
    if (field->GetText().ToString().size() > kCastSelectionCodeBytes) {
      field->SelectAll(false);
      field->ExecuteCommand(CEF_TFC_DELETE);
      field->ClearEditHistory();
    }
    if (changed_)
      changed_();
  }

private:
  std::function<void()> changed_;
  IMPLEMENT_REFCOUNTING(CodeDelegate);
};
class ScrollDelegate final : public CefViewDelegate {
public:
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(kPanelMinWidth - 2 * kGap, kScrollHeight);
  }

private:
  IMPLEMENT_REFCOUNTING(ScrollDelegate);
};
CefRefPtr<CefPanel> Column() {
  auto panel = CefPanel::CreatePanel(new SurfaceDelegate);
  CefBoxLayoutSettings layout;
  layout.horizontal = false;
  layout.between_child_spacing = kGap;
  panel->SetToBoxLayout(layout);
  return panel;
}
CefRefPtr<CefTextfield> Text(const std::string &text, int id = 0) {
  auto view = CefTextfield::CreateTextfield(new CodeDelegate({}));
  view->SetID(id);
  view->SetReadOnly(true);
  view->SetFocusable(false);
  view->SetText(text);
  view->SetAccessibleName(text);
  return view;
}
void Destroy(CefRefPtr<CefOverlayController> &overlay) {
  if (overlay && overlay->IsValid())
    overlay->Destroy();
  overlay = nullptr;
}
} // namespace

struct CastEntrySurface::State final : std::enable_shared_from_this<State> {
  struct Action {
    CefRefPtr<CefLabelButton> button;
    CastSelectionIntent intent;
    std::optional<CastVideoAnchor> anchor;
  };
  State(localization::LocaleSnapshot locale, Clock time, IntentSink callback)
      : catalog(locale.locale), clock(std::move(time)),
        sink(std::move(callback)) {}
  localization::LocaleCatalog catalog;
  Clock clock;
  IntentSink sink;
  CastSelectionPresentation presentation;
  window::ChromeLocationBar location;
  CefRefPtr<CefWindow> window;
  CefRefPtr<CefBrowserView> browser_view;
  CefRefPtr<CefLabelButton> entry;
  CefRefPtr<CefPanel> panel, list_content;
  CefRefPtr<CefTextfield> status, code;
  CefRefPtr<CefOverlayController> picker;
  std::vector<CefRefPtr<CefOverlayController>> overlays;
  std::vector<Action> actions;
  CefRect geometry_viewport;
  bool detached = false;
  bool render_pending = false, restore_entry_focus = false;
  bool dispatch_pending = false;
  std::vector<CastVideoAnchor> pending_anchors;

  std::string String(const char *key) const {
    const auto value = catalog.Find(key);
    return value ? std::string(*value) : std::string();
  }
  std::string MediaName(const CastMediaChoice &m) const {
    return m.title.empty() ? String("cast.selection.video_fallback") : m.title;
  }
  std::string DeviceName(const CastDeviceChoice &d) const {
    return d.name.empty() ? String("cast.selection.device_fallback") : d.name;
  }
  CefRefPtr<CefLabelButton> Button(const std::string &text,
                                   CastSelectionIntent intent, int id = 0,
                                   std::optional<CastVideoAnchor> anchor = {}) {
    std::weak_ptr<State> weak = shared_from_this();
    auto button = CefLabelButton::CreateLabelButton(
        new ButtonDelegate([weak](CefRefPtr<CefButton> sender) {
          if (auto self = weak.lock())
            self->Dispatch(sender);
        }),
        text);
    button->SetID(id);
    button->SetFocusable(true);
    button->SetMinimumSize(CefSize(kHitHeight, kHitHeight));
    button->SetTooltipText(text);
    button->SetAccessibleName(text);
    actions.push_back({button, std::move(intent), std::move(anchor)});
    return button;
  }
  void Dispatch(CefRefPtr<CefButton> sender) {
    CEF_REQUIRE_UI_THREAD();
    if (detached || !window || !sender->IsDrawn() || !sink)
      return;
    const auto found =
        std::find_if(actions.begin(), actions.end(),
                     [&](const auto &a) { return a.button->IsSame(sender); });
    if (found == actions.end())
      return; // Removed controls never acquire a new meaning.
    auto intent = found->intent;
    if (intent.kind == CastIntentKind::kLookupCode && code)
      intent.cast_code = code->GetText().ToString();
    if (found->anchor) {
      if (!(browser_view->GetBoundsInScreen() == geometry_viewport)) {
        QueueRender();
        return;
      }
      const auto size = browser_view->GetSize();
      if (!presentation.PlaceOverlay(*found->anchor, size.width, size.height,
                                     clock())) {
        QueueRender();
        return;
      }
    }
    Send(std::move(intent));
  }
  void Send(CastSelectionIntent intent) {
    if (dispatch_pending || detached)
      return;
    auto accepted = presentation.TakeIntent(std::move(intent), clock());
    if (!accepted || !sink)
      return;
    if (accepted->kind == CastIntentKind::kCancel) {
      restore_entry_focus = true;
      QueueRender();
    }
    dispatch_pending = true;
    Update();
    std::weak_ptr<State> weak = shared_from_this();
    // One pending dispatch, outside native input processing. A callback may
    // destroy the facade; stale navigation and already-detached owners are
    // dropped.
    CefPostTask(TID_UI,
                base::BindOnce(
                    [](std::weak_ptr<State> weak, CastSelectionIntent intent) {
                      auto self = weak.lock();
                      if (!self || self->detached)
                        return;
                      self->dispatch_pending = false;
                      const auto &current = self->presentation.snapshot();
                      self->Update();
                      if (!current || !(current->context == intent.context) ||
                          !self->sink)
                        return;
                      auto callback = self->sink;
                      callback(std::move(intent));
                    },
                    weak, std::move(*accepted)));
  }
  void QueueRender() {
    if (render_pending || detached)
      return;
    render_pending = true;
    std::weak_ptr<State> weak = shared_from_this();
    // Never destroy a native focused widget inside its own input dispatch.
    CefPostTask(TID_UI,
                base::BindOnce(
                    [](std::weak_ptr<State> weak) {
                      if (auto self = weak.lock()) {
                        self->render_pending = false;
                        if (self->detached)
                          return;
                        self->Render();
                        if (!self->pending_anchors.empty()) {
                          auto anchors = std::move(self->pending_anchors);
                          self->pending_anchors.clear();
                          self->Anchors(std::move(anchors));
                        }
                        if (self->restore_entry_focus && !self->picker &&
                            self->entry && self->entry->IsEnabled()) {
                          self->entry->RequestFocus();
                          self->restore_entry_focus = false;
                        }
                      }
                    },
                    weak));
  }
  void HideOverlays() {
    for (auto &overlay : overlays)
      Destroy(overlay);
    overlays.clear();
    actions.erase(
        std::remove_if(actions.begin(), actions.end(),
                       [](const auto &a) { return a.anchor.has_value(); }),
        actions.end());
  }
  void ClosePicker(bool restore_focus) {
    if (window && window->IsValid() && picker) {
      window->RemoveAccelerator(kEscapeCommand);
      window->RemoveAccelerator(kTabCommand);
      window->RemoveAccelerator(kBackTabCommand);
    }
    Destroy(picker);
    panel = nullptr;
    list_content = nullptr;
    status = nullptr;
    code = nullptr;
    actions.erase(std::remove_if(actions.begin(), actions.end(),
                                 [&](const auto &a) {
                                   return !a.anchor && !a.button->IsSame(entry);
                                 }),
                  actions.end());
    if (restore_focus && entry && entry->IsValid() && entry->IsEnabled())
      entry->RequestFocus();
  }
  void PlacePicker() {
    if (!picker || !picker->IsValid())
      return;
    const auto size = window->GetSize();
    CefPoint anchor(0, 0);
    const auto entry_size = entry->GetSize();
    anchor = CefPoint(entry_size.width, entry_size.height);
    if (!entry->ConvertPointToWindow(anchor)) {
      picker->SetVisible(false);
      return;
    }
    const int width = std::min(kPanelWidth, size.width - 2 * kGap);
    const int height = std::min(kPanelHeight, size.height - anchor.y - kGap);
    if (width < kPanelMinWidth || height < kPanelMinHeight) {
      picker->SetVisible(false);
      return;
    }
    picker->SetBounds(
        CefRect(std::clamp(anchor.x - width, kGap, size.width - width - kGap),
                anchor.y, width, height));
    if (list_content) {
      list_content->SetSize(CefSize(width - 2 * kGap - kScrollBarAllowance,
                                    list_content->GetSize().height));
      list_content->Layout();
    }
    picker->SetVisible(true);
  }
  void Paging(CefRefPtr<CefPanel> target, bool media) {
    const auto &s = *presentation.snapshot();
    const auto offset = media ? s.media_offset : s.device_offset;
    const auto total = media ? s.media_total : s.device_total;
    if (total <= kCastSelectionPageSize)
      return;
    auto row = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings layout;
    layout.horizontal = true;
    layout.between_child_spacing = kGap;
    row->SetToBoxLayout(layout);
    if (offset >= kCastSelectionPageSize) {
      auto i = presentation.Intent(media ? CastIntentKind::kMediaPage
                                         : CastIntentKind::kDevicePage);
      i.page_offset =
          static_cast<std::uint16_t>(offset - kCastSelectionPageSize);
      row->AddChildView(Button(String("cast.selection.previous"), i));
    }
    if (offset + kCastSelectionPageSize < total) {
      auto i = presentation.Intent(media ? CastIntentKind::kMediaPage
                                         : CastIntentKind::kDevicePage);
      i.page_offset =
          static_cast<std::uint16_t>(offset + kCastSelectionPageSize);
      row->AddChildView(Button(String("cast.selection.next"), i));
    }
    target->AddChildView(row);
  }
  void Choices(CefRefPtr<CefPanel> list) {
    const auto &s = *presentation.snapshot();
    list->AddChildView(Text(String("cast.selection.videos")));
    if (s.media.empty())
      list->AddChildView(Text(String("cast.selection.empty_video")));
    for (std::size_t n = 0; n < s.media.size(); ++n) {
      const auto &m = s.media[n];
      auto intent = presentation.Intent(CastIntentKind::kSelectMedia);
      intent.media = m.ref;
      const bool selected = s.selected_media && s.selected_media->ref == m.ref;
      list->AddChildView(Button(
          std::to_string(s.media_offset + n + 1) + " · " +
              (selected ? String("cast.selection.selected") + " · " : "") +
              MediaName(m),
          intent, kMediaFirstId + static_cast<int>(n)));
    }
    Paging(list, true);
    list->AddChildView(Text(String("cast.selection.devices")));
    if (s.devices.empty())
      list->AddChildView(Text(String("cast.picker.empty")));
    for (std::size_t n = 0; n < s.devices.size(); ++n) {
      const auto &d = s.devices[n];
      auto intent = presentation.Intent(CastIntentKind::kSelectDevice);
      intent.device_id = d.id;
      const bool selected = s.selected_device && s.selected_device->id == d.id;
      list->AddChildView(
          Button((selected ? String("cast.selection.selected") + " · " : "") +
                     DeviceName(d),
                 intent, kDeviceFirstId + static_cast<int>(n)));
    }
    Paging(list, false);
  }
  void Footer(CefRefPtr<CefPanel> list) {
    const auto &s = *presentation.snapshot();
    list->AddChildView(
        Button(String("cast.picker.refresh"),
               presentation.Intent(CastIntentKind::kRefreshDevices)));
    std::weak_ptr<State> weak = shared_from_this();
    code = CefTextfield::CreateTextfield(new CodeDelegate([weak] {
      if (auto self = weak.lock())
        self->Update();
    }));
    code->SetID(kCodeId);
    code->SetPlaceholderText(String("cast.code.label"));
    code->SetAccessibleName(String("cast.code.label"));
    list->AddChildView(code);
    list->AddChildView(
        Button(String("cast.code.connect"),
               presentation.Intent(CastIntentKind::kLookupCode)));
    list->AddChildView(
        Button(String("cast.selection.connect"),
               presentation.Intent(CastIntentKind::kConnectDevice)));
    if (s.selected_media)
      list->AddChildView(Text(String("cast.selection.selected") + " · " +
                              MediaName(*s.selected_media)));
    if (s.selected_device)
      list->AddChildView(Text(String("cast.selection.selected") + " · " +
                              DeviceName(*s.selected_device)));
    if (s.phase == CastDraftPhase::kPrepared) {
      if (s.route == CastSelectionRoute::kDirect)
        list->AddChildView(Text(String("cast.mode.direct")));
      if (s.route == CastSelectionRoute::kRelay)
        list->AddChildView(Text(String("cast.mode.relay")));
    }
    if (s.replacement_confirmation_required)
      list->AddChildView(
          Button(String("cast.selection.confirm_replace"),
                 presentation.Intent(CastIntentKind::kConfirmReplacement)));
    list->AddChildView(Button(String("cast.selection.prepare"),
                              presentation.Intent(CastIntentKind::kPrepare),
                              kPrepareId));
    if (s.session_generation) {
      list->AddChildView(Button(
          String(s.playback_paused ? "cast.control.resume"
                                   : "cast.control.pause"),
          presentation.Intent(s.playback_paused ? CastIntentKind::kResume
                                                : CastIntentKind::kPause)));
      list->AddChildView(Button(String("cast.stop"),
                                presentation.Intent(CastIntentKind::kStop)));
    }
  }
  void Render() {
    if (!entry)
      return;
    // Preserve semantic focus across owner acknowledgements, not row indices.
    std::optional<CastSelectionIntent> focus;
    bool focus_code = false;
    if (window) {
      auto current = window->GetFocusedView();
      focus_code = current && code && current->IsSame(code);
      for (const auto &a : actions)
        if (current && current->IsSame(a.button))
          focus = a.intent;
    }
    const auto saved_code = code ? code->GetText().ToString() : std::string();
    ClosePicker(false);
    HideOverlays();
    actions[0].intent = presentation.Intent(CastIntentKind::kOpen);
    const auto &s = presentation.snapshot();
    entry->SetText(String("cast.feature.idle") +
                   (s && s->eligible_count
                        ? " · " + std::to_string(s->eligible_count)
                        : ""));
    if (presentation.PickerVisible()) {
      panel = Column();
      panel->SetInsets(CefInsets(kGap, kGap, kGap, kGap));
      panel->AddChildView(Text(String("cast.picker.title")));
      status = Text(String(presentation.StatusKey(clock())), kStatusId);
      panel->AddChildView(status);
      auto list = Column();
      list_content = list;
      Choices(list);
      Footer(list);
      const auto preferred = list->GetPreferredSize();
      list->SetSize(CefSize(kPanelWidth - 2 * kGap - kScrollBarAllowance,
                            preferred.height));
      auto scroll = CefScrollView::CreateScrollView(new ScrollDelegate);
      scroll->SetContentView(list);
      panel->AddChildView(scroll);
      panel->GetLayout()->AsBoxLayout()->SetFlexForView(scroll, 1);
      auto commit =
          Button(String("cast.picker.select") +
                     (s->selected_media && s->selected_device
                          ? " · " + MediaName(*s->selected_media) + " → " +
                                DeviceName(*s->selected_device)
                          : ""),
                 presentation.Intent(CastIntentKind::kCommit), kCommitId);
      panel->AddChildView(commit);
      panel->AddChildView(Button(String("cast.picker.cancel"),
                                 presentation.Intent(CastIntentKind::kCancel),
                                 kCancelId));
      if (!saved_code.empty())
        code->SetText(saved_code);
      picker = window->AddOverlayView(panel, CEF_DOCKING_MODE_CUSTOM, true);
      if (picker) {
        window->SetAccelerator(kEscapeCommand, kEscapeKey, false, false, false,
                               true);
        window->SetAccelerator(kTabCommand, kTabKey, false, false, false, true);
        window->SetAccelerator(kBackTabCommand, kTabKey, true, false, false,
                               true);
      }
      PlacePicker();
    }
    Update();
    if (picker && picker->IsVisible()) {
      bool restored = false;
      if (focus_code && code && code->IsEnabled()) {
        code->RequestFocus();
        restored = true;
      }
      if (focus)
        for (const auto &a : actions) {
          if (a.button->IsEnabled() && a.intent.kind == focus->kind &&
              a.intent.media == focus->media &&
              a.intent.device_id == focus->device_id &&
              a.intent.page_offset == focus->page_offset &&
              !a.button->IsSame(entry)) {
            a.button->RequestFocus();
            restored = true;
            break;
          }
        }
      if (!restored)
        FocusFirst();
    }
  }
  void FocusFirst() {
    for (const auto &a : actions)
      if (!a.anchor && !a.button->IsSame(entry) && a.button->IsEnabled()) {
        a.button->RequestFocus();
        return;
      }
  }
  void Update() {
    if (!entry || detached)
      return;
    const auto now = clock();
    entry->SetTooltipText(String(presentation.StatusKey(now)));
    if (status) {
      const auto text = String(presentation.StatusKey(now));
      if (status->GetText().ToString() != text) {
        status->SetText(text);
        status->SetAccessibleName(text);
      }
    }
    for (auto &a : actions) {
      auto intent = a.intent;
      if (intent.kind == CastIntentKind::kLookupCode && code)
        intent.cast_code = code->GetText().ToString();
      a.button->SetEnabled(!dispatch_pending &&
                           presentation.Allows(intent, now));
    }
    if (code)
      code->SetEnabled(!presentation.Busy());
    bool invalid_geometry =
        browser_view &&
        !(browser_view->GetBoundsInScreen() == geometry_viewport);
    const auto size = browser_view ? browser_view->GetSize() : CefSize();
    for (const auto &a : actions)
      if (a.anchor &&
          !presentation.PlaceOverlay(*a.anchor, size.width, size.height, now))
        invalid_geometry = true;
    if (invalid_geometry)
      HideOverlays();
  }
  void Anchors(std::vector<CastVideoAnchor> anchors) {
    HideOverlays();
    pending_anchors.clear();
    if (!window || !browser_view || presentation.PickerVisible() ||
        anchors.size() > kCastSelectionPageSize)
      return;
    if (render_pending) {
      pending_anchors = std::move(anchors);
      return;
    }
    const auto size = browser_view->GetSize();
    geometry_viewport = browser_view->GetBoundsInScreen();
    for (std::size_t n = 0; n < anchors.size(); ++n) {
      const auto &anchor = anchors[n];
      for (std::size_t prev = 0; prev < n; ++prev)
        if (anchors[prev].media == anchor.media) {
          HideOverlays();
          return;
        }
      const auto bounds =
          presentation.PlaceOverlay(anchor, size.width, size.height, clock());
      if (!bounds) {
        HideOverlays();
        return;
      }
      CefPoint point(bounds->x, bounds->y);
      if (!browser_view->ConvertPointToWindow(point)) {
        HideOverlays();
        return;
      }
      auto intent = presentation.Intent(CastIntentKind::kOpenForMedia);
      intent.media = anchor.media;
      auto button = Button(String("cast.selection.overlay"), intent,
                           kOverlayFirstId + static_cast<int>(n), anchor);
      auto overlay =
          window->AddOverlayView(button, CEF_DOCKING_MODE_CUSTOM, true);
      if (!overlay) {
        HideOverlays();
        return;
      }
      overlays.push_back(overlay);
      overlay->SetBounds(
          CefRect(point.x, point.y, bounds->width, bounds->height));
      overlay->SetVisible(true);
    }
    Update();
  }
  void Detach() {
    if (detached)
      return;
    detached = true;
    pending_anchors.clear();
    sink = {};
    ClosePicker(false);
    HideOverlays();
    location.Detach();
    actions.clear();
    entry = nullptr;
    browser_view = nullptr;
    window = nullptr;
    presentation.Clear();
  }
};

CastEntrySurface::CastEntrySurface(localization::LocaleSnapshot locale,
                                   Clock clock, IntentSink sink)
    : state_(
          std::make_shared<State>(locale, std::move(clock), std::move(sink))) {
  CEF_REQUIRE_UI_THREAD();
}
CastEntrySurface::~CastEntrySurface() { Detach(); }
bool CastEntrySurface::Attach(CefRefPtr<CefWindow> window,
                              CefRefPtr<CefBrowserView> browser_view) {
  CEF_REQUIRE_UI_THREAD();
  auto s = state_;
  if (s->detached || s->entry || !s->clock || !s->sink || !window ||
      !browser_view)
    return false;
  s->window = window;
  s->browser_view = browser_view;
  s->entry = s->Button(s->String("cast.feature.idle"),
                       s->presentation.Intent(CastIntentKind::kOpen), kEntryId);
  s->entry->SetMinimumSize(CefSize(kEntryWidth, kToolbarHeight));
  s->entry->SetMaximumSize(CefSize(kEntryWidth, kToolbarHeight));
  if (!s->location.Attach(window, browser_view, s->entry)) {
    s->actions.clear();
    s->entry = nullptr;
    s->window = nullptr;
    s->browser_view = nullptr;
    return false;
  }
  s->Render();
  return true;
}
void CastEntrySurface::BindContext(CastViewContext context) {
  CEF_REQUIRE_UI_THREAD();
  state_->ClosePicker(false);
  state_->HideOverlays();
  state_->pending_anchors.clear();
  state_->presentation.BindContext(std::move(context));
  state_->Update();
  state_->QueueRender();
}
bool CastEntrySurface::Apply(CastSelectionSnapshot snapshot) {
  CEF_REQUIRE_UI_THREAD();
  if (state_->detached)
    return false;
  const bool accepted = state_->presentation.Apply(std::move(snapshot));
  if (accepted || !state_->presentation.snapshot())
    state_->QueueRender();
  state_->Update();
  return accepted;
}
void CastEntrySurface::SetVideoAnchors(std::vector<CastVideoAnchor> anchors) {
  CEF_REQUIRE_UI_THREAD();
  state_->Anchors(std::move(anchors));
}
void CastEntrySurface::InvalidateGeometry() {
  CEF_REQUIRE_UI_THREAD();
  state_->pending_anchors.clear();
  state_->HideOverlays();
}
void CastEntrySurface::LayoutChanged() {
  CEF_REQUIRE_UI_THREAD();
  state_->pending_anchors.clear();
  state_->HideOverlays();
  state_->PlacePicker();
}
void CastEntrySurface::Tick() {
  CEF_REQUIRE_UI_THREAD();
  state_->Update();
}
bool CastEntrySurface::HandleKeyEvent(const CefKeyEvent &event) {
  CEF_REQUIRE_UI_THREAD();
  auto s = state_;
  if (!s->picker || !s->picker->IsVisible() ||
      event.type != KEYEVENT_RAWKEYDOWN)
    return false;
  if (event.windows_key_code == kEscapeKey) {
    s->Send(s->presentation.Intent(CastIntentKind::kCancel));
    return true;
  }
  if (event.windows_key_code != kTabKey ||
      (event.modifiers &
       (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN | EVENTFLAG_COMMAND_DOWN)))
    return false;
  std::vector<CefRefPtr<CefView>> focusable;
  for (const auto &a : s->actions)
    if (!a.anchor && !a.button->IsSame(s->entry)) {
      if (a.intent.kind == CastIntentKind::kLookupCode && s->code->IsEnabled())
        focusable.push_back(s->code);
      if (a.button->IsEnabled())
        focusable.push_back(a.button);
    }
  if (focusable.empty())
    return true;
  auto current = s->window->GetFocusedView();
  auto found =
      std::find_if(focusable.begin(), focusable.end(), [&](const auto &v) {
        return current && v->IsSame(current);
      });
  std::size_t index = found == focusable.end()
                          ? 0
                          : static_cast<std::size_t>(found - focusable.begin());
  if (found != focusable.end())
    index = (index + ((event.modifiers & EVENTFLAG_SHIFT_DOWN)
                          ? focusable.size() - 1
                          : 1)) %
            focusable.size();
  focusable[index]->RequestFocus();
  return true;
}
bool CastEntrySurface::HandleAccelerator(int command_id) {
  CEF_REQUIRE_UI_THREAD();
  if (command_id != kEscapeCommand && command_id != kTabCommand &&
      command_id != kBackTabCommand)
    return false;
  CefKeyEvent event;
  event.type = KEYEVENT_RAWKEYDOWN;
  event.windows_key_code = command_id == kEscapeCommand ? kEscapeKey : kTabKey;
  event.modifiers = command_id == kBackTabCommand ? EVENTFLAG_SHIFT_DOWN : 0;
  return HandleKeyEvent(event);
}
CefRefPtr<CefView> CastEntrySurface::GetView(int view_id) const {
  CEF_REQUIRE_UI_THREAD();
  if (view_id <= 0 || state_->detached)
    return nullptr;
  for (const auto &a : state_->actions)
    if (a.button->GetID() == view_id)
      return a.button;
  if (state_->code && state_->code->GetID() == view_id)
    return state_->code;
  if (state_->status && state_->status->GetID() == view_id)
    return state_->status;
  return nullptr;
}
void CastEntrySurface::SuspendLocation() {
  CEF_REQUIRE_UI_THREAD();
  state_->ClosePicker(false);
  state_->HideOverlays();
  state_->location.SuspendLocation();
}
bool CastEntrySurface::RestoreLocation() {
  CEF_REQUIRE_UI_THREAD();
  const bool restored = state_->location.RestoreLocation();
  if (restored)
    state_->Render();
  return restored;
}
void CastEntrySurface::Detach() {
  CEF_REQUIRE_UI_THREAD();
  state_->Detach();
}

} // namespace crayon::browser::cef_shell
