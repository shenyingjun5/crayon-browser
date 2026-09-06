#include "browser/media_host/alloy_cast_controller.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace cast_view = ::crayon::browser_cast_view;
namespace media_host = ::crayon::browser::cef_shell::media_host;
namespace mh = ::crayon::cef_shell::ipc::media_host;
namespace mh2 = ::crayon::cef_shell::ipc::media_host_v2;

#define CHECK_CAST(value)                                                   \
  do {                                                                      \
    if (!(value)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #value    \
                << '\n';                                                   \
      return false;                                                         \
    }                                                                       \
  } while (false)

class FakeTransport final : public media_host::MediaHostTransport {
 public:
  bool Start(std::string) override {
    healthy_ = true;
    ++host_generation;
    return true;
  }
  void Stop() override { healthy_ = false; }
  bool Enqueue(mh::Message message) override {
    if (!healthy_ || !enqueue_allowed) return false;
    sent.push_back(std::move(message));
    return true;
  }
  bool EnqueuePlayer(mh2::PlayerMessage) override { return healthy_; }
  bool EnqueuePlayerList(mh2::PlayerListRequest request) override {
    if (!healthy_ || !player_messages) return false;
    player_requests.push_back(std::move(request));
    return true;
  }
  std::vector<mh2::PlayerPageReply> DrainPlayerPages(
      std::size_t maximum) override {
    return Take(&player_replies, maximum);
  }
  bool EnqueueDraft(mh2::DraftCommand command) override {
    if (!healthy_ || !draft_messages) return false;
    draft_requests.push_back(std::move(command));
    return true;
  }
  std::vector<mh2::DraftStateReply> DrainDraftStates(
      std::size_t maximum) override {
    return Take(&draft_replies, maximum);
  }
  bool supports_player_messages() const noexcept override {
    return player_messages;
  }
  bool supports_drafts() const noexcept override { return draft_messages; }
  bool supports_connect() const noexcept override { return connect_messages; }
  std::uint64_t player_session_id() const noexcept override { return 77; }
  std::vector<mh::Message> Drain(std::size_t maximum) override {
    return Take(&inbound, maximum);
  }
  bool healthy() const noexcept override { return healthy_; }
  std::uint64_t generation() const noexcept override {
    return host_generation;
  }

  template <typename T>
  static std::vector<T> Take(std::vector<T>* source, std::size_t maximum) {
    const auto count = std::min(maximum, source->size());
    std::vector<T> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back(std::move(source->front()));
      source->erase(source->begin());
    }
    return result;
  }

  bool healthy_ = false;
  bool enqueue_allowed = true;
  bool player_messages = true;
  bool draft_messages = true;
  bool connect_messages = true;
  std::uint64_t host_generation = 0;
  std::vector<mh::Message> sent;
  std::vector<mh::Message> inbound;
  std::vector<mh2::PlayerListRequest> player_requests;
  std::vector<mh2::PlayerPageReply> player_replies;
  std::vector<mh2::DraftCommand> draft_requests;
  std::vector<mh2::DraftStateReply> draft_replies;
};

cast_view::CastViewContext Context() {
  return {9, "profile-a", 7, 11, 3};
}

cast_view::CastSelectionIntent Intent(
    cast_view::CastIntentKind kind,
    const cast_view::CastSelectionSnapshot& snapshot) {
  return {kind,
          snapshot.context,
          snapshot.view_revision,
          snapshot.draft_id,
          snapshot.draft_revision,
          std::nullopt,
          {},
          {},
          0,
          snapshot.session_generation};
}

mh2::DraftStateReply State(const mh2::DraftCommand& command,
                           std::uint64_t draft_id, std::uint64_t revision,
                           mh2::DraftPhase phase) {
  return {command.context,
          draft_id,
          revision,
          phase,
          mh2::DraftError::kNone,
          std::nullopt,
          {},
          false,
          false,
          mh2::DraftRoute::kNone,
          std::nullopt,
          mh2::DraftReason::kNone,
          std::nullopt};
}

