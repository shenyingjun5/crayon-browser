#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

#include <algorithm>
#include <array>
#include <tuple>

namespace crayon::cef_shell::ipc::media_host_v2 {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {'M', 'H', 'V', '2'};
constexpr std::uint16_t kVersion = 2;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kMaxUrlBytes = 2048;
constexpr std::uint8_t kPlayerUpsertKind = 3;
constexpr std::uint8_t kPlayerRemoveKind = 4;
constexpr std::uint8_t kPlayerListKind = 5;
constexpr std::uint8_t kPlayerPageKind = 6;
constexpr std::uint8_t kDraftCommandKind = 7;
constexpr std::uint8_t kDraftStateKind = 8;
constexpr std::uint8_t kDraftStateReasonKind = 9;
constexpr std::uint8_t kDraftStateSessionKind = 10;
constexpr std::size_t kMaxIdBytes = 128;
constexpr auto kKnownCapabilities =
    kCapMediaRead | kCapDraft | kCapConnect | kCapStop | kCapReason |
    kCapSession;
bool Valid(const Handshake &h) {
  return (h.kind == Kind::kHello || h.kind == Kind::kWelcome) &&
         h.session_id != 0 && h.generation != 0 &&
         (h.capabilities & ~kKnownCapabilities) == 0 &&
         h.max_frame_bytes >= kHandshakeBytes &&
         h.max_frame_bytes <= kMaxFrameBytes && h.max_page_items != 0 &&
         h.max_page_items <= kMaxPageItems;
}
bool ValidUtf8(const std::string &value) {
  const auto *data = reinterpret_cast<const unsigned char *>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = data[index++];
    std::uint32_t code = 0;
    std::size_t remaining = 0;
    if (first < 0x80) {
      code = first;
    } else if ((first & 0xe0) == 0xc0) {
      code = first & 0x1f;
      remaining = 1;
      if (code == 0)
        return false;
    } else if ((first & 0xf0) == 0xe0) {
      code = first & 0x0f;
      remaining = 2;
    } else if ((first & 0xf8) == 0xf0) {
      code = first & 0x07;
      remaining = 3;
    } else {
      return false;
    }
    if (remaining > value.size() - index)
      return false;
    for (std::size_t i = 0; i < remaining; ++i) {
      const unsigned char next = data[index++];
      if ((next & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (next & 0x3f);
    }
    if ((remaining == 1 && code < 0x80) || (remaining == 2 && code < 0x800) ||
        (remaining == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff) || code < 0x20 ||
        (code >= 0x7f && code <= 0x9f))
      return false;
  }
  return true;
}
bool ValidText(const std::string &value, bool allow_empty) {
  if ((!allow_empty && value.empty()) || value.size() > kMaxIdBytes ||
      !ValidUtf8(value))
    return false;
  constexpr std::array<const char *, 9> kBidi = {
      "\xe2\x80\xaa", "\xe2\x80\xab", "\xe2\x80\xac",
      "\xe2\x80\xad", "\xe2\x80\xae", "\xe2\x81\xa6",
      "\xe2\x81\xa7", "\xe2\x81\xa8", "\xe2\x81\xa9"};
  return std::none_of(kBidi.begin(), kBidi.end(), [&](const char *mark) {
    return value.find(mark) != std::string::npos;
  });
}
bool ValidUrl(const std::string &value, bool allow_empty) {
  if (value.empty())
    return allow_empty;
  if (value.size() > kMaxUrlBytes || !ValidUtf8(value))
    return false;
  std::size_t authority = std::string::npos;
  if (value.rfind("http://", 0) == 0)
    authority = 7;
  else if (value.rfind("https://", 0) == 0)
    authority = 8;
  if (authority == std::string::npos || authority == value.size())
    return false;
  const auto authority_end = value.find_first_of("/?#", authority);
  const auto end =
      authority_end == std::string::npos ? value.size() : authority_end;
  const auto user_end = value.rfind('@', end - 1);
  const auto host_begin = user_end == std::string::npos || user_end < authority
                              ? authority
                              : user_end + 1;
  if (host_begin >= end ||
      std::any_of(value.begin() + authority, value.end(),
                  [](unsigned char c) { return c <= 0x20 || c == 0x7f; }))
    return false;
  std::size_t port_begin = end;
  if (value[host_begin] == '[') {
    const auto close = value.find(']', host_begin + 1);
    if (close == std::string::npos || close + 1 > end ||
        close == host_begin + 1 ||
        !std::all_of(value.begin() + host_begin + 1, value.begin() + close,
                     [](unsigned char c) {
                       return (c >= '0' && c <= '9') ||
                              (c >= 'a' && c <= 'f') ||
                              (c >= 'A' && c <= 'F') || c == ':' || c == '.';
                     }))
      return false;
    port_begin = close + 1;
    if (port_begin < end && value[port_begin] != ':')
      return false;
  } else {
    const auto colon = value.find(':', host_begin);
    port_begin = colon < end ? colon : end;
    if (port_begin == host_begin ||
        !std::all_of(value.begin() + host_begin, value.begin() + port_begin,
                     [](unsigned char c) {
                       return (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '.';
                     }))
      return false;
  }
  if (port_begin < end) {
    const auto digits = port_begin + 1;
    if (digits == end ||
        !std::all_of(value.begin() + digits, value.begin() + end,
                     [](unsigned char c) { return c >= '0' && c <= '9'; }))
      return false;
    unsigned long port = 0;
    for (auto it = value.begin() + digits; it != value.begin() + end; ++it) {
      port = port * 10 + static_cast<unsigned long>(*it - '0');
      if (port > 65'535)
        return false;
    }
  }
  return true;
}
bool ValidContext(const PlayerContext &value) {
  return value.session_id != 0 && value.host_generation != 0 &&
         value.tab_id != 0 && value.navigation_id != 0 &&
         value.tab_generation != 0 && value.instance_id != 0 &&
         value.source_revision != 0;
}
bool ValidFact(const PlayerFact &value) {
  const bool http = value.source_kind == PlayerSourceKind::kHttpUrl;
  const bool known = http || value.source_kind == PlayerSourceKind::kBlobUrl ||
                     value.source_kind == PlayerSourceKind::kMediaStream;
  return ValidContext(value.context) && value.observed_at_ms != 0 && known &&
         value.visible_fraction_ppm <= 1'000'000 &&
         ValidUrl(value.page_url, false) && ValidUrl(value.media_url, !http) &&
         (http == !value.media_url.empty());
}
bool ValidPageContext(const PlayerPageContext &value) {
  return value.session_id != 0 && value.host_generation != 0 &&
         value.request_id != 0 && value.tab_id != 0 &&
         value.navigation_id != 0 && value.tab_generation != 0;
}
bool ValidOrigin(const std::string &value) {
  if (value.empty())
    return true;
  if (!ValidUrl(value, false))
    return false;
  const std::size_t authority = value.rfind("http://", 0) == 0 ? 7 : 8;
  const bool canonical_case =
      std::none_of(value.begin() + authority, value.end(),
                   [](unsigned char c) { return c >= 'A' && c <= 'Z'; });
  const bool default_port = (authority == 7 && value.size() >= 3 &&
                             value.compare(value.size() - 3, 3, ":80") == 0) ||
                            (authority == 8 && value.size() >= 4 &&
                             value.compare(value.size() - 4, 4, ":443") == 0);
  return canonical_case && !default_port &&
         value.find('@', authority) == std::string::npos &&
         value.find_first_of("/?#", authority) == std::string::npos;
}
bool ValidProjection(const PlayerProjection &value) {
  return value.instance_id != 0 && value.source_revision != 0 &&
         (value.source_kind == PlayerSourceKind::kHttpUrl ||
          value.source_kind == PlayerSourceKind::kBlobUrl ||
          value.source_kind == PlayerSourceKind::kMediaStream) &&
         ValidOrigin(value.redacted_origin);
}
bool ValidPageReply(const PlayerPageReply &value) {
  if (!ValidPageContext(value.context) || value.snapshot_revision == 0 ||
      (value.status != PlayerPageStatus::kOk &&
       value.status != PlayerPageStatus::kStale) ||
      value.players.size() > kMaxPageItems ||
      !std::all_of(value.players.begin(), value.players.end(), ValidProjection))
    return false;
  if (value.status == PlayerPageStatus::kStale)
    return value.players.empty() && !value.next_offset;
  if (!value.next_offset)
    return true;
  const auto expected = static_cast<std::uint32_t>(value.offset) +
                        static_cast<std::uint32_t>(value.players.size());
  return !value.players.empty() && expected <= 0xffffu &&
         *value.next_offset == expected;
}
bool ValidDraftContext(const DraftContext &value) {
  return value.session_id != 0 && value.host_generation != 0 &&
         value.request_id != 0 && ValidText(value.profile_id, false) &&
         value.tab_id != 0 && value.navigation_id != 0 &&
         value.tab_generation != 0;
}
bool ValidDraftCommand(const DraftCommand &value) {
  if (!ValidDraftContext(value.context) || value.action > DraftAction::kCancel)
    return false;
  const bool open = value.action == DraftAction::kOpen;
  if (open != (value.draft_id == 0 && value.draft_revision == 0) ||
      (!open && (value.draft_id == 0 || value.draft_revision == 0)))
    return false;
  if (value.action == DraftAction::kSelectMedia)
    return value.media && value.media->instance_id != 0 &&
           value.media->source_revision != 0 && value.device_id.empty();
  if (value.action == DraftAction::kSelectDevice)
    return !value.media && ValidText(value.device_id, false);
  return !value.media && value.device_id.empty();
}
bool ValidDraftState(const DraftStateReply &value) {
  if (!ValidDraftContext(value.context) || value.draft_id == 0 ||
      value.draft_revision == 0 || value.phase > DraftPhase::kCommitted ||
      value.error > DraftError::kBusy || value.route > DraftRoute::kRelay ||
      (value.media &&
       (value.media->instance_id == 0 || value.media->source_revision == 0)) ||
      value.reason > DraftReason::kInvalidTarget ||
      (!value.device_id.empty() && !ValidText(value.device_id, false)))
    return false;
  if (value.session_generation.has_value() !=
      (value.phase == DraftPhase::kCommitted))
    return false;
  if (value.reason != DraftReason::kNone &&
      value.phase != DraftPhase::kPrepared &&
      value.phase != DraftPhase::kFailed)
    return false;
  if (value.phase == DraftPhase::kFailed) {
    if (value.error == DraftError::kNone)
      return false;
  } else if (value.phase == DraftPhase::kExpired) {
    if (value.error != DraftError::kExpired)
      return false;
  } else if (value.error != DraftError::kNone) {
    return false;
  }
  const bool ready = value.phase == DraftPhase::kPrepared ||
                     value.phase == DraftPhase::kCommitting;
  if ((value.device_connected && value.device_id.empty()) ||
      ((value.route != DraftRoute::kNone) != ready) ||
      (value.replacement_confirmation_required &&
       (ready || !value.media || value.device_id.empty())))
    return false;
  if (ready &&
      (!value.media || value.device_id.empty() || !value.device_connected ||
       value.replacement_confirmation_required))
    return false;
  return value.phase == DraftPhase::kPrepared
             ? value.prepared_until_ms && *value.prepared_until_ms != 0
             : !value.prepared_until_ms;
}
void SetError(CodecError value, CodecError *error) {
  if (error)
    *error = value;
}
template <typename T> void Put(std::vector<std::uint8_t> &out, T value) {
  for (std::size_t i = sizeof(T); i > 0; --i)
    out.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8)));
}
// Called only after the fixed-size frame has been validated.
template <typename T>
T Get(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i)
    value = static_cast<T>((value << 8) | bytes[offset + i]);
  return value;
}

