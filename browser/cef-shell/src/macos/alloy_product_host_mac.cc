// PLT-SHELL-24M1/M2: macOS production Alloy window host. Assembles the
// product layout (tab strip, toolbar, content container) and owns the
// per-tab browser views. All handler surfaces route through the
// TabController WindowClient; the host holds no business logic.
#include "macos/alloy_product_host_mac.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_color_ids.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"
#include "browser/window/alloy_chrome_palette.h"
#include "macos/alloy_titlebar_mac.h"

namespace crayon::browser::cef_shell::macos {
namespace {

// PLT-SHELL-24M2FIX-D: env-gated product-side view-tree diagnostic.
//
// The harness probes assemble the same host class but never drive the app's
// tab lifecycle, so a probe PASS on geometry and token colors cannot falsify a
// blank content area. That gap is exactly how 24M2FIX-B/C were reported green
// while the product still painted nothing: "the page loaded" (History has the
// visit) and "the view has non-zero bounds" (probe) are both true, and neither
// one implies "those pixels are on screen". The product process is the only
// place where the deciding state exists, so the shell reports it itself.
// Set CRAYON_SHELL_DIAG=1, or drop a file at $HOME/Library/Application
// Support/CEF/crayon_shell_diag.enable, to emit the product's real view tree
// and per-browser URLs; inert otherwise. Two gates because neither channel
// works everywhere: a GUI launch (`open`) cannot set the environment, and in
// this harness `open --stderr` does not connect stderr either, while the
// product itself is already proven able to write under the CEF user-data
// directory. The sentinel also keeps the diagnostic usable from a Finder
// launch, where no environment can be injected at all.
constexpr char kDiagEnv[] = "CRAYON_SHELL_DIAG";
constexpr char kDiagSentinel[] =
    "/Library/Application Support/CEF/crayon_shell_diag.enable";
constexpr char kDiagLog[] =
    "/Library/Application Support/CEF/crayon_shell_diag.log";

std::string DiagUnderHome(const char* suffix) {
  const char* home = std::getenv("HOME");
  if (!home) {
    return {};
  }
  return std::string(home) + suffix;
}

bool DiagEnabled() {
  static const bool enabled = [] {
    if (std::getenv(kDiagEnv) != nullptr) {
      return true;
    }
    const std::string sentinel = DiagUnderHome(kDiagSentinel);
    if (sentinel.empty()) {
      return false;
    }
    std::ifstream probe(sentinel);
    return probe.good();
  }();
  return enabled;
}

// PLT-SHELL-24M2FIX-B3: the log is appended to by every launch and carries no
// PID, so an entry could not be attributed to a launch. Two processes writing
// the same file interleaved their lines and the visibility flips of one launch
// read as the history of another, which is precisely the mistake that makes a
// root cause look unreproducible. Every line now carries the session tag
// (wall-clock ms minted once per process) and the elapsed time since this
// process' first line, so one launch can be split out and ordered without a
// PID channel.
std::int64_t DiagClockMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

const std::string& DiagSessionTag() {
  static const std::string tag = std::to_string(DiagClockMs());
  return tag;
}

std::string DiagElapsed() {
  static const std::int64_t start = DiagClockMs();
  return "t=" + std::to_string(DiagClockMs() - start);
}

void DiagLog(const std::string& line) {
  if (!DiagEnabled()) {
    return;
  }
  const std::string path = DiagUnderHome(kDiagLog);
  if (path.empty()) {
    return;
  }
  std::ofstream log(path, std::ios::app);
  if (!log) {
    return;
  }
  log << "[s" << DiagSessionTag() << ' ' << DiagElapsed() << "] " << line
      << '\n';
}

std::string DiagColor(std::uint32_t argb) {
  char text[16] = {};
  std::snprintf(text, sizeof(text), "0x%08x", argb);
  return text;
}

std::string DiagRect(const CefRect& rect) {
  return std::to_string(rect.width) + "x" + std::to_string(rect.height) + "@" +
         std::to_string(rect.x) + "," + std::to_string(rect.y);
}

std::string DiagView(const CefRefPtr<CefView>& view) {
  if (!view) {
    return "null";
  }
  return "bounds=" + DiagRect(view->GetBounds()) +
         " screen=" + DiagRect(view->GetBoundsInScreen()) +
         " visible=" + (view->IsVisible() ? "1" : "0") +
         " bg=" + DiagColor(view->GetBackgroundColor());
}

std::string DiagUrl(const CefRefPtr<CefBrowser>& browser) {
  if (!browser || !browser->GetMainFrame()) {
    return "null";
  }
  return browser->GetMainFrame()->GetURL().ToString();
}

// PLT-SHELL-24M2FIX-B3: a container child that is a browser view must report
// which browser it hosts. Without the identity, `child[0]`/`child[1]` cannot be
// matched against the `views` map, so a log that says "the children are
// invisible" still cannot say which tab lost its pixels — and the mapping is
// not obvious, because the child order is the mount order while `views` is a
// map keyed by browser id.
std::string DiagChild(const CefRefPtr<CefView>& view) {
  std::string text = DiagView(view);
  if (!view) {
    return text;
  }
  const CefRefPtr<CefBrowserView> browser_view = view->AsBrowserView();
  if (!browser_view) {
    return text + " kind=other";
  }
  const CefRefPtr<CefBrowser> browser = browser_view->GetBrowser();
  return text + " kind=browser id=" +
         std::to_string(browser ? browser->GetIdentifier() : 0) + " url=" +
         DiagUrl(browser);
}

// PLT-SHELL-24M2FIX-C4: the window layer publishes CEF rects; the native
// titlebar adapter stays free of CEF types, so the two are translated here.
titlebar::ChromeRect ToChromeRect(const CefRect& rect) {
  return titlebar::ChromeRect{rect.x, rect.y, rect.width, rect.height};
}

titlebar::ChromeDecoration ToTitlebarDecoration(
    const window::ChromeDecoration& source) {
  titlebar::ChromeDecoration result;
  result.tabs.reserve(source.tabs.size());
  for (const window::TabDecoration& tab : source.tabs) {
    titlebar::TabDecoration item;
    item.bounds = ToChromeRect(tab.bounds);
    item.indicator = ToChromeRect(tab.indicator);
    item.active = tab.active;
    item.loading = tab.loading;
    result.tabs.push_back(item);
  }
  result.omnibox = ToChromeRect(source.omnibox);
  result.omnibox_focused = source.omnibox_focused;
  result.omnibox_field = ToChromeRect(source.omnibox_field);
  return result;
}

class ProductWindowDelegate final : public CefWindowDelegate,
                                    public CefBrowserViewDelegate {
 public:
  struct Host {
    std::function<void(CefRefPtr<CefBrowser> browser)> browser_created;
    std::function<void(CefRefPtr<CefBrowser> browser)> browser_destroyed;
    std::function<void(CefRefPtr<CefWindow> window)> window_created;
    std::function<bool()> can_close;
    std::function<void()> window_destroyed_view;
    std::function<void()> layout_changed;
    std::function<void(int)> release_browser_view;
    std::function<void(int)> browser_closed;
    std::function<bool(const CefKeyEvent&)> key_event;
    std::function<bool(int)> accelerator;
  };

  explicit ProductWindowDelegate(Host host) : host_(std::move(host)) {}
  void DetachHost() { host_ = {}; }
  void ReleaseBrowserView(int browser_id) {
    if (host_.release_browser_view) host_.release_browser_view(browser_id);
  }

  void CompleteBrowserClose(int browser_id) {
    if (host_.browser_closed) host_.browser_closed(browser_id);
  }

  void OnBrowserCreated(CefRefPtr<CefBrowserView>,
                        CefRefPtr<CefBrowser> browser) override {
    if (host_.browser_created) host_.browser_created(browser);
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView>,
                          CefRefPtr<CefBrowser> browser) override {
    if (host_.browser_destroyed) host_.browser_destroyed(browser);
  }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  bool WithStandardWindowButtons(CefRefPtr<CefWindow>) override { return true; }
  bool GetTitlebarHeight(CefRefPtr<CefWindow>, float* height) override {
    *height = titlebar::kTabStripHeight;
    return true;
  }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    if (host_.window_created) host_.window_created(window);
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    return host_.can_close ? host_.can_close() : false;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
    if (host_.window_destroyed_view) host_.window_destroyed_view();
  }

