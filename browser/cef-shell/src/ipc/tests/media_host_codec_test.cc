#include "crayon/cef_shell_ipc/media_host_codec.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace mh = crayon::cef_shell::ipc::media_host;

#define CHECK_MH(condition)                                                    \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << " CHECK failed: " << #condition << '\n';                    \
      return false;                                                            \
    }                                                                          \
  } while (false)

mh::Playback Playback() {
  return {12'345, 60'000, false,  mh::AdContinuity::kPreserved, true, true,
          true,   true,   921'600};
}

mh::IngestUrl Ingest() {
  return {"request-1",
          "tab-1",
          7,
          9,
          123,
          "https://page.example/watch",
          "https://media.example/video.mp4?signature=fixture-value",
          mh::Source::kCurrentSrc,
          mh::HeadersClass::kNone,
          Playback(),
          false};
}

mh::DevicePageReply DevicePage() {
  return {"devices-1",
          5,
          0,
          std::nullopt,
          {{"receiver_1", "Living Room", mh::DeviceState::kReady, true}}};
}

std::vector<mh::Message> Messages() {
  return {
      Ingest(),
      mh::MarkEme{"eme-1", "tab-1", 7, 9},
      mh::Decide{"decide-1",
                 3,
                 124,
                 {true, true, false, true, false, false, 1080},
                 true},
      mh::DecideUrlLess{"url-less-1", "tab-1", 7, 9,
                        "https://page.example/watch", Playback(), false, true},
      mh::Cancel{"decide-1"},
      mh::Navigation{"nav-1", "tab-1", 8, 10},
      mh::CloseTab{"close-1", "tab-1", 10},
      mh::Shutdown{},
      mh::CandidateReply{"request-1", 3, "https://media.example"},
      mh::CandidateReply{"network-1", std::nullopt, ""},
      mh::DecisionReply{
          "decide-1",
          3,
          mh::Protocol::kMp4,
          {mh::DecisionKind::kDirect, std::nullopt, std::nullopt}},
      mh::DecisionReply{"url-less-1",
                        std::nullopt,
                        std::nullopt,
                        {mh::DecisionKind::kExternalClientHandoff,
                         mh::HandoffReason::kNoDirectUrl, std::nullopt}},
      mh::DecisionReply{"drm-1",
                        4,
                        mh::Protocol::kDash,
                        {mh::DecisionKind::kReject, std::nullopt,
                         mh::CoreError::kDrmProtected}},
      mh::Ack{"nav-1"},
      mh::ErrorReply{"bad-1", mh::HostError::kStaleContext},
      mh::Discovery{"discover-1", mh::DiscoveryAction::kRefresh},
      mh::ListDevices{"devices-1", std::nullopt, 0},
      DevicePage(),
      mh::StartCast{"cast-1", 7, "receiver_1", true},
      mh::StartCastReply{"cast-1",
                         {mh::CastStartKind::kCasting, 11,
                          mh::DeliveryRoute::kRelay, std::nullopt, std::nullopt,
                          std::nullopt}},
      mh::StartCastReply{"cast-failed",
                         {mh::CastStartKind::kFailed, std::nullopt,
                          std::nullopt, std::nullopt, std::nullopt,
                          mh::CastError::kReceiverUnreachable}},
      mh::StopCast{"stop-1", 11},
      mh::PollSessionEvents{"events-1"},
      mh::SessionEventsReply{
          "events-1",
          2,
          {{11, 3, mh::SessionPhase::kActive, mh::SessionPlayback::kPlaying,
            std::nullopt},
           {11, 4, mh::SessionPhase::kTerminated, mh::SessionPlayback::kStopped,
            mh::TerminalReason::kStoppedBySender}}},
      mh::ResolveCastCode{"code-1", "AB1 CD2"},
      mh::ResolveCastCodeReply{
          "code-1",
          mh::Device{"receiver_1", "Living Room", mh::DeviceState::kReady,
                     true},
          std::nullopt},
      mh::ResolveCastCodeReply{"code-failed", std::nullopt,
                               mh::CastError::kDeviceNotFound},
      mh::ControlCast{"pause-1", 11, mh::CastControlAction::kPause,
                      std::nullopt},
      mh::ControlCast{"seek-1", 11, mh::CastControlAction::kSeek, 30},
      mh::ControlCastReply{"pause-1", 11, std::nullopt},
      mh::ControlCastReply{"seek-failed", 11, mh::CastError::kRouteLost},
  };
}

std::string Hex(const std::vector<std::uint8_t> &bytes) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  for (auto byte : bytes) {
    result.push_back(kDigits[byte >> 4]);
    result.push_back(kDigits[byte & 15]);
  }
  return result;
}

