// MDV-02: deterministic, security-hardened Markdown → safe HTML
// renderer for the local markdown viewer (MDV-01 contract sections
// 5–7).
//
// Pipeline: input normalization (BOM strip, CRLF/CR → LF, strict
// UTF-8 validation, 5 MiB cap) → md4c parse with raw HTML disabled
// (NOHTMLBLOCKS|NOHTMLSPANS: HTML syntax renders as escaped text) and
// GFM tables/strikethrough/tasklists → output post-processing
// (image placeholders, link scheme allowlist, generated tag/attribute
// whitelist) → fail closed on any policy violation.
//
// Determinism: identical input bytes always produce identical output
// bytes on every platform (golden-locked, MD-002).
#pragma once

#include <cstddef>
#include <string>

namespace crayon::browser_markdown {

/// Maximum accepted markdown input, in bytes (MDV-01 §5).
inline constexpr std::size_t kMaxInputBytes = 5 * 1024 * 1024;

/// Closed render outcomes.
enum class RenderStatus {
  kOk = 0,
  kInputTooLarge,
  kInvalidUtf8,
  kOutputPolicyViolation,  // generated HTML left the whitelist: engine bug
};

/// Reports whether `data` is strict UTF-8 (no overlongs, no
/// surrogates, no code points above U+10FFFF).
bool IsValidUtf8(const std::string& data);

/// Renders markdown to safe HTML per the MDV-01 contract.  The output
/// is deterministic; on any non-OK status the returned string is
/// empty.
std::string RenderMarkdownToSafeHtml(const std::string& input, RenderStatus* status);

}  // namespace crayon::browser_markdown