  bool OnKeyEvent(CefRefPtr<CefWindow>, const CefKeyEvent& event) override {
    return host_.key_event && host_.key_event(event);
  }
  bool OnAccelerator(CefRefPtr<CefWindow>, int command_id) override {
    return host_.accelerator && host_.accelerator(command_id);
  }
  void OnLayoutChanged(CefRefPtr<CefView>, const CefRect&) override {
    if (host_.layout_changed) host_.layout_changed();
  }

 private:
  Host host_;

  IMPLEMENT_REFCOUNTING(ProductWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(ProductWindowDelegate);
};

}  // namespace

struct AlloyProductHostMac::Impl {
  std::string initial_url;
  std::string title;
  CefRefPtr<CefClient> client;
  CefRefPtr<CefView> tab_strip_view;
  CefRefPtr<CefView> toolbar_view;
  Callbacks callbacks;
  CefRefPtr<CefPanel> container;
  std::map<int, CefRefPtr<CefBrowserView>> views;
  CefRefPtr<CefBrowserView> first_view;
  CefRefPtr<CefWindow> window;
  std::function<window::ChromeDecoration()> chrome_decoration;
  int active_browser_id = 0;
  int enforce_depth = 0;
  bool started = false;

  void BrowserCreated(CefRefPtr<CefBrowser> created) {
    if (!created || !created->GetHost()) {
      return;
    }
    const int browser_id = created->GetIdentifier();
    if (CefRefPtr<CefBrowserView> view =
            CefBrowserView::GetForBrowser(created)) {
      views[browser_id] = view;
    }
    // TabModel::CreateTab auto-activates, so every browser created for this
    // window becomes the visible tab. Before the window exists (first
    // browser) the view is visible by default once mounted.
    ShowBrowser(browser_id);
    DumpDiag("browser-created");
    if (callbacks.view_ready) callbacks.view_ready();
  }

