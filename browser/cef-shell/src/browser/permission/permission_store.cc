#include "browser/permission/permission_store.h"

#include <utility>

namespace crayon::browser::cef_shell::permission {

namespace {

// Combines the standard string hash with the kind index for unordered_map
// lookup.
std::size_t CombineHash(std::size_t h1, std::size_t h2) {
  // A simple but effective hash combine (similar to boost::hash_combine).
  return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
}

}  // namespace

std::size_t PermissionStore::KeyHash::operator()(
    const std::pair<std::string, PermissionKind>& key) const noexcept {
  std::hash<std::string> string_hash;
  std::hash<std::size_t> size_hash;
  return CombineHash(string_hash(key.first),
                     size_hash(static_cast<std::size_t>(key.second)));
}

PermissionStore::PermissionStore() = default;

PermissionDecision PermissionStore::Query(const std::string& origin,
                                          PermissionKind kind) const {
  std::shared_lock lock(mutex_);
  const auto found = decisions_.find({origin, kind});
  if (found == decisions_.end()) {
    return PermissionDecision::kDeny;
  }
  return found->second;
}

void PermissionStore::Record(const std::string& origin, PermissionKind kind,
                             PermissionDecision decision) {
  std::unique_lock lock(mutex_);
  decisions_[{origin, kind}] = decision;
}

void PermissionStore::ClearSessionDecisions() {
  std::unique_lock lock(mutex_);
  for (auto it = decisions_.begin(); it != decisions_.end();) {
    if (it->second == PermissionDecision::kAllowSession) {
      it = decisions_.erase(it);
    } else {
      ++it;
    }
  }
}

void PermissionStore::ClearAll() {
  std::unique_lock lock(mutex_);
  decisions_.clear();
}

void PermissionStore::ClearForOrigin(const std::string& origin) {
  std::unique_lock lock(mutex_);
  for (auto it = decisions_.begin(); it != decisions_.end();) {
    if (it->first.first == origin) {
      it = decisions_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<std::tuple<std::string, PermissionKind, PermissionDecision>>
PermissionStore::Snapshot() const {
  std::shared_lock lock(mutex_);
  std::vector<std::tuple<std::string, PermissionKind, PermissionDecision>>
      result;
  result.reserve(decisions_.size());
  for (const auto& entry : decisions_) {
    result.emplace_back(entry.first.first, entry.first.second, entry.second);
  }
  return result;
}

}  // namespace crayon::browser::cef_shell::permission
