#include "browser/context/profile_id_validator.h"

#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>

// Minimal SHA-256 implementation for deterministic profile-id hashing.
// This avoids adding a dependency on OpenSSL or other crypto libraries.
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

void sha256_transform(std::array<std::uint32_t, 8>& state,
                      const std::uint8_t block[64]) {
  std::array<std::uint32_t, 64> w{};
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<std::uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 =
        rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 =
        rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];

  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
    const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

std::array<std::uint8_t, 32> Sha256(const std::string& data) {
  std::array<std::uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                        0xa54ff53a, 0x510e527f, 0x9b05688c,
                                        0x1f83d9ab, 0x5be0cd19};

  std::uint64_t bit_len = data.size() * 8;
  std::array<std::uint8_t, 64> buffer{};
  std::size_t buffer_len = 0;

  for (unsigned char ch : data) {
    buffer[buffer_len++] = ch;
    if (buffer_len == 64) {
      sha256_transform(state, buffer.data());
      buffer_len = 0;
    }
  }

  buffer[buffer_len++] = 0x80;
  if (buffer_len > 56) {
    while (buffer_len < 64) {
      buffer[buffer_len++] = 0;
    }
    sha256_transform(state, buffer.data());
    buffer_len = 0;
  }
  while (buffer_len < 56) {
    buffer[buffer_len++] = 0;
  }
  for (int i = 7; i >= 0; --i) {
    buffer[56 + i] = static_cast<std::uint8_t>(bit_len & 0xFF);
    bit_len >>= 8;
  }
  sha256_transform(state, buffer.data());

  std::array<std::uint8_t, 32> hash{};
  for (int i = 0; i < 8; ++i) {
    hash[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
    hash[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
    hash[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
    hash[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
  }
  return hash;
}

}  // namespace

namespace crayon::browser::cef_shell::context {

bool IsValidProfileId(const std::string& profile_id) noexcept {
  if (profile_id.size() < kMinProfileIdLength ||
      profile_id.size() > kMaxProfileIdLength) {
    return false;
  }
  for (unsigned char ch : profile_id) {
    if (!std::isalnum(static_cast<int>(ch)) && ch != '-' && ch != '_') {
      return false;
    }
  }
  return true;
}

std::string MapProfileIdToDirectoryName(const std::string& profile_id) {
  const auto hash = Sha256(profile_id);
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < 16; ++i) {
    oss << std::setw(2) << static_cast<int>(hash[i]);
  }
  return oss.str();
}

std::string BuildProfileCachePath(const std::string& base_cache_path,
                                  const std::string& profile_id) {
  if (base_cache_path.empty()) {
    return "";
  }
  std::string path = base_cache_path;
  if (path.back() != '/' && path.back() != '\\') {
    path += '/';
  }
  path += "profiles/";
  path += MapProfileIdToDirectoryName(profile_id);
  path += '/';
  return path;
}

}  // namespace crayon::browser::cef_shell::context
