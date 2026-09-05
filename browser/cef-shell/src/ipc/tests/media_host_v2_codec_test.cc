#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace v2 = crayon::cef_shell::ipc::media_host_v2;
namespace v1 = crayon::cef_shell::ipc::media_host;
namespace {
#define CHECK_V2(expr)                                                         \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << __LINE__ << ": " << #expr << '\n';                          \
      return false;                                                            \
    }                                                                          \
  } while (false)
v2::Handshake Hello() { return {v2::Kind::kHello, 7, 9, 15, 16384, 16}; }
v2::Handshake Welcome() { return {v2::Kind::kWelcome, 7, 9, 1, 8192, 8}; }
v2::PlayerContext PlayerContext() { return {7, 9, 11, 13, 17, 19, 23}; }
v2::PlayerFact PlayerFact() {
  return {PlayerContext(),
          29,
          v2::PlayerSourceKind::kHttpUrl,
          31,
          37,
          false,
          true,
          true,
          true,
          false,
          41,
          "https://page.test/a",
          "https://cdn.test/v.mp4"};
}
v2::PlayerPageContext PageContext() { return {7, 9, 29, 11, 13, 17}; }
v2::PlayerListRequest ListRequest() { return {PageContext(), 0, 2, 2}; }
v2::PlayerPageReply PageReply() {
  return {PageContext(),
          31,
          v2::PlayerPageStatus::kOk,
          2,
          std::uint16_t{4},
          {{19, 23, v2::PlayerSourceKind::kHttpUrl, true, true, true, false,
            "https://cdn.test"},
           {37, 41, v2::PlayerSourceKind::kBlobUrl, true, false, false, false,
            ""}}};
}
v2::PlayerPageReply StaleReply() {
  auto reply = PageReply();
  reply.status = v2::PlayerPageStatus::kStale;
  reply.next_offset.reset();
  reply.players.clear();
  return reply;
}
v2::DraftContext DraftContext() { return {7, 9, 29, "profile-a", 11, 13, 17}; }
v2::DraftCommand DraftCommand() {
  return {DraftContext(),
          v2::DraftAction::kSelectMedia,
          31,
          37,
          v2::DraftMediaRef{19, 23},
          ""};
}
v2::DraftStateReply DraftState() {
  return {DraftContext(),
          31,
          41,
          v2::DraftPhase::kPrepared,
          v2::DraftError::kNone,
          v2::DraftMediaRef{19, 23},
          "device-1",
          true,
          false,
          v2::DraftRoute::kDirect,
          std::uint64_t{43},
          v2::DraftReason::kNone,
          std::nullopt};
}
std::vector<std::uint8_t> Hex(const std::string &hex) {
  std::vector<std::uint8_t> wire;
  for (std::size_t i = 0; i < hex.size(); i += 2)
    wire.push_back(
        static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  return wire;
}
bool Golden() {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) +
                      "/tests/contracts/media_host_v2_handshake.golden");
  CHECK_V2(input.good());
  std::string name, hex;
  unsigned count = 0;
  while (input >> name >> hex) {
    CHECK_V2(name == "hello" || name == "welcome" || name == "hello-boundary");
    CHECK_V2(hex.size() == v2::kHandshakeBytes * 2);
    std::vector<std::uint8_t> wire;
    for (std::size_t i = 0; i < hex.size(); i += 2)
      wire.push_back(
          static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    auto expected = name == "welcome" ? Welcome() : Hello();
    if (name == "hello-boundary") {
      expected.session_id = std::numeric_limits<std::uint64_t>::max();
      expected.generation = 0x0102030405060708ULL;
      expected.capabilities = 0;
      expected.max_frame_bytes = 34;
      expected.max_page_items = 1;
    }
    auto encoded = v2::Encode(expected);
    CHECK_V2(encoded && *encoded == wire);
    auto decoded = v2::Decode(wire);
    CHECK_V2(decoded && *decoded == expected);
    CHECK_V2(!v1::Decode(wire, nullptr));
    ++count;
  }
  CHECK_V2(count == 3);
  auto previous = v1::Encode(v1::Message(v1::Shutdown{}), nullptr);
  CHECK_V2(previous && !v2::Decode(*previous));
  return true;
}
bool RejectFrames() {
  auto wire = v2::Encode(Hello()).value();
  for (std::size_t n = 0; n < wire.size(); ++n)
    CHECK_V2(!v2::Decode({wire.begin(), wire.begin() + n}));
  auto extra = wire;
  extra.push_back(0);
  CHECK_V2(!v2::Decode(extra));
  CHECK_V2(!v2::Decode(std::vector<std::uint8_t>(v2::kMaxFrameBytes + 1)));
  for (auto [index, byte] : std::vector<std::pair<std::size_t, std::uint8_t>>{
           {std::size_t{0}, static_cast<std::uint8_t>('X')},
           {std::size_t{5}, std::uint8_t{1}},
           {std::size_t{6}, std::uint8_t{0}},
           {std::size_t{6}, std::uint8_t{3}},
           {std::size_t{7}, std::uint8_t{1}},
           {std::size_t{27}, std::uint8_t{64}},
           {std::size_t{31}, std::uint8_t{1}},
           {std::size_t{33}, std::uint8_t{17}}}) {
    auto bad = wire;
    bad[index] = byte;
    CHECK_V2(!v2::Decode(bad));
  }
  for (auto [begin, end] : std::vector<std::pair<std::size_t, std::size_t>>{
           {8, 16}, {16, 24}, {28, 32}, {32, 34}}) {
    auto bad = wire;
    for (auto i = begin; i < end; ++i)
      bad[i] = 0;
    CHECK_V2(!v2::Decode(bad));
  }
  return true;
}
bool LocalValidationAndNegotiation() {
  CHECK_V2(v2::MatchesHello(Hello(), Welcome()));
  CHECK_V2(!v2::MatchesHello(Welcome(), Hello()));
  auto bad = Hello();
  bad.kind = static_cast<v2::Kind>(3);
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.session_id = 0;
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.generation = 0;
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.capabilities = 64;
  CHECK_V2(!v2::Encode(bad));
  for (auto frame : {0u, 33u, 16385u}) {
    bad = Hello();
    bad.max_frame_bytes = frame;
    CHECK_V2(!v2::Encode(bad));
    CHECK_V2(!v2::MatchesHello(bad, Welcome()));
  }
  for (std::uint16_t page : {std::uint16_t{0}, std::uint16_t{17}}) {
    bad = Hello();
    bad.max_page_items = page;
    CHECK_V2(!v2::Encode(bad));
  }
  bad = Welcome();
  bad.session_id = 8;
  CHECK_V2(!v2::MatchesHello(Hello(), bad));
  bad = Welcome();
  bad.generation = 10;
  CHECK_V2(!v2::MatchesHello(Hello(), bad));
  auto restricted = Hello();
  restricted.capabilities = 1;
  restricted.max_frame_bytes = 1024;
  restricted.max_page_items = 1;
  CHECK_V2(!v2::MatchesHello(restricted, Welcome()));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.max_frame_bytes = 1025;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.capabilities = 2;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.max_page_items = 2;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  auto empty = Hello();
  empty.capabilities = 0;
  empty.max_frame_bytes = 34;
  empty.max_page_items = 1;
  auto wire = v2::Encode(empty);
  CHECK_V2(wire && v2::Decode(*wire).value() == empty);
  bad = empty;
  bad.kind = v2::Kind::kWelcome;
  CHECK_V2(v2::MatchesHello(empty, bad));
  CHECK_V2(!v2::MatchesHello(empty, Welcome()));
  return true;
}
bool PlayerGoldenAndIsolation() {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) +
                      "/tests/contracts/media_host_v2_player.golden");
  CHECK_V2(input.good());
  std::string name, hex;
  unsigned count = 0;
  while (input >> name >> hex) {
    v2::PlayerMessage expected = name == "player-upsert"
                                     ? v2::PlayerMessage(PlayerFact())
                                     : v2::PlayerMessage(PlayerContext());
    CHECK_V2(name == "player-upsert" || name == "player-remove");
    const auto wire = Hex(hex);
    const auto encoded = v2::EncodePlayerMessage(expected);
    CHECK_V2(encoded && *encoded == wire);
    const auto decoded = v2::DecodePlayerMessage(wire);
    CHECK_V2(decoded && *decoded == expected);
    CHECK_V2(!v2::Decode(wire));
    CHECK_V2(!v1::Decode(wire, nullptr));
    ++count;
  }
  CHECK_V2(count == 2);
  const auto previous = v1::Encode(v1::Message(v1::Shutdown{}), nullptr);
  CHECK_V2(previous && !v2::DecodePlayerMessage(*previous));

  auto url_less = PlayerFact();
  url_less.source_kind = v2::PlayerSourceKind::kBlobUrl;
  url_less.media_url.clear();
  const auto wire = v2::EncodePlayerMessage(v2::PlayerMessage(url_less));
  CHECK_V2(wire && v2::DecodePlayerMessage(*wire).value() ==
                       v2::PlayerMessage(url_less));
  return true;
}
bool RejectPlayerValuesAndFrames() {
  for (int field = 0; field < 7; ++field) {
    auto context = PlayerContext();
    switch (field) {
    case 0:
      context.session_id = 0;
      break;
    case 1:
      context.host_generation = 0;
      break;
    case 2:
      context.tab_id = 0;
      break;
    case 3:
      context.navigation_id = 0;
      break;
    case 4:
      context.tab_generation = 0;
      break;
    case 5:
      context.instance_id = 0;
      break;
    default:
      context.source_revision = 0;
      break;
    }
    CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(context)));
  }
  auto fact = PlayerFact();
  fact.observed_at_ms = 0;
  CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(fact)));
  for (const auto &url : {std::string("file:///tmp/a"), std::string("https://"),
                          std::string("https://a.test/\nsecret")}) {
    fact = PlayerFact();
    fact.page_url = url;
    CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(fact)));
  }
  fact = PlayerFact();
  fact.page_url = "https://a.test/" + std::string(2048, 'a');
  CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(fact)));
  fact = PlayerFact();
  fact.media_url.clear();
  CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(fact)));
  fact = PlayerFact();
  fact.source_kind = v2::PlayerSourceKind::kMediaStream;
  CHECK_V2(!v2::EncodePlayerMessage(v2::PlayerMessage(fact)));

  auto wire = v2::EncodePlayerMessage(v2::PlayerMessage(PlayerFact())).value();
  for (std::size_t length = 0; length < wire.size(); ++length)
    CHECK_V2(!v2::DecodePlayerMessage(
        {wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(length)}));
  auto bad = wire;
  bad.push_back(0);
  CHECK_V2(!v2::DecodePlayerMessage(bad));
  CHECK_V2(!v2::DecodePlayerMessage(
      std::vector<std::uint8_t>(v2::kMaxFrameBytes + 1)));
  for (auto [index, byte] : std::vector<std::pair<std::size_t, std::uint8_t>>{
           {0, static_cast<std::uint8_t>('X')},
           {5, std::uint8_t{1}},
           {6, std::uint8_t{9}},
           {7, std::uint8_t{1}},
           {64, std::uint8_t{9}},
           {73, std::uint8_t{2}}}) {
    bad = wire;
    bad[index] = byte;
    CHECK_V2(!v2::DecodePlayerMessage(bad));
  }
  const std::string page_url = "https://page.test/a";
  const auto page =
      std::search(wire.begin(), wire.end(), page_url.begin(), page_url.end());
  CHECK_V2(page != wire.end());
  bad = wire;
  bad[static_cast<std::size_t>(page - wire.begin())] = 0xff;
  CHECK_V2(!v2::DecodePlayerMessage(bad));
  return true;
}
bool PlayerPageGoldenAndIsolation() {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) +
                      "/tests/contracts/media_host_v2_player_page.golden");
  CHECK_V2(input.good());
  std::string name, hex;
  unsigned count = 0;
  while (input >> name >> hex) {
    v2::PlayerPageMessage expected =
        name == "player-list"
            ? v2::PlayerPageMessage(ListRequest())
            : v2::PlayerPageMessage(name == "player-page" ? PageReply()
                                                          : StaleReply());
    CHECK_V2(name == "player-list" || name == "player-page" ||
             name == "player-page-stale");
    const auto wire = Hex(hex);
    const auto encoded = v2::EncodePlayerPageMessage(expected);
    CHECK_V2(encoded && *encoded == wire);
    const auto decoded = v2::DecodePlayerPageMessage(wire);
    CHECK_V2(decoded && *decoded == expected);
    CHECK_V2(!v2::Decode(wire));
    CHECK_V2(!v2::DecodePlayerMessage(wire));
    CHECK_V2(!v1::Decode(wire, nullptr));
    ++count;
  }
  CHECK_V2(count == 3);
  return true;
}
bool RejectPlayerPageValuesAndFrames() {
  for (int field = 0; field < 6; ++field) {
    auto request = ListRequest();
    switch (field) {
    case 0:
      request.context.session_id = 0;
      break;
    case 1:
      request.context.host_generation = 0;
      break;
    case 2:
      request.context.request_id = 0;
      break;
    case 3:
      request.context.tab_id = 0;
      break;
    case 4:
      request.context.navigation_id = 0;
      break;
    default:
      request.context.tab_generation = 0;
      break;
    }
    CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(request)));
  }
  for (std::uint16_t maximum : {std::uint16_t{0}, std::uint16_t{17}}) {
    auto request = ListRequest();
    request.max_items = maximum;
    CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(request)));
  }
  auto reply = PageReply();
  reply.snapshot_revision = 0;
  CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(reply)));
  reply = PageReply();
  reply.next_offset = std::uint16_t{5};
  CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(reply)));
  reply = StaleReply();
  reply.players.push_back(PageReply().players.front());
  CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(reply)));
  for (const auto &origin :
       {std::string("https://cdn.test/path"),
        std::string("https://user@cdn.test"), std::string("https://CDN.test"),
        std::string("https://cdn.test:443"), std::string("file:///tmp/a")}) {
    reply = PageReply();
    reply.players.front().redacted_origin = origin;
    CHECK_V2(!v2::EncodePlayerPageMessage(v2::PlayerPageMessage(reply)));
  }

  auto wire =
      v2::EncodePlayerPageMessage(v2::PlayerPageMessage(PageReply())).value();
  for (std::size_t length = 0; length < wire.size(); ++length)
    CHECK_V2(!v2::DecodePlayerPageMessage(
        {wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(length)}));
  auto bad = wire;
  bad.push_back(0);
  CHECK_V2(!v2::DecodePlayerPageMessage(bad));
  for (auto [index, byte] : std::vector<std::pair<std::size_t, std::uint8_t>>{
           {0, static_cast<std::uint8_t>('X')},
           {5, std::uint8_t{1}},
           {6, std::uint8_t{9}},
           {7, std::uint8_t{1}},
           {56, std::uint8_t{9}},
           {59, std::uint8_t{2}}}) {
    bad = wire;
    bad[index] = byte;
    CHECK_V2(!v2::DecodePlayerPageMessage(bad));
  }
  const std::string origin = "https://cdn.test";
  const auto found =
      std::search(wire.begin(), wire.end(), origin.begin(), origin.end());
  CHECK_V2(found != wire.end());
  bad = wire;
  bad[static_cast<std::size_t>(found - wire.begin())] = 0xff;
  CHECK_V2(!v2::DecodePlayerPageMessage(bad));
  return true;
}
bool DraftGoldenAndIsolation() {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) +
                      "/tests/contracts/media_host_v2_draft.golden");
  CHECK_V2(input.good());
  std::string name, hex;
  unsigned count = 0;
  while (input >> name >> hex) {
    v2::DraftMessage expected = name == "draft-select-media"
                                    ? v2::DraftMessage(DraftCommand())
                                    : v2::DraftMessage(DraftState());
    CHECK_V2(name == "draft-select-media" || name == "draft-prepared");
    const auto wire = Hex(hex);
    const auto encoded = v2::EncodeDraftMessage(expected);
    CHECK_V2(encoded && *encoded == wire);
    const auto decoded = v2::DecodeDraftMessage(wire);
    CHECK_V2(decoded && *decoded == expected);
    CHECK_V2(!v2::Decode(wire));
    CHECK_V2(!v2::DecodePlayerMessage(wire));
    CHECK_V2(!v2::DecodePlayerPageMessage(wire));
    CHECK_V2(!v1::Decode(wire, nullptr));
    ++count;
  }
  CHECK_V2(count == 2);

  std::ifstream reason_input(
      std::string(CRAYON_SOURCE_ROOT) +
      "/tests/contracts/media_host_v2_draft_reason.golden");
  CHECK_V2(reason_input.good());
  CHECK_V2(reason_input >> name >> hex);
  CHECK_V2(name == "draft-prepared-recognized");
  auto reason_state = DraftState();
  reason_state.reason = v2::DraftReason::kRecognized;
  const auto reason_wire = Hex(hex);
  const v2::DraftMessage reason_message(reason_state);
  CHECK_V2(v2::EncodeDraftMessage(reason_message) == reason_wire);
  CHECK_V2(v2::DecodeDraftMessage(reason_wire) == reason_message);
  CHECK_V2(!(reason_input >> name >> hex));

  std::ifstream session_input(
      std::string(CRAYON_SOURCE_ROOT) +
      "/tests/contracts/media_host_v2_draft_session.golden");
  CHECK_V2(session_input.good());
  CHECK_V2(session_input >> name >> hex);
  CHECK_V2(name == "draft-committed");
  auto session_state = DraftState();
  session_state.phase = v2::DraftPhase::kCommitted;
  session_state.route = v2::DraftRoute::kNone;
  session_state.prepared_until_ms.reset();
  session_state.session_generation = 47;
  const auto session_wire = Hex(hex);
  const v2::DraftMessage session_message(session_state);
  CHECK_V2(v2::EncodeDraftMessage(session_message) == session_wire);
  CHECK_V2(v2::DecodeDraftMessage(session_wire) == session_message);
  CHECK_V2(!(session_input >> name >> hex));
  return true;
}
bool DraftRoundTripAndReject() {
  for (auto action :
       {v2::DraftAction::kOpen, v2::DraftAction::kSelectMedia,
        v2::DraftAction::kSelectDevice, v2::DraftAction::kConnect,
        v2::DraftAction::kPrepare, v2::DraftAction::kConfirmReplacement,
        v2::DraftAction::kCommit, v2::DraftAction::kCancel}) {
    auto command = DraftCommand();
    command.action = action;
    command.media.reset();
    if (action == v2::DraftAction::kSelectMedia)
      command.media = v2::DraftMediaRef{19, 23};
    if (action == v2::DraftAction::kSelectDevice)
      command.device_id = "device-1";
    if (action == v2::DraftAction::kOpen) {
      command.draft_id = 0;
      command.draft_revision = 0;
    }
    const v2::DraftMessage message(command);
    const auto wire = v2::EncodeDraftMessage(message);
    CHECK_V2(wire && v2::DecodeDraftMessage(*wire) == message);
  }
  auto bad_command = DraftCommand();
  bad_command.media.reset();
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_command)));
  bad_command = DraftCommand();
  bad_command.action = v2::DraftAction::kSelectDevice;
  bad_command.media.reset();
  bad_command.device_id = std::string("bad") + "\xe2\x80\xae" + "id";
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_command)));
  auto bad_state = DraftState();
  bad_state.prepared_until_ms.reset();
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_state)));
  bad_state = DraftState();
  bad_state.phase = v2::DraftPhase::kChoosing;
  bad_state.device_connected = false;
  bad_state.route = v2::DraftRoute::kNone;
  bad_state.prepared_until_ms.reset();
  bad_state.reason = v2::DraftReason::kTimeout;
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_state)));
  bad_state = DraftState();
  bad_state.session_generation = 47;
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_state)));

  for (auto reason : {v2::DraftReason::kCredentials,
                      v2::DraftReason::kProtection,
                      v2::DraftReason::kRecognized,
                      v2::DraftReason::kUnrecognized,
                      v2::DraftReason::kRedirectRefused,
                      v2::DraftReason::kUpstreamRejected,
                      v2::DraftReason::kAddressRejected,
                      v2::DraftReason::kDns,
                      v2::DraftReason::kConnect,
                      v2::DraftReason::kTimeout,
                      v2::DraftReason::kTransport,
                      v2::DraftReason::kInvalidTarget}) {
    auto state = DraftState();
    state.reason = reason;
    const v2::DraftMessage message(state);
    const auto reason_wire = v2::EncodeDraftMessage(message);
    CHECK_V2(reason_wire && (*reason_wire)[6] == 9 &&
             v2::DecodeDraftMessage(*reason_wire) == message);
  }
  bad_state = DraftState();
  bad_state.phase = v2::DraftPhase::kChoosing;
  bad_state.prepared_until_ms.reset();
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_state)));
  bad_state = DraftState();
  bad_state.phase = v2::DraftPhase::kFailed;
  bad_state.error = v2::DraftError::kNone;
  bad_state.device_connected = false;
  bad_state.route = v2::DraftRoute::kNone;
  bad_state.prepared_until_ms.reset();
  CHECK_V2(!v2::EncodeDraftMessage(v2::DraftMessage(bad_state)));

  auto wire = v2::EncodeDraftMessage(v2::DraftMessage(DraftState())).value();
  for (std::size_t length = 0; length < wire.size(); ++length)
    CHECK_V2(!v2::DecodeDraftMessage(
        {wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(length)}));
  auto bad = wire;
  bad.push_back(0);
  CHECK_V2(!v2::DecodeDraftMessage(bad));
  for (auto [index, byte] : std::vector<std::pair<std::size_t, std::uint8_t>>{
           {0, static_cast<std::uint8_t>('X')},
           {5, std::uint8_t{1}},
           {6, std::uint8_t{11}},
           {7, std::uint8_t{1}}}) {
    bad = wire;
    bad[index] = byte;
    CHECK_V2(!v2::DecodeDraftMessage(bad));
  }
  const std::string profile = "profile-a";
  const auto found =
      std::search(wire.begin(), wire.end(), profile.begin(), profile.end());
  CHECK_V2(found != wire.end());
  bad = wire;
  bad[static_cast<std::size_t>(found - wire.begin())] = 0xff;
  CHECK_V2(!v2::DecodeDraftMessage(bad));
  auto reason_state = DraftState();
  reason_state.reason = v2::DraftReason::kTimeout;
  const auto reason_wire =
      v2::EncodeDraftMessage(v2::DraftMessage(reason_state)).value();
  CHECK_V2(!v2::DecodeDraftMessage(
      {reason_wire.begin(), reason_wire.end() - 1}));
  bad = reason_wire;
  bad.back() = 0;
  CHECK_V2(!v2::DecodeDraftMessage(bad));
  bad.back() = 13;
  CHECK_V2(!v2::DecodeDraftMessage(bad));
  auto session_state = DraftState();
  session_state.phase = v2::DraftPhase::kCommitted;
  session_state.route = v2::DraftRoute::kNone;
  session_state.prepared_until_ms.reset();
  session_state.session_generation = 47;
  const auto session_wire =
      v2::EncodeDraftMessage(v2::DraftMessage(session_state)).value();
  CHECK_V2(!v2::DecodeDraftMessage(
      {session_wire.begin(), session_wire.end() - 1}));
  bad = session_wire;
  std::fill(bad.end() - 8, bad.end(), std::uint8_t{0});
  CHECK_V2(!v2::DecodeDraftMessage(bad));
  return true;
}
} // namespace
int main() {
  if (!Golden() || !RejectFrames() || !LocalValidationAndNegotiation() ||
      !PlayerGoldenAndIsolation() || !RejectPlayerValuesAndFrames() ||
      !PlayerPageGoldenAndIsolation() || !RejectPlayerPageValuesAndFrames() ||
      !DraftGoldenAndIsolation() || !DraftRoundTripAndReject())
    return 1;
  std::cout << "media_host_v2_codec: 9 cases PASS\n";
  return 0;
}
