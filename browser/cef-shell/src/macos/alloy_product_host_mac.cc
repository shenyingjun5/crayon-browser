// PLT-SHELL-24M1/M2: macOS production Alloy window host. Assembles the
// product layout (tab strip, toolbar, content container) and owns the
// per-tab browser views. All handler surfaces route through the
// TabController WindowClient; the host holds no business logic.
#include <map>
#include <utility>

#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

#include "macos/alloy_product_host_mac.h"

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
  };

  explicit ProductWindowDelegate(Host host) : host_(std::move(host)) {}

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
  std::function<void()> window_destroyed;
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
    window->SetTitle(title);
    window->SetSize(CefSize(1100, 760));
    window->Layout();
    window->Show();
    window->Activate();
  }

  bool CanCloseWindow() const {
    if (views.empty()) {
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
      return found->second->GetBrowser()->GetHost()->TryCloseBrowser();
    }
    return false;
  }

  void WindowDestroyedView() {
    window = nullptr;
    container = nullptr;
    views.clear();
    first_view = nullptr;
    if (window_destroyed) {
      window_destroyed();
    }
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
  impl->window_destroyed = callbacks.window_destroyed;

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

  view_delegate_ =
      CefRefPtr<CefBrowserViewDelegate>(new ProductWindowDelegate(host));
  window_delegate_ =
      CefRefPtr<CefWindowDelegate>(new ProductWindowDelegate(host));
  impl_ = std::move(impl);
}

AlloyProductHostMac::~AlloyProductHostMac() = default;

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

bool AlloyProductHostMac::started() const noexcept {
  return impl_ && impl_->started;
}

const std::string& AlloyProductHostMac::initial_url() const noexcept {
  static const std::string kEmpty;
  return impl_ ? impl_->initial_url : kEmpty;
}

}  // namespace crayon::browser::cef_shell::macos
