#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crayon::browser_history_view {

/// Maximum number of entries projected onto the history page.
inline constexpr std::size_t kMaxVisibleEntries = 256;

/// Read-only projection of one history entry.
struct HistoryProjection final {
  std::uint64_t entry_id = 0;
  std::string display_title;
  std::uint64_t visited_at = 0;
};

/// Platform-neutral view model for the history page.
///
/// Holds only bounded projections; full URLs and persistence stay in the
/// domain layer.  Thread contract: single-threaded, UI thread only.
class HistoryPageStateMachine final {
 public:
  HistoryPageStateMachine() = default;

  /// Replaces the whole projection; oversize or invalid entries (zero ID)
  /// reject the batch without side effects.
  bool SetEntries(std::vector<HistoryProjection> entries);

  /// Records the active search query (display echo only; the search itself
  /// runs in the domain layer).  Bounded to 256 bytes.
  bool SetQuery(std::string query);
  const std::string& query() const noexcept { return query_; }

  /// Drops all projections (after a domain-side clear/delete completed).
  void OnHistoryCleared() noexcept;

  const std::vector<HistoryProjection>& entries() const noexcept {
    return entries_;
  }
  bool active() const noexcept { return active_; }

  /// Clears all state and rejects every subsequent event.
  void Shutdown() noexcept;

 private:
  std::vector<HistoryProjection> entries_;
  std::string query_;
  bool active_ = true;
};

}  // namespace crayon::browser_history_view
