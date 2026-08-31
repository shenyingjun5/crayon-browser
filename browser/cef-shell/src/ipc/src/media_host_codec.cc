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

class Writer final {
public:
  explicit Writer(Kind kind) : bytes_(kMagic.begin(), kMagic.end()) {
    U16(kVersion);
    U8(static_cast<std::uint8_t>(kind));
    U8(0);
  }
  void U8(std::uint8_t value) { bytes_.push_back(value); }
  void U16(std::uint16_t value) {
    U8(value >> 8);
    U8(value);
  }
  void U32(std::uint32_t value) {
    U8(value >> 24);
    U8(value >> 16);
    U8(value >> 8);
    U8(value);
  }
  void U64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
      U8(value >> shift);
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

Kind KindOf(const Message &message) {
  return std::visit(
      [](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IngestUrl>)
          return Kind::kIngestUrl;
        if constexpr (std::is_same_v<T, MarkEme>)
          return Kind::kMarkEme;
        if constexpr (std::is_same_v<T, Decide>)
          return Kind::kDecide;
        if constexpr (std::is_same_v<T, DecideUrlLess>)
          return Kind::kDecideUrlLess;
        if constexpr (std::is_same_v<T, Cancel>)
          return Kind::kCancel;
        if constexpr (std::is_same_v<T, Navigation>)
          return Kind::kNavigation;
        if constexpr (std::is_same_v<T, CloseTab>)
          return Kind::kCloseTab;
        if constexpr (std::is_same_v<T, Shutdown>)
          return Kind::kShutdown;
        if constexpr (std::is_same_v<T, CandidateReply>)
          return Kind::kCandidateReply;
        if constexpr (std::is_same_v<T, DecisionReply>)
          return Kind::kDecisionReply;
        if constexpr (std::is_same_v<T, Ack>)
          return Kind::kAck;
        return Kind::kErrorReply;
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
