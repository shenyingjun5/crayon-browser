#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crayon::browser_bookmarks {

/// Capacity bounds for the bookmark tree.
inline constexpr std::size_t kMaxBookmarkNodes = 4096;
inline constexpr std::size_t kMaxTreeDepth = 32;
inline constexpr std::size_t kMaxChildrenPerFolder = 256;
inline constexpr std::size_t kMaxTitleBytes = 512;
inline constexpr std::size_t kMaxUrlBytes = 2048;
inline constexpr std::size_t kMaxSearchResults = 64;

/// Closed node kinds.
enum class BookmarkKind {
  kFolder = 0,
  kBookmark,
};

constexpr bool IsValid(BookmarkKind kind) noexcept {
  switch (kind) {
    case BookmarkKind::kFolder:
    case BookmarkKind::kBookmark:
      return true;
  }
  return false;
}

/// Store command failure.  Stable variants carry no user data.
enum class BookmarkError {
  kUnknownId = 0,
  kInvalidTitle,
  kInvalidUrl,
  kNotAFolder,
  kCapacity,
  kDepthExceeded,
  kFolderFull,
  /// Moving a folder into itself or its own descendant.
  kCycle,
};

/// Read-only view of one node.
struct BookmarkNode final {
  std::uint64_t id = 0;
  BookmarkKind kind = BookmarkKind::kBookmark;
  std::string title;
  /// Empty for folders.
  std::string url;
  std::uint64_t parent_id = 0;
};

/// Platform-neutral bookmark tree for one profile.
///
/// Each instance is bound to a single profile; cross-profile sharing is
/// impossible by construction.  IDs are assigned monotonically and never
/// reused.  Thread contract: single-threaded, UI thread only.
class BookmarkStore final {
 public:
  /// Well-known ID of the implicit root folder.
  static constexpr std::uint64_t kRootId = 0;

  BookmarkStore();

  // --- Creation ---

  /// Adds a bookmark under `parent_id`.  Title and URL are validated;
  /// invalid input fails closed without side effects.
  std::uint64_t AddBookmark(std::uint64_t parent_id,
                            std::string title,
                            std::string url,
                            BookmarkError* error = nullptr);

  /// Adds a folder under `parent_id`.
  std::uint64_t AddFolder(std::uint64_t parent_id,
                          std::string title,
                          BookmarkError* error = nullptr);

  // --- Mutation ---

  /// Moves a node under a new parent.  Rejects cycles (a folder cannot move
  /// into itself or its descendants) and folder capacity overflow.
  bool Move(std::uint64_t node_id,
            std::uint64_t new_parent_id,
            BookmarkError* error = nullptr);

  /// Removes a node; folders cascade-remove all descendants.  Removing the
  /// root is rejected.
  bool Remove(std::uint64_t node_id);

  /// Updates title (any node) and URL (bookmarks only).
  bool Update(std::uint64_t node_id,
              std::string title,
              std::string url,
              BookmarkError* error = nullptr);

  // --- Queries ---

  const BookmarkNode* Find(std::uint64_t node_id) const noexcept;
  std::vector<std::uint64_t> ChildrenOf(std::uint64_t parent_id) const;
  std::size_t node_count() const noexcept { return nodes_.size(); }

  /// Returns IDs of bookmarks with exactly this URL (duplicates allowed).
  std::vector<std::uint64_t> FindByUrl(const std::string& url) const;

  /// Case-insensitive substring search over titles and URLs, bounded to
  /// `kMaxSearchResults`.
  std::vector<std::uint64_t> Search(const std::string& query) const;

 private:
  struct NodeEntry final {
    BookmarkNode node;
    std::vector<std::uint64_t> children;
  };

  static bool IsValidTitle(const std::string& title) noexcept;
  static bool IsValidUrl(const std::string& url) noexcept;
  bool WouldCreateCycle(std::uint64_t node_id,
                        std::uint64_t new_parent_id) const noexcept;
  std::size_t DepthOf(std::uint64_t node_id) const noexcept;
  void RemoveSubtree(std::uint64_t node_id) noexcept;
  std::uint64_t InsertNode(std::uint64_t parent_id,
                           BookmarkKind kind,
                           std::string title,
                           std::string url,
                           BookmarkError* error);

  // Live nodes only; removed IDs are never reused and leave no tombstone.
  std::unordered_map<std::uint64_t, NodeEntry> nodes_;
  std::uint64_t next_id_ = 1;  // Root occupies ID 0.
};

}  // namespace crayon::browser_bookmarks
