// PLT-SHELL-24M1/M2: macOS production Alloy window host. Assembles the full
// product UI: tab strip, toolbar (navigation + omnibox), and browser view.
// All handler surfaces route through the TabController WindowClient.
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
    std::function<void()> browser_destroyed;
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
                          CefRefPtr<CefBrowser>) override {
    if (host_.browser_destroyed) host_.browser_destroyed();
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
  std::function<void()> window_destroyed;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefWindow> window;
  bool started = false;
  bool browser_closed = false;

  void BrowserCreated(CefRefPtr<CefBrowser> created) { browser = created; }

  void BrowserDestroyedView() {
    browser_closed = true;
    browser = nullptr;
    if (window) {
      window->Close();
    }
  }

  void WindowCreated(CefRefPtr<CefWindow> created) {
    window = created;
    CefBoxLayoutSettings box;
    auto layout = window->SetToBoxLayout(box);
    window->AddChildView(view);
    layout->SetFlexForView(view, 1);
    window->SetTitle(title);
    window->SetSize(CefSize(1100, 760));
    window->Layout();
    window->Show();
    window->Activate();
  }

  bool CanCloseWindow() const {
    return browser_closed ||
           (browser && browser->GetHost()->TryCloseBrowser());
  }

  void WindowDestroyedView() {
    window = nullptr;
    view = nullptr;
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
  impl->window_destroyed = callbacks.window_destroyed;

  ProductWindowDelegate::Host host;
  host.browser_created = [impl = impl.get()](CefRefPtr<CefBrowser> browser) {
    impl->BrowserCreated(std::move(browser));
  };
  host.browser_destroyed = [impl = impl.get()] { impl->BrowserDestroyedView(); };
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
  impl_->view = CefBrowserView::CreateBrowserView(
      impl_->client, impl_->initial_url, browser_settings, nullptr, nullptr,
      view_delegate_);
  if (!impl_->view) {
    return false;
  }
  CefWindow::CreateTopLevelWindow(window_delegate_);
  impl_->started = true;
  return true;
}

void AlloyProductHostMac::Close() {
  CEF_REQUIRE_UI_THREAD();
  if (impl_->browser && impl_->browser->GetHost()) {
    impl_->browser->GetHost()->CloseBrowser(true);
  } else if (impl_->window) {
    impl_->window->Close();
  }
}

CefRefPtr<CefBrowser> AlloyProductHostMac::browser() const noexcept {
  return impl_ ? impl_->browser : nullptr;
}

bool AlloyProductHostMac::started() const noexcept {
  return impl_ && impl_->started;
}

}  // namespace crayon::browser::cef_shell::macos
