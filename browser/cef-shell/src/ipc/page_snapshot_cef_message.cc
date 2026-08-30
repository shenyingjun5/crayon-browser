#include "ipc/page_snapshot_cef_message.h"

#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_values.h"

namespace crayon::browser::cef_shell::snapshot_ipc {
namespace {

using browser_engine::BrowserUrl;
using browser_engine::NavigationId;
using browser_engine::SnapshotChunk;
using browser_engine::SnapshotDocumentMetadata;
using browser_engine::SnapshotFact;
using browser_engine::SnapshotFactKind;
using browser_engine::SnapshotMode;
using browser_engine::SnapshotRequest;
using browser_engine::SnapshotRequestId;
using browser_engine::SnapshotTerminal;
using browser_engine::SnapshotTerminalStatus;
using browser_engine::TabId;

constexpr std::size_t kRequestSize = 4;
constexpr std::size_t kChunkSize = 8;
constexpr std::size_t kTerminalSize = 5;
constexpr std::size_t kFactSize = 11;

std::string NavigationString(NavigationId id) {
  return std::to_string(id.value());
}

std::optional<NavigationId> ParseNavigation(const CefString& value) {
  const std::string text = value.ToString();
  std::uint64_t raw = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), raw);
  if (text.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != text.data() + text.size() || raw == 0) {
    return std::nullopt;
  }
  return NavigationId::FromRaw(raw);
}

std::optional<std::uint32_t> ParseOptionalOrdinal(const CefString& value) {
  const std::string text = value.ToString();
  if (text.empty()) return std::uint32_t{0};
  std::uint32_t raw = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), raw);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      raw == 0) {
    return std::nullopt;
  }
  return raw;
}

template <typename Id>
std::optional<Id> ReadId(CefRefPtr<CefListValue> values, std::size_t index) {
  if (!values || values->GetType(index) != VTYPE_STRING) {
    return std::nullopt;
  }
  return Id::TryCreate(values->GetString(index).ToString());
}

bool HasTypes(CefRefPtr<CefListValue> values, std::size_t size,
              std::initializer_list<CefValueType> types) {
  if (!values || values->GetSize() != size || types.size() != size) {
    return false;
  }
  std::size_t index = 0;
  for (const auto type : types) {
    if (values->GetType(index++) != type) return false;
  }
  return true;
}

CefRefPtr<CefListValue> EncodeFact(const SnapshotFact& fact) {
  auto values = CefListValue::Create();
  values->SetSize(kFactSize);
  values->SetInt(0, static_cast<int>(fact.kind));
  values->SetString(1, fact.text);
  values->SetString(2, fact.url ? fact.url->value() : std::string{});
  values->SetString(3, fact.language.value_or(std::string{}));
  values->SetInt(4, fact.level);
  values->SetInt(5, fact.depth);
  values->SetBool(6, fact.ordered);
  values->SetString(
      7, fact.ordinal ? std::to_string(*fact.ordinal) : std::string{});
  values->SetInt(8, fact.table_columns);
  auto cells = CefListValue::Create();
  cells->SetSize(fact.table_cells.size());
  for (std::size_t index = 0; index < fact.table_cells.size(); ++index) {
    cells->SetString(index, fact.table_cells[index]);
  }
  values->SetList(9, cells);
  values->SetInt(10, 1);
  return values;
}