bool RoundTripAndRustGolden() {
  constexpr char kRustCurrentAndPrevious[] =
      "4d4856310001010000000009726571756573742d31000000057461622d3100000000"
      "000000070000000000000009000000000000007b0000001a68747470733a2f2f7061"
      "67652e6578616d706c652f77617463680000003768747470733a2f2f6d656469612e"
      "6578616d706c652f766964656f2e6d70343f7369676e61747572653d666978747572"
      "652d76616c7565000001000000000000303901000000000000ea6000000101010100"
      "0e100000";
  mh::CodecError error;
  for (const auto &message : Messages()) {
    auto encoded = mh::Encode(message, &error);
    CHECK_MH(encoded);
    auto decoded = mh::Decode(*encoded, &error);
    CHECK_MH(decoded && *decoded == message);
  }
  auto encoded = mh::Encode(mh::Message(Ingest()), &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustCurrentAndPrevious);
  constexpr char kRustCastGolden[] =
      "4d48563100010f0000000009646576696365732d3100000000000000050000ffff"
      "00010000000a72656365697665725f310000000b4c6976696e6720526f6f6d0001";
  encoded = mh::Encode(mh::Message(DevicePage()), &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustCastGolden);
  constexpr char kRustResolveCastCodeGolden[] =
      "4d4856310001150000000006636f64652d310000000741423120434432";
  encoded =
      mh::Encode(mh::Message(mh::ResolveCastCode{"code-1", "AB1 CD2"}), &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustResolveCastCodeGolden);
  constexpr char kRustResolveCastCodeReplyGolden[] =
      "4d485631000116000000000b636f64652d6661696c65640100";
  encoded = mh::Encode(mh::Message(mh::ResolveCastCodeReply{
                           "code-failed", std::nullopt,
                           mh::CastError::kDeviceNotFound}),
                       &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustResolveCastCodeReplyGolden);
  constexpr char kRustControlCastGolden[] =
      "4d48563100011700000000067365656b2d31000000000000000b0201000000000000"
      "001e";
  encoded = mh::Encode(mh::Message(mh::ControlCast{
                           "seek-1", 11, mh::CastControlAction::kSeek, 30}),
                       &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustControlCastGolden);
  constexpr char kRustControlReplyGolden[] =
      "4d485631000118000000000b7365656b2d6661696c6564"
      "000000000000000b0108";
  encoded = mh::Encode(mh::Message(mh::ControlCastReply{
                           "seek-failed", 11, mh::CastError::kRouteLost}),
                       &error);
  CHECK_MH(encoded && Hex(*encoded) == kRustControlReplyGolden);
  return true;
}

