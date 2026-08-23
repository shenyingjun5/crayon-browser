#include "crayon/cef_shell_ipc/ipc_channel_contract.h"

#include <algorithm>
#include <cstring>

namespace crayon::cef_shell::ipc {
namespace {

bool IsProcessTokenChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_' || c == '.' || c == '-';
}

std::uint32_t ReadHeader(const std::vector<std::uint8_t>& buffer) {
  return (static_cast<std::uint32_t>(buffer[0]) << 24) |
         (static_cast<std::uint32_t>(buffer[1]) << 16) |
         (static_cast<std::uint32_t>(buffer[2]) << 8) |
         static_cast<std::uint32_t>(buffer[3]);
}

}  // namespace

const char* ToString(IpcError error) {
  switch (error) {
    case IpcError::kFrameTooLarge:
      return "frame exceeds size limit";
    case IpcError::kFrameMalformed:
      return "frame malformed";
    case IpcError::kVersionRejected:
      return "schema version rejected";
    case IpcError::kSecretMismatch:
      return "session secret mismatch";
    case IpcError::kSecretExpired:
      return "session secret expired";
    case IpcError::kProcessTokenRejected:
      return "process token rejected";
    case IpcError::kMessageTooLarge:
      return "message exceeds size limit";
  }
  return "unknown";
}

bool ConstantTimeEquals(const std::uint8_t* a, const std::uint8_t* b, std::size_t len) {
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < len; ++i) {
    diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
}

bool FrameCodec::Feed(const std::uint8_t* data, std::size_t len, IpcError* error) {
  if (len > kMaxFeedBytes || buffer_.size() + len > kMaxFeedBytes) {
    if (error != nullptr) {
      *error = IpcError::kFrameMalformed;
    }
    return false;
  }
  buffer_.insert(buffer_.end(), data, data + len);
  return true;
}

DecodeStatus FrameCodec::Take(std::vector<std::uint8_t>* payload, std::uint32_t* declared) {
  if (buffer_.size() < kFrameHeaderBytes) {
    return DecodeStatus::kIncomplete;
  }
  const std::uint32_t length = ReadHeader(buffer_);
  if (declared != nullptr) {
    *declared = length;
  }
  if (length > kMaxFrameBytes) {
    // Drop the header; the payload is poisoned and the caller must
    // resynchronize or drop the connection.
    buffer_.erase(buffer_.begin(), buffer_.begin() + kFrameHeaderBytes);
    return DecodeStatus::kOversize;
  }
  const std::size_t end = kFrameHeaderBytes + static_cast<std::size_t>(length);
  if (buffer_.size() < end) {
    return DecodeStatus::kIncomplete;
  }
  payload->assign(buffer_.begin() + kFrameHeaderBytes, buffer_.begin() + end);
  buffer_.erase(buffer_.begin(), buffer_.begin() + end);
  return DecodeStatus::kComplete;
}

// static
std::vector<std::uint8_t> FrameCodec::Encode(const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxFrameBytes) {
    return {};
  }
  const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> frame;
  frame.reserve(kFrameHeaderBytes + payload.size());
  frame.push_back(static_cast<std::uint8_t>(length >> 24));
  frame.push_back(static_cast<std::uint8_t>(length >> 16));
  frame.push_back(static_cast<std::uint8_t>(length >> 8));
  frame.push_back(static_cast<std::uint8_t>(length));
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

bool SessionSecretVerifier::SetSecret(std::vector<std::uint8_t>* slot,
                                      const std::uint8_t* secret, std::size_t len) {
  if (secret == nullptr || len != kSecretBytes) {
    return false;
  }
  slot->assign(secret, secret + len);
  return true;
}

bool SessionSecretVerifier::Install(const std::uint8_t* secret, std::size_t len) {
  if (!SetSecret(&current_, secret, len)) {
    return false;
  }
  previous_.clear();
  generation_ = 1;
  return true;
}

bool SessionSecretVerifier::Rotate(const std::uint8_t* secret, std::size_t len) {
  if (generation_ == 0 || secret == nullptr || len != kSecretBytes) {
    return false;
  }
  previous_.assign(current_.begin(), current_.end());
  current_.assign(secret, secret + len);
  ++generation_;
  return true;
}

bool SessionSecretVerifier::Verify(const std::uint8_t* secret, std::size_t len) const {
  if (generation_ == 0 || secret == nullptr || len != kSecretBytes) {
    return false;
  }
  if (ConstantTimeEquals(secret, current_.data(), kSecretBytes)) {
    return true;
  }
  // Only the immediately previous generation is still accepted.
  return previous_.size() == kSecretBytes &&
         ConstantTimeEquals(secret, previous_.data(), kSecretBytes);
}

bool IsValidProcessToken(const std::string& token) {
  return !token.empty() && token.size() <= kMaxProcessTokenLen &&
         std::all_of(token.begin(), token.end(), IsProcessTokenChar);
}

bool MessageGuard::Admit(std::uint16_t schema_version, std::uint32_t declared_payload_len,
                         const std::string& sender_process_token, IpcError* error) const {
  if (!IsValidProcessToken(sender_process_token)) {
    if (error != nullptr) {
      *error = IpcError::kProcessTokenRejected;
    }
    return false;
  }
  if (declared_payload_len > kMaxFrameBytes) {
    if (error != nullptr) {
      *error = IpcError::kMessageTooLarge;
    }
    return false;
  }
  if (schema_version != kCurrentSchemaVersion) {
    if (error != nullptr) {
      *error = IpcError::kVersionRejected;
    }
    return false;
  }
  return true;
}

}  // namespace crayon::cef_shell::ipc
