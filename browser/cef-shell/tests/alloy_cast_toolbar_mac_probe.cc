#include "alloy_cast_toolbar_mac_probe.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "browser/media_host/alloy_cast_controller.h"
#include "browser/media_host/cast_entry_surface.h"
#include "browser/window/alloy_chrome_palette.h"
#include "browser/window/tab_controller.h"
#include "crayon/browser_localization/locale_catalog.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_button.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "macos/alloy_product_host_mac.h"
#include "macos/alloy_toolbar_mac.h"

namespace {
namespace shell = crayon::browser::cef_shell;
namespace mh = crayon::cef_shell::ipc::media_host;
namespace mh2 = crayon::cef_shell::ipc::media_host_v2;
namespace cv = crayon::browser_cast_view;
using shell::CastEntrySurface;

// Only the remote transport is fake: window, toolbar, TabController, surface,
// adapter and controller are the production implementations.
class Transport final : public shell::media_host::MediaHostTransport {
 public:
  bool Start(std::string) override { return true; }
  void Stop() override {}
  bool healthy() const noexcept override { return true; }
  std::uint64_t generation() const noexcept override { return 1; }
  bool supports_player_messages() const noexcept override { return true; }
  bool supports_drafts() const noexcept override { return true; }
  bool supports_connect() const noexcept override { return true; }
  std::uint64_t player_session_id() const noexcept override { return 77; }
  bool Enqueue(mh::Message message) override {
    sent.push_back(std::move(message));
    return true;
  }
  std::vector<mh::Message> Drain(std::size_t) override { return {}; }
  bool EnqueuePlayer(mh2::PlayerMessage) override { return true; }
  bool EnqueuePlayerList(mh2::PlayerListRequest request) override {
    pages.push_back({request.context,
                     ++revision,
                     mh2::PlayerPageStatus::kOk,
                     0,
                     std::nullopt,
                     {}});
    if (media_available)
      pages.back().players.push_back({8, 1, mh2::PlayerSourceKind::kHttpUrl,
                                      true, true, true, false, "Video"});
    return true;
  }
  std::vector<mh2::PlayerPageReply> DrainPlayerPages(std::size_t) override {
    return std::exchange(pages, {});
  }
  bool EnqueueDraft(mh2::DraftCommand command) override {
    drafts.push_back(std::move(command));
    return true;
  }
  std::vector<mh2::DraftStateReply> DrainDraftStates(std::size_t) override {
    return {};
  }
  bool media_available = false;
  std::uint64_t revision = 0;
  std::vector<mh::Message> sent;
  std::vector<mh2::PlayerPageReply> pages;
  std::vector<mh2::DraftCommand> drafts;
};

class Probe final : public CefApp, public CefBrowserProcessHandler {
 public:
  explicit Probe(std::shared_ptr<AlloyCastToolbarMacProbeResult> result)
      : result_(std::move(result)) {}
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(
      const CefString&, CefRefPtr<CefCommandLine> command) override {
    command->AppendSwitch("use-mock-keychain");
    command->AppendSwitch("disable-background-networking");
  }
  void OnContextInitialized() override {
    const auto locale = crayon::browser::localization::SnapshotFor(
        crayon::browser::localization::AppLocale::kZhCn);
    auto transport = std::make_unique<Transport>();
    transport_ = transport.get();
    adapter_ = std::make_unique<shell::media_host::MediaHostAdapter>(
        std::move(transport));
    adapter_->Start("fixture");
    controller_ = std::make_unique<shell::media_host::AlloyCastController>(
        adapter_.get(),
        [this](auto snapshot) {
          if (surface_) surface_->Apply(std::move(snapshot));
        },
        "视频", "设备", [this] { return now_; });
    tabs_ = new shell::window::TabController("about:blank");
    tabs_->SetMediaObservationLifecycleCallback(
        [this](std::uint32_t tab, std::uint64_t, std::uint32_t generation,
               bool closed) {
          if (closed)
            generations_.erase(tab);
          else
            generations_[tab] = generation;
        });
    tabs_->SetBrowserClosingCallback([this](CefRefPtr<CefBrowser> browser) {
      std::cout << "alloy_cast_toolbar_mac close=browser-closing id="
                << (browser ? browser->GetIdentifier() : 0) << std::endl;
      if (browser && browser->GetIdentifier() == second_browser_id_)
        second_on_before_close_ = true;
      if (host_ && browser)
        host_->NotifyBrowserClosed(browser->GetIdentifier());
      Detach();
    });
    tabs_->SetBrowserCloseRequestedCallback(
        [this](CefRefPtr<CefBrowser> browser) {
          std::cout << "alloy_cast_toolbar_mac close=do-close id="
                    << (browser ? browser->GetIdentifier() : 0) << std::endl;
          if (!host_ || !browser ||
              !host_->browser_view(browser->GetIdentifier())) {
            return false;
          }
          if (browser->GetIdentifier() == second_browser_id_)
            second_do_close_ = true;
          if (active_browser_ == browser->GetIdentifier()) {
            Detach();
          }
          return host_->HandleBrowserClose(browser);
        });
    tabs_->SetBrowsersClosedCallback([this] {
      Detach();
      if (toolbar_)
        toolbar_->Shutdown();
    });
    // Mirrors the product wiring in app.cc, so the probe covers the whole
    // client -> controller -> toolbar chain rather than the toolbar alone.
    tabs_->SetTabLoadErrorCallback([this](int browser_id, std::string url,
                                          bool certificate_error) {
      load_errors_.push_back({browser_id, url, certificate_error});
      if (toolbar_) {
        static_cast<void>(toolbar_->OnTabLoadError(browser_id, std::move(url),
                                                   certificate_error));
      }
    });
    toolbar_ = std::make_unique<shell::macos::AlloyToolbarMac>(
        locale,
        shell::macos::AlloyToolbarMac::Callbacks{
            [this] {
              if (host_)
                static_cast<void>(host_->CreateTab("data:text/html,NEW_PAGE"));
            },
            [this](shell::window::TabId tab_id) {
              if (tabs_ && tabs_->ActivateTab(tab_id))
                SyncToolbarToActiveTab();
            },
            [this](shell::window::TabId tab_id) {
              if (tabs_)
                static_cast<void>(tabs_->RequestCloseTab(tab_id));
            },
            [this](shell::window::TabId tab_id) {
              const auto *tab = tabs_ ? tabs_->model().Find(tab_id) : nullptr;
              return tab ? (!tab->title.empty() ? tab->title : tab->url)
                         : std::string{};
            }});
    tabs_->SetTabUiUpdateCallback(
        [this](int browser_id, const std::string &url, bool is_loading,
               bool can_go_back, bool can_go_forward) {
          if (!toolbar_ || !tabs_)
            return;
          static_cast<void>(toolbar_->OnTabUiUpdate(
              browser_id, url, is_loading, can_go_back, can_go_forward));
          if (browser_id == 0) {
            SyncToolbarToActiveTab();
          } else {
            static_cast<void>(toolbar_->SyncTabs(tabs_->model()));
            if (host_)
              host_->RefreshTabChrome();
          }
        });
    tabs_->SetBrowserFocusedCallback(
        [this](CefRefPtr<CefBrowser>) { SyncToolbarToActiveTab(); });
    shell::macos::AlloyProductHostMac::Callbacks callbacks;
    callbacks.before_close = [this] { Detach(); };
    callbacks.window_destroyed = [this] {
      std::cout << "alloy_cast_toolbar_mac close=window-destroyed" << std::endl;
      CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                              &Probe::Cleanup, CefRefPtr<Probe>(this))));
    };
    callbacks.layout_changed = [this] {
      if (surface_) surface_->LayoutChanged();
    };
    callbacks.key_event = [this](const CefKeyEvent& e) {
      return surface_ && surface_->HandleKeyEvent(e);
    };
    callbacks.accelerator = [this](int id) {
      return surface_ && surface_->HandleAccelerator(id);
    };
    host_ = std::make_unique<shell::macos::AlloyProductHostMac>(
        shell::macos::AlloyProductHostMac::Dependencies{
            tabs_->client(), "about:blank", "Cast toolbar probe",
            toolbar_->tab_strip_view(), toolbar_->toolbar_view()},
        std::move(callbacks));
    if (!host_->Start()) {
      Fail("start");
      return;
    }
    Schedule();
  }

