// MDV-03 contract tests: view modes, load status matrix, debounce and
// revision fencing, zero-network CSP golden, no-persistence surface.
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_mdv/mdv_viewer.h"

namespace {

using crayon::browser_mdv::kMdvCsp;
using crayon::browser_mdv::kRenderDebounceMs;
using crayon::browser_mdv::MdvLoadStatus;
using crayon::browser_mdv::MdvViewMode;
using crayon::browser_mdv::MdvViewerModel;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool ViewModeSwitching() {
  MdvViewerModel model;
  CHECK(model.view_mode() == MdvViewMode::kPreview);  // read-only default
  model.SetViewMode(MdvViewMode::kSource);
  CHECK(model.view_mode() == MdvViewMode::kSource);
  model.SetViewMode(MdvViewMode::kSplit);
  CHECK(model.view_mode() == MdvViewMode::kSplit);
  CHECK(model.load_status() == MdvLoadStatus::kEmpty);
  return true;
}

bool LoadStatusMatrix() {
  MdvViewerModel model;
  CHECK(model.LoadContent("", true, 0) == MdvLoadStatus::kEmpty);
  CHECK(model.has_document());
  CHECK(model.LoadContent("# Title\n", true, 10) == MdvLoadStatus::kLoaded);
  CHECK(model.rendered_html().find("<h1>Title</h1>") != std::string::npos);
  CHECK(model.LoadContent("\xED\xA0\x80", true, 20) == MdvLoadStatus::kInvalidUtf8);
  CHECK(model.rendered_html().empty());
  CHECK(model.LoadContent(std::string(6 * 1024 * 1024, 'a'), true, 30) ==
        MdvLoadStatus::kTooLarge);
  // The engine re-validates regardless of the caller's UTF-8 claim.
  CHECK(model.LoadContent("\xC0\xAF", true, 40) == MdvLoadStatus::kInvalidUtf8);
  return true;
}

bool DebounceMergesRequests() {
  MdvViewerModel model;
  model.LoadContent("# T\n", true, 0);
  const auto first = model.RequestRender(100);
  const auto second = model.RequestRender(100 + kRenderDebounceMs / 2);
  CHECK(first == second);  // merged inside the window; timer slides
  // The window is measured from the LAST request, so a new generation
  // needs the full quiet period after the merge.
  const auto third = model.RequestRender(100 + kRenderDebounceMs / 2 + kRenderDebounceMs + 1);
  CHECK(third != first);  // quiet period elapsed: new generation
  return true;
}

bool StaleRenderDropped() {
  MdvViewerModel model;
  model.LoadContent("# v1\n", true, 0);
  const auto stale = model.RequestRender(0);
  model.LoadContent("# v2\n", true, 500);
  CHECK(!model.DeliverRender(stale, "<h1>old</h1>"));
  CHECK(model.rendered_html().find("old") == std::string::npos);
  const auto current = model.RequestRender(600);
  CHECK(model.DeliverRender(current, "<h1>fresh</h1>"));
  CHECK(model.rendered_html() == "<h1>fresh</h1>");
  return true;
}

bool CloseDocumentInvalidatesInflight() {
  MdvViewerModel model;
  model.LoadContent("# doc\n", true, 0);
  const auto in_flight = model.RequestRender(0);
  model.CloseDocument();
  CHECK(!model.has_document());
  CHECK(model.load_status() == MdvLoadStatus::kEmpty);
  CHECK(!model.DeliverRender(in_flight, "<h1>ghost</h1>"));
  CHECK(model.rendered_html().empty());
  CHECK(model.LoadContent("# next\n", true, 100) == MdvLoadStatus::kLoaded);
  return true;
}

bool CspGolden() {
  CHECK(std::string(kMdvCsp) ==
        "default-src 'none'; script-src 'self'; style-src 'self'; "
        "img-src 'none'; connect-src 'none'; font-src 'none'; "
        "media-src 'none'; object-src 'none'; frame-src 'none'; "
        "base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
  CHECK(std::string(crayon::browser_mdv::kResourceAppHtml) == "/app.html");
  CHECK(std::string(crayon::browser_mdv::kResourceAppCss) == "/app.css");
  CHECK(std::string(crayon::browser_mdv::kResourceAppJs) == "/app.js");
  return true;
}

bool StormInvariants() {
  std::uint64_t seed = 0xFEEDFACE0DD55EEDULL;
  auto next = [&seed]() {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return seed;
  };
  MdvViewerModel model;
  std::uint64_t clock = 0;
  std::uint64_t last_revision = 0;
  for (int step = 0; step < 5000; ++step) {
    clock += next() % 200;
    switch (next() % 5) {
      case 0:
        model.LoadContent(next() % 2 ? "# doc\n" : "", true, clock);
        break;
      case 1: {
        const auto rev = model.RequestRender(clock);
        CHECK(rev >= last_revision);
        last_revision = rev;
        break;
      }
      case 2:
        static_cast<void>(model.DeliverRender(next() % 4, "<p>storm</p>"));
        break;
      case 3:
        model.SetViewMode(static_cast<MdvViewMode>(next() % 3));
        break;
      default:
        if (next() % 8 == 0) {
          model.CloseDocument();
        }
        break;
    }
    CHECK(model.current_revision() >= last_revision);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = ViewModeSwitching() && LoadStatusMatrix() && DebounceMergesRequests() &&
                  StaleRenderDropped() && CloseDocumentInvalidatesInflight() && CspGolden() &&
                  StormInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "mdv_viewer_test passed\n";
  return EXIT_SUCCESS;
}