  void ShowBrowser(int browser_id) {
    if (views.find(browser_id) == views.end()) {
      return;
    }
    // Visibility changes can re-enter layout; publish the target first.
    active_browser_id = browser_id;
    EnforceActiveVisible("show-browser");
    if (window) {
      window->Layout();
    }
    UpdateDraggableRegions();
    DumpDiag("show-browser");
  }

  void BrowserDestroyed(CefRefPtr<CefBrowser> destroyed) {
    if (!destroyed) {
      return;
    }
    const int browser_id = destroyed->GetIdentifier();
    const auto pending = views.find(browser_id);
    if (pending != views.end() && !pending->second)
      return;
    CompleteBrowserClose(browser_id);
  }

  void CompleteBrowserClose(int browser_id) {
    const auto found = views.find(browser_id);
    if (found != views.end()) {
      if (container && found->second) {
        container->RemoveChildView(found->second);
      }
      views.erase(found);
    }
    if (active_browser_id == browser_id) {
      active_browser_id = 0;
    }
    if (views.empty()) {
      if (window) {
        window->Close();
      }
      return;
    }
    // Surface the model's surviving replacement (controller notifies the app
    // first; this fallback keeps a view visible without app involvement).
    if (active_browser_id == 0)
      ShowBrowser(views.begin()->first);
    if (callbacks.view_ready)
      callbacks.view_ready();
  }

