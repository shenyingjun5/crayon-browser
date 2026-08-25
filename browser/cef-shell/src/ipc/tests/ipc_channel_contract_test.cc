// CEF-06 contract tests: framing matrix, secret rotation/verify,
// message guard (version window, size, process token).
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "crayon/cef_shell_ipc/ipc_channel_contract.h"

namespace {

using crayon::cef_shell::ipc::ConstantTimeEquals;
using crayon::cef_shell::ipc::DecodeStatus;
using crayon::cef_shell::ipc::FrameCodec;
using crayon::cef_shell::ipc::IpcError;
using crayon::cef_shell::ipc::IsValidProcessToken;
using crayon::cef_shell::ipc::kCurrentSchemaVersion;
using crayon::cef_shell::ipc::kMaxFeedBytes;
using crayon::cef_shell::ipc::kMaxFrameBytes;
using crayon::cef_shell::ipc::kSecretBytes;
using crayon::cef_shell::ipc::MessageGuard;
using crayon::cef_shell::ipc::SessionSecretVerifier;
using crayon::cef_shell::ipc::ToString;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

std::vector<std::uint8_t> Bytes(const char* text) {
  return std::vector<std::uint8_t>(text, text + std::string(text).size());
}

std::vector<std::uint8_t> Secret(std::uint8_t fill) {
  return std::vector<std::uint8_t>(kSecretBytes, fill);
}

bool FrameCodecRoundTrip() {
  FrameCodec codec;
  std::vector<std::uint8_t> frame = FrameCodec::Encode(Bytes("hello"));
  CHECK(frame.size() == 4 + 5);
  IpcError error;
  CHECK(codec.Feed(frame.data(), frame.size(), &error));
  std::vector<std::uint8_t> payload;
  std::uint32_t declared = 0;
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kComplete);
  CHECK(payload == Bytes("hello") && declared == 5);
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kIncomplete);
  return true;
}

bool FrameCodecPartialAndBackToBack() {
  FrameCodec codec;
  std::vector<std::uint8_t> a = FrameCodec::Encode(Bytes("a"));
  std::vector<std::uint8_t> b = FrameCodec::Encode(Bytes("bbb"));
  std::vector<std::uint8_t> stream = a;
  stream.insert(stream.end(), b.begin(), b.end());
  // Feed in hostile small chunks.
  for (std::uint8_t byte : stream) {
    CHECK(codec.Feed(&byte, 1, nullptr));
  }
  std::vector<std::uint8_t> payload;
  std::uint32_t declared = 0;
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kComplete && payload == Bytes("a"));
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kComplete && payload == Bytes("bbb"));
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kIncomplete);
  // Max legal size decodes.
  std::vector<std::uint8_t> big(kMaxFrameBytes, 7);
  FrameCodec big_codec;
  std::vector<std::uint8_t> big_frame = FrameCodec::Encode(big);
  CHECK(big_codec.Feed(big_frame.data(), big_frame.size(), nullptr));
  CHECK(big_codec.Take(&payload, &declared) == DecodeStatus::kComplete && payload.size() == kMaxFrameBytes);
  return true;
}

bool FrameCodecOversizeAndHostileFeed() {
  FrameCodec codec;
  std::vector<std::uint8_t> payload;
  std::uint32_t declared = 0;
  // Declared oversize header (kMaxFrameBytes + 1).
  const std::uint32_t oversize = static_cast<std::uint32_t>(kMaxFrameBytes) + 1;
  std::vector<std::uint8_t> header = {
      static_cast<std::uint8_t>(oversize >> 24), static_cast<std::uint8_t>(oversize >> 16),
      static_cast<std::uint8_t>(oversize >> 8), static_cast<std::uint8_t>(oversize)};
  header.push_back(0);
  CHECK(codec.Feed(header.data(), header.size(), nullptr));
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kOversize && declared == oversize);
  // Hostile single chunk beyond the feed bound fails closed, buffers
  // nothing.
  FrameCodec hostile;
  std::vector<std::uint8_t> huge(kMaxFeedBytes + 1, 1);
  IpcError error;
  CHECK(!hostile.Feed(huge.data(), huge.size(), &error));
  CHECK(error == IpcError::kFrameMalformed);
  CHECK(hostile.pending_bytes() == 0);
  // Encode refuses oversize payloads.
  CHECK(FrameCodec::Encode(std::vector<std::uint8_t>(kMaxFrameBytes + 1, 0)).empty());
  return true;
}

