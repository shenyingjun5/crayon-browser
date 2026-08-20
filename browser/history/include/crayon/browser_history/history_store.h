#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace crayon::browser_history {

/// Capacity bounds.
inline constexpr std::size_t kMaxHistoryEntries = 4096;
inline constexpr std::size_t kMaxRecentlyClosed = 10;
inline constexpr std::size_t kMaxSearchResults = 64;
inline constexpr std::size_t kMaxTitleBytes = 512;
inline constexpr std::size_t kMaxUrlBytes = 2048;

/// One recorded visit.
struct HistoryEntry final {
  std::uint64_t id = 0;
  std::string url;
  std::string title;
  /// Caller-injected seconds timestamp; this module never reads a clock.
  std::uint64_t visited_at = 0;
};

/// One restorable recently-closed tab snapshot.
struct RecentlyClosedTab final {
  std::string url;
  std::string title;
  std::uint64_t closed_at = 0;
};

/// Store command failure.  Stable variants carry no user data.
enum class HistoryError {
  kInvalidUrl = 0,
  kInvalidTitle,
  /// The store is ephemeral (incognito); recording/persistence is refused.
  kEphemeral,
  /// Range deletion with `from > to`.
  kInvalidRange,
};

/// Platform-neutral browsing-history store for one profile.
///
/// Ephemeral instances (incognito) reject recording and persistence so
/// nothing about a private session can ever leave the process.  Thread
/// contract: single-threaded, UI thread only.
class HistoryStore final {
 public:
  explicit HistoryStore(bool ephemeral = false) : ephemeral_(ephemeral) {}

  bool ephemeral() const noexcept { return ephemeral_; }

  /// Records a visit.  Evicts the oldest entry at capacity.  Returns the
  /// entry ID, or 0 on validation failure / ephemeral refusal.
  std::uint64_t RecordVisit(std::string url,
                            std::string title,
                            std::uint64_t visited_at,
                            HistoryError* error = nullptr);

  /// Snapshots a closed tab for restoration.  Bounded; oldest is dropped.
  /// Refused on ephemeral instances and for URLs that fail validation.
  bool RecordClosedTab(std::string url,
                       std::string title,
                       std::uint64_t closed_at,
                       HistoryError* error = nullptr);

  /// Pops the most recently closed tab snapshot.
  std::optional<RecentlyClosedTab> RestoreRecentlyClosed();

  // --- Deletion ---

  /// Deletes entries with `from <= visited_at <= to`.  Returns the count.
  std::size_t DeleteRange(std::uint64_t from,
                          std::uint64_t to,
                          HistoryError* error = nullptr);

  /// Deletes every entry with exactly this URL.  Returns the count.
  std::size_t DeleteUrl(const std::string& url);

  /// Removes all entries and the recently-closed stack.
  void ClearAll() noexcept;

  // --- Queries ---

  const std::deque<HistoryEntry>& entries() const noexcept {
    return entries_;
  }
  const HistoryEntry* Find(std::uint64_t id) const noexcept;
  std::size_t recently_closed_count() const noexcept {
    return recently_closed_.size();
  }

  /// Case-insensitive substring search over titles and URLs, newest first,
  /// bounded to `kMaxSearchResults`.
  std::vector<HistoryEntry> Search(const std::string& query) const;

 private:
  static bool IsValidUrl(const std::string& url) noexcept;
  static bool IsValidTitle(const std::string& title) noexcept;

  std::deque<HistoryEntry> entries_;  // Oldest first.
  std::vector<RecentlyClosedTab> recently_closed_;  // Most recent last.
  std::uint64_t next_id_ = 1;
  bool ephemeral_;
};

}  // namespace crayon::browser_history
