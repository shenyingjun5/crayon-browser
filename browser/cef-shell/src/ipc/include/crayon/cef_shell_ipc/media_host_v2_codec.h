// MHV2 handshake wire; no connection owner, command dispatch or permission.
#pragma once

#include "crayon/cef_shell_ipc/media_host_codec.h"

namespace crayon::cef_shell::ipc::media_host_v2 {
using CodecError = media_host::CodecError;
inline constexpr std::size_t kHandshakeBytes = 34;
inline constexpr std::uint32_t kMaxFrameBytes = 16 * 1024;
inline constexpr std::uint16_t kMaxPageItems = 16;
inline constexpr std::uint32_t kCapMediaRead = 1, kCapDraft = 2,
                               kCapConnect = 4, kCapStop = 8;
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
} // namespace crayon::cef_shell::ipc::media_host_v2