bool ResetClearsHostileLeftovers() {
  // CEF-06 P2 fix: a connection reset must drop every buffered byte so
  // leftovers from a hostile stream never bleed into the next
  // connection.
  FrameCodec codec;
  std::vector<std::uint8_t> junk(64, 0xFF);
  CHECK(codec.Feed(junk.data(), junk.size(), nullptr));
  CHECK(codec.pending_bytes() == 64);
  codec.Reset();
  CHECK(codec.pending_bytes() == 0);
  // A clean frame decodes immediately after the reset.
  const std::vector<std::uint8_t> frame = FrameCodec::Encode(Bytes("clean"));
  CHECK(codec.Feed(frame.data(), frame.size(), nullptr));
  std::vector<std::uint8_t> payload;
  std::uint32_t declared = 0;
  CHECK(codec.Take(&payload, &declared) == DecodeStatus::kComplete);
  CHECK(payload == Bytes("clean"));
  return true;
}

bool SecretVerifyAndRotation() {
  SessionSecretVerifier verifier;
  std::vector<std::uint8_t> s1 = Secret(0x11);
  std::vector<std::uint8_t> s2 = Secret(0x22);
  std::vector<std::uint8_t> s3 = Secret(0x33);
  CHECK(!verifier.Verify(s1.data(), s1.size()));  // nothing installed
  CHECK(verifier.Install(s1.data(), s1.size()));
  CHECK(verifier.generation() == 1);
  CHECK(verifier.Verify(s1.data(), s1.size()));
  std::vector<std::uint8_t> wrong = Secret(0x99);
  CHECK(!verifier.Verify(wrong.data(), wrong.size()));
  CHECK(!verifier.Verify(wrong.data(), 8));  // wrong size fails closed
  // Rotation: previous generation stays accepted until the next one.
  CHECK(verifier.Rotate(s2.data(), s2.size()));
  CHECK(verifier.generation() == 2);
  CHECK(verifier.Verify(s2.data(), s2.size()));
  CHECK(verifier.Verify(s1.data(), s1.size()));  // previous window
  CHECK(verifier.Rotate(s3.data(), s3.size()));
  CHECK(verifier.Verify(s3.data(), s3.size()));
  CHECK(verifier.Verify(s2.data(), s2.size()));  // previous window moved
  CHECK(!verifier.Verify(s1.data(), s1.size()));  // expired
  // Bad installs rejected.
  SessionSecretVerifier bad;
  CHECK(!bad.Install(s1.data(), 8));
  CHECK(!bad.Install(nullptr, kSecretBytes));
  CHECK(!bad.Rotate(s2.data(), s2.size()));  // nothing installed yet
  return true;
}

bool ConstantTimeEqualsSanity() {
  const std::uint8_t a[4] = {1, 2, 3, 4};
  const std::uint8_t b[4] = {1, 2, 3, 4};
  const std::uint8_t c[4] = {1, 2, 3, 5};
  CHECK(ConstantTimeEquals(a, b, 4));
  CHECK(!ConstantTimeEquals(a, c, 4));
  return true;
}

bool ProcessTokenMatrix() {
  CHECK(IsValidProcessToken("cef-renderer.01"));
  CHECK(IsValidProcessToken("core"));
  CHECK(!IsValidProcessToken(""));
  CHECK(!IsValidProcessToken("bad token"));
  CHECK(!IsValidProcessToken("bad/token"));
  CHECK(!IsValidProcessToken(std::string(65, 'a')));
  return true;
}

