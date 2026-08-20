#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "crayon/browser_bookmarks/bookmark_codec.h"
#include "crayon/browser_bookmarks/bookmark_store.h"

namespace {

using crayon::browser_bookmarks::BookmarkCodecError;
using crayon::browser_bookmarks::BookmarkError;
using crayon::browser_bookmarks::BookmarkKind;
using crayon::browser_bookmarks::BookmarkStore;
using crayon::browser_bookmarks::DeserializeBookmarks;
using crayon::browser_bookmarks::LoadBookmarksFromFile;
using crayon::browser_bookmarks::SaveBookmarksToFile;
using crayon::browser_bookmarks::SerializeBookmarks;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- Store basics ----------

bool AddAndFindBookmark() {
  BookmarkStore store;
  const auto id = store.AddBookmark(BookmarkStore::kRootId, "Example",
                                    "https://example.test/");
  CHECK(id != 0);
  const auto* node = store.Find(id);
  CHECK(node != nullptr);
  CHECK(node->kind == BookmarkKind::kBookmark);
  CHECK(node->title == "Example");
  CHECK(node->parent_id == BookmarkStore::kRootId);
  CHECK(store.ChildrenOf(BookmarkStore::kRootId).size() == 1);
  return true;
}

bool ValidationMatrix() {
  BookmarkStore store;
  BookmarkError error = BookmarkError::kUnknownId;
  CHECK(store.AddBookmark(0, "", "https://a.test/", &error) == 0);
  CHECK(error == BookmarkError::kInvalidTitle);
  CHECK(store.AddBookmark(0, "x", "ftp://a.test/", &error) == 0);
  CHECK(error == BookmarkError::kInvalidUrl);
  CHECK(store.AddBookmark(0, "x", "javascript:alert(1)", &error) == 0);
  CHECK(error == BookmarkError::kInvalidUrl);
  CHECK(store.AddBookmark(0, std::string(513, 't'), "https://a.test/",
                          &error) == 0);
  CHECK(error == BookmarkError::kInvalidTitle);
  CHECK(store.AddBookmark(0, "x", std::string(2050, 'a'), &error) == 0);
  CHECK(error == BookmarkError::kInvalidUrl);
  CHECK(store.AddBookmark(0, "x", "https://a.test/\x01", &error) == 0);
  CHECK(error == BookmarkError::kInvalidUrl);
  CHECK(store.AddBookmark(999, "x", "https://a.test/", &error) == 0);
  CHECK(error == BookmarkError::kUnknownId);
  return true;
}

bool FoldersMoveAndCycles() {
  BookmarkStore store;
  const auto folder_a = store.AddFolder(0, "A");
  const auto folder_b = store.AddFolder(0, "B");
  const auto inner = store.AddFolder(folder_a, "inner");
  const auto leaf = store.AddBookmark(inner, "leaf", "https://leaf.test/");

  BookmarkError error = BookmarkError::kUnknownId;
  CHECK(store.Move(folder_b, folder_a, &error));  // B under A
  // A cannot move under its own descendant.
  CHECK(!store.Move(folder_a, inner, &error));
  CHECK(error == BookmarkError::kCycle);
  CHECK(!store.Move(folder_a, folder_a, &error));
  CHECK(error == BookmarkError::kCycle);
  // Bookmark cannot be a parent.
  CHECK(!store.Move(folder_b, leaf, &error));
  CHECK(error == BookmarkError::kNotAFolder);
  // Root cannot move.
  CHECK(!store.Move(BookmarkStore::kRootId, folder_b, &error));
  return true;
}

bool RemoveCascades() {
  BookmarkStore store;
  const auto folder = store.AddFolder(0, "F");
  const auto child_folder = store.AddFolder(folder, "C");
  const auto leaf = store.AddBookmark(child_folder, "L", "https://l.test/");
  const std::size_t before = store.node_count();
  CHECK(store.Remove(folder));
  CHECK(store.Find(folder) == nullptr);
  CHECK(store.Find(child_folder) == nullptr);
  CHECK(store.Find(leaf) == nullptr);
  CHECK(store.node_count() == before - 3);
  CHECK(!store.Remove(folder));          // repeat
  CHECK(!store.Remove(BookmarkStore::kRootId));
  return true;
}

bool IdsAreNeverReused() {
  BookmarkStore store;
  const auto first = store.AddBookmark(0, "one", "https://one.test/");
  store.Remove(first);
  const auto second = store.AddBookmark(0, "two", "https://two.test/");
  CHECK(second != first);
  CHECK(store.Find(first) == nullptr);
  return true;
}

bool DepthAndCapacityBounded() {
  BookmarkStore store;
  // Build a chain up to the depth limit.
  std::uint64_t parent = BookmarkStore::kRootId;
  std::uint64_t last = parent;
  for (std::size_t i = 0; i < 32; ++i) {
    last = store.AddFolder(parent, "d" + std::to_string(i));
    if (last == 0) {
      break;
    }
    parent = last;
  }
  BookmarkError error = BookmarkError::kUnknownId;
  CHECK(store.AddFolder(parent, "too-deep", &error) == 0);
  CHECK(error == BookmarkError::kDepthExceeded);
  return true;
}

bool SearchIsBoundedAndCaseInsensitive() {
  BookmarkStore store;
  store.AddBookmark(0, "Crayon Browser", "https://crayon.test/");
  store.AddBookmark(0, "Other", "https://other.test/crayon");
  const auto matches = store.Search("CRAYON");
  CHECK(matches.size() == 2);
  CHECK(store.Search("").empty());
  CHECK(store.Search("nothing-here").empty());
  return true;
}

bool DuplicateUrlDetection() {
  BookmarkStore store;
  store.AddBookmark(0, "a", "https://dup.test/");
  store.AddBookmark(0, "b", "https://dup.test/");
  store.AddBookmark(0, "c", "https://other.test/");
  const auto matches = store.FindByUrl("https://dup.test/");
  CHECK(matches.size() == 2);
  CHECK(store.FindByUrl("https://absent.test/").empty());
  return true;
}

bool UpdateRules() {
  BookmarkStore store;
  const auto bookmark = store.AddBookmark(0, "old", "https://old.test/");
  const auto folder = store.AddFolder(0, "F");
  BookmarkError error = BookmarkError::kUnknownId;
  CHECK(store.Update(bookmark, "new", "https://new.test/", &error));
  CHECK(store.Find(bookmark)->title == "new");
  CHECK(store.Find(bookmark)->url == "https://new.test/");
  // Folders ignore the URL field; bookmarks reject bad URLs.
  CHECK(store.Update(folder, "F2", "", &error));
  CHECK(!store.Update(bookmark, "new", "file:///etc/passwd", &error));
  CHECK(error == BookmarkError::kInvalidUrl);
  CHECK(store.Find(bookmark)->url == "https://new.test/");  // unchanged
  return true;
}

// ---------- Codec ----------

bool RoundTripPreservesTree() {
  BookmarkStore store;
  const auto folder = store.AddFolder(0, "工作");
  store.AddBookmark(folder, "文档", "https://docs.test/?a=1&b=2");
  store.AddBookmark(0, "Top", "https://top.test/");
  const auto sub = store.AddFolder(folder, "子目录");
  store.AddBookmark(sub, "深层", "https://deep.test/");

  const std::string document = SerializeBookmarks(store);
  const auto restored = DeserializeBookmarks(document);
  CHECK(restored.has_value());
  CHECK(restored->node_count() == store.node_count());
  const auto matches = restored->FindByUrl("https://deep.test/");
  CHECK(matches.size() == 1);
  // Structure: docs folder has two children (书签 + 子目录).
  const auto docs = restored->Search("工作");
  CHECK(docs.size() == 1);
  CHECK(restored->ChildrenOf(docs.front()).size() == 2);
  return true;
}

bool CorruptionMatrixFailsClosed() {
  BookmarkStore store;
  store.AddBookmark(0, "X", "https://x.test/");
  const std::string good = SerializeBookmarks(store);

  BookmarkCodecError error = BookmarkCodecError::kIoFailure;
  // Bad header.
  CHECK(!DeserializeBookmarks("CRAYON-BOOKMARKS v0\n", &error).has_value());
  CHECK(error == BookmarkCodecError::kBadHeader);
  // Truncated payload.
  CHECK(!DeserializeBookmarks(good.substr(0, good.size() - 3), &error)
             .has_value());
  // Unknown record type.
  CHECK(!DeserializeBookmarks(std::string("CRAYON-BOOKMARKS v1\nZ 0 1\nx\n"),
                              &error)
             .has_value());
  CHECK(error == BookmarkCodecError::kUnknownRecordType);
  // Depth jump: depth 2 without two folders.
  CHECK(!DeserializeBookmarks(
            std::string("CRAYON-BOOKMARKS v1\nB 2 1 3\nx\na.b\n"), &error)
            .has_value());
  CHECK(error == BookmarkCodecError::kDepthJump);
  // Oversized title length field.
  CHECK(!DeserializeBookmarks(
            std::string("CRAYON-BOOKMARKS v1\nB 0 99999 1\nx\na.b\n"), &error)
            .has_value());
  CHECK(error == BookmarkCodecError::kLengthOverflow);
  // Content violating store validation (dangerous URL).
  CHECK(!DeserializeBookmarks(
            std::string("CRAYON-BOOKMARKS v1\nB 0 1 14\nx\njavascript:bad\n"),
            &error)
            .has_value());
  CHECK(error == BookmarkCodecError::kContentRejected);
  return true;
}

bool EmptyDocumentIsValidEmptyTree() {
  const auto restored =
      DeserializeBookmarks("CRAYON-BOOKMARKS v1\n");
  CHECK(restored.has_value());
  CHECK(restored->node_count() == 1);  // root only
  return true;
}

// ---------- Atomic file persistence ----------

bool SaveLoadRoundTripThroughFile() {
  const std::string path =
      std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR")
                                                   : "/tmp") +
      "/crayon-bookmarks-test-v1.txt";
  BookmarkStore store;
  const auto folder = store.AddFolder(0, "F");
  store.AddBookmark(folder, "B", "https://b.test/");
  BookmarkCodecError error = BookmarkCodecError::kIoFailure;
  CHECK(SaveBookmarksToFile(store, path, &error));
  const auto loaded = LoadBookmarksFromFile(path, &error);
  CHECK(loaded.has_value());
  CHECK(loaded->node_count() == store.node_count());
  // The staging file must not linger.
  std::ifstream staging(path + ".tmp");
  CHECK(!staging.good());
  std::remove(path.c_str());
  return true;
}

