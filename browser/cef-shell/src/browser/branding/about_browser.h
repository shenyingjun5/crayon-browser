#ifndef CRAYON_BROWSER_CEF_SHELL_BROWSER_BRANDING_ABOUT_BROWSER_H_
#define CRAYON_BROWSER_CEF_SHELL_BROWSER_BRANDING_ABOUT_BROWSER_H_

#include <string>

#include "crayon/browser_localization/locale_catalog.h"
#include "include/cef_id_mappers.h"
#include "include/cef_resource_bundle_handler.h"

namespace crayon::browser::cef_shell::branding {

// CEF can request strings on multiple threads. The catalog is immutable and
// all unrelated Chromium strings/resources retain the upstream behavior.
class AboutBrowserResources final : public CefResourceBundleHandler {
 public:
  explicit AboutBrowserResources(localization::AppLocale locale)
      : catalog_(locale) {}

  bool GetLocalizedString(int string_id, CefString& value) override {
    const int kAboutId = cef_id_for_pack_string_name("IDS_ABOUT");
    const int kAboutMacId = cef_id_for_pack_string_name("IDS_ABOUT_MAC");
    if (string_id <= 0 || (string_id != kAboutId && string_id != kAboutMacId)) {
      return false;
    }
    const auto label = catalog_.Find("app.about");
    if (!label) {
      return false;
    }
    value = std::string(*label);
    return true;
  }

  bool GetDataResource(int, void*&, size_t&) override { return false; }
  bool GetDataResourceForScale(int, ScaleFactor, void*&, size_t&) override {
    return false;
  }

 private:
  const localization::LocaleCatalog catalog_;
  IMPLEMENT_REFCOUNTING(AboutBrowserResources);
  DISALLOW_COPY_AND_ASSIGN(AboutBrowserResources);
};

}  // namespace crayon::browser::cef_shell::branding

#endif  // CRAYON_BROWSER_CEF_SHELL_BROWSER_BRANDING_ABOUT_BROWSER_H_
