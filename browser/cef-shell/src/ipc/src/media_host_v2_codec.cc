#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

#include <algorithm>
#include <array>
#include <tuple>

namespace crayon::cef_shell::ipc::media_host_v2 {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic = {'M', 'H', 'V', '2'};
constexpr std::uint16_t kVersion = 2;
constexpr std::size_t kHeaderBytes = 8;
constexpr auto kKnownCapabilities =
    kCapMediaRead | kCapDraft | kCapConnect | kCapStop;
bool Valid(const Handshake &h) {
  return (h.kind == Kind::kHello || h.kind == Kind::kWelcome) &&
         h.session_id != 0 && h.generation != 0 &&
         (h.capabilities & ~kKnownCapabilities) == 0 &&
         h.max_frame_bytes >= kHandshakeBytes &&
         h.max_frame_bytes <= kMaxFrameBytes && h.max_page_items != 0 &&
         h.max_page_items <= kMaxPageItems;
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
} // namespace crayon::cef_shell::ipc::media_host_v2