class PlayerReader {
public:
  explicit PlayerReader(const std::vector<std::uint8_t> &bytes)
      : bytes_(bytes), offset_(kHeaderBytes) {}
  bool U8(std::uint8_t *value) { return Read(value); }
  bool U16(std::uint16_t *value) { return Read(value); }
  bool U32(std::uint32_t *value) { return Read(value); }
  bool U64(std::uint64_t *value) { return Read(value); }
  bool Nonzero(std::uint32_t *value) { return U32(value) && *value != 0; }
  bool Nonzero(std::uint64_t *value) { return U64(value) && *value != 0; }
  bool Bool(bool *value) {
    std::uint8_t raw = 0;
    if (!U8(&raw) || raw > 1)
      return false;
    *value = raw != 0;
    return true;
  }
  bool Url(std::string *value, bool allow_empty) {
    std::uint32_t length = 0;
    if (!U32(&length) || length > kMaxUrlBytes || length > Remaining())
      return false;
    value->assign(reinterpret_cast<const char *>(bytes_.data() + offset_),
                  length);
    offset_ += length;
    return ValidUrl(*value, allow_empty);
  }
  bool Origin(std::string *value) {
    std::uint32_t length = 0;
    if (!U32(&length) || length > kMaxUrlBytes || length > Remaining())
      return false;
    value->assign(reinterpret_cast<const char *>(bytes_.data() + offset_),
                  length);
    offset_ += length;
    return ValidOrigin(*value);
  }
  bool Text(std::string *value, bool allow_empty) {
    std::uint16_t length = 0;
    if (!U16(&length) || length > kMaxIdBytes || length > Remaining())
      return false;
    value->assign(reinterpret_cast<const char *>(bytes_.data() + offset_),
                  length);
    offset_ += length;
    return ValidText(*value, allow_empty);
  }
  bool Context(PlayerContext *value) {
    return Nonzero(&value->session_id) && Nonzero(&value->host_generation) &&
           Nonzero(&value->tab_id) && Nonzero(&value->navigation_id) &&
           Nonzero(&value->tab_generation) && Nonzero(&value->instance_id) &&
           Nonzero(&value->source_revision);
  }
  bool PageContext(PlayerPageContext *value) {
    return Nonzero(&value->session_id) && Nonzero(&value->host_generation) &&
           Nonzero(&value->request_id) && Nonzero(&value->tab_id) &&
           Nonzero(&value->navigation_id) && Nonzero(&value->tab_generation);
  }
  bool DraftContextValue(DraftContext *value) {
    return Nonzero(&value->session_id) && Nonzero(&value->host_generation) &&
           Nonzero(&value->request_id) && Text(&value->profile_id, false) &&
           Nonzero(&value->tab_id) && Nonzero(&value->navigation_id) &&
           Nonzero(&value->tab_generation);
  }
  bool Done() const { return offset_ == bytes_.size(); }

private:
  template <typename T> bool Read(T *value) {
    if (Remaining() < sizeof(T))
      return false;
    *value = Get<T>(bytes_, offset_);
    offset_ += sizeof(T);
    return true;
  }
  std::size_t Remaining() const { return bytes_.size() - offset_; }
  const std::vector<std::uint8_t> &bytes_;
  std::size_t offset_;
};