  void WindowCreated(CefRefPtr<CefWindow> created) {
    window = created;
    CefBoxLayoutSettings box;
    auto layout = window->SetToBoxLayout(box);
    if (tab_strip_view) {
      window->AddChildView(tab_strip_view);
    }
    if (toolbar_view) {
      window->AddChildView(toolbar_view);
    }
    container = CefPanel::CreatePanel(nullptr);
    // PLT-SHELL-24M2FIX-B: the content container must FILL its mounted child.
    // Under a box layout the browser view receives its preferred size, which is
    // zero height, so the page loaded and painted nothing — measured via
    // `alloy_cast_toolbar_mac`: container 1100x640, browser view 1100x0. Fill
    // layout mirrors AlloyContentViewHost's container (fill + mounted view),
    // which is the arrangement every harness probe uses.
    container->SetToFillLayout();
    window->AddChildView(container);
    layout->SetFlexForView(container, 1);
    // The first browser view is created in Start() before the window; mount
    // it now so its about:blank warmup is already under way.
    if (first_view) {
      container->AddChildView(first_view);
    }
    window->SetTitle(title);
    window->SetSize(CefSize(1100, 760));
    titlebar::ApplyMergedTitlebar(window->GetWindowHandle());
    // PLT-SHELL-24M2FIX-C5: the navigation row is a chrome surface now, not
    // white. Measured cause: a panel whose delegate does not repaint on theme
    // changes keeps the theme's default background, so the toolbar panel's own
    // SetBackgroundColor call never reached the screen (probe read
    // `child1 bg=0xffffffff` while the token was already #F9F9FF). Setting the
    // window theme's primary background moves the toolbar, the navigation
    // panel and its buttons together, instead of leaving white rectangles on a
    // tinted row.
    window->SetThemeColor(CEF_ColorPrimaryBackground,
                          window::chrome_palette::kToolbarBackground);
    // PLT-SHELL-24M2FIX-C4-a: the address field is a pill, and CEF paints the
    // textfield's own fill plus a 1px outline from the window theme colours.
    // Measured on the product before this: the field's white fill and its
    // 0xc7c7c7 outline covered the omnibox panel entirely, so the panel's pill
    // fill never reached the screen (screenshot rows 92-93 and 162-163 at the
    // pill's exact top and bottom edges). Setting the two theme colours makes
    // the field itself the pill surface; the rounded ends are then cut by the
    // native decoration. Set before Show(), as CefWindow::SetThemeColor
    // documents, and ThemeChanged() publishes the new colours.
    window->SetThemeColor(CEF_ColorTextfieldBackground,
                          window::chrome_palette::kOmniboxBackground);
    window->SetThemeColor(CEF_ColorTextfieldOutline,
                          window::chrome_palette::kOmniboxBackground);
    window->SetThemeColor(CEF_ColorTextfieldHover,
                          window::chrome_palette::kOmniboxBackground);
    // PLT-SHELL-24M2FIX-C10: CEF paints a RECTANGULAR focus ring for the
    // textfield (measured on the product: a 2 DIP blue box inset inside the
    // pill, rows 103-110/145-152 at logical x 1015). Setting the theme's ring
    // colour to the focused field surface removes that box; the rounded ring on
    // the pill's outline is drawn by the native decoration instead. Known cost,
    // registered in the roadmap: toolbar buttons lose their keyboard focus ring
    // on this surface (tab buttons keep theirs, the band is a different colour).
    window->SetThemeColor(CEF_ColorSysStateFocusRing,
                          window::chrome_palette::kOmniboxFocusedBackground);
    // The textfield sits inside a FocusableBorder in Views, and THAT is what
    // paints the rectangular outline seen on the product (measured: a 4 DIP
    // blue line inset ~5 DIP inside the pill). Both of its states are set to
    // the field's own surface so the rectangle blends away.
    window->SetThemeColor(CEF_ColorFocusableBorderFocused,
                          window::chrome_palette::kOmniboxFocusedBackground);
    window->SetThemeColor(CEF_ColorFocusableBorderUnfocused,
                          window::chrome_palette::kOmniboxBackground);
    window->ThemeChanged();
    window->Layout();
    UpdateDraggableRegions();
    window->Show();
    window->Activate();
    DumpDiag("window-created");
    if (callbacks.view_ready) callbacks.view_ready();
  }

