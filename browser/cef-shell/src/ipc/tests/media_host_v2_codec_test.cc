#include "crayon/cef_shell_ipc/media_host_v2_codec.h"

#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace v2 = crayon::cef_shell::ipc::media_host_v2;
namespace v1 = crayon::cef_shell::ipc::media_host;
namespace {
#define CHECK_V2(expr)                                                         \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << __LINE__ << ": " << #expr << '\n';                          \
      return false;                                                            \
    }                                                                          \
  } while (false)
v2::Handshake Hello() { return {v2::Kind::kHello, 7, 9, 15, 16384, 16}; }
v2::Handshake Welcome() { return {v2::Kind::kWelcome, 7, 9, 1, 8192, 8}; }
bool Golden() {
  std::ifstream input(std::string(CRAYON_SOURCE_ROOT) +
                      "/tests/contracts/media_host_v2_handshake.golden");
  CHECK_V2(input.good());
  std::string name, hex;
  unsigned count = 0;
  while (input >> name >> hex) {
    CHECK_V2(name == "hello" || name == "welcome" || name == "hello-boundary");
    CHECK_V2(hex.size() == v2::kHandshakeBytes * 2);
    std::vector<std::uint8_t> wire;
    for (std::size_t i = 0; i < hex.size(); i += 2)
      wire.push_back(
          static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    auto expected = name == "welcome" ? Welcome() : Hello();
    if (name == "hello-boundary") {
      expected.session_id = std::numeric_limits<std::uint64_t>::max();
      expected.generation = 0x0102030405060708ULL;
      expected.capabilities = 0;
      expected.max_frame_bytes = 34;
      expected.max_page_items = 1;
    }
    auto encoded = v2::Encode(expected);
    CHECK_V2(encoded && *encoded == wire);
    auto decoded = v2::Decode(wire);
    CHECK_V2(decoded && *decoded == expected);
    CHECK_V2(!v1::Decode(wire, nullptr));
    ++count;
  }
  CHECK_V2(count == 3);
  auto previous = v1::Encode(v1::Message(v1::Shutdown{}), nullptr);
  CHECK_V2(previous && !v2::Decode(*previous));
  return true;
}
bool RejectFrames() {
  auto wire = v2::Encode(Hello()).value();
  for (std::size_t n = 0; n < wire.size(); ++n)
    CHECK_V2(!v2::Decode({wire.begin(), wire.begin() + n}));
  auto extra = wire;
  extra.push_back(0);
  CHECK_V2(!v2::Decode(extra));
  CHECK_V2(!v2::Decode(std::vector<std::uint8_t>(v2::kMaxFrameBytes + 1)));
  for (auto [index, byte] :
       std::vector<std::pair<std::size_t, std::uint8_t>>{{0, 'X'},
                                                         {5, 1},
                                                         {6, 0},
                                                         {6, 3},
                                                         {7, 1},
                                                         {27, 16},
                                                         {31, 1},
                                                         {33, 17}}) {
    auto bad = wire;
    bad[index] = byte;
    CHECK_V2(!v2::Decode(bad));
  }
  for (auto [begin, end] : std::vector<std::pair<std::size_t, std::size_t>>{
           {8, 16}, {16, 24}, {28, 32}, {32, 34}}) {
    auto bad = wire;
    for (auto i = begin; i < end; ++i)
      bad[i] = 0;
    CHECK_V2(!v2::Decode(bad));
  }
  return true;
}
bool LocalValidationAndNegotiation() {
  CHECK_V2(v2::MatchesHello(Hello(), Welcome()));
  CHECK_V2(!v2::MatchesHello(Welcome(), Hello()));
  auto bad = Hello();
  bad.kind = static_cast<v2::Kind>(3);
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.session_id = 0;
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.generation = 0;
  CHECK_V2(!v2::Encode(bad));
  bad = Hello();
  bad.capabilities = 16;
  CHECK_V2(!v2::Encode(bad));
  for (auto frame : {0u, 33u, 16385u}) {
    bad = Hello();
    bad.max_frame_bytes = frame;
    CHECK_V2(!v2::Encode(bad));
    CHECK_V2(!v2::MatchesHello(bad, Welcome()));
  }
  for (std::uint16_t page : {0, 17}) {
    bad = Hello();
    bad.max_page_items = page;
    CHECK_V2(!v2::Encode(bad));
  }
  bad = Welcome();
  bad.session_id = 8;
  CHECK_V2(!v2::MatchesHello(Hello(), bad));
  bad = Welcome();
  bad.generation = 10;
  CHECK_V2(!v2::MatchesHello(Hello(), bad));
  auto restricted = Hello();
  restricted.capabilities = 1;
  restricted.max_frame_bytes = 1024;
  restricted.max_page_items = 1;
  CHECK_V2(!v2::MatchesHello(restricted, Welcome()));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.max_frame_bytes = 1025;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.capabilities = 2;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  bad = restricted;
  bad.kind = v2::Kind::kWelcome;
  bad.max_page_items = 2;
  CHECK_V2(!v2::MatchesHello(restricted, bad));
  auto empty = Hello();
  empty.capabilities = 0;
  empty.max_frame_bytes = 34;
  empty.max_page_items = 1;
  auto wire = v2::Encode(empty);
  CHECK_V2(wire && v2::Decode(*wire).value() == empty);
  bad = empty;
  bad.kind = v2::Kind::kWelcome;
  CHECK_V2(v2::MatchesHello(empty, bad));
  CHECK_V2(!v2::MatchesHello(empty, Welcome()));
  return true;
}
} // namespace
int main() {
  if (!Golden() || !RejectFrames() || !LocalValidationAndNegotiation())
    return 1;
  std::cout << "media_host_v2_codec: 3 cases PASS\n";
  return 0;
}
