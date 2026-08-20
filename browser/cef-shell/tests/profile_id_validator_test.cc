#include <cstdlib>
#include <iostream>
#include <string>

#include "browser/context/profile_id_validator.h"

namespace {

using crayon::browser::cef_shell::context::BuildProfileCachePath;
using crayon::browser::cef_shell::context::IsValidProfileId;
using crayon::browser::cef_shell::context::MapProfileIdToDirectoryName;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool EmptyProfileIdRejected() {
  CHECK(!IsValidProfileId(""));
  return true;
}

bool TooLongProfileIdRejected() {
  CHECK(!IsValidProfileId(std::string(65, 'a')));
  return true;
}

bool ProfileIdWithPathSeparatorRejected() {
  CHECK(!IsValidProfileId("foo/bar"));
  CHECK(!IsValidProfileId("foo\\bar"));
  return true;
}

bool ProfileIdWithDotRejected() {
  CHECK(!IsValidProfileId("foo.bar"));
  return true;
}

bool ProfileIdWithSpaceRejected() {
  CHECK(!IsValidProfileId("foo bar"));
  return true;
}

bool ProfileIdWithUnicodeRejected() {
  CHECK(!IsValidProfileId("\xe4\xb8\xad"));  // UTF-8 "中"
  return true;
}

bool ValidProfileIdAccepted() {
  CHECK(IsValidProfileId("default"));
  CHECK(IsValidProfileId("user-1"));
  CHECK(IsValidProfileId("user_2"));
  CHECK(IsValidProfileId("A1-b2_c3"));
  CHECK(IsValidProfileId(std::string(64, 'a')));
  return true;
}

bool MappingIsDeterministic() {
  const auto a1 = MapProfileIdToDirectoryName("default");
  const auto a2 = MapProfileIdToDirectoryName("default");
  CHECK(a1 == a2);
  CHECK(a1.size() == 32);  // 16 bytes as hex
  return true;
}

bool DifferentIdsMapDifferently() {
  const auto a = MapProfileIdToDirectoryName("default");
  const auto b = MapProfileIdToDirectoryName("profile-a");
  CHECK(a != b);
  return true;
}

bool ProfileIdNotInHashOutput() {
  const auto hash = MapProfileIdToDirectoryName("default");
  CHECK(hash.find("default") == std::string::npos);
  return true;
}

bool BuildProfileCachePathStructure() {
  const auto path = BuildProfileCachePath("/tmp/crayon", "default");
  CHECK(path.find("/tmp/crayon/profiles/") == 0);
  CHECK(path.back() == '/');
  CHECK(path.find("default") == std::string::npos);  // ID not literal
  return true;
}

bool BuildProfileCachePathHandlesTrailingSlash() {
  const auto a = BuildProfileCachePath("/tmp/crayon", "p");
  const auto b = BuildProfileCachePath("/tmp/crayon/", "p");
  CHECK(a == b);
  return true;
}

bool EmptyBasePathReturnsEmpty() {
  CHECK(BuildProfileCachePath("", "default").empty());
  return true;
}

}  // namespace

int main() {
  if (!EmptyProfileIdRejected() ||
      !TooLongProfileIdRejected() ||
      !ProfileIdWithPathSeparatorRejected() ||
      !ProfileIdWithDotRejected() ||
      !ProfileIdWithSpaceRejected() ||
      !ProfileIdWithUnicodeRejected() ||
      !ValidProfileIdAccepted() ||
      !MappingIsDeterministic() ||
      !DifferentIdsMapDifferently() ||
      !ProfileIdNotInHashOutput() ||
      !BuildProfileCachePathStructure() ||
      !BuildProfileCachePathHandlesTrailingSlash() ||
      !EmptyBasePathReturnsEmpty()) {
    return 1;
  }
  return 0;
}
