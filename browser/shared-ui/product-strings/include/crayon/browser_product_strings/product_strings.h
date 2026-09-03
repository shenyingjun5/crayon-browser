#ifndef CRAYON_BROWSER_SHARED_UI_PRODUCT_STRINGS_INCLUDE_CRAYON_BROWSER_PRODUCT_STRINGS_PRODUCT_STRINGS_H_
#define CRAYON_BROWSER_SHARED_UI_PRODUCT_STRINGS_INCLUDE_CRAYON_BROWSER_PRODUCT_STRINGS_PRODUCT_STRINGS_H_

#include <optional>
#include <string>

#include "crayon/browser_localization/locale_snapshot.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "crayon/browser_new_tab/new_tab_page.h"

namespace crayon::browser::product_strings {

struct PageMarkdownStrings final {
  std::string preview_command;
  std::string copy_command;
  std::string save_as_command;
  std::string copied_status;
  std::string copy_failed_status;
  std::string save_cancelled_status;
};

struct CastStrings final {
  std::string button_select;
  std::string button_stop;
  std::string picker_title;
  std::string picker_empty;
  std::string picker_select;
  std::string picker_refresh;
  std::string picker_cancel;
  std::string cast_code_label;
  std::string cast_code_connect;
  std::string cast_code_failed;
  std::string playback_pause;
  std::string playback_resume;
  std::string playback_seek;
  std::string playback_seconds;
  std::string playback_failed;
};

struct ProductStrings final {
  std::string product_name;
  browser_new_tab::NewTabPageStrings new_tab;
  browser_mdv::MdvPageStrings mdv;
  PageMarkdownStrings page_markdown;
  CastStrings cast;
};

std::optional<ProductStrings> BuildProductStrings(
    const localization::LocaleSnapshot& snapshot,
    browser_mdv::MdvShortcutPlatform shortcut_platform);

bool ProductStringsAreComplete(const ProductStrings& strings) noexcept;

}  // namespace crayon::browser::product_strings

#endif  // CRAYON_BROWSER_SHARED_UI_PRODUCT_STRINGS_INCLUDE_CRAYON_BROWSER_PRODUCT_STRINGS_PRODUCT_STRINGS_H_
