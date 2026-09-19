// PLT-SHELL-24M1/M2: macOS production Alloy window host. Assembles the
// product layout (tab strip, toolbar, content container) and owns the
// per-tab browser views. All handler surfaces route through the
// TabController WindowClient; the host holds no business logic.
#include "macos/alloy_product_host_mac.h"

#include <map>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"
#include "macos/alloy_titlebar_mac.h"

namespace crayon::browser::cef_shell::macos {
namespace {

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
    std::function<bool(const CefKeyEvent&)> key_event;
    std::function<bool(int)> accelerator;
  };

  explicit ProductWindowDelegate(Host host) : host_(std::move(host)) {}
  void DetachHost() { host_ = {}; }

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
  int active_browser_id = 0;
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
    if (callbacks.view_ready) callbacks.view_ready();
  }

  void ShowBrowser(int browser_id) {
    if (views.find(browser_id) == views.end()) {
      return;
    }
    for (auto& entry : views) {
      if (entry.second) {
        entry.second->SetVisible(entry.first == browser_id);
      }
    }
    active_browser_id = browser_id;
    if (window) {
      window->Layout();
    }
  }

  void BrowserDestroyed(CefRefPtr<CefBrowser> destroyed) {
    if (!destroyed) {
      return;
    }
    const int browser_id = destroyed->GetIdentifier();
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
    ShowBrowser(views.begin()->first);
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
    CefBoxLayoutSettings container_settings;
    auto container_layout = container->SetToBoxLayout(container_settings);
    static_cast<void>(container_layout);
    window->AddChildView(container);
    layout->SetFlexForView(container, 1);
    // The first browser view is created in Start() before the window; mount
    // it now so its about:blank warmup is already under way.
    if (first_view) {
      container->AddChildView(first_view);
    }
    // PLT-SHELL-24M2UIP-c: the tab strip row doubles as the titlebar; the
    // native title text is hidden and the traffic lights move into the
    // strip's leading inset. Applied before Show so the first layout is
    // already full-size.
    titlebar::ApplyMergedTitlebar(window->GetWindowHandle(),
                                  /*strip_height=*/40.0);
    window->SetSize(CefSize(1100, 760));
    window->Layout();
    window->Show();
    window->Activate();
    // Re-apply once on screen: the traffic-light frames only exist after
    // the window is visible.
    titlebar::ApplyMergedTitlebar(window->GetWindowHandle(),
                                  /*strip_height=*/40.0);
    if (callbacks.view_ready) callbacks.view_ready();
  }

  bool CanCloseWindow() const {
    if (views.empty()) {
      if (callbacks.before_close) callbacks.before_close();
      return true;
    }
    // Drain one close request per attempt; the result MUST be propagated:
    // TryCloseBrowser returning true means the browser is already closing
    // (or closed) and the window close may proceed. Returning false here
    // after a synchronous close aborts the window and wedges the teardown.
    auto found = views.find(active_browser_id);
    if (found == views.end()) {
      found = views.begin();
    }
    if (found != views.end() && found->second &&
        found->second->GetBrowser() && found->second->GetBrowser()->GetHost()) {
      const bool allowed =
          found->second->GetBrowser()->GetHost()->TryCloseBrowser();
      if (allowed && callbacks.before_close) callbacks.before_close();
      return allowed;
    }
    return false;
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
};

AlloyProductHostMac::AlloyProductHostMac(Dependencies dependencies,
                                         Callbacks callbacks) {
  auto impl = std::make_unique<Impl>();
  impl->initial_url = dependencies.initial_url;
  impl->title = dependencies.title;
  impl->client = dependencies.client;
  impl->tab_strip_view = dependencies.tab_strip_view;
  impl->toolbar_view = dependencies.toolbar_view;
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

  host.layout_changed = impl->callbacks.layout_changed;
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
  impl_->container->AddChildView(view);
  view->SetVisible(false);
  if (impl_->window) {
    impl_->window->Layout();
  }
  return true;
}

void AlloyProductHostMac::ShowBrowser(int browser_id) {
  CEF_REQUIRE_UI_THREAD();
  impl_->ShowBrowser(browser_id);
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