std::string LastDeviceRequestId(const FakeTransport& fake) {
  for (auto it = fake.sent.rbegin(); it != fake.sent.rend(); ++it) {
    if (const auto* request = std::get_if<mh::ListDevices>(&*it))
      return request->request_id;
  }
  return {};
}

bool FullFlow() {
  auto transport = std::make_unique<FakeTransport>();
  auto* fake = transport.get();
  media_host::MediaHostAdapter adapter(std::move(transport));
  CHECK_CAST(adapter.Start("media-host"));
  std::vector<cast_view::CastSelectionSnapshot> emitted;
  media_host::AlloyCastController controller(
      &adapter, [&](auto snapshot) { emitted.push_back(std::move(snapshot)); },
      "Video", "Device", [] { return std::uint64_t{1000}; });
  CHECK_CAST(controller.BindContext(Context()));
  CHECK_CAST(controller.snapshot().compatible);
  CHECK_CAST(fake->player_requests.size() == 1);
  CHECK_CAST(!LastDeviceRequestId(*fake).empty());

  auto first_context = fake->player_requests.back().context;
  mh2::PlayerPageReply first{first_context, 41, mh2::PlayerPageStatus::kOk,
                             0, 16, {}};
  for (std::uint64_t index = 1; index <= 16; ++index) {
    first.players.push_back({index, 2, mh2::PlayerSourceKind::kHttpUrl,
                             true, true, index != 3, index == 2,
                             "https://same.example"});
  }
  fake->player_replies.push_back(std::move(first));
  fake->inbound.push_back(mh::DevicePageReply{
      LastDeviceRequestId(*fake), 5, 0, std::nullopt,
      {{"device-1", "Living room", mh::DeviceState::kReady, true}}});
  controller.Tick();
  CHECK_CAST(fake->player_requests.size() == 2);
  const auto second_context = fake->player_requests.back().context;
  fake->player_replies.push_back(
      {second_context,
       41,
       mh2::PlayerPageStatus::kOk,
       16,
       std::nullopt,
       {{17, 2, mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
         "https://same.example"}}});
  controller.Tick();
  CHECK_CAST(controller.snapshot().media_total == 17);
  CHECK_CAST(controller.snapshot().eligible_count == 15);
  CHECK_CAST(controller.snapshot().device_total == 1);

  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kOpen, controller.snapshot())));
  auto state = State(fake->draft_requests.back(), 91, 1,
                     mh2::DraftPhase::kChoosing);
  fake->draft_replies.push_back(state);
  controller.Tick();
  auto page = Intent(cast_view::CastIntentKind::kMediaPage,
                     controller.snapshot());
  page.page_offset = 16;
  CHECK_CAST(controller.HandleIntent(page));
  CHECK_CAST(controller.snapshot().media.size() == 1);
  CHECK_CAST(controller.snapshot().media.front().ref.instance_id == 17);
  page = Intent(cast_view::CastIntentKind::kMediaPage, controller.snapshot());
  page.page_offset = 0;
  CHECK_CAST(controller.HandleIntent(page));
  auto select = Intent(cast_view::CastIntentKind::kSelectMedia,
                       controller.snapshot());
  select.media = controller.snapshot().media.front().ref;
  CHECK_CAST(controller.HandleIntent(select));
  state = State(fake->draft_requests.back(), 91, 2,
                mh2::DraftPhase::kChoosing);
  state.media = mh2::DraftMediaRef{1, 2};
  fake->draft_replies.push_back(state);
  controller.Tick();

  auto code = Intent(cast_view::CastIntentKind::kLookupCode,
                     controller.snapshot());
  code.cast_code = "AB12";
  CHECK_CAST(controller.HandleIntent(code));
  std::string resolve_id;
  for (auto it = fake->sent.rbegin(); it != fake->sent.rend(); ++it) {
    if (const auto* request = std::get_if<mh::ResolveCastCode>(&*it)) {
      resolve_id = request->request_id;
      break;
    }
  }
  CHECK_CAST(!resolve_id.empty());
  fake->inbound.push_back(mh::ResolveCastCodeReply{
      resolve_id,
      mh::Device{"device-2", "Bedroom", mh::DeviceState::kReady, true},
      std::nullopt});
  controller.Tick();
  CHECK_CAST(fake->draft_requests.back().action ==
             mh2::DraftAction::kSelectDevice);
  CHECK_CAST(std::none_of(fake->sent.begin(), fake->sent.end(), [](const auto& m) {
    return std::holds_alternative<mh::StartCast>(m);
  }));
  state = State(fake->draft_requests.back(), 91, 3,
                mh2::DraftPhase::kChoosing);
  state.media = mh2::DraftMediaRef{1, 2};
  state.device_id = "device-2";
  fake->draft_replies.push_back(state);
  controller.Tick();

  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kConnectDevice,
             controller.snapshot())));
  state = State(fake->draft_requests.back(), 91, 4,
                mh2::DraftPhase::kChoosing);
  state.media = mh2::DraftMediaRef{1, 2};
  state.device_id = "device-2";
  state.device_connected = true;
  fake->draft_replies.push_back(state);
  controller.Tick();
  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kPrepare, controller.snapshot())));
  state = State(fake->draft_requests.back(), 91, 5,
                mh2::DraftPhase::kFailed);
  state.error = mh2::DraftError::kUnavailable;
  state.media = mh2::DraftMediaRef{1, 2};
  state.device_id = "device-2";
  state.device_connected = true;
  state.reason = mh2::DraftReason::kTimeout;
  fake->draft_replies.push_back(state);
  controller.Tick();
  CHECK_CAST(controller.snapshot().reason ==
             cast_view::CastFailureReason::kTimeout);

  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kPrepare, controller.snapshot())));
  state = State(fake->draft_requests.back(), 91, 6,
                mh2::DraftPhase::kPrepared);
  state.media = mh2::DraftMediaRef{1, 2};
  state.device_id = "device-2";
  state.device_connected = true;
  state.route = mh2::DraftRoute::kDirect;
  state.prepared_until_ms = 10'000;
  fake->draft_replies.push_back(state);
  controller.Tick();
  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kCommit, controller.snapshot())));
  state = State(fake->draft_requests.back(), 91, 7,
                mh2::DraftPhase::kCommitted);
  state.media = mh2::DraftMediaRef{1, 2};
  state.device_id = "device-2";
  state.device_connected = true;
  state.session_generation = 47;
  fake->draft_replies.push_back(state);
  controller.Tick();
  CHECK_CAST(controller.snapshot().phase ==
             cast_view::CastDraftPhase::kCommitted);
  CHECK_CAST(controller.snapshot().session_generation == 47);
  CHECK_CAST(controller.snapshot().picker_open);
  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kCancel, controller.snapshot())));
  CHECK_CAST(!controller.snapshot().picker_open);
  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kOpen, controller.snapshot())));
  CHECK_CAST(controller.snapshot().picker_open);

  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kPause, controller.snapshot())));
  CHECK_CAST(std::holds_alternative<mh::ControlCast>(fake->sent.back()));
  CHECK_CAST(std::get<mh::ControlCast>(fake->sent.back()).action ==
             mh::CastControlAction::kPause);
  CHECK_CAST(controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kStop, controller.snapshot())));
  CHECK_CAST(std::holds_alternative<mh::StopCast>(fake->sent.back()));
  CHECK_CAST(std::get<mh::StopCast>(fake->sent.back()).session_generation == 47);
  controller.Shutdown();
  return true;
}

