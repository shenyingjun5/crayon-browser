#include <iostream>

#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_product_strings/product_strings.h"

namespace {

using crayon::browser::localization::AppLocale;
using crayon::browser::localization::SnapshotFor;
using crayon::browser::product_strings::BuildProductStrings;
using crayon::browser::product_strings::ProductStringsAreComplete;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool AllLocalesBuildCompleteProductSurfaces() {
  for (const AppLocale locale :
       {AppLocale::kEnUs, AppLocale::kZhCn, AppLocale::kZhTw}) {
    const auto windows = BuildProductStrings(
        SnapshotFor(locale), crayon::browser_mdv::MdvShortcutPlatform::kWindows);
    const auto macos = BuildProductStrings(
        SnapshotFor(locale), crayon::browser_mdv::MdvShortcutPlatform::kMacOS);
    CHECK(windows.has_value());
    CHECK(macos.has_value());
    CHECK(ProductStringsAreComplete(*windows));
    CHECK(ProductStringsAreComplete(*macos));
    CHECK(windows->new_tab.language == SnapshotFor(locale).html_language);
    CHECK(windows->mdv.language == SnapshotFor(locale).html_language);
    CHECK(windows->mdv.shortcut_platform ==
          crayon::browser_mdv::MdvShortcutPlatform::kWindows);
    CHECK(macos->mdv.shortcut_platform ==
          crayon::browser_mdv::MdvShortcutPlatform::kMacOS);
  }
  return true;
}

bool TraditionalAndEnglishSamplesAreExact() {
  const auto traditional = BuildProductStrings(
      SnapshotFor(AppLocale::kZhTw),
      crayon::browser_mdv::MdvShortcutPlatform::kWindows);
  CHECK(traditional.has_value());
  CHECK(traditional->product_name == "蠟筆 AI Agent 投影瀏覽器");
  CHECK(traditional->new_tab.document_title == "蠟筆瀏覽器");
  CHECK(traditional->mdv.view_source == "原始碼");
  CHECK(traditional->page_markdown.save_cancelled_status == "已取消儲存");
  CHECK(traditional->cast.cast_code_label == "投影碼");

  const auto english = BuildProductStrings(
      SnapshotFor(AppLocale::kEnUs),
      crayon::browser_mdv::MdvShortcutPlatform::kMacOS);
  CHECK(english.has_value());
  CHECK(english->product_name == "Crayon AI Agent Cast Browser");
  CHECK(english->cast.playback_failed == "Control failed");
  return true;
}

bool BuiltInDocumentsUseTheSnapshotHtmlLanguage() {
  for (const AppLocale locale :
       {AppLocale::kEnUs, AppLocale::kZhCn, AppLocale::kZhTw}) {
    const auto strings = BuildProductStrings(
        SnapshotFor(locale), crayon::browser_mdv::MdvShortcutPlatform::kWindows);
    CHECK(strings.has_value());

    const std::string expected =
        std::string{"<html lang=\""} +
        std::string{SnapshotFor(locale).html_language} + "\"";
    const auto new_tab_model = crayon::browser_new_tab::BuildNewTabPageModel(
        crayon::browser_new_tab::NewTabProfileMode::kRegular, {});
    const std::string new_tab = crayon::browser_new_tab::RenderNewTabDocument(
        new_tab_model, strings->new_tab);
    CHECK(new_tab.find(expected) != std::string::npos);

    const std::string mdv = crayon::browser_mdv::RenderMdvDocument(
        crayon::browser_mdv::MdvPageSnapshot{}, strings->mdv);
    CHECK(mdv.find(expected) != std::string::npos);
  }
  return true;
}

}  // namespace

int main() {
  return AllLocalesBuildCompleteProductSurfaces() &&
                 TraditionalAndEnglishSamplesAreExact() &&
                 BuiltInDocumentsUseTheSnapshotHtmlLanguage()
             ? 0
             : 1;
}
