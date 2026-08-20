#include "crayon/browser_history/history_store.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace crayon::browser_history {

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
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

void SetError(HistoryError* error, HistoryError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

}  // namespace

bool HistoryStore::IsValidUrl(const std::string& url) noexcept {
  if (url.size() > kMaxUrlBytes || HasControlChars(url)) {
    return false;
  }
  return StartsWith(url, "https://") || StartsWith(url, "http://");
}

bool HistoryStore::IsValidTitle(const std::string& title) noexcept {
  return title.size() <= kMaxTitleBytes && !HasControlChars(title);
}

std::uint64_t HistoryStore::RecordVisit(std::string url,
                                        std::string title,
                                        std::uint64_t visited_at,
                                        HistoryError* error) {
  if (ephemeral_) {
    SetError(error, HistoryError::kEphemeral);
    return 0;
  }
  if (!IsValidUrl(url)) {
    SetError(error, HistoryError::kInvalidUrl);
    return 0;
  }
  if (!IsValidTitle(title)) {
    SetError(error, HistoryError::kInvalidTitle);
    return 0;
  }
  HistoryEntry entry;
  entry.id = next_id_++;
  entry.url = std::move(url);
  entry.title = std::move(title);
  entry.visited_at = visited_at;
  entries_.push_back(std::move(entry));
  while (entries_.size() > kMaxHistoryEntries) {
    entries_.pop_front();
  }
  return next_id_ - 1;
}

bool HistoryStore::RecordClosedTab(std::string url,
                                   std::string title,
                                   std::uint64_t closed_at,
                                   HistoryError* error) {
  if (ephemeral_) {
    SetError(error, HistoryError::kEphemeral);
    return false;
  }
  if (!IsValidUrl(url)) {
    SetError(error, HistoryError::kInvalidUrl);
    return false;
  }
  if (!IsValidTitle(title)) {
    SetError(error, HistoryError::kInvalidTitle);
    return false;
  }
  recently_closed_.push_back(
      RecentlyClosedTab{std::move(url), std::move(title), closed_at});
  while (recently_closed_.size() > kMaxRecentlyClosed) {
    recently_closed_.erase(recently_closed_.begin());
  }
  return true;
}

std::optional<RecentlyClosedTab> HistoryStore::RestoreRecentlyClosed() {
  if (recently_closed_.empty()) {
    return std::nullopt;
  }
  RecentlyClosedTab tab = std::move(recently_closed_.back());
  recently_closed_.pop_back();
  return tab;
}

std::size_t HistoryStore::DeleteRange(std::uint64_t from,
                                      std::uint64_t to,
                                      HistoryError* error) {
  if (from > to) {
    SetError(error, HistoryError::kInvalidRange);
    return 0;
  }
  const std::size_t before = entries_.size();
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [&](const HistoryEntry& entry) {
                       return entry.visited_at >= from &&
                              entry.visited_at <= to;
                     }),
      entries_.end());
  return before - entries_.size();
}

std::size_t HistoryStore::DeleteUrl(const std::string& url) {
  const std::size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const HistoryEntry& entry) {
                                  return entry.url == url;
                                }),
                 entries_.end());
  return before - entries_.size();
}

void HistoryStore::ClearAll() noexcept {
  entries_.clear();
  recently_closed_.clear();
}

const HistoryEntry* HistoryStore::Find(std::uint64_t id) const noexcept {
  for (const auto& entry : entries_) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

std::vector<HistoryEntry> HistoryStore::Search(const std::string& query) const {
  std::vector<HistoryEntry> matches;
  if (query.empty()) {
    return matches;
  }
  for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
    if (ContainsIgnoreCase(it->title, query) ||
        ContainsIgnoreCase(it->url, query)) {
      matches.push_back(*it);
      if (matches.size() >= kMaxSearchResults) {
        break;
      }
    }
  }
  return matches;
}

}  // namespace crayon::browser_history