bool FailedContextBindCanRetry() {
  auto transport = std::make_unique<FakeTransport>();
  auto* fake = transport.get();
  media_host::MediaHostAdapter adapter(std::move(transport));
  CHECK_CAST(adapter.Start("media-host"));
  fake->enqueue_allowed = false;
  media_host::AlloyCastController controller(
      &adapter, [](auto) {}, "Video", "Device",
      [] { return std::uint64_t{1000}; });
  CHECK_CAST(!controller.BindContext(Context()));
  fake->enqueue_allowed = true;
  CHECK_CAST(controller.BindContext(Context()));
  CHECK_CAST(controller.snapshot().compatible);
  CHECK_CAST(!fake->sent.empty());
  return true;
}

bool CompatibilityFailsClosed() {
  auto transport = std::make_unique<FakeTransport>();
  auto* fake = transport.get();
  fake->connect_messages = false;
  media_host::MediaHostAdapter adapter(std::move(transport));
  CHECK_CAST(adapter.Start("media-host"));
  media_host::AlloyCastController controller(&adapter, {}, "Video", "Device");
  CHECK_CAST(controller.BindContext(Context()));
  CHECK_CAST(!controller.snapshot().compatible);
  CHECK_CAST(!controller.HandleIntent(
      Intent(cast_view::CastIntentKind::kOpen, controller.snapshot())));
  CHECK_CAST(fake->draft_requests.empty());
  return true;
}