std::optional<SnapshotFact> DecodeFact(CefRefPtr<CefListValue> values) {
  if (!HasTypes(values, kFactSize,
                {VTYPE_INT, VTYPE_STRING, VTYPE_STRING, VTYPE_STRING, VTYPE_INT,
                 VTYPE_INT, VTYPE_BOOL, VTYPE_STRING, VTYPE_INT, VTYPE_LIST,
                 VTYPE_INT}) ||
      values->GetInt(10) != 1) {
    return std::nullopt;
  }
  const int kind = values->GetInt(0);
  const int level = values->GetInt(4);
  const int depth = values->GetInt(5);
  const auto ordinal = ParseOptionalOrdinal(values->GetString(7));
  const int columns = values->GetInt(8);
  if (kind < 0 || kind > static_cast<int>(SnapshotFactKind::kQuote) ||
      level < 0 || level > std::numeric_limits<std::uint8_t>::max() ||
      depth < 0 || depth > std::numeric_limits<std::uint8_t>::max() ||
      !ordinal || columns < 0 ||
      columns > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  SnapshotFact fact;
  fact.kind = static_cast<SnapshotFactKind>(kind);
  fact.text = values->GetString(1).ToString();
  const std::string url = values->GetString(2).ToString();
  if (!url.empty()) {
    auto parsed = BrowserUrl::TryParse(url);
    if (!parsed) return std::nullopt;
    fact.url = std::move(*parsed);
  }
  const std::string language = values->GetString(3).ToString();
  if (!language.empty()) fact.language = language;
  fact.level = static_cast<std::uint8_t>(level);
  fact.depth = static_cast<std::uint8_t>(depth);
  fact.ordered = values->GetBool(6);
  if (*ordinal != 0) fact.ordinal = *ordinal;
  fact.table_columns = static_cast<std::uint16_t>(columns);
  auto cells = values->GetList(9);
  if (!cells || cells->GetSize() > browser_engine::kMaxSnapshotTableCells) {
    return std::nullopt;
  }
  fact.table_cells.reserve(cells->GetSize());
  for (std::size_t index = 0; index < cells->GetSize(); ++index) {
    if (cells->GetType(index) != VTYPE_STRING) return std::nullopt;
    fact.table_cells.push_back(cells->GetString(index).ToString());
  }
  return browser_engine::IsValid(fact)
             ? std::optional<SnapshotFact>(std::move(fact))
             : std::nullopt;
}

CefRefPtr<CefProcessMessage> Message(const char* name, std::size_t size) {
  auto message = CefProcessMessage::Create(name);
  message->GetArgumentList()->SetSize(size);
  return message;
}

}  // namespace

CefRefPtr<CefProcessMessage> CreateRequestMessage(
    const SnapshotRequest& request) {
  auto message = Message(kRequestMessageName, kRequestSize);
  auto values = message->GetArgumentList();
  values->SetString(0, request.request_id.value());
  values->SetString(1, request.tab_id.value());
  values->SetString(2, NavigationString(request.navigation_id));
  values->SetInt(3, static_cast<int>(request.mode));
  return message;
}

CefRefPtr<CefProcessMessage> CreateCancelMessage(
    const SnapshotRequestId& request_id) {
  auto message = Message(kCancelMessageName, 1);
  message->GetArgumentList()->SetString(0, request_id.value());
  return message;
}

CefRefPtr<CefProcessMessage> CreateChunkMessage(const SnapshotChunk& chunk) {
  auto message = Message(kChunkMessageName, kChunkSize);
  auto values = message->GetArgumentList();
  values->SetString(0, chunk.request_id.value());
  values->SetString(1, chunk.tab_id.value());
  values->SetString(2, NavigationString(chunk.navigation_id));
  values->SetInt(3, static_cast<int>(chunk.sequence));
  values->SetBool(4, chunk.document.has_value());
  values->SetString(
      5, chunk.document ? chunk.document->url.value() : std::string{});
  values->SetString(6, chunk.document ? chunk.document->title : std::string{});
  auto facts = CefListValue::Create();
  facts->SetSize(chunk.facts.size());
  for (std::size_t index = 0; index < chunk.facts.size(); ++index) {
    facts->SetList(index, EncodeFact(chunk.facts[index]));
  }
  values->SetList(7, facts);
  return message;
}

CefRefPtr<CefProcessMessage> CreateTerminalMessage(
    const SnapshotTerminal& terminal) {
  auto message = Message(kTerminalMessageName, kTerminalSize);
  auto values = message->GetArgumentList();
  values->SetString(0, terminal.request_id.value());
  values->SetString(1, terminal.tab_id.value());
  values->SetString(2, NavigationString(terminal.navigation_id));
  values->SetInt(3, static_cast<int>(terminal.status));
  values->SetInt(4, static_cast<int>(terminal.error));
  return message;
}

