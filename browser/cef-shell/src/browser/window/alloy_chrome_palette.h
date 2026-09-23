#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_PALETTE_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_PALETTE_H_

// Single source of truth for the Alloy chrome band colors on every platform.
// Values mirror browser/shared-ui/design/tokens.json (themes.light.colors) so
// the running shell cannot drift from the design tokens again: the previous
// per-file literals (0xFFDEE4F4 / 0xFFE9EDF6) matched neither the tokens nor
// each other. Colors are 0xAARRGGBB as consumed by CefView::SetBackgroundColor.
//
// PLT-SHELL-24M2FIX-C5 retuned the light surfaces to the reference Chrome build
// (153.0.8010.53) measured on this machine. Screenshots render one unit off per
// channel, so these are the display-corrected readings: strip #DEE2F0, toolbar
// #F9F9FF, address field #E7E9F3. The three-step scale is the point: the strip
// is the darkest surface, the toolbar and the active tab share the lightest
// (which is what joins them), and the field sits between them. The page below
// stays pure white and a hairline separates it from the band.

#include <cstdint>

namespace crayon::browser::cef_shell::window::chrome_palette {

// tokens.json -> themes.light.colors.tabStripBackground
inline constexpr std::uint32_t kTabStripBackground = 0xFFDEE2F0;

// tokens.json -> themes.light.colors.activeTabBackground. The active tab takes
// the toolbar value, which is what makes it read as joined to the row below.
inline constexpr std::uint32_t kActiveTabBackground = 0xFFF9F9FF;

// tokens.json -> themes.light.colors.toolbarBackground
inline constexpr std::uint32_t kToolbarBackground = 0xFFF9F9FF;

// tokens.json -> themes.light.colors.omniboxBackground. Its own token since
// PLT-SHELL-24M2FIX-C5: the address field is a step between the toolbar and the
// strip band, so the earlier workaround of aliasing the band colour painted it
// darker than the reference against the lighter toolbar.
inline constexpr std::uint32_t kOmniboxBackground = 0xFFE7E9F3;

// tokens.json -> themes.light.colors.separator. Hairline under the chrome band,
// which is what keeps the band visually separate from the white page. The
// reference build draws a slightly lighter line (measured #E0E1EA); the system
// separator token is used rather than adding a second near-identical grey.
inline constexpr std::uint32_t kChromeSeparator = 0xFFD0D5DD;

// tokens.json -> themes.light.colors.brandAction. The tab loading indicator is
// the one chrome element that is not a surface, so it takes the action color.
inline constexpr std::uint32_t kTabLoadingIndicator = 0xFF2F6FED;

} // namespace crayon::browser::cef_shell::window::chrome_palette

#endif // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_WINDOW_ALLOY_CHROME_PALETTE_H_
