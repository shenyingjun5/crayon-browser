#include "crayon/browser_engine/snapshot.h"

namespace crayon::browser_engine {
namespace {

bool IsContinuation(unsigned char byte) noexcept {
  return (byte & 0xC0U) == 0x80U;
}

bool IsValidText(const std::string& text, std::size_t max_bytes,
                 bool allow_empty) noexcept {
  if ((!allow_empty && text.empty()) || text.size() > max_bytes) {
    return false;
  }
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = 0;
    std::size_t width = 0;
    if (lead <= 0x7FU) {
      codepoint = lead;
      width = 1;
    } else if (lead >= 0xC2U && lead <= 0xDFU) {
      codepoint = lead & 0x1FU;
      width = 2;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      codepoint = lead & 0x0FU;
      width = 3;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      codepoint = lead & 0x07U;
      width = 4;
    } else {
      return false;
    }
    if (index + width > text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset < width; ++offset) {
      const auto byte = static_cast<unsigned char>(text[index + offset]);
      if (!IsContinuation(byte)) {
        return false;
      }
      codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    if ((width == 3 && codepoint < 0x800U) ||
        (width == 4 && codepoint < 0x10000U) ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
        codepoint > 0x10FFFFU ||
        (codepoint < 0x20U && codepoint != '\n' && codepoint != '\t') ||
        (codepoint >= 0x7FU && codepoint <= 0x9FU)) {
      return false;
    }
    index += width;
  }
  return true;
}

bool IsValidLanguage(const std::optional<std::string>& language) noexcept {
  if (!language.has_value()) {
    return true;
  }
  if (language->empty() || language->size() > 32) {
    return false;
  }
  for (const unsigned char byte : *language) {
    if (!(byte >= 'a' && byte <= 'z') && !(byte >= '0' && byte <= '9') &&
        byte != '_' && byte != '+' && byte != '-') {
      return false;
    }
  }
  return true;
}

bool HasOnlyBaseFields(const SnapshotFact& fact) noexcept {
  return !fact.url.has_value() && !fact.language.has_value() &&
         fact.level == 0 && fact.depth == 0 && !fact.ordered &&
         !fact.ordinal.has_value() && fact.table_columns == 0 &&
         fact.table_cells.empty();
}

}  // namespace

bool IsValid(SnapshotMode value) noexcept {
  return value == SnapshotMode::kStandard || value == SnapshotMode::kCompact;
}

bool IsValid(SnapshotFactKind value) noexcept {
  return value >= SnapshotFactKind::kHeading &&
         value <= SnapshotFactKind::kQuote;
}

bool IsValid(SnapshotTerminalStatus value) noexcept {
  return value >= SnapshotTerminalStatus::kCompleted &&
         value <= SnapshotTerminalStatus::kRejected;
}

bool IsValid(const SnapshotFact& fact) noexcept {
  if (!IsValid(fact.kind)) {
    return false;
  }
  switch (fact.kind) {
    case SnapshotFactKind::kHeading:
      return fact.level >= 1 && fact.level <= 6 &&
             IsValidText(fact.text, kMaxSnapshotFactTextBytes, false) &&
             !fact.url.has_value() && !fact.language.has_value() &&
             fact.depth == 0 && !fact.ordered && !fact.ordinal.has_value() &&
             fact.table_columns == 0 && fact.table_cells.empty();
    case SnapshotFactKind::kListItem:
      return IsValidText(fact.text, kMaxSnapshotFactTextBytes, false) &&
             !fact.url.has_value() && !fact.language.has_value() &&
             fact.level == 0 && fact.depth >= 1 && fact.depth <= 8 &&
             (fact.ordered == fact.ordinal.has_value()) &&
             (!fact.ordinal.has_value() || *fact.ordinal > 0) &&
             fact.table_columns == 0 && fact.table_cells.empty();
    case SnapshotFactKind::kLink:
      return IsValidText(fact.text, kMaxSnapshotFactTextBytes, false) &&
             fact.url.has_value() && !fact.language.has_value() &&
             fact.level == 0 && fact.depth == 0 && !fact.ordered &&
             !fact.ordinal.has_value() && fact.table_columns == 0 &&
             fact.table_cells.empty();
    case SnapshotFactKind::kImage:
      return IsValidText(fact.text, kMaxSnapshotFactTextBytes, true) &&
             fact.url.has_value() && !fact.language.has_value() &&
             fact.level == 0 && fact.depth == 0 && !fact.ordered &&
             !fact.ordinal.has_value() && fact.table_columns == 0 &&
             fact.table_cells.empty();
    case SnapshotFactKind::kTable: {
      if (!fact.text.empty() || fact.url.has_value() ||
          fact.language.has_value() || fact.level != 0 || fact.depth != 0 ||
          fact.ordered || fact.ordinal.has_value() || fact.table_columns == 0 ||
          fact.table_columns > 32 || fact.table_cells.empty() ||
          fact.table_cells.size() > kMaxSnapshotTableCells ||
          fact.table_cells.size() % fact.table_columns != 0 ||
          fact.table_cells.size() / fact.table_columns > 256) {
        return false;
      }
      for (const auto& cell : fact.table_cells) {
        if (!IsValidText(cell, 1024, true)) {
          return false;
        }
      }
      return true;
    }
    case SnapshotFactKind::kDivider:
      return fact.text.empty() && HasOnlyBaseFields(fact);
    case SnapshotFactKind::kParagraph:
    case SnapshotFactKind::kQuote:
      return IsValidText(fact.text, kMaxSnapshotFactTextBytes, false) &&
             HasOnlyBaseFields(fact);
    case SnapshotFactKind::kCodeBlock:
      return IsValidText(fact.text, kMaxSnapshotCodeBytes, false) &&
             !fact.url.has_value() && IsValidLanguage(fact.language) &&
             fact.level == 0 && fact.depth == 0 && !fact.ordered &&
             !fact.ordinal.has_value() && fact.table_columns == 0 &&
             fact.table_cells.empty();
  }
  return false;
}

bool IsValid(const SnapshotFact& fact, SnapshotMode mode) noexcept {
  if (!IsValid(fact) || !IsValid(mode)) {
    return false;
  }
  if (mode == SnapshotMode::kCompact &&
      fact.kind != SnapshotFactKind::kCodeBlock &&
      fact.kind != SnapshotFactKind::kTable &&
      fact.text.size() > kMaxCompactSnapshotFactTextBytes) {
    return false;
  }
  return true;
}

std::optional<std::size_t> SnapshotFactByteSize(
    const SnapshotFact& fact) noexcept {
  if (!IsValid(fact)) {
    return std::nullopt;
  }
  std::size_t bytes = fact.text.size();
  for (const auto& cell : fact.table_cells) {
    bytes += cell.size();
  }
  if (fact.url.has_value()) {
    bytes += fact.url->value().size();
  }
  if (fact.language.has_value()) {
    bytes += fact.language->size();
  }
  return bytes;
}

bool IsValid(const SnapshotChunk& chunk) noexcept {
  if (chunk.facts.size() > kMaxSnapshotFactsPerChunk ||
      (chunk.sequence == 0) != chunk.document.has_value() ||
      (chunk.facts.empty() && !chunk.document.has_value()) ||
      (chunk.document.has_value() &&
       (!IsValidText(chunk.document->title, 512, false) ||
        chunk.document->title.find_first_of("\n\r\t") != std::string::npos))) {
    return false;
  }
  std::size_t bytes = 0;
  if (chunk.document.has_value()) {
    const auto document_bytes =
        chunk.document->url.value().size() + chunk.document->title.size();
    if (document_bytes > kMaxSnapshotChunkBytes) {
      return false;
    }
    bytes = document_bytes;
  }
  for (const auto& fact : chunk.facts) {
    if (!IsValid(fact)) {
      return false;
    }
    if (fact.text.size() > kMaxSnapshotChunkBytes - bytes) {
      return false;
    }
    bytes += fact.text.size();
    for (const auto& cell : fact.table_cells) {
      if (cell.size() > kMaxSnapshotChunkBytes - bytes) {
        return false;
      }
      bytes += cell.size();
    }
    if (fact.url.has_value()) {
      if (fact.url->value().size() > kMaxSnapshotChunkBytes - bytes) {
        return false;
      }
      bytes += fact.url->value().size();
    }
    if (fact.language.has_value()) {
      if (fact.language->size() > kMaxSnapshotChunkBytes - bytes) {
        return false;
      }
      bytes += fact.language->size();
    }
  }
  return bytes <= kMaxSnapshotChunkBytes;
}

bool IsValid(const SnapshotChunk& chunk, SnapshotMode mode) noexcept {
  if (!IsValid(mode) || !IsValid(chunk)) {
    return false;
  }
  for (const auto& fact : chunk.facts) {
    if (!IsValid(fact, mode)) {
      return false;
    }
  }
  return SnapshotChunkByteSize(chunk).has_value();
}

std::optional<std::size_t> SnapshotChunkByteSize(
    const SnapshotChunk& chunk) noexcept {
  if (!IsValid(chunk)) {
    return std::nullopt;
  }
  std::size_t bytes = 0;
  if (chunk.document.has_value()) {
    bytes += chunk.document->url.value().size();
    bytes += chunk.document->title.size();
  }
  for (const auto& fact : chunk.facts) {
    bytes += fact.text.size();
    for (const auto& cell : fact.table_cells) {
      bytes += cell.size();
    }
    if (fact.url.has_value()) {
      bytes += fact.url->value().size();
    }
    if (fact.language.has_value()) {
      bytes += fact.language->size();
    }
  }
  return bytes;
}

std::size_t SnapshotModeMaxFacts(SnapshotMode mode) noexcept {
  return mode == SnapshotMode::kCompact ? kMaxCompactSnapshotFacts
                                        : kMaxStandardSnapshotFacts;
}

std::size_t SnapshotModeMaxBytes(SnapshotMode mode) noexcept {
  return mode == SnapshotMode::kCompact ? kMaxCompactSnapshotBytes
                                        : kMaxStandardSnapshotBytes;
}

}  // namespace crayon::browser_engine
