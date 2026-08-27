// MDV-13 contract tests: preview image classification matrix
// (cloud https / validated local opaque route / placeholder) with an
// injected filesystem probe — zero real IO.

#include "crayon/browser_mdv/mdv_images.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

using crayon::browser_mdv::HasWhitelistedImageExtension;
using crayon::browser_mdv::LocalImageProbe;
using crayon::browser_mdv::PreparePreviewHtml;

int g_failures = 0;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      ++g_failures;                                         \
    }                                                       \
  } while (false)

/// Engine marker for one image reference.
std::string Marker(const std::string& raw, const std::string& alt) {
  return "<img class=\"md-img\" src=\"mdv-img:0\" data-mdv-raw=\"" + raw +
         "\" alt=\"" + alt + "\">";
}

/// Probe: known sizes for listed paths; anything else does not exist.
LocalImageProbe FakeProbe(
    const std::vector<std::pair<std::string, std::uint64_t>>& known) {
  return [known](const std::string& path, std::uint64_t* size) {
    for (const auto& entry : known) {
      if (entry.first == path) {
        *size = entry.second;
        return true;
      }
    }
    return false;
  };
}

void TestExtensionWhitelist() {
  for (const char* ext :
       {"a.png", "b.jpg", "c.jpeg", "d.gif", "e.webp", "f.bmp", "g.svg"}) {
    CHECK(HasWhitelistedImageExtension(ext));
  }
  CHECK(!HasWhitelistedImageExtension("x.exe"));
  CHECK(!HasWhitelistedImageExtension("no-extension"));
  CHECK(!HasWhitelistedImageExtension("a.png/exe"));  // dot in the tail
  // Case-insensitive.
  CHECK(HasWhitelistedImageExtension("A.PNG"));
}

void TestCloudHttpsLoadsDirectly() {
  std::vector<std::string> images;
  const std::string out =
      PreparePreviewHtml(Marker("https://img.example/pic.png", "示例图"),
                         "D:/docs", FakeProbe({}), &images);
  CHECK(out.find("src=\"https://img.example/pic.png\"") != std::string::npos);
  CHECK(out.find("data-mdv-raw") == std::string::npos);
  CHECK(images.empty());
}

void TestHttpDowngradeAndSchemesPlaceholder() {
  std::vector<std::string> images;
  for (const char* raw :
       {"http://img.example/x.png", "data:image/png;base64,AAAA",
        "javascript:alert(1)"}) {
    const std::string out = PreparePreviewHtml(Marker(raw, "alt"), "D:/docs",
                                               FakeProbe({}), &images);
    CHECK(out.find("md-img-placeholder") != std::string::npos);
    CHECK(out.find("<img") == std::string::npos);
    CHECK(images.empty());
  }
}

void TestLocalValidatedToOpaqueRoute() {
  std::vector<std::string> images;
  const std::string out =
      PreparePreviewHtml(Marker("./pic.png", "本地图"), "D:/docs",
                         FakeProbe({{"D:/docs/pic.png", 100}}), &images);
  CHECK(out.find("src=\"/img/0\"") != std::string::npos);
  CHECK(out.find("pic.png") == std::string::npos ||
        out.find("alt=\"本地图\"") != std::string::npos);
  CHECK(images.size() == 1 && images[0] == "D:/docs/pic.png");
}

void TestTraversalAndAbsolutesRejected() {
  std::vector<std::string> images;
  // .. escape must stay a placeholder even if the probe would accept it.
  const std::string out =
      PreparePreviewHtml(Marker("../outside/pic.png", "逃逸"), "D:/docs",
                         FakeProbe({{"D:/outside/pic.png", 10}}), &images);
  CHECK(out.find("md-img-placeholder") != std::string::npos);
  CHECK(out.find("/img/") == std::string::npos);
  CHECK(images.empty());

  // Absolute path inside the document directory is fine.
  std::vector<std::string> images2;
  const std::string abs_ok =
      PreparePreviewHtml(Marker("D:/docs/sub/a.png", "ok"), "D:/docs",
                         FakeProbe({{"D:/docs/sub/a.png", 50}}), &images2);
  CHECK(abs_ok.find("src=\"/img/0\"") != std::string::npos);
  CHECK(images2.size() == 1);

  // Absolute path outside the document directory rejects.
  std::vector<std::string> images3;
  const std::string abs_bad =
      PreparePreviewHtml(Marker("C:/Windows/system.dll", "bad"), "D:/docs",
                         FakeProbe({{"C:/Windows/system.dll", 10}}), &images3);
  CHECK(abs_bad.find("md-img-placeholder") != std::string::npos);
  CHECK(images3.empty());
}

void TestMissingOversizedAndFixtureDirPlaceholder() {
  std::vector<std::string> images;
  // Missing file.
  const std::string missing = PreparePreviewHtml(
      Marker("./gone.png", "缺"), "D:/docs", FakeProbe({}), &images);
  CHECK(missing.find("md-img-placeholder") != std::string::npos);
  // Oversized file.
  const std::string huge = PreparePreviewHtml(
      Marker("./big.png", "大"), "D:/docs",
      FakeProbe({{"D:/docs/big.png", 21 * 1024 * 1024}}), &images);
  CHECK(huge.find("md-img-placeholder") != std::string::npos);
  // Fixture mode (no doc dir): even valid shapes placeholder.
  const std::string fixture = PreparePreviewHtml(
      Marker("./pic.png", "fix"), "", FakeProbe({{"pic.png", 1}}), &images);
  CHECK(fixture.find("md-img-placeholder") != std::string::npos);
  CHECK(images.empty());
}

void TestNonWhitelistedExtensionRejected() {
  std::vector<std::string> images;
  const std::string out =
      PreparePreviewHtml(Marker("./notes.txt", "文本"), "D:/docs",
                         FakeProbe({{"D:/docs/notes.txt", 10}}), &images);
  CHECK(out.find("md-img-placeholder") != std::string::npos);
  CHECK(images.empty());
}

}  // namespace

int main() {
  TestExtensionWhitelist();
  TestCloudHttpsLoadsDirectly();
  TestHttpDowngradeAndSchemesPlaceholder();
  TestLocalValidatedToOpaqueRoute();
  TestTraversalAndAbsolutesRejected();
  TestMissingOversizedAndFixtureDirPlaceholder();
  TestNonWhitelistedExtensionRejected();
  if (g_failures != 0) {
    std::cerr << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "ALL TESTS PASSED\n";
  return 0;
}