void PutContext(std::vector<std::uint8_t> &out, const PlayerContext &value) {
  Put(out, value.session_id);
  Put(out, value.host_generation);
  Put(out, value.tab_id);
  Put(out, value.navigation_id);
  Put(out, value.tab_generation);
  Put(out, value.instance_id);
  Put(out, value.source_revision);
}
void PutPageContext(std::vector<std::uint8_t> &out,
                    const PlayerPageContext &value) {
  Put(out, value.session_id);
  Put(out, value.host_generation);
  Put(out, value.request_id);
  Put(out, value.tab_id);
  Put(out, value.navigation_id);
  Put(out, value.tab_generation);
}
void PutText(std::vector<std::uint8_t> &out, const std::string &value) {
  Put(out, static_cast<std::uint16_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}
void PutDraftContext(std::vector<std::uint8_t> &out,
                     const DraftContext &value) {
  Put(out, value.session_id);
  Put(out, value.host_generation);
  Put(out, value.request_id);
  PutText(out, value.profile_id);
  Put(out, value.tab_id);
  Put(out, value.navigation_id);
  Put(out, value.tab_generation);
}
void PutBool(std::vector<std::uint8_t> &out, bool value) {
  out.push_back(value ? 1 : 0);
}
void PutUrl(std::vector<std::uint8_t> &out, const std::string &value) {
  Put(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}
bool ValidatePlayerHeader(const std::vector<std::uint8_t> &bytes,
                          CodecError *error) {
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return false;
  }
  if (bytes.size() < kHeaderBytes) {
    SetError(CodecError::kTruncated, error);
    return false;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(CodecError::kInvalidMagic, error);
    return false;
  }
  if (Get<std::uint16_t>(bytes, 4) != kVersion) {
    SetError(CodecError::kUnsupportedVersion, error);
    return false;
  }
  if (bytes[7] != 0) {
    SetError(CodecError::kInvalidFlags, error);
    return false;
  }
  if (bytes[6] != kPlayerUpsertKind && bytes[6] != kPlayerRemoveKind) {
    SetError(CodecError::kUnknownKind, error);
    return false;
  }
  return true;
}
bool ValidatePlayerPageHeader(const std::vector<std::uint8_t> &bytes,
                              CodecError *error) {
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return false;
  }
  if (bytes.size() < kHeaderBytes) {
    SetError(CodecError::kTruncated, error);
    return false;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(CodecError::kInvalidMagic, error);
    return false;
  }
  if (Get<std::uint16_t>(bytes, 4) != kVersion) {
    SetError(CodecError::kUnsupportedVersion, error);
    return false;
  }
  if (bytes[7] != 0) {
    SetError(CodecError::kInvalidFlags, error);
    return false;
  }
  if (bytes[6] != kPlayerListKind && bytes[6] != kPlayerPageKind) {
    SetError(CodecError::kUnknownKind, error);
    return false;
  }
  return true;
}
bool ValidateDraftHeader(const std::vector<std::uint8_t> &bytes,
                         CodecError *error) {
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return false;
  }
  if (bytes.size() < kHeaderBytes) {
    SetError(CodecError::kTruncated, error);
    return false;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(CodecError::kInvalidMagic, error);
    return false;
  }
  if (Get<std::uint16_t>(bytes, 4) != kVersion) {
    SetError(CodecError::kUnsupportedVersion, error);
    return false;
  }
  if (bytes[7] != 0) {
    SetError(CodecError::kInvalidFlags, error);
    return false;
  }
  if (bytes[6] != kDraftCommandKind && bytes[6] != kDraftStateKind &&
      bytes[6] != kDraftStateReasonKind &&
      bytes[6] != kDraftStateSessionKind) {
    SetError(CodecError::kUnknownKind, error);
    return false;
  }
  return true;
}
} // namespace

