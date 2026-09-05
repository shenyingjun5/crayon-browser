// MHV2 handshake and bounded player wire; no connection owner, command
// dispatch or permission.
#pragma once

#include <string>
#include <tuple>
#include <variant>

#include "crayon/cef_shell_ipc/media_host_codec.h"

namespace crayon::cef_shell::ipc::media_host_v2 {
using CodecError = media_host::CodecError;
inline constexpr std::size_t kHandshakeBytes = 34;
inline constexpr std::uint32_t kMaxFrameBytes = 16 * 1024;
inline constexpr std::uint16_t kMaxPageItems = 16;
inline constexpr std::uint32_t kCapMediaRead = 1, kCapDraft = 2,
                               kCapConnect = 4, kCapStop = 8, kCapReason = 16,
                               kCapSession = 32;
enum class Kind : std::uint8_t { kHello = 1, kWelcome = 2 };
struct Handshake {
  Kind kind = Kind::kHello;
  std::uint64_t session_id = 0;
  std::uint64_t generation = 0;
  // Production callers advertise only actually implemented capabilities.
  std::uint32_t capabilities = 0;
  std::uint32_t max_frame_bytes = kMaxFrameBytes;
  std::uint16_t max_page_items = kMaxPageItems;
};
bool operator==(const Handshake &a, const Handshake &b);
// Echo/subset check only; never sufficient to authorize a command.
bool MatchesHello(const Handshake &hello, const Handshake &welcome);
std::optional<std::vector<std::uint8_t>> Encode(const Handshake &message,
                                                CodecError *error = nullptr);
std::optional<Handshake> Decode(const std::vector<std::uint8_t> &bytes,
                                CodecError *error = nullptr);

enum class PlayerSourceKind : std::uint8_t {
  kHttpUrl = 0,
  kBlobUrl = 1,
  kMediaStream = 2,
};

struct PlayerContext {
  std::uint64_t session_id = 0, host_generation = 0;
  std::uint32_t tab_id = 0;
  std::uint64_t navigation_id = 0;
  std::uint32_t tab_generation = 0;
  std::uint64_t instance_id = 0, source_revision = 0;
  friend bool operator==(const PlayerContext &a, const PlayerContext &b) {
    return std::tie(a.session_id, a.host_generation, a.tab_id, a.navigation_id,
                    a.tab_generation, a.instance_id, a.source_revision) ==
           std::tie(b.session_id, b.host_generation, b.tab_id, b.navigation_id,
                    b.tab_generation, b.instance_id, b.source_revision);
  }
};

struct PlayerFact {
  PlayerContext context;
  std::uint64_t observed_at_ms = 0;
  PlayerSourceKind source_kind = PlayerSourceKind::kHttpUrl;
  std::uint64_t position_ms = 0;
  std::optional<std::uint64_t> duration_ms;
  bool is_live = false, has_video = false, has_audio = false, visible = false,
       eme_encrypted = false;
  std::uint32_t visible_fraction_ppm = 0;
  std::string page_url, media_url;
  friend bool operator==(const PlayerFact &a, const PlayerFact &b) {
    return std::tie(a.context, a.observed_at_ms, a.source_kind, a.position_ms,
                    a.duration_ms, a.is_live, a.has_video, a.has_audio,
                    a.visible, a.eme_encrypted, a.visible_fraction_ppm,
                    a.page_url, a.media_url) ==
           std::tie(b.context, b.observed_at_ms, b.source_kind, b.position_ms,
                    b.duration_ms, b.is_live, b.has_video, b.has_audio,
                    b.visible, b.eme_encrypted, b.visible_fraction_ppm,
                    b.page_url, b.media_url);
  }
};

using PlayerMessage = std::variant<PlayerFact, PlayerContext>;
std::optional<std::vector<std::uint8_t>>
EncodePlayerMessage(const PlayerMessage &message, CodecError *error = nullptr);
std::optional<PlayerMessage>
DecodePlayerMessage(const std::vector<std::uint8_t> &bytes,
                    CodecError *error = nullptr);

struct PlayerPageContext {
  std::uint64_t session_id = 0, host_generation = 0, request_id = 0;
  std::uint32_t tab_id = 0;
  std::uint64_t navigation_id = 0;
  std::uint32_t tab_generation = 0;
  friend bool operator==(const PlayerPageContext &a,
                         const PlayerPageContext &b) {
    return std::tie(a.session_id, a.host_generation, a.request_id, a.tab_id,
                    a.navigation_id, a.tab_generation) ==
           std::tie(b.session_id, b.host_generation, b.request_id, b.tab_id,
                    b.navigation_id, b.tab_generation);
  }
};

struct PlayerListRequest {
  PlayerPageContext context;
  std::uint64_t snapshot_revision = 0;
  std::uint16_t offset = 0, max_items = 0;
  friend bool operator==(const PlayerListRequest &a,
                         const PlayerListRequest &b) {
    return std::tie(a.context, a.snapshot_revision, a.offset, a.max_items) ==
           std::tie(b.context, b.snapshot_revision, b.offset, b.max_items);
  }
};

enum class PlayerPageStatus : std::uint8_t { kOk = 0, kStale = 1 };

struct PlayerProjection {
  std::uint64_t instance_id = 0, source_revision = 0;
  PlayerSourceKind source_kind = PlayerSourceKind::kHttpUrl;
  bool has_video = false, has_audio = false, visible = false,
       eme_encrypted = false;
  std::string redacted_origin;
  friend bool operator==(const PlayerProjection &a, const PlayerProjection &b) {
    return std::tie(a.instance_id, a.source_revision, a.source_kind,
                    a.has_video, a.has_audio, a.visible, a.eme_encrypted,
                    a.redacted_origin) ==
           std::tie(b.instance_id, b.source_revision, b.source_kind,
                    b.has_video, b.has_audio, b.visible, b.eme_encrypted,
                    b.redacted_origin);
  }
};

struct PlayerPageReply {
  PlayerPageContext context;
  std::uint64_t snapshot_revision = 0;
  PlayerPageStatus status = PlayerPageStatus::kOk;
  std::uint16_t offset = 0;
  std::optional<std::uint16_t> next_offset;
  std::vector<PlayerProjection> players;
  friend bool operator==(const PlayerPageReply &a, const PlayerPageReply &b) {
    return std::tie(a.context, a.snapshot_revision, a.status, a.offset,
                    a.next_offset, a.players) ==
           std::tie(b.context, b.snapshot_revision, b.status, b.offset,
                    b.next_offset, b.players);
  }
};

using PlayerPageMessage = std::variant<PlayerListRequest, PlayerPageReply>;
std::optional<std::vector<std::uint8_t>>
EncodePlayerPageMessage(const PlayerPageMessage &message,
                        CodecError *error = nullptr);
std::optional<PlayerPageMessage>
DecodePlayerPageMessage(const std::vector<std::uint8_t> &bytes,
                        CodecError *error = nullptr);

struct DraftContext {
  std::uint64_t session_id = 0, host_generation = 0, request_id = 0;
  std::string profile_id;
  std::uint32_t tab_id = 0;
  std::uint64_t navigation_id = 0;
  std::uint32_t tab_generation = 0;
  friend bool operator==(const DraftContext &a, const DraftContext &b) {
    return std::tie(a.session_id, a.host_generation, a.request_id, a.profile_id,
                    a.tab_id, a.navigation_id, a.tab_generation) ==
           std::tie(b.session_id, b.host_generation, b.request_id, b.profile_id,
                    b.tab_id, b.navigation_id, b.tab_generation);
  }
};
enum class DraftAction : std::uint8_t {
  kOpen = 0,
  kSelectMedia = 1,
  kSelectDevice = 2,
  kConnect = 3,
  kPrepare = 4,
  kConfirmReplacement = 5,
  kCommit = 6,
  kCancel = 7
};
struct DraftMediaRef {
  std::uint64_t instance_id = 0, source_revision = 0;
  friend bool operator==(const DraftMediaRef &a, const DraftMediaRef &b) {
    return std::tie(a.instance_id, a.source_revision) ==
           std::tie(b.instance_id, b.source_revision);
  }
};
struct DraftCommand {
  DraftContext context;
  DraftAction action = DraftAction::kOpen;
  std::uint64_t draft_id = 0, draft_revision = 0;
  std::optional<DraftMediaRef> media;
  std::string device_id;
  friend bool operator==(const DraftCommand &a, const DraftCommand &b) {
    return std::tie(a.context, a.action, a.draft_id, a.draft_revision, a.media,
                    a.device_id) == std::tie(b.context, b.action, b.draft_id,
                                             b.draft_revision, b.media,
                                             b.device_id);
  }
};
enum class DraftPhase : std::uint8_t {
  kChoosing = 0,
  kConnecting = 1,
  kPreparing = 2,
  kPrepared = 3,
  kCommitting = 4,
  kFailed = 5,
  kExpired = 6,
  kCancelled = 7,
  kCommitted = 8
};
enum class DraftError : std::uint8_t {
  kNone = 0,
  kInvalid = 1,
  kStale = 2,
  kUnavailable = 3,
  kDenied = 4,
  kExpired = 5,
  kBusy = 6
};
enum class DraftRoute : std::uint8_t { kNone = 0, kDirect = 1, kRelay = 2 };
enum class DraftReason : std::uint8_t {
  kNone = 0,
  kCredentials = 1,
  kProtection = 2,
  kRecognized = 3,
  kUnrecognized = 4,
  kRedirectRefused = 5,
  kUpstreamRejected = 6,
  kAddressRejected = 7,
  kDns = 8,
  kConnect = 9,
  kTimeout = 10,
  kTransport = 11,
  kInvalidTarget = 12
};
struct DraftStateReply {
  DraftContext context;
  std::uint64_t draft_id = 0, draft_revision = 0;
  DraftPhase phase = DraftPhase::kChoosing;
  DraftError error = DraftError::kNone;
  std::optional<DraftMediaRef> media;
  std::string device_id;
  bool device_connected = false;
  bool replacement_confirmation_required = false;
  DraftRoute route = DraftRoute::kNone;
  std::optional<std::uint64_t> prepared_until_ms;
  DraftReason reason = DraftReason::kNone;
  std::optional<std::uint64_t> session_generation;
  friend bool operator==(const DraftStateReply &a, const DraftStateReply &b) {
    return std::tie(a.context, a.draft_id, a.draft_revision, a.phase, a.error,
                    a.media, a.device_id, a.device_connected,
                    a.replacement_confirmation_required, a.route,
                    a.prepared_until_ms, a.reason, a.session_generation) ==
           std::tie(b.context, b.draft_id, b.draft_revision, b.phase, b.error,
                    b.media, b.device_id, b.device_connected,
                    b.replacement_confirmation_required, b.route,
                    b.prepared_until_ms, b.reason, b.session_generation);
  }
};
using DraftMessage = std::variant<DraftCommand, DraftStateReply>;
std::optional<std::vector<std::uint8_t>>
EncodeDraftMessage(const DraftMessage &message, CodecError *error = nullptr);
std::optional<DraftMessage>
DecodeDraftMessage(const std::vector<std::uint8_t> &bytes,
                   CodecError *error = nullptr);
} // namespace crayon::cef_shell::ipc::media_host_v2
