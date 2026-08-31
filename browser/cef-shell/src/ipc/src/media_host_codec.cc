#include "crayon/cef_shell_ipc/media_host_codec.h"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>

namespace crayon::cef_shell::ipc::media_host {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'M', 'H', 'V', '1'};
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kMaxIdBytes = 128;
constexpr std::size_t kMaxUrlBytes = 2048;
constexpr std::size_t kMaxOriginBytes = 512;
constexpr std::uint64_t kMaxExactF64Integer = 9'007'199'254'740'992ULL;

enum class Kind : std::uint8_t {
  kIngestUrl = 1,
  kMarkEme,
  kDecide,
  kDecideUrlLess,
  kCancel,
  kNavigation,
  kCloseTab,
  kShutdown,
  kCandidateReply,
  kDecisionReply,
  kAck,
  kErrorReply,
  kDiscovery,
  kListDevices,
  kDevicePageReply,
  kStartCast,
  kStartCastReply,
  kStopCast,
  kPollSessionEvents,
  kSessionEventsReply,
};

void SetError(CodecError value, CodecError *error) {
  if (error)
    *error = value;
}

bool IsValidUtf8(const std::string &value) {
  const auto *data = reinterpret_cast<const unsigned char *>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = data[index++];
    std::uint32_t code = 0;
    std::size_t remaining = 0;
    if (first < 0x80) {
      code = first;
    } else if ((first & 0xE0) == 0xC0) {
      code = first & 0x1F;
      remaining = 1;
      if (code == 0)
        return false;
    } else if ((first & 0xF0) == 0xE0) {
      code = first & 0x0F;
      remaining = 2;
    } else if ((first & 0xF8) == 0xF0) {
      code = first & 0x07;
      remaining = 3;
    } else {
      return false;
    }
    if (remaining > value.size() - index)
      return false;
    for (std::size_t i = 0; i < remaining; ++i) {
      const unsigned char next = data[index++];
      if ((next & 0xC0) != 0x80)
        return false;
      code = (code << 6) | (next & 0x3F);
    }
    if ((remaining == 1 && code < 0x80) || (remaining == 2 && code < 0x800) ||
        (remaining == 3 && code < 0x10000) || code > 0x10FFFF ||
        (code >= 0xD800 && code <= 0xDFFF) || code < 0x20 ||
        (code >= 0x7F && code <= 0x9F)) {
      return false;
    }
  }
  return true;
}

bool ValidId(const std::string &value, bool tab) {
  return !value.empty() && value.size() <= kMaxIdBytes &&
         std::all_of(value.begin(), value.end(), [tab](unsigned char c) {
           const bool base = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_';
           return base || (!tab && (c == '.' || c == ':'));
         });
}

