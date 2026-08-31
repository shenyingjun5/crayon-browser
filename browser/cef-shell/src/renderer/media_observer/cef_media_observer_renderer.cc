#include "renderer/media_observer/cef_media_observer_renderer.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "include/wrapper/cef_helpers.h"
#include "ipc/media_observation_cef_message.h"

namespace crayon::browser::cef_shell::renderer {
namespace {

using ::crayon::cef_shell::renderer::ClassifySourceUrl;
using ::crayon::cef_shell::renderer::MediaObservation;
using ::crayon::cef_shell::renderer::MediaPlaybackState;
using ::crayon::cef_shell::renderer::MediaSourceKind;
using ::crayon::cef_shell::renderer::ObserveResult;

constexpr char kExtensionName[] = "crayon/media-observer-v1";
constexpr char kNativeFunction[] = "crayonMediaObservationNative";
constexpr char kExtensionCode[] =
    "native function crayonMediaObservationNative();";

// This script only observes media facts. It never calls play(), click(),
// currentTime/rate setters, or filters by host/ad semantics (BR-009/010).
constexpr char kCollectorScript[] = R"JS(
(() => {
  const installedKey = Symbol.for('crayon.media-observer.v1.installed');
  if (globalThis[installedKey]) return;
  globalThis[installedKey] = true;
  const emitNative = (...facts) => {
    const nativeFunction = globalThis.crayonMediaObservationNative;
    if (typeof nativeFunction === 'function') nativeFunction(...facts);
  };
  const ids = new WeakMap();
  const attached = new WeakSet();
  let nextId = 1;
  let tracked = 0;
  const maxTracked = 16;
  const visibleFraction = (element) => {
    const rect = element.getBoundingClientRect();
    if (!(rect.width > 0 && rect.height > 0)) return 0;
    const style = globalThis.getComputedStyle(element);
    if (style.display === 'none' || style.visibility === 'hidden') return 0;
    const width = Math.max(0, Math.min(rect.right, innerWidth) - Math.max(rect.left, 0));
    const height = Math.max(0, Math.min(rect.bottom, innerHeight) - Math.max(rect.top, 0));
    return Math.max(0, Math.min(1, (width * height) / (rect.width * rect.height)));
  };
  const emit = (element, encrypted = false) => {
    const id = ids.get(element);
    if (!id) return;
    const state = element.ended ? 3 : (element.paused ? 2 : 1);
    const stream = typeof globalThis.MediaStream === 'function' &&
                   element.srcObject instanceof globalThis.MediaStream;
    const source = stream ? '' : String(element.currentSrc || element.src || '');
    emitNative(id, state, stream ? 2 : 0, source,
               visibleFraction(element), Number(element.currentTime) || 0,
               Boolean(encrypted));
  };
  const attach = (element) => {
    if (attached.has(element) || tracked >= maxTracked) return;
    attached.add(element);
    ids.set(element, nextId++);
    tracked += 1;
    for (const name of ['play', 'playing', 'pause', 'ended', 'timeupdate',
                        'loadedmetadata', 'durationchange']) {
      element.addEventListener(name, () => emit(element), {passive: true});
    }
    element.addEventListener('encrypted', () => emit(element, true),
                             {passive: true});
    emit(element);
  };
  const scan = (root) => {
    if (root instanceof HTMLMediaElement) attach(root);
    if (root && typeof root.querySelectorAll === 'function') {
      for (const element of root.querySelectorAll('video,audio')) attach(element);
    }
  };
  scan(document);
  new MutationObserver((records) => {
    for (const record of records) for (const node of record.addedNodes) scan(node);
  }).observe(document, {childList: true, subtree: true});
  globalThis.setInterval(() => {
    for (const element of document.querySelectorAll('video,audio')) emit(element);
  }, 250);
})();
)JS";

bool IsInt(CefRefPtr<CefV8Value> value) {
  return value && (value->IsInt() || value->IsUInt());
}

bool IsNumber(CefRefPtr<CefV8Value> value) {
  return value && (value->IsDouble() || value->IsInt() || value->IsUInt());
}

}  // namespace

class CefMediaObserverRenderer::NativeHandler final : public CefV8Handler {
 public:
  explicit NativeHandler(CefMediaObserverRenderer* owner) : owner_(owner) {}

  bool Execute(const CefString& name, CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    static_cast<void>(object);
    static_cast<void>(retval);
    if (name != kNativeFunction || !owner_) return false;
    return owner_->HandleNativeObservation(arguments, &exception);
  }

 private:
  CefMediaObserverRenderer* owner_;

  IMPLEMENT_REFCOUNTING(NativeHandler);
  DISALLOW_COPY_AND_ASSIGN(NativeHandler);
};

CefMediaObserverRenderer::CefMediaObserverRenderer()
    : native_handler_(new NativeHandler(this)) {}

CefMediaObserverRenderer::~CefMediaObserverRenderer() = default;