  bool CanCloseWindow() const {
    if (views.empty()) {
      if (callbacks.before_close)
        callbacks.before_close();
      return true;
    }
    // A window close requests every tab, but must wait for their unload
    // decisions and asynchronous view release. One ready tab cannot authorize
    // destruction of its siblings.
    std::vector<CefRefPtr<CefBrowser>> closing;
    for (const auto &entry : views) {
      if (entry.second && entry.second->GetBrowser())
        closing.push_back(entry.second->GetBrowser());
    }
    for (const auto &browser : closing)
      browser->GetHost()->CloseBrowser(false);
    return false;
  }

  void ReleaseBrowserView(int browser_id) {
    const auto found = views.find(browser_id);
    if (found == views.end())
      return;
    // Retain a bounded closing slot until WindowClient::OnBeforeClose. CEF
    // may destroy the BrowserView before delivering OnBrowserDestroyed, so
    // that optional view callback cannot own final window completion.
    auto view = std::exchange(found->second, nullptr);
    if (!view)
      return;
    if (first_view && first_view->IsSame(view))
      first_view = nullptr;
    if (container && view)
      container->RemoveChildView(view);
    // Dropping the final view reference completes CEF close. OnBeforeClose
    // updates TabModel before NotifyBrowserClosed projects the replacement.
  }

  void WindowDestroyedView() {
    if (callbacks.before_close) callbacks.before_close();
    window = nullptr;
    container = nullptr;
    views.clear();
    first_view = nullptr;
    // The toolbar panels were only borrowed from the app assembly; without
    // this release their CefView wrappers would outlive CefShutdown (Impl
    // is destroyed after main's CefShutdown call).
    tab_strip_view = nullptr;
    toolbar_view = nullptr;
    if (callbacks.window_destroyed) {
      callbacks.window_destroyed();
    }
  }

  ~Impl() {
    // Failed-start and abnormal teardown: window destruction never ran, so
    // drop every CEF view reference here before CefShutdown audits wrappers.
    window = nullptr;
    container = nullptr;
    views.clear();
    first_view = nullptr;
    tab_strip_view = nullptr;
    toolbar_view = nullptr;
  }

  void UpdateDraggableRegions() {
    if (!window || !tab_strip_view)
      return;
    const CefRect strip = tab_strip_view->GetBounds();
    std::vector<CefDraggableRegion> regions;
    // Mark only genuinely empty strip space as draggable. Overlapping a
    // draggable band with control exclusions let macOS swallow clicks near
    // inactive tab close buttons despite their visible CEF button bounds.
    const int controls_right = strip.x + titlebar::kWindowControlsInset;
    int content_right = controls_right;
    int first_control_left = 0;
    const auto panel = tab_strip_view->AsPanel();
    if (panel) {
      for (std::size_t i = 0; i < panel->GetChildViewCount(); ++i) {
        auto child = panel->GetChildViewAt(i);
        if (!child->IsVisible())
          continue;
        CefPoint origin;
        if (!child->ConvertPointToWindow(origin)) {
          // A missing control coordinate cannot safely define free space, and
          // the tab geometry it would have produced is untrustworthy too.
          window->SetDraggableRegions({});
          RefreshChromeDecoration();
          return;
        }
        const auto size = child->GetSize();
        if (first_control_left == 0 || origin.x < first_control_left) {
          first_control_left = origin.x;
        }
        content_right = std::max(content_right, origin.x + size.width);
      }
    }
    // PLT-SHELL-24M2FIX-C5: the strip begins after the window controls, so the
    // band between them and the first tab is empty and must stay draggable --
    // otherwise the leading inset the reference build needs would turn into a
    // dead zone. The region stops exactly at the first control's left edge, the
    // same rule the trailing region already follows.
    if (first_control_left > controls_right) {
      CefDraggableRegion leading;
      leading.bounds = CefRect(controls_right, strip.y,
                               first_control_left - controls_right,
                               strip.height);
      leading.draggable = true;
      regions.push_back(leading);
    }
    const int strip_right = strip.x + strip.width;
    if (content_right < strip_right) {
      CefDraggableRegion blank;
      blank.bounds = CefRect(content_right, strip.y, strip_right - content_right,
                             strip.height);
      blank.draggable = true;
      regions.push_back(blank);
    }
    window->SetDraggableRegions(regions);
    RefreshChromeDecoration();
  }

