#include "crayon/browser_product_strings/product_strings.h"

#include <string_view>

#include "crayon/browser_localization/locale_catalog.h"

namespace crayon::browser::product_strings {
namespace {

std::string Required(const localization::LocaleCatalog& catalog,
                     std::string_view key, bool* complete) {
  const std::optional<std::string_view> value = catalog.Find(key);
  if (!value || value->empty()) {
    *complete = false;
    return {};
  }
  return std::string(*value);
}

}  // namespace

std::optional<ProductStrings> BuildProductStrings(
    const localization::LocaleSnapshot& snapshot,
    browser_mdv::MdvShortcutPlatform shortcut_platform) {
  const localization::LocaleCatalog catalog(snapshot.locale);
  bool complete = true;
  ProductStrings strings{
      Required(catalog, "app.title", &complete),
      browser_new_tab::NewTabPageStrings{
          std::string(snapshot.html_language),
          Required(catalog, "new_tab.title", &complete),
          Required(catalog, "new_tab.regular_heading", &complete),
          Required(catalog, "new_tab.incognito_heading", &complete),
          Required(catalog, "new_tab.regular_description", &complete),
          Required(catalog, "new_tab.incognito_description", &complete),
          Required(catalog, "new_tab.omnibox_hint", &complete),
          Required(catalog, "new_tab.shortcuts_heading", &complete),
          Required(catalog, "new_tab.empty_shortcuts", &complete),
          Required(catalog, "new_tab.config_error", &complete)},
      browser_mdv::MdvPageStrings{
          std::string(snapshot.html_language),
          Required(catalog, "mdv.title", &complete),
          Required(catalog, "mdv.view_source", &complete),
          Required(catalog, "mdv.view_preview", &complete),
          Required(catalog, "mdv.view_split", &complete),
          Required(catalog, "mdv.status_empty", &complete),
          Required(catalog, "mdv.status_too_large", &complete),
          Required(catalog, "mdv.status_invalid_utf8", &complete),
          Required(catalog, "mdv.status_render_policy", &complete),
          Required(catalog, "mdv.status_not_markdown", &complete),
          Required(catalog, "mdv.status_saved", &complete),
          Required(catalog, "mdv.confirm_text", &complete),
          Required(catalog, "mdv.label_save", &complete),
          Required(catalog, "mdv.label_discard", &complete),
          Required(catalog, "mdv.label_cancel", &complete),
          Required(catalog, "mdv.label_open_in_viewer", &complete),
          Required(catalog, "mdv.toolbar.title", &complete),
          Required(catalog, "mdv.tool.bold", &complete),
          Required(catalog, "mdv.tool.italic", &complete),
          Required(catalog, "mdv.tool.strike", &complete),
          Required(catalog, "mdv.tool.inline_code", &complete),
          Required(catalog, "mdv.tool.bullet_list", &complete),
          Required(catalog, "mdv.tool.ordered_list", &complete),
          Required(catalog, "mdv.tool.task_list", &complete),
          Required(catalog, "mdv.tool.quote", &complete),
          Required(catalog, "mdv.tool.code_block", &complete),
          Required(catalog, "mdv.tool.table", &complete),
          Required(catalog, "mdv.tool.link", &complete),
          Required(catalog, "mdv.tool.divider", &complete),
          Required(catalog, "mdv.tool.heading1", &complete),
          Required(catalog, "mdv.tool.heading2", &complete),
          Required(catalog, "mdv.tool.heading3", &complete),
          Required(catalog, "mdv.tool.structure", &complete),
          Required(catalog, "mdv.tool.indent", &complete),
          Required(catalog, "mdv.tool.outdent", &complete),
          Required(catalog, "mdv.tool.align_default", &complete),
          Required(catalog, "mdv.tool.align_left", &complete),
          Required(catalog, "mdv.tool.align_center", &complete),
          Required(catalog, "mdv.tool.align_right", &complete),
          Required(catalog, "mdv.tooltip.view", &complete),
          Required(catalog, "mdv.tooltip.markdown", &complete),
          Required(catalog, "mdv.tooltip.structure", &complete),
          Required(catalog, "mdv.tooltip.table_alignment", &complete),
          Required(catalog, "mdv.mermaid.fullscreen", &complete),
          Required(catalog, "mdv.mermaid.source", &complete),
          Required(catalog, "mdv.mermaid.close", &complete),
          Required(catalog, "mdv.mermaid.error", &complete),
          shortcut_platform},
      PageMarkdownStrings{
          Required(catalog, "page_markdown.preview_command", &complete),
          Required(catalog, "page_markdown.copy_command", &complete),
          Required(catalog, "page_markdown.save_as_command", &complete),
          Required(catalog, "page_markdown.copied_status", &complete),
          Required(catalog, "page_markdown.copy_failed_status", &complete),
          Required(catalog, "page_markdown.save_cancelled_status", &complete)},
      CastStrings{
          Required(catalog, "cast.select_receiver", &complete),
          Required(catalog, "cast.stop", &complete),
          Required(catalog, "cast.picker.title", &complete),
          Required(catalog, "cast.picker.empty", &complete),
          Required(catalog, "cast.picker.select", &complete),
          Required(catalog, "cast.picker.refresh", &complete),
          Required(catalog, "cast.picker.cancel", &complete),
          Required(catalog, "cast.code.label", &complete),
          Required(catalog, "cast.code.connect", &complete),
          Required(catalog, "cast.code.failed", &complete),
          Required(catalog, "cast.control.pause", &complete),
          Required(catalog, "cast.control.resume", &complete),
          Required(catalog, "cast.control.seek", &complete),
          Required(catalog, "cast.control.seconds", &complete),
          Required(catalog, "cast.control.failed", &complete)}};
  if (!complete || !ProductStringsAreComplete(strings)) {
    return std::nullopt;
  }
  return strings;
}

bool ProductStringsAreComplete(const ProductStrings& strings) noexcept {
  const auto present = [](const std::string& value) { return !value.empty(); };
  const auto& new_tab = strings.new_tab;
  const auto& mdv = strings.mdv;
  const auto& page = strings.page_markdown;
  const auto& cast = strings.cast;
  return present(strings.product_name) && present(new_tab.language) &&
         present(new_tab.document_title) && present(new_tab.regular_heading) &&
         present(new_tab.incognito_heading) &&
         present(new_tab.regular_description) &&
         present(new_tab.incognito_description) &&
         present(new_tab.omnibox_hint) && present(new_tab.shortcuts_heading) &&
         present(new_tab.empty_shortcuts) && present(new_tab.config_error) &&
         present(mdv.language) && present(mdv.document_title) &&
         present(mdv.view_source) && present(mdv.view_preview) &&
         present(mdv.view_split) && present(mdv.status_empty) &&
         present(mdv.status_too_large) && present(mdv.status_invalid_utf8) &&
         present(mdv.status_render_policy) && present(mdv.status_not_markdown) &&
         present(mdv.status_saved) && present(mdv.confirm_text) &&
         present(mdv.label_save) && present(mdv.label_discard) &&
         present(mdv.label_cancel) && present(mdv.label_open_in_viewer) &&
         present(mdv.toolbar_title) && present(mdv.tool_bold) &&
         present(mdv.tool_italic) && present(mdv.tool_strike) &&
         present(mdv.tool_inline_code) && present(mdv.tool_bullet_list) &&
         present(mdv.tool_ordered_list) && present(mdv.tool_task_list) &&
         present(mdv.tool_quote) && present(mdv.tool_code_block) &&
         present(mdv.tool_table) && present(mdv.tool_link) &&
         present(mdv.tool_divider) && present(mdv.tool_heading1) &&
         present(mdv.tool_heading2) && present(mdv.tool_heading3) &&
         present(mdv.tool_structure) && present(mdv.tool_indent) &&
         present(mdv.tool_outdent) && present(mdv.tool_align_default) &&
         present(mdv.tool_align_left) && present(mdv.tool_align_center) &&
         present(mdv.tool_align_right) && present(mdv.tooltip_view) &&
         present(mdv.tooltip_markdown) && present(mdv.tooltip_structure) &&
         present(mdv.tooltip_table_alignment) &&
         present(mdv.mermaid_fullscreen) && present(mdv.mermaid_source) &&
         present(mdv.mermaid_close) && present(mdv.mermaid_error) &&
         present(page.preview_command) && present(page.copy_command) &&
         present(page.save_as_command) && present(page.copied_status) &&
         present(page.copy_failed_status) &&
         present(page.save_cancelled_status) && present(cast.button_select) &&
         present(cast.button_stop) && present(cast.picker_title) &&
         present(cast.picker_empty) && present(cast.picker_select) &&
         present(cast.picker_refresh) && present(cast.picker_cancel) &&
         present(cast.cast_code_label) && present(cast.cast_code_connect) &&
         present(cast.cast_code_failed) && present(cast.playback_pause) &&
         present(cast.playback_resume) && present(cast.playback_seek) &&
         present(cast.playback_seconds) && present(cast.playback_failed);
}

}  // namespace crayon::browser::product_strings
