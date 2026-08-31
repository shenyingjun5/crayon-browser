// PLT-M05b2b2: platform-neutral Browser <-> Rust media-host v1 DTO/codec.
// URL-bearing request DTOs deliberately expose no diagnostic formatting API.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace crayon::cef_shell::ipc::media_host {

inline constexpr std::size_t kMaxFrameBytes = 16 * 1024;

enum class Source : std::uint8_t { kCurrentSrc = 0, kNetworkRequest = 1 };
enum class HeadersClass : std::uint8_t {
  kNone = 0,
  kRefererOnly,
  kRefererAndUa,
  kCredentialBound,
};
enum class AdContinuity : std::uint8_t {
  kPreserved = 0,
  kNotApplicable,
  kUnknown,
};
enum class Protocol : std::uint8_t { kHls = 0, kDash, kMp4 };
enum class DecisionKind : std::uint8_t {
  kDirect = 0,
  kRelay,
  kExternalClientHandoff,
  kReject,
};
enum class HandoffReason : std::uint8_t {
  kKeyRequired = 0,
  kNoDirectUrl,
  kProbeInconclusive,
  kCredentialBound,
  kReceiverIncompatible,
  kAdContinuityUnknown,
  kStartFailed,
  kDashRelayUnsupported,
  kLegacyMirror,
};
enum class CoreError : std::uint8_t {
  kUntrustedObservation = 0,
  kMissingUserActivation,
  kPlaybackNotAdvanced,
  kCapabilitiesUnavailable,
  kDrmProtected,
  kReceiverIncompatible,
  kCredentialBoundMedia,
  kAdContinuityUnknown,
  kPolicyDenied,
  kUnsupportedSchemaVersion,
  kInvalidMessage,
  kSessionUnknown,
  kSessionExpired,
  kUpstreamRejected,
};
enum class HostError : std::uint8_t {
  kInvalidMessage = 0,
  kInvalidState,
  kStaleContext,
  kCapacityExceeded,
  kCancelled,
  kCandidateUnavailable,
  kHostUnavailable,
};
enum class CodecError {
  kFrameTooLarge = 0,
  kInvalidMagic,
  kUnsupportedVersion,
  kUnknownKind,
  kInvalidFlags,
  kTruncated,
  kTrailingBytes,
  kInvalidUtf8,
  kInvalidValue,
  kLengthExceeded,
};

struct Playback final {
  std::uint64_t position_ms = 0;
  std::optional<std::uint64_t> duration_ms;
  bool is_live = false;
  AdContinuity ad_continuity = AdContinuity::kUnknown;
  bool current_src = false;
  bool near_play_event = false;
  bool audible = false;
  bool main_frame = false;
  std::uint32_t visible_area_px = 0;
  friend bool operator==(const Playback &a, const Playback &b) {
    return a.position_ms == b.position_ms && a.duration_ms == b.duration_ms &&
           a.is_live == b.is_live && a.ad_continuity == b.ad_continuity &&
           a.current_src == b.current_src &&
           a.near_play_event == b.near_play_event && a.audible == b.audible &&
           a.main_frame == b.main_frame &&
           a.visible_area_px == b.visible_area_px;
  }
};

struct Receiver final {
  bool mp4 = false, hls = false, dash = false;
  bool h264 = false, hevc = false, av1 = false;
  std::uint16_t max_height = 0;
  friend bool operator==(const Receiver &a, const Receiver &b) {
    return a.mp4 == b.mp4 && a.hls == b.hls && a.dash == b.dash &&
           a.h264 == b.h264 && a.hevc == b.hevc && a.av1 == b.av1 &&
           a.max_height == b.max_height;
  }
};

struct Decision final {
  DecisionKind kind = DecisionKind::kReject;
  std::optional<HandoffReason> handoff_reason;
  std::optional<CoreError> reject_reason;
  friend bool operator==(const Decision &a, const Decision &b) {
    return a.kind == b.kind && a.handoff_reason == b.handoff_reason &&
           a.reject_reason == b.reject_reason;
  }
};