bool ValidUrl(const std::string &value) {
  if (value.empty() || value.size() > kMaxUrlBytes || !IsValidUtf8(value))
    return false;
  std::size_t authority = std::string::npos;
  if (value.rfind("http://", 0) == 0)
    authority = 7;
  if (value.rfind("https://", 0) == 0)
    authority = 8;
  if (authority == std::string::npos || authority == value.size())
    return false;
  const auto end = value.find_first_of("/?#", authority);
  const auto host_end = end == std::string::npos ? value.size() : end;
  const auto user_end = value.rfind('@', host_end - 1);
  const auto host_begin = user_end == std::string::npos || user_end < authority
                              ? authority
                              : user_end + 1;
  if (host_begin >= host_end ||
      std::any_of(value.begin() + authority, value.end(),
                  [](unsigned char c) { return c <= 0x20 || c == 0x7f; }))
    return false;
  std::size_t port_begin = host_end;
  if (value[host_begin] == '[') {
    const auto close = value.find(']', host_begin + 1);
    if (close == std::string::npos || close + 1 > host_end ||
        close == host_begin + 1 ||
        !std::all_of(value.begin() + host_begin + 1, value.begin() + close,
                     [](unsigned char c) {
                       return (c >= '0' && c <= '9') ||
                              (c >= 'a' && c <= 'f') ||
                              (c >= 'A' && c <= 'F') || c == ':' || c == '.';
                     }))
      return false;
    port_begin = close + 1;
    if (port_begin < host_end && value[port_begin] != ':')
      return false;
  } else {
    const auto colon = value.find(':', host_begin);
    port_begin = colon < host_end ? colon : host_end;
    if (port_begin == host_begin ||
        !std::all_of(value.begin() + host_begin, value.begin() + port_begin,
                     [](unsigned char c) {
                       return (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-' || c == '.';
                     }))
      return false;
  }
  if (port_begin < host_end) {
    const auto digits = port_begin + 1;
    if (digits == host_end ||
        !std::all_of(value.begin() + digits, value.begin() + host_end,
                     [](unsigned char c) { return c >= '0' && c <= '9'; }))
      return false;
    unsigned long port = 0;
    for (auto it = value.begin() + digits; it != value.begin() + host_end;
         ++it) {
      port = port * 10 + static_cast<unsigned long>(*it - '0');
      if (port > 65'535)
        return false;
    }
  }
  return true;
}

bool ValidOrigin(const std::string &value) {
  if (value.empty())
    return true;
  if (value.size() > kMaxOriginBytes || !ValidUrl(value))
    return false;
  const std::size_t authority = value.rfind("https://", 0) == 0 ? 8 : 7;
  if (value.find('@', authority) != std::string::npos ||
      value.find('?', authority) != std::string::npos ||
      value.find('#', authority) != std::string::npos)
    return false;
  const auto slash = value.find('/', authority);
  return slash == std::string::npos || slash == value.size() - 1;
}

template <typename E> bool EnumAtMost(E value, std::uint8_t maximum) {
  return static_cast<std::uint8_t>(value) <= maximum;
}

bool ValidPlayback(const Playback &value) {
  return value.position_ms <= kMaxExactF64Integer &&
         (!value.duration_ms || *value.duration_ms <= kMaxExactF64Integer) &&
         EnumAtMost(value.ad_continuity, 2);
}

bool ValidDecision(const Decision &value) {
  if (!EnumAtMost(value.kind, 3))
    return false;
  if (value.kind == DecisionKind::kExternalClientHandoff)
    return value.handoff_reason && EnumAtMost(*value.handoff_reason, 8) &&
           !value.reject_reason;
  if (value.kind == DecisionKind::kReject)
    return value.reject_reason && EnumAtMost(*value.reject_reason, 13) &&
           !value.handoff_reason;
  return !value.handoff_reason && !value.reject_reason;
}

bool ValidDevicePage(const DevicePageReply &value) {
  const auto end =
      static_cast<std::size_t>(value.offset) + value.devices.size();
  return value.snapshot_revision && value.offset < kMaxDevices &&
         value.devices.size() <= kMaxDevicePage && end <= kMaxDevices &&
         (!value.devices.empty() || !value.next_offset) &&
         (!value.next_offset ||
          (*value.next_offset == end && end < kMaxDevices));
}

bool HasUniqueDevices(const DevicePageReply &value) {
  for (std::size_t i = 0; i < value.devices.size(); ++i) {
    for (std::size_t prior = 0; prior < i; ++prior) {
      if (value.devices[prior].device_id == value.devices[i].device_id)
        return false;
    }
  }
  return true;
}

bool ValidCastStartOutcome(const CastStartOutcome &value) {
  if (!EnumAtMost(value.kind, 3))
    return false;
  const bool casting = value.kind == CastStartKind::kCasting;
  const bool handoff = value.kind == CastStartKind::kHandoff;
  const bool rejected = value.kind == CastStartKind::kRejected;
  const bool failed = value.kind == CastStartKind::kFailed;
  return (casting == (value.session_generation && *value.session_generation)) &&
         (casting == value.route.has_value()) &&
         (!value.route || EnumAtMost(*value.route, 1)) &&
         (handoff == value.handoff_reason.has_value()) &&
         (!value.handoff_reason || EnumAtMost(*value.handoff_reason, 8)) &&
         (rejected == value.reject_reason.has_value()) &&
         (!value.reject_reason || EnumAtMost(*value.reject_reason, 13)) &&
         (failed == value.error.has_value()) &&
         (!value.error || EnumAtMost(*value.error, 12));
}

bool ValidSessionEvent(const SessionEvent &value) {
  const bool terminated = value.phase == SessionPhase::kTerminated;
  return value.session_generation && value.state_revision &&
         EnumAtMost(value.phase, 5) && EnumAtMost(value.playback, 7) &&
         terminated == value.terminal_reason.has_value() &&
         (!value.terminal_reason || EnumAtMost(*value.terminal_reason, 10));
}

class Writer final {
public:
  explicit Writer(Kind kind) : bytes_(kMagic.begin(), kMagic.end()) {
    U16(kVersion);
    U8(static_cast<std::uint8_t>(kind));
    U8(0);
  }
  void U8(std::uint8_t value) { bytes_.push_back(value); }
  void U16(std::uint16_t value) {
    U8(static_cast<std::uint8_t>(value >> 8));
    U8(static_cast<std::uint8_t>(value));
  }
  void U32(std::uint32_t value) {
    U8(static_cast<std::uint8_t>(value >> 24));
    U8(static_cast<std::uint8_t>(value >> 16));
    U8(static_cast<std::uint8_t>(value >> 8));
    U8(static_cast<std::uint8_t>(value));
  }
  void U64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
      U8(static_cast<std::uint8_t>(value >> shift));
  }
  bool Nonzero(std::uint64_t value) {
    if (!value)
      return Fail(CodecError::kInvalidValue);
    U64(value);
    return true;
  }
  void OptionalNonzero(std::optional<std::uint64_t> value) {
    U64(value.value_or(0));
  }
  void Bool(bool value) { U8(value ? 1 : 0); }
  bool String(const std::string &value, std::size_t max, bool empty) {
    if (value.size() > max)
      return Fail(CodecError::kLengthExceeded);
    if ((!empty && value.empty()) || !IsValidUtf8(value))
      return Fail(CodecError::kInvalidValue);
    U32(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return true;
  }
  bool Id(const std::string &value, bool tab = false) {
    return ValidId(value, tab) ? String(value, kMaxIdBytes, false)
                               : Fail(CodecError::kInvalidValue);
  }
  bool DeviceId(const std::string &value) { return Id(value, true); }
  bool Text(const std::string &value, std::size_t max) {
    return String(value, max, false);
  }
  bool Url(const std::string &value) {
    return ValidUrl(value) ? String(value, kMaxUrlBytes, false)
                           : Fail(CodecError::kInvalidValue);
  }
  bool Origin(const std::string &value) {
    return ValidOrigin(value) ? String(value, kMaxOriginBytes, true)
                              : Fail(CodecError::kInvalidValue);
  }
  bool Fail(CodecError error) {
    if (!error_)
      error_ = error;
    return false;
  }
  std::optional<std::vector<std::uint8_t>> Finish(CodecError *error) {
    if (!error_ && bytes_.size() > kMaxFrameBytes)
      error_ = CodecError::kFrameTooLarge;
    if (error_) {
      SetError(*error_, error);
      return std::nullopt;
    }
    return bytes_;
  }

private:
  std::vector<std::uint8_t> bytes_;
  std::optional<CodecError> error_;
};

class Reader final {
public:
  explicit Reader(const std::vector<std::uint8_t> &bytes) : bytes_(bytes) {}
  const std::uint8_t *Take(std::size_t count) {
    if (count > bytes_.size() - offset_) {
      Fail(CodecError::kTruncated);
      return nullptr;
    }
    const auto *result = bytes_.data() + offset_;
    offset_ += count;
    return result;
  }
  bool U8(std::uint8_t *out) {
    const auto *p = Take(1);
    if (!p)
      return false;
    *out = p[0];
    return true;
  }
  bool U16(std::uint16_t *out) {
    const auto *p = Take(2);
    if (!p)
      return false;
    *out = (std::uint16_t(p[0]) << 8) | p[1];
    return true;
  }
  bool U32(std::uint32_t *out) {
    const auto *p = Take(4);
    if (!p)
      return false;
    *out = (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
    return true;
  }
  bool U64(std::uint64_t *out) {
    const auto *p = Take(8);
    if (!p)
      return false;
    *out = 0;
    for (int i = 0; i < 8; ++i)
      *out = (*out << 8) | p[i];
    return true;
  }
  bool Nonzero(std::uint64_t *out) {
    return U64(out) && (*out != 0 || Fail(CodecError::kInvalidValue));
  }
  bool OptionalNonzero(std::optional<std::uint64_t> *out) {
    std::uint64_t value = 0;
    if (!U64(&value))
      return false;
    *out = value ? std::optional<std::uint64_t>(value) : std::nullopt;
    return true;
  }
  bool Bool(bool *out) {
    std::uint8_t value = 0;
    if (!U8(&value))
      return false;
    if (value > 1)
      return Fail(CodecError::kInvalidValue);
    *out = value == 1;
    return true;
  }
  bool String(std::string *out, std::size_t max, bool empty) {
    std::uint32_t length = 0;
    if (!U32(&length))
      return false;
    if (length > max)
      return Fail(CodecError::kLengthExceeded);
    const auto *p = Take(length);
    if (!p)
      return false;
    std::string value(reinterpret_cast<const char *>(p), length);
    if ((!empty && value.empty()) || !IsValidUtf8(value))
      return Fail(CodecError::kInvalidUtf8);
    *out = std::move(value);
    return true;
  }
  bool Id(std::string *out, bool tab = false) {
    return String(out, kMaxIdBytes, false) &&
           (ValidId(*out, tab) || Fail(CodecError::kInvalidValue));
  }
  bool DeviceId(std::string *out) { return Id(out, true); }
  bool Text(std::string *out, std::size_t max) {
    return String(out, max, false);
  }
  bool Url(std::string *out) {
    return String(out, kMaxUrlBytes, false) &&
           (ValidUrl(*out) || Fail(CodecError::kInvalidValue));
  }
  bool Origin(std::string *out) {
    return String(out, kMaxOriginBytes, true) &&
           (ValidOrigin(*out) || Fail(CodecError::kInvalidValue));
  }
  bool Done() {
    return error_
               ? false
               : (offset_ == bytes_.size() || Fail(CodecError::kTrailingBytes));
  }
  CodecError error() const {
    return error_.value_or(CodecError::kInvalidValue);
  }
  bool Fail(CodecError error) {
    if (!error_)
      error_ = error;
    return false;
  }

private:
  const std::vector<std::uint8_t> &bytes_;
  std::size_t offset_ = kHeaderBytes;
  std::optional<CodecError> error_;
};

void EncodePlayback(Writer *writer, const Playback &value) {
  if (!ValidPlayback(value)) {
    writer->Fail(CodecError::kInvalidValue);
    return;
  }
  writer->U64(value.position_ms);
  writer->Bool(value.duration_ms.has_value());
  if (value.duration_ms)
    writer->U64(*value.duration_ms);
  writer->Bool(value.is_live);
  writer->U8(static_cast<std::uint8_t>(value.ad_continuity));
  writer->Bool(value.current_src);
  writer->Bool(value.near_play_event);
  writer->Bool(value.audible);
  writer->Bool(value.main_frame);
  writer->U32(value.visible_area_px);
}

bool DecodePlayback(Reader *reader, Playback *value) {
  bool duration = false;
  std::uint8_t continuity = 0;
  if (!reader->U64(&value->position_ms) || !reader->Bool(&duration))
    return false;
  std::uint64_t duration_ms = 0;
  if (duration && !reader->U64(&duration_ms))
    return false;
  value->duration_ms =
      duration ? std::optional<std::uint64_t>(duration_ms) : std::nullopt;
  if (!reader->Bool(&value->is_live) || !reader->U8(&continuity) ||
      continuity > 2 || !reader->Bool(&value->current_src) ||
      !reader->Bool(&value->near_play_event) ||
      !reader->Bool(&value->audible) || !reader->Bool(&value->main_frame) ||
      !reader->U32(&value->visible_area_px))
    return reader->Fail(CodecError::kInvalidValue);
  value->ad_continuity = static_cast<AdContinuity>(continuity);
  return ValidPlayback(*value) || reader->Fail(CodecError::kInvalidValue);
}

void EncodeReceiver(Writer *writer, const Receiver &value) {
  writer->Bool(value.mp4);
  writer->Bool(value.hls);
  writer->Bool(value.dash);
  writer->Bool(value.h264);
  writer->Bool(value.hevc);
  writer->Bool(value.av1);
  writer->U16(value.max_height);
}

bool DecodeReceiver(Reader *reader, Receiver *value) {
  return reader->Bool(&value->mp4) && reader->Bool(&value->hls) &&
         reader->Bool(&value->dash) && reader->Bool(&value->h264) &&
         reader->Bool(&value->hevc) && reader->Bool(&value->av1) &&
         reader->U16(&value->max_height);
}

void EncodeDecision(Writer *writer, const Decision &value) {
  if (!ValidDecision(value)) {
    writer->Fail(CodecError::kInvalidValue);
    return;
  }
  writer->U8(static_cast<std::uint8_t>(value.kind));
  if (value.handoff_reason)
    writer->U8(static_cast<std::uint8_t>(*value.handoff_reason));
  if (value.reject_reason)
    writer->U8(static_cast<std::uint8_t>(*value.reject_reason));
}

bool DecodeDecision(Reader *reader, Decision *value) {
  std::uint8_t kind = 0, reason = 0;
  if (!reader->U8(&kind) || kind > 3)
    return reader->Fail(CodecError::kInvalidValue);
  value->kind = static_cast<DecisionKind>(kind);
  if (value->kind == DecisionKind::kExternalClientHandoff) {
    if (!reader->U8(&reason) || reason > 8)
      return reader->Fail(CodecError::kInvalidValue);
    value->handoff_reason = static_cast<HandoffReason>(reason);
  } else if (value->kind == DecisionKind::kReject) {
    if (!reader->U8(&reason) || reason > 13)
      return reader->Fail(CodecError::kInvalidValue);
    value->reject_reason = static_cast<CoreError>(reason);
  }
  return true;
}

void EncodeDevicePage(Writer *writer, const DevicePageReply &value) {
  if (!ValidDevicePage(value) || !HasUniqueDevices(value)) {
    writer->Fail(CodecError::kInvalidValue);
    return;
  }
  writer->Id(value.request_id);
  writer->Nonzero(value.snapshot_revision);
  writer->U16(value.offset);
  writer->U16(
      value.next_offset.value_or(std::numeric_limits<std::uint16_t>::max()));
  writer->U16(static_cast<std::uint16_t>(value.devices.size()));
  for (const auto &device : value.devices) {
    writer->DeviceId(device.device_id);
    writer->Text(device.display_name, kMaxDeviceNameBytes);
    if (!EnumAtMost(device.state, 4))
      writer->Fail(CodecError::kInvalidValue);
    writer->U8(static_cast<std::uint8_t>(device.state));
    writer->Bool(device.is_crayon_receiver);
  }
}

bool DecodeDevicePage(Reader *reader, DevicePageReply *value) {
  std::uint16_t next = 0, count = 0;
  if (!reader->Id(&value->request_id) ||
      !reader->Nonzero(&value->snapshot_revision) ||
      !reader->U16(&value->offset) || !reader->U16(&next) ||
      !reader->U16(&count))
    return false;
  value->next_offset = next == std::numeric_limits<std::uint16_t>::max()
                           ? std::nullopt
                           : std::optional<std::uint16_t>(next);
  if (count > kMaxDevicePage)
    return reader->Fail(CodecError::kInvalidValue);
  value->devices.resize(count);
  if (!ValidDevicePage(*value))
    return reader->Fail(CodecError::kInvalidValue);
  for (auto &device : value->devices) {
    std::uint8_t state = 0;
    if (!reader->DeviceId(&device.device_id) ||
        !reader->Text(&device.display_name, kMaxDeviceNameBytes) ||
        !reader->U8(&state) || state > 4 ||
        !reader->Bool(&device.is_crayon_receiver))
      return reader->Fail(CodecError::kInvalidValue);
    device.state = static_cast<DeviceState>(state);
  }
  return HasUniqueDevices(*value) || reader->Fail(CodecError::kInvalidValue);
}

void EncodeCastStartOutcome(Writer *writer, const CastStartOutcome &value) {
  if (!ValidCastStartOutcome(value)) {
    writer->Fail(CodecError::kInvalidValue);
    return;
  }
  writer->U8(static_cast<std::uint8_t>(value.kind));
  if (value.session_generation) {
    writer->Nonzero(*value.session_generation);
    writer->U8(static_cast<std::uint8_t>(*value.route));
  } else if (value.handoff_reason) {
    writer->U8(static_cast<std::uint8_t>(*value.handoff_reason));
  } else if (value.reject_reason) {
    writer->U8(static_cast<std::uint8_t>(*value.reject_reason));
  } else if (value.error) {
    writer->U8(static_cast<std::uint8_t>(*value.error));
  }
}

bool DecodeCastStartOutcome(Reader *reader, CastStartOutcome *value) {
  std::uint8_t kind = 0, detail = 0;
  if (!reader->U8(&kind) || kind > 3)
    return reader->Fail(CodecError::kInvalidValue);
  value->kind = static_cast<CastStartKind>(kind);
  if (value->kind == CastStartKind::kCasting) {
    std::uint64_t generation = 0;
    if (!reader->Nonzero(&generation) || !reader->U8(&detail) || detail > 1)
      return reader->Fail(CodecError::kInvalidValue);
    value->session_generation = generation;
    value->route = static_cast<DeliveryRoute>(detail);
  } else {
    if (!reader->U8(&detail))
      return false;
    if (value->kind == CastStartKind::kHandoff && detail <= 8)
      value->handoff_reason = static_cast<HandoffReason>(detail);
    else if (value->kind == CastStartKind::kRejected && detail <= 13)
      value->reject_reason = static_cast<CoreError>(detail);
    else if (value->kind == CastStartKind::kFailed && detail <= 12)
      value->error = static_cast<CastError>(detail);
    else
      return reader->Fail(CodecError::kInvalidValue);
  }
  return ValidCastStartOutcome(*value) ||
         reader->Fail(CodecError::kInvalidValue);
}

void EncodeSessionEvents(Writer *writer, const SessionEventsReply &value) {
  if (value.events.size() > kMaxSessionEvents) {
    writer->Fail(CodecError::kInvalidValue);
    return;
  }
  writer->Id(value.request_id);
  writer->U64(value.dropped_events);
  writer->U16(static_cast<std::uint16_t>(value.events.size()));
  for (const auto &event : value.events) {
    if (!ValidSessionEvent(event)) {
      writer->Fail(CodecError::kInvalidValue);
      return;
    }
    writer->Nonzero(event.session_generation);
    writer->Nonzero(event.state_revision);
    writer->U8(static_cast<std::uint8_t>(event.phase));
    writer->U8(static_cast<std::uint8_t>(event.playback));
    writer->U8(event.terminal_reason
                   ? static_cast<std::uint8_t>(*event.terminal_reason)
                   : 0xff);
  }
}

bool DecodeSessionEvents(Reader *reader, SessionEventsReply *value) {
  std::uint16_t count = 0;
  if (!reader->Id(&value->request_id) || !reader->U64(&value->dropped_events) ||
      !reader->U16(&count) || count > kMaxSessionEvents)
    return reader->Fail(CodecError::kInvalidValue);
  value->events.resize(count);
  for (auto &event : value->events) {
    std::uint8_t phase = 0, playback = 0, reason = 0;
    if (!reader->Nonzero(&event.session_generation) ||
        !reader->Nonzero(&event.state_revision) || !reader->U8(&phase) ||
        phase > 5 || !reader->U8(&playback) || playback > 7 ||
        !reader->U8(&reason))
      return reader->Fail(CodecError::kInvalidValue);
    event.phase = static_cast<SessionPhase>(phase);
    event.playback = static_cast<SessionPlayback>(playback);
    if (reason != 0xff) {
      if (reason > 10)
        return reader->Fail(CodecError::kInvalidValue);
      event.terminal_reason = static_cast<TerminalReason>(reason);
    }
    if (!ValidSessionEvent(event))
      return reader->Fail(CodecError::kInvalidValue);
  }
  return true;
}

Kind KindOf(const Message &message) {
  return std::visit(
      [](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IngestUrl>)
          return Kind::kIngestUrl;
        else if constexpr (std::is_same_v<T, MarkEme>)
          return Kind::kMarkEme;
        else if constexpr (std::is_same_v<T, Decide>)
          return Kind::kDecide;
        else if constexpr (std::is_same_v<T, DecideUrlLess>)
          return Kind::kDecideUrlLess;
        else if constexpr (std::is_same_v<T, Cancel>)
          return Kind::kCancel;
        else if constexpr (std::is_same_v<T, Navigation>)
          return Kind::kNavigation;
        else if constexpr (std::is_same_v<T, CloseTab>)
          return Kind::kCloseTab;
        else if constexpr (std::is_same_v<T, Shutdown>)
          return Kind::kShutdown;
        else if constexpr (std::is_same_v<T, CandidateReply>)
          return Kind::kCandidateReply;
        else if constexpr (std::is_same_v<T, DecisionReply>)
          return Kind::kDecisionReply;
        else if constexpr (std::is_same_v<T, Ack>)
          return Kind::kAck;
        else if constexpr (std::is_same_v<T, ErrorReply>)
          return Kind::kErrorReply;
        else if constexpr (std::is_same_v<T, Discovery>)
          return Kind::kDiscovery;
        else if constexpr (std::is_same_v<T, ListDevices>)
          return Kind::kListDevices;
        else if constexpr (std::is_same_v<T, DevicePageReply>)
          return Kind::kDevicePageReply;
        else if constexpr (std::is_same_v<T, StartCast>)
          return Kind::kStartCast;
        else if constexpr (std::is_same_v<T, StartCastReply>)
          return Kind::kStartCastReply;
        else if constexpr (std::is_same_v<T, StopCast>)
          return Kind::kStopCast;
        else if constexpr (std::is_same_v<T, PollSessionEvents>)
          return Kind::kPollSessionEvents;
        else
          return Kind::kSessionEventsReply;
      },
      message);
}

} // namespace

std::optional<std::vector<std::uint8_t>> Encode(const Message &message,
                                                CodecError *error) {
  Writer writer(KindOf(message));
  std::visit(
      [&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IngestUrl>) {
          writer.Id(value.request_id);
          writer.Id(value.tab_id, true);
          writer.Nonzero(value.navigation_id);
          writer.Nonzero(value.generation);
          writer.U64(value.observed_at_ms);
          writer.Url(value.page_url);
          writer.Url(value.media_url);
          if (!EnumAtMost(value.source, 1) ||
              !EnumAtMost(value.headers_class, 3))
            writer.Fail(CodecError::kInvalidValue);
          writer.U8(static_cast<std::uint8_t>(value.source));
          writer.U8(static_cast<std::uint8_t>(value.headers_class));
          writer.Bool(value.playback.has_value());
          if (value.playback)
            EncodePlayback(&writer, *value.playback);
          writer.Bool(value.eme_encrypted);
        } else if constexpr (std::is_same_v<T, MarkEme> ||
                             std::is_same_v<T, Navigation>) {
          writer.Id(value.request_id);
          writer.Id(value.tab_id, true);
          writer.Nonzero(value.navigation_id);
          writer.Nonzero(value.generation);
        } else if constexpr (std::is_same_v<T, Decide>) {
          writer.Id(value.request_id);
          writer.Nonzero(value.candidate_id);
          writer.U64(value.now_ms);
          EncodeReceiver(&writer, value.receiver);
          writer.Bool(value.handoff_available);
        } else if constexpr (std::is_same_v<T, DecideUrlLess>) {
          writer.Id(value.request_id);
          writer.Id(value.tab_id, true);
          writer.Nonzero(value.navigation_id);
          writer.Nonzero(value.generation);
          writer.Url(value.page_url);
          EncodePlayback(&writer, value.playback);
          writer.Bool(value.eme_encrypted);
          writer.Bool(value.handoff_available);
        } else if constexpr (std::is_same_v<T, Cancel> ||
                             std::is_same_v<T, Ack>) {
          writer.Id(value.request_id);
        } else if constexpr (std::is_same_v<T, CloseTab>) {
          writer.Id(value.request_id);
          writer.Id(value.tab_id, true);
          writer.Nonzero(value.generation);
        } else if constexpr (std::is_same_v<T, CandidateReply>) {
          writer.Id(value.request_id);
          writer.OptionalNonzero(value.candidate_id);
          writer.Origin(value.redacted_origin);
          if (value.candidate_id.has_value() == value.redacted_origin.empty())
            writer.Fail(CodecError::kInvalidValue);
        } else if constexpr (std::is_same_v<T, DecisionReply>) {
          writer.Id(value.request_id);
          writer.OptionalNonzero(value.candidate_id);
          writer.U8(value.protocol ? static_cast<std::uint8_t>(*value.protocol)
                                   : 0xff);
          if (value.candidate_id.has_value() != value.protocol.has_value() ||
              (value.protocol && !EnumAtMost(*value.protocol, 2)))
            writer.Fail(CodecError::kInvalidValue);
          EncodeDecision(&writer, value.decision);
        } else if constexpr (std::is_same_v<T, ErrorReply>) {
          writer.Id(value.request_id);
          if (!EnumAtMost(value.code, 6))
            writer.Fail(CodecError::kInvalidValue);
          writer.U8(static_cast<std::uint8_t>(value.code));
        } else if constexpr (std::is_same_v<T, Discovery>) {
          writer.Id(value.request_id);
          if (!EnumAtMost(value.action, 2))
            writer.Fail(CodecError::kInvalidValue);
          writer.U8(static_cast<std::uint8_t>(value.action));
        } else if constexpr (std::is_same_v<T, ListDevices>) {
          writer.Id(value.request_id);
          writer.OptionalNonzero(value.snapshot_revision);
          if (value.offset >= kMaxDevices ||
              (!value.snapshot_revision && value.offset != 0))
            writer.Fail(CodecError::kInvalidValue);
          writer.U16(value.offset);
        } else if constexpr (std::is_same_v<T, DevicePageReply>) {
          EncodeDevicePage(&writer, value);
        } else if constexpr (std::is_same_v<T, StartCast>) {
          writer.Id(value.request_id);
          writer.Nonzero(value.candidate_id);
          writer.DeviceId(value.device_id);
          writer.Bool(value.handoff_available);
        } else if constexpr (std::is_same_v<T, StartCastReply>) {
          writer.Id(value.request_id);
          EncodeCastStartOutcome(&writer, value.outcome);
        } else if constexpr (std::is_same_v<T, StopCast>) {
          writer.Id(value.request_id);
          writer.Nonzero(value.session_generation);
        } else if constexpr (std::is_same_v<T, PollSessionEvents>) {
          writer.Id(value.request_id);
        } else if constexpr (std::is_same_v<T, SessionEventsReply>) {
          EncodeSessionEvents(&writer, value);
        }
      },
      message);
  return writer.Finish(error);
}