bool OverlayMediaIsRevalidated() {
  auto transport = std::make_unique<FakeTransport>();
  auto* fake = transport.get();
  media_host::MediaHostAdapter adapter(std::move(transport));
  CHECK_CAST(adapter.Start("media-host"));
  media_host::AlloyCastController controller(&adapter, {}, "Video", "Device");
  CHECK_CAST(controller.BindContext(Context()));
  CHECK_CAST(!controller.OpenForMedia({0, 1}));
  CHECK_CAST(!controller.OpenForMedia({8, 1}));
  const auto request = fake->player_requests.back();
  fake->player_replies.push_back(
      {request.context,
       1,
       mh2::PlayerPageStatus::kOk,
       0,
       std::nullopt,
       {{8, 2, mh2::PlayerSourceKind::kHttpUrl, true, true, true, false,
         "Video"}}});
  controller.Tick();
  CHECK_CAST(!controller.OpenForMedia({8, 1}));
  CHECK_CAST(controller.OpenForMedia({8, 2}));
  CHECK_CAST(fake->draft_requests.size() == 1);
  CHECK_CAST(fake->draft_requests.back().action == mh2::DraftAction::kOpen);
  auto state = State(fake->draft_requests.back(), 4, 1,
                     mh2::DraftPhase::kChoosing);
  fake->draft_replies.push_back(state);
  controller.Tick();
  CHECK_CAST(fake->draft_requests.size() == 2);
  CHECK_CAST(fake->draft_requests.back().action ==
             mh2::DraftAction::kSelectMedia);
  CHECK_CAST(fake->draft_requests.back().media &&
             fake->draft_requests.back().media->instance_id == 8 &&
             fake->draft_requests.back().media->source_revision == 2);
  const cast_view::CastViewContext next_context{1, "profile", 2, 4, 4};
  CHECK_CAST(controller.BindContext(next_context));
  CHECK_CAST(!controller.OpenForMedia({8, 2}));
  return true;
}

}  // namespace

int main() {
  const std::pair<const char*, bool (*)()> tests[] = {
      {"full_flow", &FullFlow},
      {"failed_context_bind_can_retry", &FailedContextBindCanRetry},
      {"compatibility_fails_closed", &CompatibilityFailsClosed},
      {"overlay_media_is_revalidated", &OverlayMediaIsRevalidated},
  };
  for (const auto& [name, test] : tests) {
    if (!test()) {
      std::cerr << name << " failed\n";
      return 1;
    }
  }
  std::cout << "alloy cast controller tests passed\n";
  return 0;
}