struct IngestUrl final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0, observed_at_ms = 0;
  std::string page_url, media_url;
  Source source = Source::kCurrentSrc;
  HeadersClass headers_class = HeadersClass::kNone;
  std::optional<Playback> playback;
  bool eme_encrypted = false;
  friend bool operator==(const IngestUrl &a, const IngestUrl &b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.observed_at_ms == b.observed_at_ms && a.page_url == b.page_url &&
           a.media_url == b.media_url && a.source == b.source &&
           a.headers_class == b.headers_class && a.playback == b.playback &&
           a.eme_encrypted == b.eme_encrypted;
  }
};
struct MarkEme final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  friend bool operator==(const MarkEme &a, const MarkEme &b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation;
  }
};
struct Decide final {
  std::string request_id;
  std::uint64_t candidate_id = 0, now_ms = 0;
  Receiver receiver;
  bool handoff_available = false;
  friend bool operator==(const Decide &a, const Decide &b) {
    return a.request_id == b.request_id && a.candidate_id == b.candidate_id &&
           a.now_ms == b.now_ms && a.receiver == b.receiver &&
           a.handoff_available == b.handoff_available;
  }
};
struct DecideUrlLess final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  std::string page_url;
  Playback playback;
  bool eme_encrypted = false, handoff_available = false;
  friend bool operator==(const DecideUrlLess &a, const DecideUrlLess &b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation &&
           a.page_url == b.page_url && a.playback == b.playback &&
           a.eme_encrypted == b.eme_encrypted &&
           a.handoff_available == b.handoff_available;
  }
};
struct Cancel final {
  std::string request_id;
  friend bool operator==(const Cancel &a, const Cancel &b) {
    return a.request_id == b.request_id;
  }
};
struct Navigation final {
  std::string request_id, tab_id;
  std::uint64_t navigation_id = 0, generation = 0;
  friend bool operator==(const Navigation &a, const Navigation &b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.navigation_id == b.navigation_id && a.generation == b.generation;
  }
};
struct CloseTab final {
  std::string request_id, tab_id;
  std::uint64_t generation = 0;
  friend bool operator==(const CloseTab &a, const CloseTab &b) {
    return a.request_id == b.request_id && a.tab_id == b.tab_id &&
           a.generation == b.generation;
  }
};
struct Shutdown final {
  friend bool operator==(Shutdown, Shutdown) { return true; }
};
struct CandidateReply final {
  std::string request_id;
  std::optional<std::uint64_t> candidate_id;
  std::string redacted_origin;
  friend bool operator==(const CandidateReply &a, const CandidateReply &b) {
    return a.request_id == b.request_id && a.candidate_id == b.candidate_id &&
           a.redacted_origin == b.redacted_origin;
  }
};
struct DecisionReply final {
  std::string request_id;
  std::optional<std::uint64_t> candidate_id;
  std::optional<Protocol> protocol;
  Decision decision;
  friend bool operator==(const DecisionReply &a, const DecisionReply &b) {
    return a.request_id == b.request_id && a.candidate_id == b.candidate_id &&
           a.protocol == b.protocol && a.decision == b.decision;
  }
};
struct Ack final {
  std::string request_id;
  friend bool operator==(const Ack &a, const Ack &b) {
    return a.request_id == b.request_id;
  }
};
struct ErrorReply final {
  std::string request_id;
  HostError code = HostError::kInvalidMessage;
  friend bool operator==(const ErrorReply &a, const ErrorReply &b) {
    return a.request_id == b.request_id && a.code == b.code;
  }
};

using Message = std::variant<IngestUrl, MarkEme, Decide, DecideUrlLess, Cancel,
                             Navigation, CloseTab, Shutdown, CandidateReply,
                             DecisionReply, Ack, ErrorReply>;

std::optional<std::vector<std::uint8_t>> Encode(const Message &message,
                                                CodecError *error);
std::optional<Message> Decode(const std::vector<std::uint8_t> &bytes,
                              CodecError *error);
const char *ToString(CodecError error);

} // namespace crayon::cef_shell::ipc::media_host
