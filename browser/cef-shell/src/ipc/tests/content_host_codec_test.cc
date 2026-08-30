#include "crayon/cef_shell_ipc/content_host_codec.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace ch = crayon::cef_shell::ipc::content_host;

#define CHECK_CH(condition)                                 \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

ch::Fact Base(ch::FactKind kind, std::string text) {
  ch::Fact fact;
  fact.kind = kind;
  fact.text = std::move(text);
  return fact;
}

std::vector<std::pair<std::string, ch::Message>> Messages() {
  auto heading = Base(ch::FactKind::kHeading, "Heading");
  heading.level = 2;
  auto paragraph = Base(ch::FactKind::kParagraph, "Paragraph");
  auto list = Base(ch::FactKind::kListItem, "Item");
  list.depth = 2;
  list.ordered = true;
  list.ordinal = 3;
  auto link = Base(ch::FactKind::kLink, "Link");
  link.url = "https://example.test/a";
  auto image = Base(ch::FactKind::kImage, "Alt");
  image.url = "https://example.test/i.png";
  auto table = Base(ch::FactKind::kTable, "");
  table.table_columns = 2;
  table.table_cells = {"A", "B", "1", "2"};
  auto code = Base(ch::FactKind::kCodeBlock, "let x = 1;\n");
  code.language = "rust";
  auto divider = Base(ch::FactKind::kDivider, "");
  auto quote = Base(ch::FactKind::kQuote, "Quote");
  return {
      {"begin", ch::Begin{"req-1", "tab-1", 7, 11, ch::Mode::kStandard,
                          "https://example.test/", "Example"}},
      {"fact_batch", ch::FactBatch{"req-1",
                                   "tab-1",
                                   7,
                                   11,
                                   0,
                                   {heading, paragraph, list, link, image,
                                    table, code, divider, quote}}},
      {"terminal",
       ch::Terminal{"req-1", "tab-1", 7, 11, ch::TerminalStatus::kCompleted,
                    ch::EngineError::kNone}},
      {"terminal_error",
       ch::Terminal{"req-1", "tab-1", 7, 11, ch::TerminalStatus::kRejected,
                    ch::EngineError::kCapacityExceeded}},
      {"cancel", ch::Cancel{"req-2"}},
      {"navigation", ch::Navigation{"tab-1", 8, 12}},
      {"close_tab", ch::CloseTab{"tab-1"}},
      {"shutdown", ch::Shutdown{}},
      {"markdown_chunk",
       ch::MarkdownChunk{"req-1", "tab-1", 7, 11, 1, true, "# Example\n"}},
      {"error_reply", ch::ErrorReply{"req-2", ch::HostError::kCancelled}},
  };
}

std::string Hex(const std::vector<std::uint8_t>& bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto byte : bytes) {
    result.push_back(kDigits[byte >> 4]);
    result.push_back(kDigits[byte & 15]);
  }
  return result;
}

std::vector<std::pair<std::string, std::string>> ReadGolden(
    const std::string& set) {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) + "/schemas/" + set +
                      "/content_host_v1.json");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string text = buffer.str();
  std::vector<std::pair<std::string, std::string>> result;
  std::size_t cursor = 0;
  while ((cursor = text.find("{\"name\":\"", cursor)) != std::string::npos) {
    cursor += 9;
    const auto name_end = text.find('"', cursor);
    if (name_end == std::string::npos) return {};
    const std::string name = text.substr(cursor, name_end - cursor);
    cursor = text.find("\"hex\":\"", name_end);
    if (cursor == std::string::npos) return {};
    cursor += 7;
    const auto hex_end = text.find('"', cursor);
    if (hex_end == std::string::npos) return {};
    result.emplace_back(name, text.substr(cursor, hex_end - cursor));
    cursor = hex_end;
  }
  return result;
}

bool RoundTripAndGolden() {
  std::vector<std::pair<std::string, std::string>> actual;
  for (const auto& [name, message] : Messages()) {
    ch::CodecError error;
    auto encoded = ch::Encode(message, &error);
    CHECK_CH(encoded.has_value());
    auto decoded = ch::Decode(*encoded, &error);
    CHECK_CH(decoded.has_value());
    CHECK_CH(*decoded == message);
    actual.emplace_back(name, Hex(*encoded));
  }
  CHECK_CH(ReadGolden("current") == actual);
  CHECK_CH(ReadGolden("previous") == actual);
  return true;
}

bool HostileAndBounds() {
  ch::CodecError error;
  auto invalid = ch::Message(
      ch::Begin{"r", "t", 0, 1, ch::Mode::kStandard, "https://e.test/", "T"});
  CHECK_CH(!ch::Encode(invalid, &error) &&
           error == ch::CodecError::kInvalidValue);
  std::vector<std::uint8_t> oversize(ch::kMaxFrameBytes + 1);
  CHECK_CH(!ch::Decode(oversize, &error) &&
           error == ch::CodecError::kFrameTooLarge);
  ch::Message max_id = ch::Cancel{std::string(128, 'r')};
  auto max_id_bytes = ch::Encode(max_id, &error);
  CHECK_CH(max_id_bytes && ch::Decode(*max_id_bytes, &error) == max_id);
  CHECK_CH(ch::Encode(ch::Message(ch::Cancel{"request.with:scope"}), &error));
  CHECK_CH(!ch::Encode(ch::Message(ch::CloseTab{"tab.with.dot"}), &error) &&
           error == ch::CodecError::kInvalidValue);
  for (const auto& pair : Messages()) {
    auto encoded = ch::Encode(pair.second, &error);
    CHECK_CH(encoded);
    for (std::size_t cut = 0; cut < encoded->size(); ++cut) {
      std::vector<std::uint8_t> truncated(encoded->begin(),
                                          encoded->begin() + cut);
      CHECK_CH(!ch::Decode(truncated, &error));
    }
    auto trailing = *encoded;
    trailing.push_back(0);
    CHECK_CH(!ch::Decode(trailing, &error) &&
             error == ch::CodecError::kTrailingBytes);
    auto version = *encoded;
    version[5] = 2;
    CHECK_CH(!ch::Decode(version, &error) &&
             error == ch::CodecError::kUnsupportedVersion);
    auto flags = *encoded;
    flags[7] = 1;
    CHECK_CH(!ch::Decode(flags, &error) &&
             error == ch::CodecError::kInvalidFlags);
  }
  auto unknown = ch::Encode(ch::Message(ch::Shutdown{}), &error).value();
  unknown[6] = 0xff;
  CHECK_CH(!ch::Decode(unknown, &error) &&
           error == ch::CodecError::kUnknownKind);
  auto fact_batch = ch::Encode(Messages()[1].second, &error).value();
  fact_batch[48] = 0xff;
  CHECK_CH(!ch::Decode(fact_batch, &error) &&
           error == ch::CodecError::kInvalidValue);
  auto bad_utf8 = ch::Encode(ch::Message(ch::Cancel{"r"}), &error).value();
  bad_utf8.back() = 0xff;
  CHECK_CH(!ch::Decode(bad_utf8, &error) &&
           error == ch::CodecError::kInvalidUtf8);
  ch::MarkdownChunk max{
      "r", "t", 1, 1, 0, true, std::string(ch::kMaxMarkdownBytes, 'x')};
  CHECK_CH(ch::Encode(ch::Message(max), &error));
  max.markdown.push_back('x');
  CHECK_CH(!ch::Encode(ch::Message(max), &error) &&
           error == ch::CodecError::kLengthExceeded);
  return true;
}
}  // namespace

bool RunContentHostCodecTests() {
  return RoundTripAndGolden() && HostileAndBounds();
}