void CefMediaObserverRenderer::OnWebKitInitialized() {
  CefRegisterExtension(kExtensionName, kExtensionCode, native_handler_);
}

void CefMediaObserverRenderer::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (!browser || !frame || !frame->IsMain()) return;
  browsers_.try_emplace(browser->GetIdentifier());
  InstallCollector(frame);
}

void CefMediaObserverRenderer::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                                 CefRefPtr<CefFrame> frame) {
  CEF_REQUIRE_RENDERER_THREAD();
  // A cross-origin navigation can create the new renderer context before the
  // old context is released. Navigation fencing already cleared old facts;
  // tearing down by browser id here would also kill the new context. Final
  // teardown belongs to OnBrowserDestroyed.
  static_cast<void>(browser);
  static_cast<void>(frame);
}

void CefMediaObserverRenderer::OnBrowserDestroyed(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (browser) browsers_.erase(browser->GetIdentifier());
}

bool CefMediaObserverRenderer::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (!message || message->GetName() != media_ipc::kAdvanceMessageName) {
    return false;
  }
  if (source_process != PID_BROWSER || !browser || !frame || !frame->IsMain()) {
    return true;
  }
  const auto navigation_id = media_ipc::ReadAdvanceMessage(message);
  if (!navigation_id) return true;
  auto [iterator, inserted] = browsers_.try_emplace(browser->GetIdentifier());
  if (!inserted && iterator->second.observer.torn_down()) {
    iterator->second = BrowserState{};
  }
  iterator->second.navigation_id = *navigation_id;
  iterator->second.observer.AdvanceNavigation(*navigation_id);
  InstallCollector(frame);
  return true;
}

bool CefMediaObserverRenderer::HandleNativeObservation(
    const CefV8ValueList& arguments, CefString* exception) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (arguments.size() != 7 || !IsInt(arguments[0]) || !IsInt(arguments[1]) ||
      !IsInt(arguments[2]) || !arguments[3]->IsString() ||
      !IsNumber(arguments[4]) || !IsNumber(arguments[5]) ||
      !arguments[6]->IsBool()) {
    if (exception) *exception = "invalid media observation";
    return true;
  }
  CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
  CefRefPtr<CefBrowser> browser = context ? context->GetBrowser() : nullptr;
  CefRefPtr<CefFrame> frame = context ? context->GetFrame() : nullptr;
  if (!browser || !frame || !frame->IsMain()) return true;
  const auto found = browsers_.find(browser->GetIdentifier());
  if (found == browsers_.end() || found->second.navigation_id == 0 ||
      found->second.observer.torn_down()) {
    return true;
  }
  const int element_id = arguments[0]->GetIntValue();
  const int playback = arguments[1]->GetIntValue();
  const int source_tag = arguments[2]->GetIntValue();
  const double visible = arguments[4]->GetDoubleValue();
  const double current_time = arguments[5]->GetDoubleValue();
  if (element_id <= 0 ||
      playback < static_cast<int>(MediaPlaybackState::kIdle) ||
      playback > static_cast<int>(MediaPlaybackState::kEnded) ||
      (source_tag != 0 && source_tag != 2) || !std::isfinite(visible) ||
      !std::isfinite(current_time) || current_time < 0.0) {
    return true;
  }
  std::string source_url = arguments[3]->GetStringValue().ToString();
  std::string normalized;
  MediaSourceKind source_kind = MediaSourceKind::kUnknown;
  if (source_tag == 2) {
    source_url.clear();
    source_kind = MediaSourceKind::kMediaStream;
  } else {
    source_kind = ClassifySourceUrl(source_url, &normalized);
    if (source_kind == MediaSourceKind::kBlobUrl) {
      source_url.clear();
    } else if (source_kind == MediaSourceKind::kHttpUrl) {
      source_url = std::move(normalized);
    } else {
      source_url.clear();
    }
  }
  MediaObservation observation;
  observation.navigation_id = found->second.navigation_id;
  observation.element_id = static_cast<std::uint32_t>(element_id);
  observation.playback = static_cast<MediaPlaybackState>(playback);
  observation.source_kind = source_kind;
  observation.source_url = std::move(source_url);
  observation.visible_fraction = visible;
  observation.current_time_seconds = current_time;
  if (found->second.observer.Observe(observation) != ObserveResult::kAccepted) {
    return true;
  }
  auto eligible =
      found->second.observer.FindEligible(found->second.navigation_id);
  if (eligible && eligible->element_id == observation.element_id) {
    observation = std::move(*eligible);
  }
  frame->SendProcessMessage(
      PID_BROWSER,
      media_ipc::CreateObservationMessage(media_ipc::MediaObservationEnvelope{
          std::move(observation), arguments[6]->GetBoolValue()}));
  return true;
}

void CefMediaObserverRenderer::InstallCollector(CefRefPtr<CefFrame> frame) {
  if (frame && frame->IsMain()) {
    frame->ExecuteJavaScript(kCollectorScript, frame->GetURL(), 1);
  }
}

}  // namespace crayon::browser::cef_shell::renderer