std::optional<SnapshotRequest> ReadRequestMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kRequestMessageName)
    return std::nullopt;
  auto values = message->GetArgumentList();
  if (!HasTypes(values, kRequestSize,
                {VTYPE_STRING, VTYPE_STRING, VTYPE_STRING, VTYPE_INT})) {
    return std::nullopt;
  }
  auto request_id = ReadId<SnapshotRequestId>(values, 0);
  auto tab_id = ReadId<TabId>(values, 1);
  auto navigation = ParseNavigation(values->GetString(2));
  const int mode = values->GetInt(3);
  if (!request_id || !tab_id || !navigation || mode < 0 ||
      mode > static_cast<int>(SnapshotMode::kCompact)) {
    return std::nullopt;
  }
  return SnapshotRequest{std::move(*request_id), std::move(*tab_id),
                         *navigation, static_cast<SnapshotMode>(mode)};
}

std::optional<SnapshotRequestId> ReadCancelMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kCancelMessageName) return std::nullopt;
  auto values = message->GetArgumentList();
  if (!HasTypes(values, 1, {VTYPE_STRING})) return std::nullopt;
  return ReadId<SnapshotRequestId>(values, 0);
}

std::optional<SnapshotChunk> ReadChunkMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kChunkMessageName) return std::nullopt;
  auto values = message->GetArgumentList();
  if (!HasTypes(values, kChunkSize,
                {VTYPE_STRING, VTYPE_STRING, VTYPE_STRING, VTYPE_INT,
                 VTYPE_BOOL, VTYPE_STRING, VTYPE_STRING, VTYPE_LIST})) {
    return std::nullopt;
  }
  auto request_id = ReadId<SnapshotRequestId>(values, 0);
  auto tab_id = ReadId<TabId>(values, 1);
  auto navigation = ParseNavigation(values->GetString(2));
  const int sequence = values->GetInt(3);
  if (!request_id || !tab_id || !navigation || sequence < 0)
    return std::nullopt;
  std::optional<SnapshotDocumentMetadata> document;
  if (values->GetBool(4)) {
    auto url = BrowserUrl::TryParse(values->GetString(5).ToString());
    if (!url) return std::nullopt;
    document = SnapshotDocumentMetadata{std::move(*url),
                                        values->GetString(6).ToString()};
  } else if (!values->GetString(5).ToString().empty() ||
             !values->GetString(6).ToString().empty()) {
    return std::nullopt;
  }
  auto encoded_facts = values->GetList(7);
  if (!encoded_facts ||
      encoded_facts->GetSize() > browser_engine::kMaxSnapshotFactsPerChunk) {
    return std::nullopt;
  }
  std::vector<SnapshotFact> facts;
  facts.reserve(encoded_facts->GetSize());
  for (std::size_t index = 0; index < encoded_facts->GetSize(); ++index) {
    if (encoded_facts->GetType(index) != VTYPE_LIST) return std::nullopt;
    auto fact = DecodeFact(encoded_facts->GetList(index));
    if (!fact) return std::nullopt;
    facts.push_back(std::move(*fact));
  }
  SnapshotChunk chunk{std::move(*request_id),
                      std::move(*tab_id),
                      *navigation,
                      static_cast<std::uint32_t>(sequence),
                      std::move(document),
                      std::move(facts)};
  return browser_engine::IsValid(chunk)
             ? std::optional<SnapshotChunk>(std::move(chunk))
             : std::nullopt;
}

std::optional<SnapshotTerminal> ReadTerminalMessage(
    CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != kTerminalMessageName)
    return std::nullopt;
  auto values = message->GetArgumentList();
  if (!HasTypes(
          values, kTerminalSize,
          {VTYPE_STRING, VTYPE_STRING, VTYPE_STRING, VTYPE_INT, VTYPE_INT})) {
    return std::nullopt;
  }
  auto request_id = ReadId<SnapshotRequestId>(values, 0);
  auto tab_id = ReadId<TabId>(values, 1);
  auto navigation = ParseNavigation(values->GetString(2));
  const int status = values->GetInt(3);
  const int error = values->GetInt(4);
  if (!request_id || !tab_id || !navigation || status < 0 ||
      status > static_cast<int>(SnapshotTerminalStatus::kRejected) ||
      error < 0 ||
      error > static_cast<int>(
                  browser_engine::EngineErrorCode::kNavigationFailed)) {
    return std::nullopt;
  }
  return SnapshotTerminal{std::move(*request_id), std::move(*tab_id),
                          *navigation,
                          static_cast<SnapshotTerminalStatus>(status),
                          static_cast<browser_engine::EngineErrorCode>(error)};
}

}  // namespace crayon::browser::cef_shell::snapshot_ipc