  // PLT-SHELL-24M2FIX-C4: pushes the tab corners, tab loading indicator and
  // omnibox pill geometry to the native decoration. Republishing on every
  // layout pass is deliberate: the geometry is the CEF layout's output, so it
  // is only correct after that layout has run (same reasoning as
  // EnforceActiveVisible for view visibility).
  void RefreshChromeDecoration() {
    if (!window) {
      return;
    }
    window::ChromeDecoration published;
    if (chrome_decoration) {
      published = chrome_decoration();
    }
    titlebar::UpdateChromeDecoration(window->GetWindowHandle(),
                                     ToTitlebarDecoration(published));
  }

  // PLT-SHELL-24M2FIX-D: reports the state that decides what the user sees —
  // window/client geometry, every chrome view, the container's mounted
  // children, the active browser id, and each browser's real main-frame URL.
  // "views.size()" vs "mounted" is the pair that separates "the tab exists and
  // loaded" from "the tab's pixels are in the window".
  void DumpDiag(const char* when) {
    if (!DiagEnabled()) {
      return;
    }
    const std::size_t mounted =
        container ? container->GetChildViewCount() : std::size_t{0};
    DiagLog(std::string("== ") + when +
            " active=" + std::to_string(active_browser_id) +
            " browsers=" + std::to_string(views.size()) +
            " mounted=" + std::to_string(mounted));
    if (window) {
      DiagLog("  window " + DiagRect(window->GetBounds()) +
              " screen=" + DiagRect(window->GetBoundsInScreen()) +
              " client=" +
              DiagRect(window->GetClientAreaBoundsInScreen()));
    }
    DiagLog("  strip   " + DiagView(tab_strip_view));
    DiagLog("  toolbar " + DiagView(toolbar_view));
    DiagLog("  content " + DiagView(container));
    for (std::size_t i = 0; i < mounted; ++i) {
      DiagLog("  child[" + std::to_string(i) + "] " +
              DiagChild(container->GetChildViewAt(i)));
    }
    for (const auto& entry : views) {
      const CefRefPtr<CefBrowser> browser =
          entry.second ? entry.second->GetBrowser() : nullptr;
      DiagLog("  browser id=" + std::to_string(entry.first) + " " +
              DiagView(entry.second) + " url=" + DiagUrl(browser));
    }
    if (first_view) {
      DiagLog("  first_view " + DiagView(first_view) +
              " url=" + DiagUrl(first_view->GetBrowser()));
    }
  }