bool LoadRejectsMissingAndCorruptFiles() {
  BookmarkCodecError error = BookmarkCodecError::kIoFailure;
  CHECK(!LoadBookmarksFromFile("/nonexistent/crayon-none.txt", &error)
             .has_value());
  CHECK(error == BookmarkCodecError::kIoFailure);
  const std::string path =
      std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR")
                                                   : "/tmp") +
      "/crayon-bookmarks-corrupt.txt";
  {
    std::ofstream out(path);
    out << "garbage";
  }
  CHECK(!LoadBookmarksFromFile(path, &error).has_value());
  CHECK(error == BookmarkCodecError::kBadHeader);
  std::remove(path.c_str());
  return true;
}

}  // namespace

int main() {
  if (!AddAndFindBookmark() || !ValidationMatrix() || !FoldersMoveAndCycles() ||
      !RemoveCascades() || !IdsAreNeverReused() || !DepthAndCapacityBounded() ||
      !SearchIsBoundedAndCaseInsensitive() || !DuplicateUrlDetection() ||
      !UpdateRules() || !RoundTripPreservesTree() ||
      !CorruptionMatrixFailsClosed() || !EmptyDocumentIsValidEmptyTree() ||
      !SaveLoadRoundTripThroughFile() || !LoadRejectsMissingAndCorruptFiles()) {
    return 1;
  }
  return 0;
}