bool HostileAndBounds() {
  mh::CodecError error;
  auto encoded = mh::Encode(mh::Message(Ingest()), &error).value();
  for (std::size_t cut = 0; cut < encoded.size(); ++cut) {
    CHECK_MH(!mh::Decode({encoded.begin(), encoded.begin() + cut}, &error));
  }
  auto malformed = encoded;
  malformed[0] = 'X';
  CHECK_MH(!mh::Decode(malformed, &error) &&
           error == mh::CodecError::kInvalidMagic);
  malformed = encoded;
  malformed[5] = 2;
  CHECK_MH(!mh::Decode(malformed, &error) &&
           error == mh::CodecError::kUnsupportedVersion);
  malformed = encoded;
  malformed[6] = 0xff;
  CHECK_MH(!mh::Decode(malformed, &error) &&
           error == mh::CodecError::kUnknownKind);
  malformed = encoded;
  malformed[7] = 1;
  CHECK_MH(!mh::Decode(malformed, &error) &&
           error == mh::CodecError::kInvalidFlags);
  malformed = encoded;
  malformed.push_back(0);
  CHECK_MH(!mh::Decode(malformed, &error) &&
           error == mh::CodecError::kTrailingBytes);
  CHECK_MH(
      !mh::Decode(std::vector<std::uint8_t>(mh::kMaxFrameBytes + 1), &error) &&
      error == mh::CodecError::kFrameTooLarge);

  auto bad = Ingest();
  bad.request_id.assign(129, 'x');
  CHECK_MH(!mh::Encode(mh::Message(bad), &error));
  bad = Ingest();
  bad.playback->position_ms = 9'007'199'254'740'993ULL;
  CHECK_MH(!mh::Encode(mh::Message(bad), &error));
  bad = Ingest();
  bad.media_url = "https://:443/video.mp4";
  CHECK_MH(!mh::Encode(mh::Message(bad), &error));
  bad = Ingest();
  bad.media_url = "https://media.example:99999/video.mp4";
  CHECK_MH(!mh::Encode(mh::Message(bad), &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::CandidateReply{"r-1", std::nullopt,
                                                      "https://media.example"}),
                       &error));
  CHECK_MH(
      !mh::Encode(mh::Message(mh::DecisionReply{
                      "r-2",
                      1,
                      std::nullopt,
                      {mh::DecisionKind::kDirect, std::nullopt, std::nullopt}}),
                  &error));
  CHECK_MH(!mh::Encode(
      mh::Message(mh::ListDevices{"bad-list", std::nullopt, 16}), &error));
  auto bad_page = DevicePage();
  bad_page.next_offset = uint16_t{2};
  CHECK_MH(!mh::Encode(mh::Message(bad_page), &error));
  bad_page = DevicePage();
  bad_page.devices.front().device_id = "receiver/invalid";
  CHECK_MH(!mh::Encode(mh::Message(bad_page), &error));
  bad_page = DevicePage();
  bad_page.devices.push_back(bad_page.devices.front());
  CHECK_MH(!mh::Encode(mh::Message(bad_page), &error));
  auto bad_outcome = mh::CastStartOutcome{mh::CastStartKind::kCasting,
                                          0,
                                          mh::DeliveryRoute::kDirect,
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt};
  CHECK_MH(!mh::Encode(mh::Message(mh::StartCastReply{"bad-cast", bad_outcome}),
                       &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::SessionEventsReply{
                           "bad-events",
                           0,
                           {{1, 1, mh::SessionPhase::kTerminated,
                             mh::SessionPlayback::kStopped, std::nullopt}}}),
                       &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::ResolveCastCode{"bad-code", "ABC/123"}),
                       &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::ResolveCastCodeReply{
                           "bad-code", std::nullopt, std::nullopt}),
                       &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::ResolveCastCodeReply{
                           "bad-code",
                           mh::Device{"receiver_1", "Living Room",
                                      mh::DeviceState::kReady, true},
                           mh::CastError::kDeviceNotFound}),
                       &error));
  CHECK_MH(!mh::Encode(
      mh::Message(mh::ControlCast{"bad-control", 0,
                                  mh::CastControlAction::kPlay, std::nullopt}),
      &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::ControlCast{
                           "bad-control", 1, mh::CastControlAction::kPause, 1}),
                       &error));
  CHECK_MH(!mh::Encode(mh::Message(mh::ControlCast{"bad-control", 1,
                                                   mh::CastControlAction::kSeek,
                                                   mh::kMaxSeekSeconds + 1}),
                       &error));
  auto bad_control_reply =
      mh::Encode(mh::Message(
                     mh::ControlCastReply{"bad-control", 1, std::nullopt}),
                 &error)
          .value();
  bad_control_reply.back() = 2;
  CHECK_MH(!mh::Decode(bad_control_reply, &error));
  auto cast = mh::Encode(mh::Message(DevicePage()), &error).value();
  auto oversized_count = cast;
  oversized_count[33] = 0;
  oversized_count[34] = static_cast<std::uint8_t>(mh::kMaxDevicePage + 1);
  CHECK_MH(!mh::Decode(oversized_count, &error));
  for (std::size_t cut = 0; cut < cast.size(); ++cut) {
    CHECK_MH(!mh::Decode({cast.begin(), cast.begin() + cut}, &error));
  }
  return true;
}

} // namespace

bool RunMediaHostCodecTests() {
  return RoundTripAndRustGolden() && HostileAndBounds();
}