bool operator==(const Handshake &a, const Handshake &b) {
  return std::tie(a.kind, a.session_id, a.generation, a.capabilities,
                  a.max_frame_bytes, a.max_page_items) ==
         std::tie(b.kind, b.session_id, b.generation, b.capabilities,
                  b.max_frame_bytes, b.max_page_items);
}
bool MatchesHello(const Handshake &hello, const Handshake &welcome) {
  return Valid(hello) && Valid(welcome) && hello.kind == Kind::kHello &&
         welcome.kind == Kind::kWelcome &&
         hello.session_id == welcome.session_id &&
         hello.generation == welcome.generation &&
         (welcome.capabilities & ~hello.capabilities) == 0 &&
         welcome.max_frame_bytes <= hello.max_frame_bytes &&
         welcome.max_page_items <= hello.max_page_items;
}
std::optional<std::vector<std::uint8_t>> Encode(const Handshake &message,
                                                CodecError *error) {
  if (!Valid(message)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(kHandshakeBytes);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  Put(bytes, kVersion);
  bytes.push_back(static_cast<std::uint8_t>(message.kind));
  bytes.push_back(0);
  Put(bytes, message.session_id);
  Put(bytes, message.generation);
  Put(bytes, message.capabilities);
  Put(bytes, message.max_frame_bytes);
  Put(bytes, message.max_page_items);
  return bytes;
}
std::optional<Handshake> Decode(const std::vector<std::uint8_t> &bytes,
                                CodecError *error) {
  auto fail = [error](CodecError value) -> std::optional<Handshake> {
    SetError(value, error);
    return std::nullopt;
  };
  if (bytes.size() > kMaxFrameBytes)
    return fail(CodecError::kFrameTooLarge);
  if (bytes.size() < kHeaderBytes)
    return fail(CodecError::kTruncated);
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
    return fail(CodecError::kInvalidMagic);
  if (Get<std::uint16_t>(bytes, 4) != kVersion)
    return fail(CodecError::kUnsupportedVersion);
  if (bytes[7] != 0)
    return fail(CodecError::kInvalidFlags);
  if (bytes[6] != static_cast<std::uint8_t>(Kind::kHello) &&
      bytes[6] != static_cast<std::uint8_t>(Kind::kWelcome))
    return fail(CodecError::kUnknownKind);
  if (bytes.size() < kHandshakeBytes)
    return fail(CodecError::kTruncated);
  if (bytes.size() > kHandshakeBytes)
    return fail(CodecError::kTrailingBytes);
  Handshake message{
      static_cast<Kind>(bytes[6]),   Get<std::uint64_t>(bytes, 8),
      Get<std::uint64_t>(bytes, 16), Get<std::uint32_t>(bytes, 24),
      Get<std::uint32_t>(bytes, 28), Get<std::uint16_t>(bytes, 32)};
  if (!Valid(message))
    return fail(CodecError::kInvalidValue);
  return message;
}

std::optional<std::vector<std::uint8_t>>
EncodePlayerMessage(const PlayerMessage &message, CodecError *error) {
  const auto *fact = std::get_if<PlayerFact>(&message);
  const auto &context = fact ? fact->context : std::get<PlayerContext>(message);
  if (!ValidContext(context) || (fact && !ValidFact(*fact))) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(128);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  Put(bytes, kVersion);
  bytes.push_back(fact ? kPlayerUpsertKind : kPlayerRemoveKind);
  bytes.push_back(0);
  PutContext(bytes, context);
  if (fact) {
    Put(bytes, fact->observed_at_ms);
    bytes.push_back(static_cast<std::uint8_t>(fact->source_kind));
    Put(bytes, fact->position_ms);
    PutBool(bytes, fact->duration_ms.has_value());
    if (fact->duration_ms)
      Put(bytes, *fact->duration_ms);
    PutBool(bytes, fact->is_live);
    PutBool(bytes, fact->has_video);
    PutBool(bytes, fact->has_audio);
    PutBool(bytes, fact->visible);
    PutBool(bytes, fact->eme_encrypted);
    Put(bytes, fact->visible_fraction_ppm);
    PutUrl(bytes, fact->page_url);
    PutUrl(bytes, fact->media_url);
  }
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return std::nullopt;
  }
  return bytes;
}