bool MessageGuardMatrix() {
  MessageGuard guard;
  IpcError error;
  CHECK(guard.Admit(kCurrentSchemaVersion, 100, "core", &error));
  // Old and future schema versions are rejected before payload checks.
  CHECK(!guard.Admit(0, 100, "core", &error));
  CHECK(error == IpcError::kVersionRejected);
  CHECK(!guard.Admit(kCurrentSchemaVersion + 1, 100, "core", &error));
  CHECK(error == IpcError::kVersionRejected);
  // Oversize declared payload.
  CHECK(!guard.Admit(kCurrentSchemaVersion, kMaxFrameBytes + 1, "core", &error));
  CHECK(error == IpcError::kMessageTooLarge);
  // Bad process token.
  CHECK(!guard.Admit(kCurrentSchemaVersion, 100, "../evil", &error));
  CHECK(error == IpcError::kProcessTokenRejected);
  // Error strings are closed and data-free.
  CHECK(std::string(ToString(IpcError::kVersionRejected)) == "schema version rejected");
  CHECK(std::string(ToString(IpcError::kFrameTooLarge)) == "frame exceeds size limit");
  return true;
}

/// Deterministic pseudo-random hostile stream: no panics, bounded
/// pending buffer, decode outcomes closed.
bool HostileStreamInvariants() {
  std::uint64_t state = 0x853C'49E6'748F'E9A3;
  auto next = [&state]() {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
  };
  FrameCodec codec;
  for (int step = 0; step < 3'000; ++step) {
    const std::uint64_t choice = next() % 10;
    if (choice < 5) {
      std::vector<std::uint8_t> payload(static_cast<std::size_t>(next() % 64), 0xAB);
      const std::vector<std::uint8_t> frame = FrameCodec::Encode(payload);
      CHECK(codec.Feed(frame.data(), frame.size(), nullptr));
      std::vector<std::uint8_t> decoded;
      std::uint32_t declared = 0;
      static_cast<void>(codec.Take(&decoded, &declared));
      // Buffered junk from earlier iterations may legitimately surface
      // as kOversize or kIncomplete here; exact decode semantics are
      // covered by the deterministic tests above.
    } else if (choice < 7) {
      const std::uint32_t oversize =
          static_cast<std::uint32_t>(kMaxFrameBytes + 1 + (next() % 7));
      std::vector<std::uint8_t> header = {
          static_cast<std::uint8_t>(oversize >> 24), static_cast<std::uint8_t>(oversize >> 16),
          static_cast<std::uint8_t>(oversize >> 8), static_cast<std::uint8_t>(oversize)};
      CHECK(codec.Feed(header.data(), header.size(), nullptr));
      std::vector<std::uint8_t> decoded;
      std::uint32_t declared = 0;
      // Earlier iterations may have left buffered bytes that decode
      // first; the invariant is bounded memory and no crash, exact
      // semantics live in the deterministic tests.
      static_cast<void>(codec.Take(&decoded, &declared));
    } else {
      std::vector<std::uint8_t> junk(static_cast<std::size_t>(next() % 32), 0xFF);
      CHECK(codec.Feed(junk.data(), junk.size(), nullptr));
    }
    CHECK(codec.pending_bytes() <= kMaxFeedBytes);
  }
  return true;
}

}  // namespace

int main() {
  const bool ok = FrameCodecRoundTrip() && FrameCodecPartialAndBackToBack() &&
                  FrameCodecOversizeAndHostileFeed() && ResetClearsHostileLeftovers() &&
                  SecretVerifyAndRotation() &&
                  ConstantTimeEqualsSanity() && ProcessTokenMatrix() && MessageGuardMatrix() &&
                  HostileStreamInvariants();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "ipc_channel_contract_test passed\n";
  return EXIT_SUCCESS;
}
