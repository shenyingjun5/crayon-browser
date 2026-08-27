// MDV-13: preview image classification pipeline (contract §7 v1.1).
//
// The markdown engine emits intermediate markers
// `<img class="md-img" src="mdv-img:N" data-mdv-raw="<raw ref>" alt=…>`
// and this module turns each into the final form:
// - https:// references load directly (CSP img-src 'self' https:)
// - local references are validated against the document directory and
//   rewritten to the opaque index route `/img/N` (paths never enter
//   the URL or the DOM; the Browser process owns the mapping)
// - everything else (http:, data:, other schemes, non-whitelisted
//   extensions, missing/oversized/escaped files) renders as the
//   placeholder with alt text and the reference address
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace crayon::browser_mdv {

/// Maximum accepted local image size, in bytes (contract v1.1).
inline constexpr std::uint64_t kMaxLocalImageBytes = 20 * 1024 * 1024;

/// Injected filesystem probe: returns true when `path_utf8` exists as a
/// regular file; on success writes its byte size.
using LocalImageProbe =
    std::function<bool(const std::string& path_utf8, std::uint64_t* size)>;

/// Reports whether `path_utf8` carries a whitelisted image extension
/// (png/jpg/jpeg/gif/webp/bmp/svg, case-insensitive).
bool HasWhitelistedImageExtension(const std::string& path_utf8);

/// Classifies every engine image marker in `html` into its final form.
/// `doc_dir_utf8` is the directory of the open document (empty for the
/// fixture: local references then always become placeholders).
/// Validated local files are appended to `local_images` in document
/// order; index N in `/img/N` indexes that vector.  All raw
/// `data-mdv-raw` attributes are stripped from the returned HTML.
std::string PreparePreviewHtml(const std::string& html,
                               const std::string& doc_dir_utf8,
                               const LocalImageProbe& probe,
                               std::vector<std::string>* local_images);

}  // namespace crayon::browser_mdv
