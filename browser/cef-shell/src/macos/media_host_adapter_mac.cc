#include "macos/media_host_adapter_mac.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace crayon::browser::cef_shell::macos {
namespace {

constexpr std::size_t kMaxTrackedRequests = 256;
constexpr std::size_t kMaxTrackedCandidates = 256;
constexpr std::size_t kMaxTrackedTabs = 64;
constexpr std::size_t kMaxBrowserReplies = 64;

std::string ReplyRequestId(const media_host_ipc::Message &message) {
  return std::visit(
      [](const auto &value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, media_host_ipc::CandidateReply> ||
                      std::is_same_v<T, media_host_ipc::DecisionReply> ||
                      std::is_same_v<T, media_host_ipc::Ack> ||
                      std::is_same_v<T, media_host_ipc::ErrorReply>)
          return value.request_id;
        return {};
      },
      message);
}

std::string NewRequestId(const media_host_ipc::Message &message) {
  return std::visit(
      [](const auto &value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, media_host_ipc::IngestUrl> ||
                      std::is_same_v<T, media_host_ipc::MarkEme> ||
                      std::is_same_v<T, media_host_ipc::Decide> ||
                      std::is_same_v<T, media_host_ipc::DecideUrlLess> ||
                      std::is_same_v<T, media_host_ipc::Navigation> ||
                      std::is_same_v<T, media_host_ipc::CloseTab>)
          return value.request_id;
        return {};
      },
      message);
}

} // namespace

MediaHostAdapter::MediaHostAdapter()
    : MediaHostAdapter(std::make_unique<MediaHostProcess>()) {}

MediaHostAdapter::MediaHostAdapter(
    std::unique_ptr<MediaHostTransport> transport)
    : process_(std::move(transport)) {}

bool MediaHostAdapter::Start(std::string executable_path) {
  if (!process_ || !process_->Start(std::move(executable_path)))
    return false;
  saw_healthy_ = false;
  process_generation_ = 0;
  return true;
}

void MediaHostAdapter::Stop() {
  FailAll();
  tabs_.clear();
  saw_healthy_ = false;
  process_generation_ = 0;
  if (process_)
    process_->Stop();
}

bool MediaHostAdapter::healthy() const noexcept {
  return process_ && process_->healthy();
}

bool MediaHostAdapter::Submit(media_host_ipc::Message message) {
  PollReplies();
  if (!healthy())
    return false;
  media_host_ipc::CodecError codec_error =
      media_host_ipc::CodecError::kInvalidValue;
  if (!media_host_ipc::Encode(message, &codec_error))
    return false;
  const std::string new_request_id = NewRequestId(message);
  if (!new_request_id.empty() &&
      (requests_.find(new_request_id) != requests_.end() ||
       requests_.size() >= kMaxTrackedRequests))
    return false;
  std::string request_id;
  Context context;
  if (!Admit(message, &request_id, &context))
    return false;
  if (!process_->Enqueue(std::move(message))) {
    FailAll();
    tabs_.clear();
    saw_healthy_ = false;
    process_generation_ = 0;
    return false;
  }
  if (!request_id.empty())
    requests_[request_id] = std::move(context);
  return true;
}

void MediaHostAdapter::Tick() { PollReplies(); }

std::vector<media_host_ipc::Message>
MediaHostAdapter::Drain(std::size_t maximum) {
  PollReplies();
  std::vector<media_host_ipc::Message> result;
  const std::size_t count = std::min(maximum, replies_.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    result.push_back(std::move(replies_.front()));
    replies_.pop_front();
  }
  return result;
}

