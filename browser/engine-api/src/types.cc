#include "crayon/browser_engine/types.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace crayon::browser_engine {
namespace {

constexpr std::size_t kMaximumPortDigits = 5;
constexpr std::uint32_t kMinimumUrlPort = 1;
constexpr std::uint32_t kMaximumUrlPort = 65535;

constexpr bool IsAsciiDigit(unsigned char character) noexcept {
  return character >= '0' && character <= '9';
}

constexpr bool IsAsciiAlphaNumeric(unsigned char character) noexcept {
  return IsAsciiDigit(character) || (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z');
}

constexpr unsigned char FoldAsciiCase(unsigned char character) noexcept {
  return character >= 'A' && character <= 'Z'
             ? static_cast<unsigned char>(character + ('a' - 'A'))
             : character;
}

bool StartsWithAsciiCaseInsensitive(std::string_view value,
                                    std::string_view prefix) noexcept {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (FoldAsciiCase(static_cast<unsigned char>(value[index])) !=
        static_cast<unsigned char>(prefix[index])) {
      return false;
    }
  }
  return true;
}

bool HasAllowedScheme(std::string_view value) noexcept {
  constexpr std::string_view kHttpScheme = "http://";
  constexpr std::string_view kHttpsScheme = "https://";
  return StartsWithAsciiCaseInsensitive(value, kHttpScheme) ||
         StartsWithAsciiCaseInsensitive(value, kHttpsScheme);
}

bool IsValidPort(std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumPortDigits) {
    return false;
  }
  std::uint32_t port = 0;
  for (const unsigned char character : value) {
    if (!IsAsciiDigit(character)) {
      return false;
    }
    port = port * 10 + static_cast<std::uint32_t>(character - '0');
  }
  return port >= kMinimumUrlPort && port <= kMaximumUrlPort;
}

bool IsValidDnsOrIpv4Host(std::string_view host) noexcept {
  constexpr std::size_t kMaxHostBytes = 253;
  constexpr std::size_t kMaxLabelBytes = 63;
  if (host.empty() || host.size() > kMaxHostBytes || host.front() == '.' ||
      host.back() == '.') {
    return false;
  }

  std::size_t label_start = 0;
  while (label_start < host.size()) {
    const auto label_end = host.find('.', label_start);
    const auto label =
        host.substr(label_start, label_end == std::string_view::npos
                                     ? host.size() - label_start
                                     : label_end - label_start);
    if (label.empty() || label.size() > kMaxLabelBytes ||
        label.front() == '-' || label.back() == '-') {
      return false;
    }
    if (!std::all_of(label.begin(), label.end(), [](unsigned char character) {
          return IsAsciiAlphaNumeric(character) || character == '-';
        })) {
      return false;
    }
    if (label_end == std::string_view::npos) {
      return true;
    }
    label_start = label_end + 1;
  }
  return true;
}

bool HasValidAuthority(std::string_view value) noexcept {
  const auto scheme_end = value.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  const auto authority_start = scheme_end + 3;
  const auto authority_end = value.find_first_of("/?#", authority_start);
  auto authority =
      value.substr(authority_start, authority_end == std::string_view::npos
                                        ? value.size() - authority_start
                                        : authority_end - authority_start);
  if (authority.empty() || authority.find('@') != std::string_view::npos ||
      authority.front() == '[') {
    return false;
  }

  const auto port_separator = authority.find(':');
  if (port_separator != std::string_view::npos) {
    if (authority.find(':', port_separator + 1) != std::string_view::npos ||
        !IsValidPort(authority.substr(port_separator + 1))) {
      return false;
    }
    authority = authority.substr(0, port_separator);
  }
  return IsValidDnsOrIpv4Host(authority);
}

template <typename Enum>
constexpr int Raw(Enum value) noexcept {
  return static_cast<int>(value);
}

}  // namespace

bool IsValidOpaqueId(std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaxOpaqueIdBytes) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return IsAsciiAlphaNumeric(character) || character == '-' ||
           character == '_' || character == '.' || character == ':';
  });
}

std::optional<BrowserUrl> BrowserUrl::TryParse(std::string value) {
  if (value.empty() || value.size() > kMaxBrowserUrlBytes ||
      !HasAllowedScheme(value) || !HasValidAuthority(value)) {
    return std::nullopt;
  }
  const bool has_forbidden_character =
      std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7f;
      });
  if (has_forbidden_character) {
    return std::nullopt;
  }
  return BrowserUrl(std::move(value));
}

std::optional<ZoomFactor> ZoomFactor::TryCreate(double value) noexcept {
  if (!std::isfinite(value) || value < kMinimumZoomFactor ||
      value > kMaximumZoomFactor) {
    return std::nullopt;
  }
  return ZoomFactor(value);
}

bool IsValid(ProfileMode value) noexcept {
  return Raw(value) >= Raw(ProfileMode::kPersistent) &&
         Raw(value) <= Raw(ProfileMode::kPrivate);
}

bool IsValid(PermissionKind value) noexcept {
  return Raw(value) >= Raw(PermissionKind::kCamera) &&
         Raw(value) <= Raw(PermissionKind::kDownload);
}

bool IsValid(PermissionDecision value) noexcept {
  return Raw(value) >= Raw(PermissionDecision::kAllowOnce) &&
         Raw(value) <= Raw(PermissionDecision::kDeny);
}

bool IsValid(TrustedInputKind value) noexcept {
  return Raw(value) >= Raw(TrustedInputKind::kKeyboard) &&
         Raw(value) <= Raw(TrustedInputKind::kTouch);
}

bool IsValid(ObservationTopic value) noexcept {
  return Raw(value) >= Raw(ObservationTopic::kNavigation) &&
         Raw(value) <= Raw(ObservationTopic::kTrustedInput);
}

bool IsValid(ObservationKind value) noexcept {
  return Raw(value) >= Raw(ObservationKind::kDocumentReady) &&
         Raw(value) <= Raw(ObservationKind::kTrustedInput);
}

const char* ToStableCode(EngineErrorCode code) noexcept {
  switch (code) {
    case EngineErrorCode::kNone:
      return "none";
    case EngineErrorCode::kInvalidArgument:
      return "invalid_argument";
    case EngineErrorCode::kInvalidState:
      return "invalid_state";
    case EngineErrorCode::kAlreadyExists:
      return "already_exists";
    case EngineErrorCode::kNotFound:
      return "not_found";
    case EngineErrorCode::kStaleNavigation:
      return "stale_navigation";
    case EngineErrorCode::kUnsupported:
      return "unsupported";
    case EngineErrorCode::kCapacityExceeded:
      return "capacity_exceeded";
    case EngineErrorCode::kNavigationFailed:
      return "navigation_failed";
  }
  return "invalid_error_code";
}

}  // namespace crayon::browser_engine
