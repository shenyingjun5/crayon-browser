#include "browser/window/alloy_icon.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "browser/window/generated/alloy_icon_masks.h"
#include "include/cef_color_ids.h"
#include "include/cef_image.h"

namespace crayon::browser::cef_shell::window {
namespace {

struct Masks final {
  const std::uint8_t *one = nullptr;
  std::size_t one_size = 0;
  const std::uint8_t *two = nullptr;
  std::size_t two_size = 0;
};

#define CRAYON_MASKS(name)                                                     \
  Masks {                                                                      \
    generated::k##name##1x, sizeof(generated::k##name##1x),                    \
        generated::k##name##2x, sizeof(generated::k##name##2x)                 \
  }

Masks GetMasks(AlloyIcon icon) {
  switch (icon) {
  case AlloyIcon::kTabNew:
    return CRAYON_MASKS(TabNew);
  case AlloyIcon::kTabClose:
    return CRAYON_MASKS(TabClose);
  case AlloyIcon::kTabMoveWindow:
    return CRAYON_MASKS(TabMoveWindow);
  case AlloyIcon::kBack:
    return CRAYON_MASKS(NavBack);
  case AlloyIcon::kForward:
    return CRAYON_MASKS(NavForward);
  case AlloyIcon::kReload:
    return CRAYON_MASKS(NavReload);
  case AlloyIcon::kStop:
    return CRAYON_MASKS(NavStop);
  case AlloyIcon::kSiteInfo:
    return CRAYON_MASKS(OmniboxSiteInfo);
  case AlloyIcon::kBookmarkOutline:
    return CRAYON_MASKS(BookmarkOutline);
  case AlloyIcon::kBookmarkFilled:
    return CRAYON_MASKS(BookmarkFilled);
  case AlloyIcon::kCast:
    return CRAYON_MASKS(CastDevice);
  case AlloyIcon::kMenu:
    return CRAYON_MASKS(MenuMore);
  case AlloyIcon::kDownload:
    return CRAYON_MASKS(DownloadOpen);
  case AlloyIcon::kHistory:
    return CRAYON_MASKS(HistoryOpen);
  }
  return {};
}

#undef CRAYON_MASKS

bool AddRepresentation(CefRefPtr<CefImage> image, float scale, int size,
                       const std::uint8_t *mask, std::size_t mask_size,
                       cef_color_t color) {
  if (!image || !mask || mask_size != static_cast<std::size_t>(size * size)) {
    return false;
  }
  std::vector<std::uint8_t> rgba(mask_size * 4);
  const std::uint8_t color_alpha = CefColorGetA(color);
  for (std::size_t index = 0; index < mask_size; ++index) {
    const std::size_t pixel = index * 4;
    rgba[pixel] = CefColorGetR(color);
    rgba[pixel + 1] = CefColorGetG(color);
    rgba[pixel + 2] = CefColorGetB(color);
    rgba[pixel + 3] = static_cast<std::uint8_t>(
        (static_cast<unsigned>(mask[index]) * color_alpha) / 255U);
  }
  return image->AddBitmap(scale, size, size, CEF_COLOR_TYPE_RGBA_8888,
                          CEF_ALPHA_TYPE_POSTMULTIPLIED, rgba.data(),
                          rgba.size());
}

CefRefPtr<CefImage> Image(const Masks &masks, cef_color_t color) {
  auto image = CefImage::CreateImage();
  if (!AddRepresentation(image, 1.0F, 20, masks.one, masks.one_size, color) ||
      !AddRepresentation(image, 2.0F, 40, masks.two, masks.two_size, color)) {
    return nullptr;
  }
  return image;
}

} // namespace

bool ApplyAlloyIcon(CefRefPtr<CefLabelButton> button, AlloyIcon icon,
                    const CefString &label) {
  if (!button || label.empty())
    return false;
  const Masks masks = GetMasks(icon);
  auto normal = Image(masks, button->GetThemeColor(CEF_ColorIcon));
  auto hovered = Image(masks, button->GetThemeColor(CEF_ColorIconHovered));
  auto disabled = Image(masks, button->GetThemeColor(CEF_ColorIconDisabled));
  if (!normal || !hovered || !disabled)
    return false;
  button->SetTooltipText(label);
  button->SetAccessibleName(label);
  button->SetImage(CEF_BUTTON_STATE_NORMAL, normal);
  button->SetImage(CEF_BUTTON_STATE_HOVERED, hovered);
  button->SetImage(CEF_BUTTON_STATE_PRESSED, hovered);
  button->SetImage(CEF_BUTTON_STATE_DISABLED, disabled);
  return true;
}

void ReleaseAlloyIconFocus(CefRefPtr<CefButton> button) {
  if (!button)
    return;
  auto label = button->AsLabelButton();
  if (!label || !label->GetText().empty() ||
      !label->GetImage(CEF_BUTTON_STATE_NORMAL)) {
    return;
  }
  // Reset the pressed visual state without discarding keyboard focus. CEF's
  // activation callback does not identify pointer vs keyboard input, so it
  // cannot safely decide that the focus ring should be removed.
  if (button->IsEnabled()) button->SetState(CEF_BUTTON_STATE_NORMAL);
}

} // namespace crayon::browser::cef_shell::window