std::optional<PlayerMessage>
DecodePlayerMessage(const std::vector<std::uint8_t> &bytes, CodecError *error) {
  if (!ValidatePlayerHeader(bytes, error))
    return std::nullopt;
  PlayerReader reader(bytes);
  PlayerContext context;
  if (!reader.Context(&context)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  if (bytes[6] == kPlayerRemoveKind) {
    if (!reader.Done()) {
      SetError(CodecError::kTrailingBytes, error);
      return std::nullopt;
    }
    return PlayerMessage(context);
  }
  PlayerFact fact;
  fact.context = context;
  std::uint8_t source_kind = 0;
  bool has_duration = false;
  std::uint64_t duration = 0;
  if (!reader.Nonzero(&fact.observed_at_ms) || !reader.U8(&source_kind) ||
      source_kind > static_cast<std::uint8_t>(PlayerSourceKind::kMediaStream) ||
      !reader.U64(&fact.position_ms) || !reader.Bool(&has_duration) ||
      (has_duration && !reader.U64(&duration)) || !reader.Bool(&fact.is_live) ||
      !reader.Bool(&fact.has_video) || !reader.Bool(&fact.has_audio) ||
      !reader.Bool(&fact.visible) || !reader.Bool(&fact.eme_encrypted) ||
      !reader.U32(&fact.visible_fraction_ppm) ||
      fact.visible_fraction_ppm > 1'000'000 ||
      !reader.Url(&fact.page_url, false) ||
      !reader.Url(&fact.media_url, source_kind != 0)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  fact.source_kind = static_cast<PlayerSourceKind>(source_kind);
  if (has_duration)
    fact.duration_ms = duration;
  if (!reader.Done()) {
    SetError(CodecError::kTrailingBytes, error);
    return std::nullopt;
  }
  if (!ValidFact(fact)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  return PlayerMessage(std::move(fact));
}

std::optional<std::vector<std::uint8_t>>
EncodePlayerPageMessage(const PlayerPageMessage &message, CodecError *error) {
  const auto *request = std::get_if<PlayerListRequest>(&message);
  const auto *reply = std::get_if<PlayerPageReply>(&message);
  if ((request &&
       (!ValidPageContext(request->context) || request->max_items == 0 ||
        request->max_items > kMaxPageItems)) ||
      (reply && !ValidPageReply(*reply))) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(128);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  Put(bytes, kVersion);
  bytes.push_back(request ? kPlayerListKind : kPlayerPageKind);
  bytes.push_back(0);
  PutPageContext(bytes, request ? request->context : reply->context);
  if (request) {
    Put(bytes, request->snapshot_revision);
    Put(bytes, request->offset);
    Put(bytes, request->max_items);
  } else {
    Put(bytes, reply->snapshot_revision);
    bytes.push_back(static_cast<std::uint8_t>(reply->status));
    Put(bytes, reply->offset);
    PutBool(bytes, reply->next_offset.has_value());
    if (reply->next_offset)
      Put(bytes, *reply->next_offset);
    Put(bytes, static_cast<std::uint16_t>(reply->players.size()));
    for (const auto &player : reply->players) {
      Put(bytes, player.instance_id);
      Put(bytes, player.source_revision);
      bytes.push_back(static_cast<std::uint8_t>(player.source_kind));
      PutBool(bytes, player.has_video);
      PutBool(bytes, player.has_audio);
      PutBool(bytes, player.visible);
      PutBool(bytes, player.eme_encrypted);
      PutUrl(bytes, player.redacted_origin);
    }
  }
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return std::nullopt;
  }
  return bytes;
}

std::optional<PlayerPageMessage>
DecodePlayerPageMessage(const std::vector<std::uint8_t> &bytes,
                        CodecError *error) {
  if (!ValidatePlayerPageHeader(bytes, error))
    return std::nullopt;
  PlayerReader reader(bytes);
  PlayerPageContext context;
  if (!reader.PageContext(&context)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  if (bytes[6] == kPlayerListKind) {
    PlayerListRequest request;
    request.context = context;
    if (!reader.U64(&request.snapshot_revision) ||
        !reader.U16(&request.offset) || !reader.U16(&request.max_items) ||
        request.max_items == 0 || request.max_items > kMaxPageItems) {
      SetError(CodecError::kInvalidValue, error);
      return std::nullopt;
    }
    if (!reader.Done()) {
      SetError(CodecError::kTrailingBytes, error);
      return std::nullopt;
    }
    return PlayerPageMessage(request);
  }
  PlayerPageReply reply;
  reply.context = context;
  std::uint8_t status = 0;
  bool has_next = false;
  std::uint16_t next = 0, count = 0;
  if (!reader.Nonzero(&reply.snapshot_revision) || !reader.U8(&status) ||
      status > static_cast<std::uint8_t>(PlayerPageStatus::kStale) ||
      !reader.U16(&reply.offset) || !reader.Bool(&has_next) ||
      (has_next && !reader.U16(&next)) || !reader.U16(&count) ||
      count > kMaxPageItems) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  reply.status = static_cast<PlayerPageStatus>(status);
  if (has_next)
    reply.next_offset = next;
  reply.players.reserve(count);
  for (std::uint16_t index = 0; index < count; ++index) {
    PlayerProjection player;
    std::uint8_t source = 0;
    if (!reader.Nonzero(&player.instance_id) ||
        !reader.Nonzero(&player.source_revision) || !reader.U8(&source) ||
        source > static_cast<std::uint8_t>(PlayerSourceKind::kMediaStream) ||
        !reader.Bool(&player.has_video) || !reader.Bool(&player.has_audio) ||
        !reader.Bool(&player.visible) || !reader.Bool(&player.eme_encrypted) ||
        !reader.Origin(&player.redacted_origin)) {
      SetError(CodecError::kInvalidValue, error);
      return std::nullopt;
    }
    player.source_kind = static_cast<PlayerSourceKind>(source);
    reply.players.push_back(std::move(player));
  }
  if (!reader.Done()) {
    SetError(CodecError::kTrailingBytes, error);
    return std::nullopt;
  }
  if (!ValidPageReply(reply)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  return PlayerPageMessage(std::move(reply));
}

std::optional<std::vector<std::uint8_t>>
EncodeDraftMessage(const DraftMessage &message, CodecError *error) {
  const auto *command = std::get_if<DraftCommand>(&message);
  const auto *state = std::get_if<DraftStateReply>(&message);
  if ((command && !ValidDraftCommand(*command)) ||
      (state && !ValidDraftState(*state))) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(192);
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  Put(bytes, kVersion);
  bytes.push_back(
      command ? kDraftCommandKind
              : state->session_generation
                    ? kDraftStateSessionKind
                    : state->reason == DraftReason::kNone ? kDraftStateKind
                                                         : kDraftStateReasonKind);
  bytes.push_back(0);
  PutDraftContext(bytes, command ? command->context : state->context);
  if (command) {
    bytes.push_back(static_cast<std::uint8_t>(command->action));
    Put(bytes, command->draft_id);
    Put(bytes, command->draft_revision);
    PutBool(bytes, command->media.has_value());
    if (command->media) {
      Put(bytes, command->media->instance_id);
      Put(bytes, command->media->source_revision);
    }
    PutText(bytes, command->device_id);
  } else {
    Put(bytes, state->draft_id);
    Put(bytes, state->draft_revision);
    bytes.push_back(static_cast<std::uint8_t>(state->phase));
    bytes.push_back(static_cast<std::uint8_t>(state->error));
    PutBool(bytes, state->media.has_value());
    if (state->media) {
      Put(bytes, state->media->instance_id);
      Put(bytes, state->media->source_revision);
    }
    PutText(bytes, state->device_id);
    PutBool(bytes, state->device_connected);
    PutBool(bytes, state->replacement_confirmation_required);
    bytes.push_back(static_cast<std::uint8_t>(state->route));
    PutBool(bytes, state->prepared_until_ms.has_value());
    if (state->prepared_until_ms)
      Put(bytes, *state->prepared_until_ms);
    if (state->reason != DraftReason::kNone)
      bytes.push_back(static_cast<std::uint8_t>(state->reason));
    if (state->session_generation)
      Put(bytes, *state->session_generation);
  }
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return std::nullopt;
  }
  return bytes;
}

std::optional<DraftMessage>
DecodeDraftMessage(const std::vector<std::uint8_t> &bytes, CodecError *error) {
  if (!ValidateDraftHeader(bytes, error))
    return std::nullopt;
  PlayerReader reader(bytes);
  DraftContext context;
  if (!reader.DraftContextValue(&context)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  if (bytes[6] == kDraftCommandKind) {
    DraftCommand command;
    command.context = std::move(context);
    std::uint8_t action = 0;
    bool has_media = false;
    DraftMediaRef media;
    if (!reader.U8(&action) ||
        action > static_cast<std::uint8_t>(DraftAction::kCancel) ||
        !reader.U64(&command.draft_id) ||
        !reader.U64(&command.draft_revision) || !reader.Bool(&has_media) ||
        (has_media && (!reader.Nonzero(&media.instance_id) ||
                       !reader.Nonzero(&media.source_revision))) ||
        !reader.Text(&command.device_id, true)) {
      SetError(CodecError::kInvalidValue, error);
      return std::nullopt;
    }
    command.action = static_cast<DraftAction>(action);
    if (has_media)
      command.media = media;
    if (!reader.Done()) {
      SetError(CodecError::kTrailingBytes, error);
      return std::nullopt;
    }
    if (!ValidDraftCommand(command)) {
      SetError(CodecError::kInvalidValue, error);
      return std::nullopt;
    }
    return DraftMessage(std::move(command));
  }
  const bool has_reason = bytes[6] == kDraftStateReasonKind;
  const bool has_session = bytes[6] == kDraftStateSessionKind;
  DraftStateReply state;
  state.context = std::move(context);
  std::uint8_t phase = 0, state_error = 0, route = 0, reason = 0;
  bool has_media = false, has_expiry = false;
  DraftMediaRef media;
  std::uint64_t expiry = 0, session_generation = 0;
  if (!reader.Nonzero(&state.draft_id) ||
      !reader.Nonzero(&state.draft_revision) || !reader.U8(&phase) ||
      phase > static_cast<std::uint8_t>(DraftPhase::kCommitted) ||
      !reader.U8(&state_error) ||
      state_error > static_cast<std::uint8_t>(DraftError::kBusy) ||
      !reader.Bool(&has_media) ||
      (has_media && (!reader.Nonzero(&media.instance_id) ||
                     !reader.Nonzero(&media.source_revision))) ||
      !reader.Text(&state.device_id, true) ||
      !reader.Bool(&state.device_connected) ||
      !reader.Bool(&state.replacement_confirmation_required) ||
      !reader.U8(&route) ||
      route > static_cast<std::uint8_t>(DraftRoute::kRelay) ||
      !reader.Bool(&has_expiry) || (has_expiry && !reader.Nonzero(&expiry)) ||
      (has_reason && (!reader.U8(&reason) || reason == 0 ||
                      reason > static_cast<std::uint8_t>(
                                   DraftReason::kInvalidTarget))) ||
      (has_session && !reader.Nonzero(&session_generation))) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  state.phase = static_cast<DraftPhase>(phase);
  state.error = static_cast<DraftError>(state_error);
  state.route = static_cast<DraftRoute>(route);
  if (has_reason)
    state.reason = static_cast<DraftReason>(reason);
  if (has_session)
    state.session_generation = session_generation;
  if (has_media)
    state.media = media;
  if (has_expiry)
    state.prepared_until_ms = expiry;
  if (!reader.Done()) {
    SetError(CodecError::kTrailingBytes, error);
    return std::nullopt;
  }
  if (!ValidDraftState(state)) {
    SetError(CodecError::kInvalidValue, error);
    return std::nullopt;
  }
  return DraftMessage(std::move(state));
}
} // namespace crayon::cef_shell::ipc::media_host_v2
