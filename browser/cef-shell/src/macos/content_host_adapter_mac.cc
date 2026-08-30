#include "macos/content_host_adapter_mac.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>

namespace crayon::browser::cef_shell::macos {
namespace {

using content_host_ipc::EngineError;
using content_host_ipc::Fact;
using content_host_ipc::FactKind;
using content_host_ipc::Message;
using content_host_ipc::Mode;
using content_host_ipc::TerminalStatus;

constexpr std::size_t kMaxBrowserReplies = 64;

Mode Convert(browser_engine::SnapshotMode mode) {
  return mode == browser_engine::SnapshotMode::kCompact ? Mode::kCompact
                                                        : Mode::kStandard;
}

FactKind Convert(browser_engine::SnapshotFactKind kind) {
  switch (kind) {
    case browser_engine::SnapshotFactKind::kHeading:
      return FactKind::kHeading;
    case browser_engine::SnapshotFactKind::kParagraph:
      return FactKind::kParagraph;
    case browser_engine::SnapshotFactKind::kListItem:
      return FactKind::kListItem;
    case browser_engine::SnapshotFactKind::kLink:
      return FactKind::kLink;
    case browser_engine::SnapshotFactKind::kImage:
      return FactKind::kImage;
    case browser_engine::SnapshotFactKind::kTable:
      return FactKind::kTable;
    case browser_engine::SnapshotFactKind::kCodeBlock:
      return FactKind::kCodeBlock;
    case browser_engine::SnapshotFactKind::kDivider:
      return FactKind::kDivider;
    case browser_engine::SnapshotFactKind::kQuote:
      return FactKind::kQuote;
  }
  return FactKind::kParagraph;
}

TerminalStatus Convert(browser_engine::SnapshotTerminalStatus status) {
  switch (status) {
    case browser_engine::SnapshotTerminalStatus::kCompleted:
      return TerminalStatus::kCompleted;
    case browser_engine::SnapshotTerminalStatus::kCancelled:
      return TerminalStatus::kCancelled;
    case browser_engine::SnapshotTerminalStatus::kStaleNavigation:
      return TerminalStatus::kStaleNavigation;
    case browser_engine::SnapshotTerminalStatus::kRejected:
      return TerminalStatus::kRejected;
  }
  return TerminalStatus::kRejected;
}

EngineError Convert(browser_engine::EngineErrorCode error) {
  switch (error) {
    case browser_engine::EngineErrorCode::kNone:
      return EngineError::kNone;
    case browser_engine::EngineErrorCode::kInvalidArgument:
      return EngineError::kInvalidArgument;
    case browser_engine::EngineErrorCode::kInvalidState:
      return EngineError::kInvalidState;
    case browser_engine::EngineErrorCode::kAlreadyExists:
      return EngineError::kAlreadyExists;
    case browser_engine::EngineErrorCode::kNotFound:
      return EngineError::kNotFound;
    case browser_engine::EngineErrorCode::kStaleNavigation:
      return EngineError::kStaleNavigation;
    case browser_engine::EngineErrorCode::kUnsupported:
      return EngineError::kUnsupported;
    case browser_engine::EngineErrorCode::kCapacityExceeded:
      return EngineError::kCapacityExceeded;
    case browser_engine::EngineErrorCode::kNavigationFailed:
      return EngineError::kNavigationFailed;
  }
  return EngineError::kInvalidState;
}

Fact Convert(const browser_engine::SnapshotFact& source) {
  Fact fact;
  fact.kind = Convert(source.kind);
  fact.text = source.text;
  if (source.url) fact.url = source.url->value();
  fact.language = source.language;
  fact.level = source.level;
  fact.depth = source.depth;
  fact.ordered = source.ordered;
  fact.ordinal = source.ordinal;
  fact.table_columns = source.table_columns;
  fact.table_cells = source.table_cells;
  return fact;
}

}  // namespace

ContentHostAdapter::ContentHostAdapter()
    : ContentHostAdapter(std::make_unique<ContentHostProcess>()) {}

ContentHostAdapter::ContentHostAdapter(
    std::unique_ptr<ContentHostTransport> transport)
    : process_(std::move(transport)) {}

bool ContentHostAdapter::Start(std::string executable_path) {
  return process_ && process_->Start(std::move(executable_path));
}

void ContentHostAdapter::Stop() {
  requests_.clear();
  replies_.clear();
  if (process_) process_->Stop();
}

bool ContentHostAdapter::healthy() const noexcept {
  return process_ && process_->healthy();
}

void ContentHostAdapter::Consume(std::vector<ContentSnapshotEvent> events) {
  if (!healthy()) {
    FailAll();
    return;
  }
  for (const auto& event : events) {
    if (const auto* chunk =
            std::get_if<browser_engine::SnapshotChunk>(&event)) {
      ConsumeChunk(*chunk);
    } else {
      ConsumeTerminal(std::get<browser_engine::SnapshotTerminal>(event));
    }
  }
  PollReplies();
}

void ContentHostAdapter::Tick() { PollReplies(); }

std::vector<Message> ContentHostAdapter::Drain(std::size_t max_messages) {
  PollReplies();
  std::vector<Message> result;
  const std::size_t count = std::min(max_messages, replies_.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(std::move(replies_.front()));
    replies_.pop_front();
  }
  return result;
}

void ContentHostAdapter::OnSnapshotStarted(
    const browser_engine::SnapshotRequest& request) {
  if (!healthy()) return;
  requests_.emplace(
      request.request_id.value(),
      RequestState{request.tab_id.value(), request.navigation_id.value(),
                   Convert(request.mode), 0, false});
}

void ContentHostAdapter::OnSnapshotCancelled(
    const browser_engine::SnapshotRequestId& request_id) {
  const auto found = requests_.find(request_id.value());
  if (found == requests_.end()) return;
  if (!Send(content_host_ipc::Cancel{request_id.value()})) {
    FailAll();
    return;
  }
  requests_.erase(found);
}

void ContentHostAdapter::OnSnapshotNavigation(
    const browser_engine::TabId& tab_id,
    browser_engine::NavigationId navigation_id) {
  for (auto iterator = requests_.begin(); iterator != requests_.end();) {
    if (iterator->second.tab_id == tab_id.value()) {
      iterator = requests_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (!Send(content_host_ipc::Navigation{tab_id.value(), navigation_id.value(),
                                         navigation_id.value()})) {
    FailAll();
  }
}

void ContentHostAdapter::OnSnapshotClosed(const browser_engine::TabId& tab_id) {
  for (auto iterator = requests_.begin(); iterator != requests_.end();) {
    if (iterator->second.tab_id == tab_id.value()) {
      iterator = requests_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (!Send(content_host_ipc::CloseTab{tab_id.value()})) FailAll();
}

void ContentHostAdapter::OnSnapshotShutdown() {
  requests_.clear();
  replies_.clear();
}

void ContentHostAdapter::ConsumeChunk(
    const browser_engine::SnapshotChunk& chunk) {
  const auto found = requests_.find(chunk.request_id.value());
  if (found == requests_.end()) return;
  RequestState& state = found->second;
  if (state.tab_id != chunk.tab_id.value() ||
      state.navigation_id != chunk.navigation_id.value()) {
    requests_.erase(found);
    return;
  }
  if (!state.began) {
    if (!chunk.document ||
        !Send(content_host_ipc::Begin{chunk.request_id.value(), state.tab_id,
                                      state.navigation_id, state.navigation_id,
                                      state.mode, chunk.document->url.value(),
                                      chunk.document->title})) {
      FailAll();
      return;
    }
    state.began = true;
  }
  if (chunk.facts.empty()) return;
  std::vector<Fact> facts;
  facts.reserve(chunk.facts.size());
  std::transform(
      chunk.facts.begin(), chunk.facts.end(), std::back_inserter(facts),
      [](const browser_engine::SnapshotFact& fact) { return Convert(fact); });
  const std::uint32_t sequence = state.next_batch_sequence;
  if (!Send(content_host_ipc::FactBatch{
          chunk.request_id.value(), state.tab_id, state.navigation_id,
          state.navigation_id, sequence, std::move(facts)})) {
    FailAll();
    return;
  }
  ++state.next_batch_sequence;
}

void ContentHostAdapter::ConsumeTerminal(
    const browser_engine::SnapshotTerminal& terminal) {
  const auto found = requests_.find(terminal.request_id.value());
  if (found == requests_.end()) return;
  const RequestState state = found->second;
  if (state.tab_id != terminal.tab_id.value() ||
      state.navigation_id != terminal.navigation_id.value() || !state.began) {
    requests_.erase(found);
    return;
  }
  if (!Send(content_host_ipc::Terminal{
          terminal.request_id.value(), state.tab_id, state.navigation_id,
          state.navigation_id, Convert(terminal.status),
          Convert(terminal.error)})) {
    FailAll();
  }
}

void ContentHostAdapter::PollReplies() {
  if (!healthy()) {
    FailAll();
    return;
  }
  for (Message& message : process_->Drain(kMaxBrowserReplies)) {
    std::string request_id;
    bool completed = true;
    if (const auto* chunk =
            std::get_if<content_host_ipc::MarkdownChunk>(&message)) {
      request_id = chunk->request_id;
      completed = chunk->completed;
      const auto found = requests_.find(request_id);
      if (found == requests_.end() || found->second.tab_id != chunk->tab_id ||
          found->second.navigation_id != chunk->navigation_id ||
          found->second.navigation_id != chunk->generation) {
        continue;
      }
    } else if (const auto* error =
                   std::get_if<content_host_ipc::ErrorReply>(&message)) {
      request_id = error->request_id;
      if (requests_.find(request_id) == requests_.end()) continue;
    } else {
      continue;
    }
    if (replies_.size() >= kMaxBrowserReplies) {
      FailAll();
      return;
    }
    replies_.push_back(std::move(message));
    if (completed) requests_.erase(request_id);
  }
}

bool ContentHostAdapter::Send(Message message) {
  return process_ && process_->Enqueue(std::move(message));
}

void ContentHostAdapter::FailAll() {
  requests_.clear();
  replies_.clear();
}

}  // namespace crayon::browser::cef_shell::macos