bool MediaHostAdapter::Admit(const media_host_ipc::Message &message,
                             std::string *request_id, Context *context) {
  if (const auto *value = std::get_if<media_host_ipc::IngestUrl>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value = std::get_if<media_host_ipc::MarkEme>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value =
          std::get_if<media_host_ipc::DecideUrlLess>(&message)) {
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return Current(*context);
  }
  if (const auto *value = std::get_if<media_host_ipc::Decide>(&message)) {
    const auto found = candidates_.find(value->candidate_id);
    if (found == candidates_.end() || !Current(found->second))
      return false;
    *request_id = value->request_id;
    *context = found->second;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::Cancel>(&message)) {
    const auto found = requests_.find(value->request_id);
    if (found == requests_.end())
      return false;
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::Navigation>(&message)) {
    const auto found = tabs_.find(value->tab_id);
    if (found != tabs_.end() && value->generation <= found->second.generation)
      return false;
    if (found == tabs_.end() && tabs_.size() >= kMaxTrackedTabs)
      return false;
    tabs_[value->tab_id] = {value->navigation_id, value->generation, false};
    InvalidateTab(value->tab_id);
    *request_id = value->request_id;
    *context = {value->tab_id, value->navigation_id, value->generation};
    return true;
  }
  if (const auto *value = std::get_if<media_host_ipc::CloseTab>(&message)) {
    const auto found = tabs_.find(value->tab_id);
    if (found != tabs_.end() && value->generation < found->second.generation)
      return false;
    if (found == tabs_.end() && tabs_.size() >= kMaxTrackedTabs)
      return false;
    const std::uint64_t navigation =
        found == tabs_.end() ? 0 : found->second.navigation_id;
    tabs_[value->tab_id] = {navigation, value->generation, true};
    InvalidateTab(value->tab_id);
    *request_id = value->request_id;
    *context = {};
    return true;
  }
  return false;
}

bool MediaHostAdapter::Current(const Context &context) const {
  const auto found = tabs_.find(context.tab_id);
  if (found == tabs_.end())
    return false;
  return !found->second.closed &&
         found->second.navigation_id == context.navigation_id &&
         found->second.generation == context.generation;
}

void MediaHostAdapter::PollReplies() {
  const bool is_healthy = healthy();
  if (!is_healthy) {
    if (saw_healthy_)
      FailAll();
    saw_healthy_ = false;
    process_generation_ = 0;
    return;
  }
  const std::uint64_t generation = process_->generation();
  if (!saw_healthy_ || generation == 0 || generation != process_generation_) {
    FailAll();
    tabs_.clear();
    saw_healthy_ = true;
    process_generation_ = generation;
  }
  for (auto &message : process_->Drain(kMaxBrowserReplies)) {
    const std::string request_id = ReplyRequestId(message);
    const auto found = requests_.find(request_id);
    if (request_id.empty() || found == requests_.end())
      continue;
    if (!found->second.tab_id.empty() && !Current(found->second)) {
      requests_.erase(found);
      continue;
    }
    if (const auto *candidate =
            std::get_if<media_host_ipc::CandidateReply>(&message)) {
      if (candidate->candidate_id) {
        if (candidates_.size() >= kMaxTrackedCandidates &&
            candidates_.find(*candidate->candidate_id) == candidates_.end()) {
          FailAll();
          return;
        }
        candidates_[*candidate->candidate_id] = found->second;
      }
    }
    if (replies_.size() >= kMaxBrowserReplies) {
      FailAll();
      return;
    }
    replies_.push_back(std::move(message));
    requests_.erase(found);
  }
}

void MediaHostAdapter::InvalidateTab(const std::string &tab_id) {
  for (auto it = requests_.begin(); it != requests_.end();) {
    if (it->second.tab_id == tab_id)
      it = requests_.erase(it);
    else
      ++it;
  }
  for (auto it = candidates_.begin(); it != candidates_.end();) {
    if (it->second.tab_id == tab_id)
      it = candidates_.erase(it);
    else
      ++it;
  }
}

void MediaHostAdapter::FailAll() {
  requests_.clear();
  candidates_.clear();
  replies_.clear();
}

} // namespace crayon::browser::cef_shell::macos