private:
  void Cleanup() {
    if (!finished_) {
      std::cerr << "alloy_cast_toolbar_mac window destroyed phase=" << phase_
                << " active=" << active_browser_ << std::endl;
    }
    Detach();
    if (controller_)
      controller_->Shutdown();
    toolbar_->Shutdown();
    host_.reset();
    toolbar_.reset();
    tabs_ = nullptr;
    result_->closed = true;
    CefQuitMessageLoop();
  }
  void Detach() {
    if (surface_) surface_->Detach();
    surface_.reset();
    context_.reset();
  }
  bool Bind() {
    const auto browser = tabs_->ActiveBrowser();
    if (!browser || browser->IsLoading()) return false;
    const auto* tab = tabs_->model().FindByBrowser(browser->GetIdentifier());
    if (!tab || !generations_.count(tab->id)) return false;
    const auto view = host_->browser_view(browser->GetIdentifier());
    if (!view || !host_->window()) return false;
    if (active_browser_ != browser->GetIdentifier()) {
      active_browser_ = browser->GetIdentifier();
      tabs_->client()->AdvanceMediaObservationNavigation(
          browser, static_cast<std::uint32_t>(tab->id),
          tab->navigation_generation);
    }
    const cv::CastViewContext next{
        9, "fixture", static_cast<std::uint32_t>(tab->id),
        tab->navigation_generation, generations_.at(tab->id)};
    if (context_ && *context_ == next) return true;
    Detach();
    toolbar_->AttachBrowser(tab->id, browser);
    toolbar_->SyncTabs(tabs_->model());
    host_->ShowBrowser(browser->GetIdentifier());
    surface_ = std::make_unique<CastEntrySurface>(
        crayon::browser::localization::SnapshotFor(
            crayon::browser::localization::AppLocale::kZhCn),
        [this] { return now_; },
        [this](auto intent) {
          if (!controller_->HandleIntent(intent)) Fail("intent rejected");
        });
    if (!surface_->Attach(host_->window(), view, toolbar_->toolbar_panel())) {
      Fail("attach");
      return false;
    }
    // PLT-SHELL-24M2FIX-C7: the app calls this right after a successful cast
    // attach so the trailing menu stays last; the probe mirrors that call
    // rather than relying on construction order.
    static_cast<void>(toolbar_->EnsureTrailingMenuButton());
    surface_->BindContext(next);
    if (!controller_->BindContext(next)) {
      Fail("bind");
      return false;
    }
    context_ = next;
    return true;
  }
  std::size_t EntryCount() {
    std::size_t count = 0;
    auto panel = toolbar_->toolbar_panel();
    for (std::size_t i = 0; i < panel->GetChildViewCount(); ++i)
      if (panel->GetChildViewAt(i)->GetID() == CastEntrySurface::kEntryId)
        ++count;
    return count;
  }
  // PLT-SHELL-24M2FIX-B: a failed main-frame load must be visible in the real
  // toolbar. Driven through the production entry point with the bound browser
  // id, so no OS input, navigation timing or network is involved.
  bool CheckLoadErrorProjection() {
    const auto catalog = crayon::browser::localization::LocaleCatalog(
        crayon::browser::localization::AppLocale::kZhCn);
    const auto message = catalog.Find("omnibox.notice.load_failed");
    if (!message || message->empty()) {
      Fail("missing load_failed key");
      return false;
    }
    const std::string failed = "http://127.0.0.1:1/";
    // Baseline: nothing is shown before a failure happens.
    if (!LoadErrorNoticeText().empty()) {
      Fail("notice before failure");
      return false;
    }
    // A browser this toolbar is not bound to must be ignored outright.
    if (toolbar_->OnTabLoadError(active_browser_ + 1000, failed, false) ||
        !LoadErrorNoticeText().empty()) {
      Fail("foreign browser id accepted");
      return false;
    }
    const auto browser = tabs_->ActiveBrowser();
    if (!browser || !browser->GetMainFrame()) {
      Fail("no bound browser");
      return false;
    }
    const auto identifier = browser->GetIdentifier();
    // Real load-handler entry point, main frame: connection refused.
    tabs_->client()->OnLoadError(
        browser, browser->GetMainFrame(),
        static_cast<cef_errorcode_t>(-102), CefString("refused"),
        CefString(failed.c_str()));
    if (load_errors_.size() != 1 || load_errors_[0].browser_id != identifier ||
        load_errors_[0].url != failed || load_errors_[0].certificate_error) {
      Fail("client load error");
      return false;
    }
    if (LoadErrorNoticeText() != *message) {
      Fail("load notice");
      return false;
    }
    // A certificate failure is classified apart and stays equally visible.
    tabs_->client()->OnLoadError(
        browser, browser->GetMainFrame(), static_cast<cef_errorcode_t>(-107),
        CefString("ssl"), CefString(failed.c_str()));
    if (load_errors_.size() != 2 || !load_errors_[1].certificate_error ||
        LoadErrorNoticeText() != *message) {
      Fail("certificate notice");
      return false;
    }
    // A load the user aborted, or one a newer navigation replaced, is reported
    // as ERR_ABORTED: it must not be dressed up as a failure.
    tabs_->client()->OnLoadError(
        browser, browser->GetMainFrame(), ERR_ABORTED, CefString("aborted"),
        CefString(failed.c_str()));
    if (load_errors_.size() != 2 || LoadErrorNoticeText() != *message) {
      Fail("aborted reported");
      return false;
    }
    return true;
  }
  // Reads the notice from the real view tree: the omnibox panel's second child
  // is the strip that carries either suggestions or the notice. The omnibox is
  // asked for by name (PLT-SHELL-24M2FIX-C4: it sits inside a pill holder, so
  // the toolbar's child index no longer identifies it).
  std::string LoadErrorNoticeText() {
    auto omnibox_panel = toolbar_ && toolbar_->omnibox_view()
                             ? toolbar_->omnibox_view()->AsPanel()
                             : nullptr;
    if (!omnibox_panel || omnibox_panel->GetChildViewCount() < 2) return {};
    auto strip = omnibox_panel->GetChildViewAt(1)->AsPanel();
    if (!strip || strip->GetChildViewCount() == 0) return {};
    auto button = strip->GetChildViewAt(0)->AsButton();
    auto label = button ? button->AsLabelButton() : nullptr;
    return label ? label->GetText().ToString() : std::string{};
  }
  void Schedule() {
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           &Probe::Tick, CefRefPtr<Probe>(this))),
                       20);
  }

  void CloseTimeout() {
    if (result_->closed)
      return;
    std::cerr << "alloy_cast_toolbar_mac FAIL close timeout phase=" << phase_
              << " active_browser=" << active_browser_
              << " tabs=" << (tabs_ ? tabs_->model().size() : 0) << std::endl;
    result_->passed = false;
    if (host_)
      host_->Close();
  }

  // PLT-SHELL-24M2FIX: the regression guard for the two defects that shipped
  // because nothing asserted the product assembly's content rect or chrome
  // colors. Measured before the fix: container 1100x640 but the mounted
  // browser view 1100x0 (page rendered into a zero-height view), and the strip
  // panel painted 0xffffffff instead of the strip token.
  bool CheckChromeGeometry() {
    const auto window = host_ ? host_->window() : nullptr;
    if (!window) {
      Fail("geometry window");
      return false;
    }
    const auto browser = tabs_ ? tabs_->ActiveBrowser() : nullptr;
    const auto view =
        browser ? host_->browser_view(browser->GetIdentifier()) : nullptr;
    if (!view) {
      Fail("geometry browser view");
      return false;
    }
    const auto vb = view->GetBounds();
    if (vb.width <= 0 || vb.height <= 0) {
      Fail("geometry content view collapsed");
      return false;
    }
    const auto strip = toolbar_ ? toolbar_->tab_strip_view() : nullptr;
    if (!strip || strip->GetBackgroundColor() !=
                      shell::window::chrome_palette::kTabStripBackground) {
      Fail("geometry strip color");
      return false;
    }
    const auto window_bounds = window->GetBoundsInScreen();
    const auto strip_bounds = strip->GetBoundsInScreen();
    if (strip_bounds.y != window_bounds.y) {
      Fail("tab strip is below window top");
      return false;
    }
    const auto toolbar_row = toolbar_ ? toolbar_->toolbar_panel() : nullptr;
    if (!toolbar_row || toolbar_row->GetBackgroundColor() !=
                            shell::window::chrome_palette::kToolbarBackground) {
      Fail("geometry toolbar color");
      return false;
    }
    // PLT-SHELL-24M2FIX-C4-a: the address field is a pill on the navigation
    // bar: pill height (2 x pillRadiusDip), centred in the bar, filled with the
    // omnibox surface colour, and stopped short of the window edge.
    const auto omnibox = toolbar_ ? toolbar_->omnibox_view() : nullptr;
    if (!omnibox) {
      Fail("geometry omnibox missing");
      return false;
    }
    const auto ob = omnibox->GetBounds();
    if (ob.height != shell::window::kOmniboxPillHeightDip ||
        ob.height <= 0 || ob.height >= toolbar_row->GetBounds().height) {
      Fail("geometry omnibox pill height");
      return false;
    }
    if (omnibox->GetBackgroundColor() !=
        shell::window::chrome_palette::kOmniboxBackground) {
      Fail("geometry omnibox color");
      return false;
    }
    CefPoint toolbar_origin;
    CefPoint omnibox_origin;
    if (!toolbar_row->ConvertPointToWindow(toolbar_origin) ||
        !omnibox->ConvertPointToWindow(omnibox_origin)) {
      Fail("geometry window coordinates");
      return false;
    }
    const auto tb_row = toolbar_row->GetBounds();
    const int toolbar_right = toolbar_origin.x + tb_row.width;
    const int pill_top_margin = omnibox_origin.y - toolbar_origin.y;
    const int pill_bottom_margin =
        toolbar_origin.y + tb_row.height - (omnibox_origin.y + ob.height);
    const int pill_right_margin = toolbar_right - (omnibox_origin.x + ob.width);
    if (pill_top_margin != shell::window::kOmniboxPillMarginDip ||
        pill_bottom_margin != shell::window::kOmniboxPillMarginDip) {
      Fail("geometry omnibox pill vertical margins");
      return false;
    }
    // PLT-SHELL-24M2FIX-C4-a: the trailing inset is an ordering invariant, not
    // a fixed pill margin. The product permanently attaches the cast entry
    // after the omnibox, so the pill's own right margin is that control plus
    // the inset. What must hold is that the inset is honoured at the trailing
    // edge and that the pill never reaches it.
    int trailing_right = toolbar_right;
    if (toolbar_row->GetChildViewCount() > 0) {
      const auto last =
          toolbar_row->GetChildViewAt(toolbar_row->GetChildViewCount() - 1);
      CefPoint last_origin;
      if (!last || !last->ConvertPointToWindow(last_origin)) {
        Fail("geometry toolbar trailing control");
        return false;
      }
      trailing_right = last_origin.x + last->GetBounds().width;
    }
    if (toolbar_right - trailing_right != shell::window::kChromeTrailingInsetDip ||
        pill_right_margin < shell::window::kChromeTrailingInsetDip) {
      Fail("geometry toolbar trailing inset");
      return false;
    }
    // PLT-SHELL-24M2FIX-C4: the projection the native decoration draws from
    // must agree with the views it describes. This is the value the host
    // forwards on every layout pass, so a mismatch here is a drawing bug.
    const auto decoration = toolbar_->decoration();
    const auto strip_panel = strip ? strip->AsPanel() : nullptr;
    const auto ordered = tabs_->model().ordered_tabs();
    if (!strip_panel || strip_panel->GetChildViewCount() < ordered.size() ||
        decoration.tabs.size() != ordered.size()) {
      Fail("geometry decoration tabs");
      return false;
    }
    for (std::size_t index = 0; index < decoration.tabs.size(); ++index) {
      const auto row = strip_panel->GetChildViewAt(index);
      const auto slot = row && row->AsPanel() && row->AsPanel()->GetChildViewCount() > 0
                            ? row->AsPanel()->GetChildViewAt(0)
                            : nullptr;
      CefPoint row_origin;
      CefPoint slot_origin;
      if (!row || !slot || !row->ConvertPointToWindow(row_origin) ||
          !slot->ConvertPointToWindow(slot_origin)) {
        Fail("geometry decoration row");
        return false;
      }
      const auto row_bounds = row->GetBounds();
      const auto slot_bounds = slot->GetBounds();
      const auto *snapshot = tabs_->model().Find(ordered[index]);
      const auto &tab = decoration.tabs[index];
      if (!snapshot || tab.bounds.x != row_origin.x ||
          tab.bounds.y != row_origin.y ||
          tab.bounds.width != row_bounds.width ||
          tab.bounds.height != row_bounds.height ||
          // PLT-SHELL-24M2FIX-C4-c: in the product assembly the chrome band is
          // a fixed height, so the row and its indicator slot are exactly the
          // tab strip token.
          row_bounds.height != shell::window::kTabStripHeightDip ||
          slot_bounds.width != shell::window::kTabIndicatorSlotWidthDip ||
          slot_bounds.height != shell::window::kTabStripHeightDip ||
          slot_bounds.x != 0 || slot_bounds.y != 0 ||
          tab.indicator.x != slot_origin.x ||
          tab.indicator.y != slot_origin.y ||
          tab.indicator.width != slot_bounds.width ||
          tab.indicator.height != slot_bounds.height ||
          tab.active != (ordered[index] == tabs_->model().active_tab()) ||
          tab.loading != snapshot->loading) {
        Fail("geometry decoration tab rect");
        return false;
      }
    }
    if (decoration.omnibox.x != omnibox_origin.x ||
        decoration.omnibox.y != omnibox_origin.y ||
        decoration.omnibox.width != ob.width ||
        decoration.omnibox.height != ob.height) {
      Fail("geometry decoration omnibox rect");
      return false;
    }
    // PLT-SHELL-24M2FIX-C7: the trailing menu must exist and must come AFTER
    // the cast entry, which the cast surface appends to this same row. The
    // order is the user-visible requirement, so it is asserted on the real row
    // rather than assumed from the order the buttons were created in.
    const auto menu_button = toolbar_->menu_button();
    if (!menu_button || !menu_button->IsEnabled() ||
        !menu_button->GetImage(CEF_BUTTON_STATE_NORMAL)) {
      Fail("trailing menu button missing");
      return false;
    }
    {
      int cast_index = -1;
      int menu_index = -1;
      for (std::size_t i = 0; i < toolbar_row->GetChildViewCount(); ++i) {
        const auto child = toolbar_row->GetChildViewAt(i);
        if (child->IsSame(menu_button)) {
          menu_index = static_cast<int>(i);
        } else if (child->GetID() == CastEntrySurface::kEntryId) {
          cast_index = static_cast<int>(i);
        }
      }
      const auto ids = toolbar_->menu_command_ids();
      if (menu_index < 0 || menu_index !=
                                static_cast<int>(toolbar_row->GetChildViewCount()) - 1) {
        Fail("trailing menu is not last");
        return false;
      }
      // Cast entry present => it must precede the menu; absent => nothing to
      // order against. Both are accepted so the guard does not depend on media
      // readiness.
      if (cast_index >= 0 && cast_index > menu_index) {
        Fail("menu precedes cast entry");
        return false;
      }
      if (ids.size() != 6) {
        Fail("trailing menu item count");
        return false;
      }
      std::cout << "alloy_cast_toolbar_mac trailing_menu index=" << menu_index
                << " cast=" << cast_index << " items=" << ids.size() << '\n';
      std::cout.flush();
    }
    std::cout << "alloy_cast_toolbar_mac geometry content=" << vb.width << "x"
              << vb.height << " strip=0x" << std::hex
              << strip->GetBackgroundColor() << " toolbar=0x"
              << toolbar_row->GetBackgroundColor() << " omnibox=0x"
              << omnibox->GetBackgroundColor() << std::dec << " pill=" << ob.width
              << "x" << ob.height << "@" << omnibox_origin.x << ","
              << omnibox_origin.y << " margins=t" << pill_top_margin << ",b"
              << pill_bottom_margin << ",r" << pill_right_margin << " tabs="
              << decoration.tabs.size() << '\n';
    std::cout.flush();
    return true;
  }

  // PLT-SHELL-24M2FIX-E / corrected by -B3: opening a second tab must leave
  // that tab on screen.
  //
  // Every earlier assertion re-activated the first tab afterwards, and
  // ShowBrowser() re-showed it, so this defect stayed invisible to the suite.
  // The strip, the tab titles, the address bar and the CEF history all kept
  // working -- only the content area went blank, which is indistinguishable from
  // "the page never loaded".
  //
  // HONEST SCOPE (measured, 2026-09-23): this harness cannot reproduce it. With
  // BOTH B3 fixes reverted, this probe still reports `PASS` and `child2.0`
  // `visible=1`, because the product's own client does not deliver
  // OnBrowserCreated re-entrantly from AddChildView the way the real app does.
  // The defect and its fix were therefore established on the product itself
  // (`CRAYON_SHELL_DIAG` view-tree dumps plus `screencapture` pixel scans of
  // the main window: 5 consecutive blank frames before the fix, 1 transient
  // frame after). This guard is kept as the harness-side statement of the
  // invariant, not as the reproduction of the failure.
  bool CheckNewTabStaysVisible() {
    const auto browser = tabs_ ? tabs_->ActiveBrowser() : nullptr;
    if (!browser) {
      Fail("new tab browser");
      return false;
    }
    const auto view = host_ ? host_->browser_view(browser->GetIdentifier())
                            : nullptr;
    if (!view) {
      Fail("new tab view");
      return false;
    }
    if (!view->IsVisible()) {
      Fail("new tab hidden");
      return false;
    }
    const auto bounds = view->GetBounds();
    if (bounds.width <= 0 || bounds.height <= 0) {
      Fail("new tab collapsed");
      return false;
    }
    return true;
  }

  // PLT-SHELL-24M2FIX-C2: the active tab must be visually distinguishable.
  //
  // The strip used to paint the tab color on the row PANEL, which the row's two
  // CefLabelButton children covered end to end, so every tab came out the same
  // default white and the strip read as one solid bar. The color only survives
  // if it is applied from the button delegate's OnThemeChanged, because
  // CefView::SetBackgroundColor is reset whenever that callback fires. This
  // guard asserts the resulting per-tab color rather than any particular code
  // path. Measured on the product window before the fix (screencapture pixel
  // scan): active and inactive tabs both 0xffffff against a 0xe8eef8 band.
  bool CheckTabSurfacesDistinguishActive() {
    const auto strip_view = toolbar_ ? toolbar_->tab_strip_view() : nullptr;
    const auto panel = strip_view ? strip_view->AsPanel() : nullptr;
    if (!panel || !tabs_) {
      Fail("tab colors strip");
      return false;
    }
    const auto order = tabs_->model().ordered_tabs();
    if (order.size() < 2 || panel->GetChildViewCount() != order.size() + 1) {
      Fail("tab colors rows");
      return false;
    }
    bool saw_active = false;
    bool saw_inactive = false;
    for (std::size_t index = 0; index < order.size(); ++index) {
      const auto row = panel->GetChildViewAt(index)->AsPanel();
      if (!row || row->GetChildViewCount() == 0) {
        Fail("tab colors row");
        return false;
      }
      // PLT-SHELL-24M2FIX-C4: the row is [indicator slot][title][close], so the
      // title and close button moved one slot to the right.
      const auto indicator = row->GetChildViewAt(0);
      const auto title = row->GetChildViewCount() > 1 ? row->GetChildViewAt(1)
                                                     : nullptr;
      const auto close = row->GetChildViewCount() > 2 ? row->GetChildViewAt(2)
                                                      : nullptr;
      if (!indicator || !title) {
        Fail("tab colors row children");
        return false;
      }
      const auto close_button = close ? close->AsButton() : nullptr;
      const auto close_label = close_button ? close_button->AsLabelButton()
                                            : nullptr;
      // The multiplication sign is spelled as an escape so this source stays
      // ASCII at the byte level.
      if (!close_label || close_label->GetText().ToString() != "\xc3\x97") {
        Fail("tab close button glyph");
        return false;
      }
      // PLT-SHELL-24M2FIX-C4-c: every row reserves the leading indicator slot,
      // whether or not the tab is loading, so the title cannot shift.
      const auto ib = indicator->GetBounds();
      if (ib.x != 0 || ib.y != 0 ||
          ib.width != shell::window::kTabIndicatorSlotWidthDip ||
          ib.height != shell::window::kTabStripHeightDip) {
        Fail("tab indicator slot geometry");
        return false;
      }
      if (indicator->GetBackgroundColor() != title->GetBackgroundColor()) {
        Fail("tab indicator slot color");
        return false;
      }
      const auto rb = row->GetBounds();
      const auto rs = row->GetBoundsInScreen();
      const auto tb = title->GetBounds();
      const auto ts = title->GetBoundsInScreen();
      const auto cb = close ? close->GetBounds() : CefRect{};
      const auto cs = close ? close->GetBoundsInScreen() : CefRect{};
      std::cout << "alloy_cast_toolbar_mac tab_bounds index=" << index
                << " id=" << order[index] << " row=" << rb.x << "," << rb.y
                << "," << rb.width << "x" << rb.height << " screen=" << rs.x
                << "," << rs.y << "," << rs.width << "x" << rs.height
                << " title=" << tb.x << "," << tb.y << "," << tb.width << "x"
                << tb.height << " screen=" << ts.x << "," << ts.y << ","
                << ts.width << "x" << ts.height << " visible="
                << title->IsVisible() << " enabled=" << title->IsEnabled()
                << " close=" << cb.x << "," << cb.y << "," << cb.width << "x"
                << cb.height << " screen=" << cs.x << "," << cs.y << ","
                << cs.width << "x" << cs.height << " visible="
                << (close && close->IsVisible()) << " enabled="
                << (close && close->IsEnabled()) << std::endl;
      if (row->GetBounds().width > 240) {
        Fail("tab row exceeds 240 DIP maximum");
        return false;
      }
      // The tab surface colour is read from the title button: the indicator
      // slot only mirrors it (asserted above).
      const std::uint32_t background = title->GetBackgroundColor();
      if (order[index] == tabs_->model().active_tab()) {
        if (background != shell::window::chrome_palette::kActiveTabBackground) {
          Fail("tab colors active tab");
          return false;
        }
        saw_active = true;
      } else {
        if (background == shell::window::chrome_palette::kActiveTabBackground) {
          Fail("tab colors inactive tab");
          return false;
        }
        saw_inactive = true;
      }
    }
    if (!saw_active || !saw_inactive) {
      Fail("tab colors coverage");
      return false;
    }
    return true;
  }

  // PLT-SHELL-24M2FIX-C4: resolved by name, not by child index -- the pill is
  // wrapped in a holder panel so it can be centred in the navigation bar.
  CefRefPtr<CefPanel> OmniboxPanel() const {
    const auto view = toolbar_ ? toolbar_->omnibox_view() : nullptr;
    return view ? view->AsPanel() : nullptr;
  }

  std::string DisplayedOmniboxText() const {
    // PLT-SHELL-24M2FIX-C6: the field is addressed by name. It now sits inside
    // the pill's field row, so a child index would return that row instead.
    const auto textfield = toolbar_ ? toolbar_->omnibox_textfield() : nullptr;
    return textfield ? textfield->GetText().ToString() : std::string{};
  }

  bool BeginUnsubmittedOmniboxEdit(const std::string &text) {
    const auto textfield = toolbar_ ? toolbar_->omnibox_textfield() : nullptr;
    if (!textfield)
      return false;
    textfield->RequestFocus();
    textfield->SetText(text);
    auto *delegate =
        static_cast<CefTextfieldDelegate *>(textfield->GetDelegate().get());
    if (!delegate)
      return false;
    delegate->OnAfterUserAction(textfield);
    return DisplayedOmniboxText() == text;
  }

  bool PressTabCloseButton(shell::window::TabId tab_id) {
    const auto strip = toolbar_ ? toolbar_->tab_strip_view() : nullptr;
    const auto panel = strip ? strip->AsPanel() : nullptr;
    if (!panel || !tabs_)
      return false;
    const auto ordered = tabs_->model().ordered_tabs();
    const auto found = std::find(ordered.begin(), ordered.end(), tab_id);
    if (found == ordered.end())
      return false;
    const auto index = static_cast<std::size_t>(found - ordered.begin());
    if (index >= panel->GetChildViewCount())
      return false;
    const auto row = panel->GetChildViewAt(index)->AsPanel();
    // PLT-SHELL-24M2FIX-C4: [indicator][title][close].
    if (!row || row->GetChildViewCount() < 3)
      return false;
    const auto close = row->GetChildViewAt(2)->AsButton();
    auto *delegate = close ? static_cast<CefButtonDelegate *>(
                                 close->GetDelegate().get())
                           : nullptr;
    if (!close || !close->IsEnabled() || !delegate)
      return false;
    delegate->OnButtonPressed(close);
    return true;
  }

  void SyncToolbarToActiveTab() {
    if (!tabs_ || !toolbar_)
      return;
    const auto browser = tabs_->ActiveBrowser();
    if (!browser)
      return;
    const auto *tab = tabs_->model().FindByBrowser(browser->GetIdentifier());
    if (!tab)
      return;
    if (host_)
      host_->ShowBrowser(browser->GetIdentifier());
    static_cast<void>(toolbar_->AttachBrowser(tab->id, browser));
    static_cast<void>(toolbar_->SyncTabs(tabs_->model()));
    if (host_)
      host_->RefreshTabChrome();
  }

  void Fail(const char *detail) {
    std::cerr << "alloy_cast_toolbar_mac FAIL " << detail << '\n';
    finished_ = true;
    Detach();
    if (tabs_) {
      tabs_->CloseAllBrowsers(true);
    } else if (host_) {
      host_->Close();
    }
  }

  // PLT-SHELL-24M2FIX diagnostics: the product path (AlloyProductHostMac) is
  // the only place that assembles strip + toolbar + content container, and no
  // assertion has ever checked that the browser view actually receives a
  // non-zero content rect or which background colors the chrome views hold.
  // Print both so the numbers, not the intent, drive the fix.
  void ReportChromeGeometry(const char* label) {
    const auto window = host_ ? host_->window() : nullptr;
    if (!window) {
      std::cout << "geometry[" << label << "] window=none\n";
      return;
    }
    const auto wb = window->GetBounds();
    const auto client = window->GetClientAreaBoundsInScreen();
    std::cout << "geometry[" << label << "] window=" << wb.width << "x"
              << wb.height << " client=" << client.width << "x" << client.height
              << " children=" << window->GetChildViewCount() << '\n';
    const std::size_t children = window->GetChildViewCount();
    for (std::size_t i = 0; i < children; ++i) {
      auto child = window->GetChildViewAt(i);
      if (!child) continue;
      const auto b = child->GetBounds();
      std::cout << "geometry[" << label << "]  child" << i << " pos=" << b.x
                << "," << b.y << " size=" << b.width << "x" << b.height
                << " visible=" << (child->IsVisible() ? 1 : 0) << " bg=0x"
                << std::hex << child->GetBackgroundColor() << std::dec << '\n';
      auto panel = child->AsPanel();
      if (!panel) continue;
      const std::size_t inner_count = panel->GetChildViewCount();
      for (std::size_t j = 0; j < inner_count; ++j) {
        auto inner = panel->GetChildViewAt(j);
        if (!inner) continue;
        const auto ib = inner->GetBounds();
        std::cout << "geometry[" << label << "]   child" << i << "." << j
                  << " pos=" << ib.x << "," << ib.y << " size=" << ib.width
                  << "x" << ib.height
                  << " visible=" << (inner->IsVisible() ? 1 : 0) << " bg=0x"
                  << std::hex << inner->GetBackgroundColor() << std::dec
                  << '\n';
      }
    }
    const auto browser = tabs_ ? tabs_->ActiveBrowser() : nullptr;
    if (browser) {
      auto view = host_->browser_view(browser->GetIdentifier());
      if (view) {
        const auto vb = view->GetBounds();
        std::cout << "geometry[" << label << "] browser_view pos=" << vb.x
                  << "," << vb.y << " size=" << vb.width << "x" << vb.height
                  << " visible=" << (view->IsVisible() ? 1 : 0) << '\n';
      } else {
        std::cout << "geometry[" << label << "] browser_view=none\n";
      }
    }
    if (toolbar_) {
      auto row = toolbar_->toolbar_panel();
      if (row) {
        const auto rb = row->GetBounds();
        std::cout << "geometry[" << label << "] toolbar_panel size=" << rb.width
                  << "x" << rb.height << " bg=0x" << std::hex
                  << row->GetBackgroundColor() << std::dec << '\n';
      }
      auto strip = toolbar_->tab_strip_view();
      if (strip) {
        const auto sb = strip->GetBounds();
        std::cout << "geometry[" << label << "] tab_strip_view size=" << sb.width
                  << "x" << sb.height << " bg=0x" << std::hex
                  << strip->GetBackgroundColor() << std::dec << '\n';
      }
    }
    std::cout.flush();
  }

  void Tick() {
    if (finished_) return;
    if (++polls_ > 500) {
      Fail("timeout");
      return;
    }
    if (!Bind()) {
      Schedule();
      return;
    }
    if (logged_phase_ != phase_) {
      logged_phase_ = phase_;
      std::cout << "alloy_cast_toolbar_mac phase=" << phase_
                << " active_tab=" << context_->tab_id
                << " browser=" << active_browser_ << std::endl;
    }
    controller_->Tick();
    surface_->Tick();
    if (EntryCount() != 1) {
      Fail("entry count");
      return;
    }
    if (!load_error_checked_) {
      load_error_checked_ = true;
      if (!CheckLoadErrorProjection())
        return;
    }
    if (geometry_reports_ == 0) {
      geometry_reports_ = 1;
      ReportChromeGeometry("early");
    }
    auto entry = surface_->GetView(CastEntrySurface::kEntryId);
    switch (phase_) {
    case 0:
      if (!entry || entry->IsEnabled()) {
        Fail("initial disabled");
        return;
      }
      first_ = *context_;
      first_browser_id_ = active_browser_;
      transport_->media_available = true;
      now_ += 1000;
      ++phase_;
      break;
    case 1:
      if (!entry->IsEnabled())
        break;
      // Invoke the real native button delegate without OS input permissions.
      static_cast<CefButtonDelegate *>(entry->GetDelegate().get())
          ->OnButtonPressed(entry->AsButton());
      ++phase_;
      break;
    case 2:
      if (transport_->drafts.empty())
        break;
      if (transport_->drafts.size() != 1 ||
          transport_->drafts.back().action != mh2::DraftAction::kOpen ||
          transport_->drafts.back().context.tab_id != first_.tab_id) {
        Fail("open intent");
        return;
      }
      stale_ = controller_->snapshot();
      transport_->media_available = false;
      tabs_->ActiveBrowser()->GetMainFrame()->LoadURL(
          "data:text/html,FIRST_PAGE");
      ++phase_;
      break;
    case 3:
      if (context_->navigation_id == first_.navigation_id)
        break;
      if (entry->IsEnabled()) {
        Fail("navigation not fenced");
        return;
      }
      if (surface_->Apply(stale_)) {
        Fail("stale snapshot accepted");
        return;
      }
      if (!host_->CreateTab("data:text/html,SECOND_PAGE")) {
        Fail("create tab");
        return;
      }
      ++phase_;
      break;
    case 4:
      if (context_->tab_id == first_.tab_id)
        break;
      // PLT-SHELL-24M2FIX-E: assess the new tab while it is still the active
      // one. Activating the first tab below would re-show its view through
      // ShowBrowser and mask a hidden new tab.
      if (!CheckNewTabStaysVisible())
        return;
      second_tab_ = context_->tab_id;
      second_browser_id_ = active_browser_;
      {
        const auto browser = tabs_->ActiveBrowser();
        const auto frame = browser ? browser->GetMainFrame() : nullptr;
        if (!browser || browser->GetIdentifier() != second_browser_id_ ||
            !frame ||
            frame->GetURL().ToString() != "data:text/html,SECOND_PAGE") {
          Fail("second page not active");
          return;
        }
      }
      if (entry->IsEnabled()) {
        Fail("new tab not disabled");
        return;
      }
      if (!tabs_->ActivateTab(first_.tab_id)) {
        Fail("activate");
        return;
      }
      ++phase_;
      break;
    case 5:
      if (context_->tab_id != first_.tab_id)
        break;
      {
        const auto browser = tabs_->ActiveBrowser();
        const auto frame = browser ? browser->GetMainFrame() : nullptr;
        const auto first_view = host_->browser_view(first_browser_id_);
        if (!browser || !frame ||
            browser->GetIdentifier() != first_browser_id_ ||
            frame->GetURL().ToString() != "data:text/html,FIRST_PAGE" ||
            !first_view || !first_view->IsVisible()) {
          Fail("switch did not show first page");
          return;
        }
      }
      if (std::none_of(transport_->sent.begin(), transport_->sent.end(),
                       [](const auto &message) {
                         return std::holds_alternative<mh::CloseTab>(message);
                       })) {
        Fail("missing CloseTab");
        return;
      }
      if (!host_->CreateTab("data:text/html,THIRD_PAGE")) {
        Fail("create third tab");
        return;
      }
      ++phase_;
      break;
    case 6: {
      if (context_->tab_id == first_.tab_id || context_->tab_id == second_tab_)
        break;
      third_tab_ = context_->tab_id;
      third_browser_id_ = active_browser_;
      if (!CheckNewTabStaysVisible())
        return;
      {
        const auto browser = tabs_->ActiveBrowser();
        const auto frame = browser ? browser->GetMainFrame() : nullptr;
        if (!browser || browser->GetIdentifier() != third_browser_id_ ||
            !frame ||
            frame->GetURL().ToString() != "data:text/html,THIRD_PAGE") {
          Fail("third page not active");
          return;
        }
      }
      if (!CheckTabSurfacesDistinguishActive())
        return;
      if (!BeginUnsubmittedOmniboxEdit("C")) {
        Fail("could not establish unsubmitted omnibox edit");
        return;
      }
      if (!PressTabCloseButton(second_tab_)) {
        Fail("press background second tab close button");
        return;
      }
      ++phase_;
      break;
    }
    case 7: {
      if (context_->tab_id != third_tab_ ||
          tabs_->model().active_tab() != third_tab_ ||
          active_browser_ != third_browser_id_) {
        Fail("background close changed active newtab");
        return;
      }
      const auto *closing_tab = tabs_->model().Find(second_tab_);
      if (closing_tab) {
        if (closing_tab->lifecycle != shell::window::TabLifecycle::kClosing) {
          Fail("background close callback did not mark tab closing");
          return;
        }
        if (!second_close_stage_logged_) {
          second_close_stage_logged_ = true;
          std::cout << "alloy_cast_toolbar_mac background_close stage=closing"
                    << " do_close=" << second_do_close_
                    << " host_view="
                    << static_cast<bool>(host_->browser_view(second_browser_id_))
                    << std::endl;
        }
        break;
      }
      if (!second_do_close_ || !second_on_before_close_ ||
          host_->browser_view(second_browser_id_)) {
        if (!second_close_stage_logged_) {
          second_close_stage_logged_ = true;
          std::cout << "alloy_cast_toolbar_mac background_close stage=detached"
                    << " do_close=" << second_do_close_
                    << " on_before_close=" << second_on_before_close_
                    << " host_view="
                    << static_cast<bool>(host_->browser_view(second_browser_id_))
                    << std::endl;
        }
        break;
      }
      if (DisplayedOmniboxText() != "C") {
        Fail("background close changed active omnibox draft");
        return;
      }
      if (!tabs_->RequestCloseTab(first_.tab_id)) {
        Fail("close background first tab request");
        return;
      }
      ++phase_;
      break;
    }
    case 8: {
      const auto active_id = tabs_->model().active_tab();
      const auto browser = tabs_->ActiveBrowser();
      const auto host_browser = host_->browser();
      if (active_id != third_tab_ || !browser || !host_browser ||
          browser->GetIdentifier() != third_browser_id_ ||
          host_browser->GetIdentifier() != browser->GetIdentifier()) {
        Fail("active-tab replacement disagrees with visible host");
        return;
      }
      const auto third_view = host_->browser_view(third_browser_id_);
      if (!third_view || !third_view->IsVisible()) {
        Fail("newtab view lost after background close");
        return;
      }
      if (tabs_->model().Find(first_.tab_id))
        break;
      ++phase_;
      break;
    }
    case 9: {
      const auto active_id = tabs_->model().active_tab();
      const auto browser = tabs_->ActiveBrowser();
      const auto host_browser = host_->browser();
      if (active_id != third_tab_ || !browser || !host_browser ||
          browser->GetIdentifier() != third_browser_id_ ||
          host_browser->GetIdentifier() != browser->GetIdentifier()) {
        Fail("closing background first tab changed active page");
        return;
      }
      if (tabs_->model().Find(first_.tab_id))
        break;
      const auto frame = browser->GetMainFrame();
      const auto third_view = host_->browser_view(third_browser_id_);
      if (!frame || frame->GetURL().ToString() != "data:text/html,THIRD_PAGE" ||
          !third_view || !third_view->IsVisible()) {
        Fail("active third page lost after background close");
        return;
      }
      ++phase_;
      break;
    }
    case 10:
      ReportChromeGeometry("settled");
      if (!CheckChromeGeometry())
        return;
      finished_ = true;
      Detach();
      result_->passed = EntryCount() == 0;
      std::cout << "alloy_cast_toolbar_mac "
                << (result_->passed ? "PASS" : "FAIL") << '\n';
      std::cout.flush();
      if (!tabs_->RequestCloseTab(third_tab_)) {
        Fail("close final tab request");
        return;
      }
      CefPostDelayedTask(TID_UI,
                         CefCreateClosureTask(base::BindOnce(
                             &Probe::CloseTimeout, CefRefPtr<Probe>(this))),
                         8000);
      return;
    }
    Schedule();
  }
  std::shared_ptr<AlloyCastToolbarMacProbeResult> result_;
  Transport *transport_ = nullptr;
  std::unique_ptr<shell::media_host::MediaHostAdapter> adapter_;
  std::unique_ptr<shell::media_host::AlloyCastController> controller_;
  std::unique_ptr<shell::macos::AlloyToolbarMac> toolbar_;
  std::unique_ptr<shell::macos::AlloyProductHostMac> host_;
  std::unique_ptr<CastEntrySurface> surface_;
  CefRefPtr<shell::window::TabController> tabs_;
  std::map<std::uint32_t, std::uint32_t> generations_;
  struct RecordedLoadError final {
    int browser_id;
    std::string url;
    bool certificate_error;
  };
  std::vector<RecordedLoadError> load_errors_;
  std::optional<cv::CastViewContext> context_;
  cv::CastViewContext first_;
  cv::CastSelectionSnapshot stale_;
  std::uint64_t now_ = 1000;
  int polls_ = 0;
  int phase_ = 0;
  int logged_phase_ = -1;
  int active_browser_ = 0;
  int first_browser_id_ = 0;
  shell::window::TabId second_tab_ = 0;
  int second_browser_id_ = 0;
  shell::window::TabId third_tab_ = 0;
  int third_browser_id_ = 0;
  bool load_error_checked_ = false;
  int geometry_reports_ = 0;
  bool second_do_close_ = false;
  bool second_on_before_close_ = false;
  bool second_close_stage_logged_ = false;
  bool finished_ = false;
  IMPLEMENT_REFCOUNTING(Probe);
};
}  // namespace

CefRefPtr<CefApp> CreateAlloyCastToolbarMacProbe(
    std::shared_ptr<AlloyCastToolbarMacProbeResult> result) {
  return new Probe(std::move(result));
}