  // PLT-SHELL-24M2FIX-B3: "exactly the active tab's browser view is visible" is
  // an invariant CEF does not hold for us across a layout pass.
  //
  // Measured on the product, opening a second tab with the toolbar's new-tab
  // action: `content visible=1` while BOTH mounted browser views read back
  // `visible=0`, with `active_browser_id` still pointing at the new tab. The
  // container then paints its own white background and no page pixel reaches
  // the screen — the content area the user reports as "not displaying". Timed
  // dumps (`[s<t> t<ms>]` prefixes) place the flip inside CEF's layout work:
  // the dump is taken at the ENTRY of the layout callback, before any product
  // code for that event runs, and the flag is already 0 there.
  //
  // So the flag cannot be owned by a single write. `ShowBrowser()` is the only
  // place that decides WHICH tab should be visible, but CEF re-writes the flag
  // during layout, so the decision has to be re-applied afterwards — the same
  // shape as CefView::SetBackgroundColor, which cef_view.h documents as
  // "automatically reset when CefViewDelegate::OnThemeChanged is called" and
  // which the tab strip already re-applies from its delegate.
  //
  // Returns whether a bit actually had to be flipped, and records it: a
  // non-empty `enforce-visible` / `create-tab-enforce` line is direct evidence
  // that CEF had hidden the tab the user was looking at, and its absence is the
  // regression signal.
  bool EnforceActiveVisible(const char* when) {
    if (active_browser_id == 0 || enforce_depth > 2) {
      return false;
    }
    ++enforce_depth;
    bool flipped = false;
    for (const auto& entry : views) {
      if (!entry.second) {
        continue;
      }
      const bool want = (entry.first == active_browser_id);
      if (entry.second->IsVisible() != want) {
        entry.second->SetVisible(want);
        flipped = true;
      }
    }
    --enforce_depth;
    if (flipped) DumpDiag(when);
    return flipped;
  }
};

AlloyProductHostMac::AlloyProductHostMac(Dependencies dependencies,
                                         Callbacks callbacks) {
  auto impl = std::make_unique<Impl>();
  impl->initial_url = dependencies.initial_url;
  impl->title = dependencies.title;
  impl->client = dependencies.client;
  impl->tab_strip_view = dependencies.tab_strip_view;
  impl->toolbar_view = dependencies.toolbar_view;
  impl->chrome_decoration = std::move(dependencies.chrome_decoration);
  impl->callbacks = std::move(callbacks);

  ProductWindowDelegate::Host host;
  host.browser_created = [impl = impl.get()](CefRefPtr<CefBrowser> browser) {
    impl->BrowserCreated(std::move(browser));
  };
  host.browser_destroyed = [impl = impl.get()](CefRefPtr<CefBrowser> browser) {
    impl->BrowserDestroyed(std::move(browser));
  };
  host.window_created = [impl = impl.get()](CefRefPtr<CefWindow> window) {
    impl->WindowCreated(std::move(window));
  };
  host.can_close = [impl = impl.get()] { return impl->CanCloseWindow(); };
  host.window_destroyed_view = [impl = impl.get()] {
    impl->WindowDestroyedView();
  };

  host.layout_changed = [impl = impl.get()] {
    impl->UpdateDraggableRegions();
    impl->DumpDiag("layout-changed");
    if (impl->callbacks.layout_changed) impl->callbacks.layout_changed();
    // PLT-SHELL-24M2FIX-B3: the layout pass is where CEF re-writes browser-view
    // visibility, so it is also where the "exactly one tab on screen" invariant
    // has to be restored. The app callback runs first so `active_browser_id` is
    // already settled when the flag is asserted.
    static_cast<void>(impl->EnforceActiveVisible("enforce-visible"));
  };
  host.release_browser_view = [impl = impl.get()](int browser_id) {
    impl->ReleaseBrowserView(browser_id);
  };
  host.browser_closed = [impl = impl.get()](int browser_id) {
    impl->CompleteBrowserClose(browser_id);
  };
  host.key_event = impl->callbacks.key_event;
  host.accelerator = impl->callbacks.accelerator;

  view_delegate_ =
      CefRefPtr<CefBrowserViewDelegate>(new ProductWindowDelegate(host));
  window_delegate_ =
      CefRefPtr<CefWindowDelegate>(new ProductWindowDelegate(host));
  impl_ = std::move(impl);
}

AlloyProductHostMac::~AlloyProductHostMac() {
  static_cast<ProductWindowDelegate*>(view_delegate_.get())->DetachHost();
  static_cast<ProductWindowDelegate*>(window_delegate_.get())->DetachHost();
}

bool AlloyProductHostMac::Start() {
  CEF_REQUIRE_UI_THREAD();
  if (impl_->started) return false;
  CefBrowserSettings browser_settings;
  impl_->first_view = CefBrowserView::CreateBrowserView(
      impl_->client, impl_->initial_url, browser_settings, nullptr, nullptr,
      view_delegate_);
  if (!impl_->first_view) {
    return false;
  }
  CefWindow::CreateTopLevelWindow(window_delegate_);
  impl_->started = true;
  return true;
}

void AlloyProductHostMac::Close() {
  CEF_REQUIRE_UI_THREAD();
  if (impl_->callbacks.before_close) impl_->callbacks.before_close();
  bool requested = false;
  for (auto& entry : impl_->views) {
    if (entry.second && entry.second->GetBrowser() &&
        entry.second->GetBrowser()->GetHost()) {
      entry.second->GetBrowser()->GetHost()->CloseBrowser(true);
      requested = true;
    }
  }
  if (!requested && impl_->window) {
    impl_->window->Close();
  }
}

bool AlloyProductHostMac::HandleBrowserClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !impl_->views.count(browser->GetIdentifier()))
    return false;
  return CefPostTask(TID_UI,
                     base::BindOnce(&ProductWindowDelegate::ReleaseBrowserView,
                                    CefRefPtr<ProductWindowDelegate>(
                                        static_cast<ProductWindowDelegate *>(
                                            view_delegate_.get())),
                                    browser->GetIdentifier()));
}

