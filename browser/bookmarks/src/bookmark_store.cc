#include "crayon/browser_bookmarks/bookmark_store.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace crayon::browser_bookmarks {

namespace {

bool StartsWith(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

bool HasControlChars(std::string_view text) noexcept {
  for (const char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7F) {
      return true;
    }
  }
  return false;
}

char AsciiLower(char c) noexcept {
  return static_cast<char>(
      std::tolower(static_cast<unsigned char>(c)));
}

bool ContainsIgnoreCase(std::string_view haystack,
                        std::string_view needle) noexcept {
  if (needle.size() > haystack.size()) {
    return false;
  }
  for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (AsciiLower(haystack[i + j]) != AsciiLower(needle[j])) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

void SetError(BookmarkError* error, BookmarkError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

}  // namespace

bool BookmarkStore::IsValidTitle(const std::string& title) noexcept {
  return !title.empty() && title.size() <= kMaxTitleBytes &&
         !HasControlChars(title);
}

bool BookmarkStore::IsValidUrl(const std::string& url) noexcept {
  if (url.size() > kMaxUrlBytes || HasControlChars(url)) {
    return false;
  }
  return StartsWith(url, "https://") || StartsWith(url, "http://");
}

BookmarkStore::BookmarkStore() {
  NodeEntry root;
  root.node.id = kRootId;
  root.node.kind = BookmarkKind::kFolder;
  root.node.title = "root";
  nodes_.emplace(kRootId, std::move(root));
}

std::uint64_t BookmarkStore::InsertNode(std::uint64_t parent_id,
                                        BookmarkKind kind,
                                        std::string title,
                                        std::string url,
                                        BookmarkError* error) {
  const auto parent = nodes_.find(parent_id);
  if (parent == nodes_.end()) {
    SetError(error, BookmarkError::kUnknownId);
    return 0;
  }
  if (parent->second.node.kind != BookmarkKind::kFolder) {
    SetError(error, BookmarkError::kNotAFolder);
    return 0;
  }
  if (parent->second.children.size() >= kMaxChildrenPerFolder) {
    SetError(error, BookmarkError::kFolderFull);
    return 0;
  }
  if (nodes_.size() >= kMaxBookmarkNodes) {
    SetError(error, BookmarkError::kCapacity);
    return 0;
  }
  if (DepthOf(parent_id) + 1 > kMaxTreeDepth) {
    SetError(error, BookmarkError::kDepthExceeded);
    return 0;
  }
  const std::uint64_t id = next_id_++;
  NodeEntry entry;
  entry.node.id = id;
  entry.node.kind = kind;
  entry.node.title = std::move(title);
  entry.node.url = std::move(url);
  entry.node.parent_id = parent_id;
  nodes_.emplace(id, std::move(entry));
  nodes_.at(parent_id).children.push_back(id);
  return id;
}

std::uint64_t BookmarkStore::AddBookmark(std::uint64_t parent_id,
                                         std::string title,
                                         std::string url,
                                         BookmarkError* error) {
  if (!IsValidTitle(title)) {
    SetError(error, BookmarkError::kInvalidTitle);
    return 0;
  }
  if (!IsValidUrl(url)) {
    SetError(error, BookmarkError::kInvalidUrl);
    return 0;
  }
  return InsertNode(parent_id, BookmarkKind::kBookmark, std::move(title),
                    std::move(url), error);
}

std::uint64_t BookmarkStore::AddFolder(std::uint64_t parent_id,
                                       std::string title,
                                       BookmarkError* error) {
  if (!IsValidTitle(title)) {
    SetError(error, BookmarkError::kInvalidTitle);
    return 0;
  }
  return InsertNode(parent_id, BookmarkKind::kFolder, std::move(title),
                    std::string{}, error);
}

bool BookmarkStore::WouldCreateCycle(std::uint64_t node_id,
                                     std::uint64_t new_parent_id) const noexcept {
  std::uint64_t cursor = new_parent_id;
  while (cursor != kRootId) {
    if (cursor == node_id) {
      return true;
    }
    const auto it = nodes_.find(cursor);
    if (it == nodes_.end()) {
      return false;
    }
    cursor = it->second.node.parent_id;
  }
  return node_id == kRootId;
}

std::size_t BookmarkStore::DepthOf(std::uint64_t node_id) const noexcept {
  std::size_t depth = 0;
  std::uint64_t cursor = node_id;
  while (cursor != kRootId) {
    const auto it = nodes_.find(cursor);
    if (it == nodes_.end() || depth > kMaxTreeDepth) {
      break;
    }
    cursor = it->second.node.parent_id;
    ++depth;
  }
  return depth;
}

bool BookmarkStore::Move(std::uint64_t node_id,
                         std::uint64_t new_parent_id,
                         BookmarkError* error) {
  const auto node = nodes_.find(node_id);
  const auto parent = nodes_.find(new_parent_id);
  if (node == nodes_.end() || parent == nodes_.end()) {
    SetError(error, BookmarkError::kUnknownId);
    return false;
  }
  if (node_id == kRootId ||
      parent->second.node.kind != BookmarkKind::kFolder) {
    SetError(error, BookmarkError::kNotAFolder);
    return false;
  }
  if (WouldCreateCycle(node_id, new_parent_id)) {
    SetError(error, BookmarkError::kCycle);
    return false;
  }
  if (parent->second.children.size() >= kMaxChildrenPerFolder &&
      node->second.node.parent_id != new_parent_id) {
    SetError(error, BookmarkError::kFolderFull);
    return false;
  }
  auto& old_children = nodes_.at(node->second.node.parent_id).children;
  old_children.erase(
      std::remove(old_children.begin(), old_children.end(), node_id),
      old_children.end());
  nodes_.at(new_parent_id).children.push_back(node_id);
  nodes_.at(node_id).node.parent_id = new_parent_id;
  return true;
}

void BookmarkStore::RemoveSubtree(std::uint64_t node_id) noexcept {
  const auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return;
  }
  const std::vector<std::uint64_t> children = it->second.children;
  for (const std::uint64_t child : children) {
    RemoveSubtree(child);
  }
  nodes_.erase(node_id);
}

bool BookmarkStore::Remove(std::uint64_t node_id) {
  if (node_id == kRootId) {
    return false;
  }
  const auto it = nodes_.find(node_id);
  if (it == nodes_.end()) {
    return false;
  }
  const std::uint64_t parent_id = it->second.node.parent_id;
  auto& siblings = nodes_.at(parent_id).children;
  siblings.erase(std::remove(siblings.begin(), siblings.end(), node_id),
                 siblings.end());
  RemoveSubtree(node_id);
  return true;
}

bool BookmarkStore::Update(std::uint64_t node_id,
                           std::string title,
                           std::string url,
                           BookmarkError* error) {
  const auto it = nodes_.find(node_id);
  if (it == nodes_.end() || node_id == kRootId) {
    SetError(error, BookmarkError::kUnknownId);
    return false;
  }
  if (!IsValidTitle(title)) {
    SetError(error, BookmarkError::kInvalidTitle);
    return false;
  }
  NodeEntry& entry = nodes_.at(node_id);
  if (entry.node.kind == BookmarkKind::kBookmark) {
    if (!IsValidUrl(url)) {
      SetError(error, BookmarkError::kInvalidUrl);
      return false;
    }
    entry.node.url = std::move(url);
  }
  entry.node.title = std::move(title);
  return true;
}

const BookmarkNode* BookmarkStore::Find(std::uint64_t node_id) const noexcept {
  const auto it = nodes_.find(node_id);
  return it == nodes_.end() ? nullptr : &it->second.node;
}

std::vector<std::uint64_t> BookmarkStore::ChildrenOf(
    std::uint64_t parent_id) const {
  const auto it = nodes_.find(parent_id);
  return it == nodes_.end() ? std::vector<std::uint64_t>{}
                            : it->second.children;
}

std::vector<std::uint64_t> BookmarkStore::FindByUrl(
    const std::string& url) const {
  std::vector<std::uint64_t> matches;
  for (const auto& [id, entry] : nodes_) {
    if (entry.node.kind == BookmarkKind::kBookmark && entry.node.url == url) {
      matches.push_back(id);
    }
  }
  return matches;
}

std::vector<std::uint64_t> BookmarkStore::Search(
    const std::string& query) const {
  std::vector<std::uint64_t> matches;
  if (query.empty()) {
    return matches;
  }
  for (const auto& [id, entry] : nodes_) {
    if (id == kRootId) {
      continue;
    }
    if (ContainsIgnoreCase(entry.node.title, query) ||
        ContainsIgnoreCase(entry.node.url, query)) {
      matches.push_back(id);
      if (matches.size() >= kMaxSearchResults) {
        break;
      }
    }
  }
  return matches;
}

}  // namespace crayon::browser_bookmarks