std::optional<Message> Decode(const std::vector<std::uint8_t> &bytes,
                              CodecError *error) {
  if (bytes.size() > kMaxFrameBytes) {
    SetError(CodecError::kFrameTooLarge, error);
    return {};
  }
  if (bytes.size() < kHeaderBytes) {
    SetError(CodecError::kTruncated, error);
    return {};
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(CodecError::kInvalidMagic, error);
    return {};
  }
  if (bytes[4] != 0 || bytes[5] != kVersion) {
    SetError(CodecError::kUnsupportedVersion, error);
    return {};
  }
  if (bytes[7] != 0) {
    SetError(CodecError::kInvalidFlags, error);
    return {};
  }
  Reader reader(bytes);
  std::optional<Message> message;
  switch (bytes[6]) {
  case 1: {
    IngestUrl v;
    std::uint8_t source = 0, headers = 0;
    bool playback = false;
    if (reader.Id(&v.request_id) && reader.Id(&v.tab_id, true) &&
        reader.Nonzero(&v.navigation_id) && reader.Nonzero(&v.generation) &&
        reader.U64(&v.observed_at_ms) && reader.Url(&v.page_url) &&
        reader.Url(&v.media_url) && reader.U8(&source) && source <= 1 &&
        reader.U8(&headers) && headers <= 3 && reader.Bool(&playback)) {
      v.source = static_cast<Source>(source);
      v.headers_class = static_cast<HeadersClass>(headers);
      Playback p;
      if (playback && !DecodePlayback(&reader, &p))
        break;
      if (playback)
        v.playback = p;
      if (reader.Bool(&v.eme_encrypted))
        message = std::move(v);
    } else {
      reader.Fail(CodecError::kInvalidValue);
    }
    break;
  }
  case 2:
  case 6: {
    Navigation v;
    if (reader.Id(&v.request_id) && reader.Id(&v.tab_id, true) &&
        reader.Nonzero(&v.navigation_id) && reader.Nonzero(&v.generation))
      message = bytes[6] == 2 ? Message(MarkEme{v.request_id, v.tab_id,
                                                v.navigation_id, v.generation})
                              : Message(std::move(v));
    break;
  }
  case 3: {
    Decide v;
    if (reader.Id(&v.request_id) && reader.Nonzero(&v.candidate_id) &&
        reader.U64(&v.now_ms) && DecodeReceiver(&reader, &v.receiver) &&
        reader.Bool(&v.handoff_available))
      message = std::move(v);
    break;
  }
  case 4: {
    DecideUrlLess v;
    if (reader.Id(&v.request_id) && reader.Id(&v.tab_id, true) &&
        reader.Nonzero(&v.navigation_id) && reader.Nonzero(&v.generation) &&
        reader.Url(&v.page_url) && DecodePlayback(&reader, &v.playback) &&
        reader.Bool(&v.eme_encrypted) && reader.Bool(&v.handoff_available))
      message = std::move(v);
    break;
  }
  case 5: {
    Cancel v;
    if (reader.Id(&v.request_id))
      message = std::move(v);
    break;
  }
  case 7: {
    CloseTab v;
    if (reader.Id(&v.request_id) && reader.Id(&v.tab_id, true) &&
        reader.Nonzero(&v.generation)) {
      message = std::move(v);
    }
    break;
  }
  case 8:
    message = Shutdown{};
    break;
  case 9: {
    CandidateReply v;
    if (reader.Id(&v.request_id) && reader.OptionalNonzero(&v.candidate_id) &&
        reader.Origin(&v.redacted_origin) &&
        v.candidate_id.has_value() != v.redacted_origin.empty())
      message = std::move(v);
    else
      reader.Fail(CodecError::kInvalidValue);
    break;
  }
  case 10: {
    DecisionReply v;
    std::uint8_t protocol = 0;
    if (reader.Id(&v.request_id) && reader.OptionalNonzero(&v.candidate_id) &&
        reader.U8(&protocol)) {
      if (protocol != 0xff) {
        if (protocol > 2) {
          reader.Fail(CodecError::kInvalidValue);
          break;
        }
        v.protocol = static_cast<Protocol>(protocol);
      }
      if (v.candidate_id.has_value() != v.protocol.has_value()) {
        reader.Fail(CodecError::kInvalidValue);
        break;
      }
      if (DecodeDecision(&reader, &v.decision))
        message = std::move(v);
    }
    break;
  }
  case 11: {
    Ack v;
    if (reader.Id(&v.request_id))
      message = std::move(v);
    break;
  }
  case 12: {
    ErrorReply v;
    std::uint8_t code = 0;
    if (reader.Id(&v.request_id) && reader.U8(&code) && code <= 6) {
      v.code = static_cast<HostError>(code);
      message = std::move(v);
    } else
      reader.Fail(CodecError::kInvalidValue);
    break;
  }
  case 13: {
    Discovery v;
    std::uint8_t action = 0;
    if (reader.Id(&v.request_id) && reader.U8(&action) && action <= 2) {
      v.action = static_cast<DiscoveryAction>(action);
      message = std::move(v);
    } else {
      reader.Fail(CodecError::kInvalidValue);
    }
    break;
  }
  case 14: {
    ListDevices v;
    if (reader.Id(&v.request_id) &&
        reader.OptionalNonzero(&v.snapshot_revision) && reader.U16(&v.offset) &&
        v.offset < kMaxDevices && (v.snapshot_revision || v.offset == 0)) {
      message = std::move(v);
    } else {
      reader.Fail(CodecError::kInvalidValue);
    }
    break;
  }
  case 15: {
    DevicePageReply v;
    if (DecodeDevicePage(&reader, &v))
      message = std::move(v);
    break;
  }
  case 16: {
    StartCast v;
    if (reader.Id(&v.request_id) && reader.Nonzero(&v.candidate_id) &&
        reader.DeviceId(&v.device_id) && reader.Bool(&v.handoff_available))
      message = std::move(v);
    break;
  }
  case 17: {
    StartCastReply v;
    if (reader.Id(&v.request_id) && DecodeCastStartOutcome(&reader, &v.outcome))
      message = std::move(v);
    break;
  }
  case 18: {
    StopCast v;
    if (reader.Id(&v.request_id) && reader.Nonzero(&v.session_generation))
      message = std::move(v);
    break;
  }
  case 19: {
    PollSessionEvents v;
    if (reader.Id(&v.request_id))
      message = std::move(v);
    break;
  }
  case 20: {
    SessionEventsReply v;
    if (DecodeSessionEvents(&reader, &v))
      message = std::move(v);
    break;
  }
  default:
    SetError(CodecError::kUnknownKind, error);
    return {};
  }
  if (!message || !reader.Done()) {
    SetError(reader.error(), error);
    return {};
  }
  return message;
}

const char *ToString(CodecError error) {
  switch (error) {
  case CodecError::kFrameTooLarge:
    return "media-host frame exceeds size limit";
  case CodecError::kInvalidMagic:
    return "media-host magic rejected";
  case CodecError::kUnsupportedVersion:
    return "media-host version rejected";
  case CodecError::kUnknownKind:
    return "media-host message kind rejected";
  case CodecError::kInvalidFlags:
    return "media-host flags rejected";
  case CodecError::kTruncated:
    return "media-host frame truncated";
  case CodecError::kTrailingBytes:
    return "media-host frame has trailing bytes";
  case CodecError::kInvalidUtf8:
    return "media-host string is not UTF-8";
  case CodecError::kInvalidValue:
    return "media-host value rejected";
  case CodecError::kLengthExceeded:
    return "media-host field exceeds size limit";
  }
  return "media-host codec error";
}

} // namespace crayon::cef_shell::ipc::media_host
