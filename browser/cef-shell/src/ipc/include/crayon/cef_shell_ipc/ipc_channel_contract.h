// CEF-06: platform-neutral IPC contract between the CEF browser process
// and the crayon core side: length-prefixed framing, session-secret
// verification with rotation generations, and message guards (schema
// version window, size caps, process tokens).  No OS transport lives
// here (CEF-07 / AGT-12); no CEF types leak into this interface.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::cef_shell::ipc {

/// Frame header size: 4-byte big-endian payload length.
inline constexpr std::size_t kFrameHeaderBytes = 4;
/// Hard per-frame payload cap; oversize frames are rejected, never
/// buffered.
inline constexpr std::size_t kMaxFrameBytes = 65'536;
/// Hard single-feed cap: a hostile chunk larger than twice the frame
/// cap is rejected without buffering.
inline constexpr std::size_t kMaxFeedBytes = kMaxFrameBytes * 2;
/// Session secret size in bytes.
inline constexpr std::size_t kSecretBytes = 32;
/// Current wire schema version (single-version v1 window; must stay in
/// sync with crayon-ipc-schema).
inline constexpr std::uint16_t kCurrentSchemaVersion = 1;
/// Maximum process token length in bytes.
inline constexpr std::size_t kMaxProcessTokenLen = 64;

/// Closed IPC failure causes; stable string forms via ToString.
enum class IpcError {
  kFrameTooLarge = 0,
  kFrameMalformed,
  kVersionRejected,
  kSecretMismatch,
  kSecretExpired,
  kProcessTokenRejected,
  kMessageTooLarge,
};

/// Stable closed-form error string (no payload data).
const char* ToString(IpcError error);

/// One decode outcome.
enum class DecodeStatus { kComplete, kIncomplete, kOversize };

/// Streaming length-prefixed frame decoder (pure, no IO).
class FrameCodec final {
 public:
  /// Appends `chunk` and decodes one frame.  A hostile chunk larger
  /// than `kMaxFeedBytes` fails closed without buffering anything.
  bool Feed(const std::uint8_t* data, std::size_t len, IpcError* error);

  /// Decodes the next frame from already-buffered bytes.
  DecodeStatus Take(std::vector<std::uint8_t>* payload, std::uint32_t* declared);

  /// Clears all buffered bytes (connection reset / resynchronize);
  /// pending hostile leftovers must not leak into a new connection.
  void Reset();

  /// Encodes `payload` into a full frame.
  static std::vector<std::uint8_t> Encode(const std::vector<std::uint8_t>& payload);

  std::size_t pending_bytes() const { return buffer_.size(); }

 private:
  std::vector<std::uint8_t> buffer_;
};

/// Session secret verification with rotation generations.  Comparison
/// is constant-time; a rotated-out secret stays accepted only for the
/// immediately preceding generation, then expires.
class SessionSecretVerifier final {
 public:
  /// Installs the initial secret (32 bytes) at generation 1.
  bool Install(const std::uint8_t* secret, std::size_t len);

  /// Rotates to a new secret and bumps the generation; the previous
  /// secret remains verifiable until the next rotation.
  bool Rotate(const std::uint8_t* secret, std::size_t len);

  /// Verifies `len` bytes against the current (or immediately previous)
  /// secret.  Wrong-size or mismatched input is a stable rejection.
  bool Verify(const std::uint8_t* secret, std::size_t len) const;

  std::uint32_t generation() const { return generation_; }

 private:
  bool SetSecret(std::vector<std::uint8_t>* slot, const std::uint8_t* secret,
                 std::size_t len);

  std::vector<std::uint8_t> current_;
  std::vector<std::uint8_t> previous_;
  std::uint32_t generation_ = 0;
};

/// Reports whether `token` matches the closed process charset
/// `[A-Za-z0-9_.-]` within the bound.
bool IsValidProcessToken(const std::string& token);

/// Per-message guard: schema version window + declared size + sender
/// process token.  Version and token are checked before any payload
/// interpretation (fail closed on old/new schema versions).
class MessageGuard final {
 public:
  /// Checks a message envelope.  `declared_payload_len` must not exceed
  /// `kMaxFrameBytes`; `schema_version` must equal the current window.
  bool Admit(std::uint16_t schema_version, std::uint32_t declared_payload_len,
             const std::string& sender_process_token, IpcError* error) const;
};

/// Constant-time equality for equal-length byte strings.
bool ConstantTimeEquals(const std::uint8_t* a, const std::uint8_t* b, std::size_t len);

}  // namespace crayon::cef_shell::ipc
