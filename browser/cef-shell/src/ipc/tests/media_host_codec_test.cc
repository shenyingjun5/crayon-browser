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
  return true;
}

} // namespace

bool RunMediaHostCodecTests() {
  return RoundTripAndRustGolden() && HostileAndBounds();
}