void AlloyProductHostMac::RefreshTabChrome() {
  CEF_REQUIRE_UI_THREAD();
  if (!impl_->window)
    return;
  impl_->window->Layout();
  impl_->UpdateDraggableRegions();
}

void AlloyProductHostMac::NotifyBrowserClosed(int browser_id) {
  CEF_REQUIRE_UI_THREAD();
  if (!impl_->views.count(browser_id))
    return;
  CefPostTask(TID_UI,
              base::BindOnce(&ProductWindowDelegate::CompleteBrowserClose,
                             CefRefPtr<ProductWindowDelegate>(
                                 static_cast<ProductWindowDelegate *>(
                                     view_delegate_.get())),
                             browser_id));
}

bool AlloyProductHostMac::CreateTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!impl_->started || !impl_->container) {
    return false;
  }
  CefBrowserSettings browser_settings;
  CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
      impl_->client, url, browser_settings, nullptr, nullptr, view_delegate_);
  if (!view) {
    return false;
  }
  // PLT-SHELL-24M2FIX-B3: a new tab must not be visible until ShowBrowser()
  // makes it the active one. Measured on the product, this write is NOT what
  // decides the final state: CEF makes a freshly mounted browser view visible
  // regardless (the child reads back `visible=1` while it hosts no browser),
  // and CEF's own work while the view is mounted hides the view it has just
  // activated — so neither this statement nor ShowBrowser alone can hold the
  // flag. See EnforceActiveVisible() for where the invariant is actually kept.
  impl_->container->AddChildView(view);
  view->SetVisible(false);
  if (impl_->window) {
    impl_->window->Layout();
  }
  // The mount plus the layout above is where the product was measured losing
  // the tab it had just activated (both mounted browser views `visible=0` while
  // `active_browser_id` still pointed at the new tab). Re-assert here as well
  // as after every layout: `create-tab-enforce` / `enforce-visible` are emitted
  // only when a bit really had to be flipped, so their absence from a run is
  // the regression signal.
  static_cast<void>(impl_->EnforceActiveVisible("create-tab-enforce"));
  impl_->DumpDiag("create-tab");
  return true;
}

void AlloyProductHostMac::ShowBrowser(int browser_id) {
  CEF_REQUIRE_UI_THREAD();
  impl_->ShowBrowser(browser_id);
}

void AlloyProductHostMac::DumpDiagnostics(const char* when) {
  impl_->DumpDiag(when);
}

CefRefPtr<CefBrowser> AlloyProductHostMac::browser() const noexcept {
  if (!impl_) {
    return nullptr;
  }
  auto found = impl_->views.find(impl_->active_browser_id);
  if (found == impl_->views.end()) {
    return nullptr;
  }
  return found->second ? found->second->GetBrowser() : nullptr;
}

CefRefPtr<CefWindow> AlloyProductHostMac::window() const {
  return impl_->window;
}

CefRefPtr<CefBrowserView> AlloyProductHostMac::browser_view(
    int browser_id) const {
  const auto found = impl_->views.find(browser_id);
  return found == impl_->views.end() ? nullptr : found->second;
}

bool AlloyProductHostMac::started() const noexcept {
  return impl_ && impl_->started;
}

const std::string& AlloyProductHostMac::initial_url() const noexcept {
  static const std::string kEmpty;
  return impl_ ? impl_->initial_url : kEmpty;
}

}  // namespace crayon::browser::cef_shell::macos
