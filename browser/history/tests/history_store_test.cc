#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "crayon/browser_history/history_codec.h"
#include "crayon/browser_history/history_store.h"

namespace {

using crayon::browser_history::DeserializeHistory;
using crayon::browser_history::HistoryCodecError;
using crayon::browser_history::HistoryError;
using crayon::browser_history::HistoryStore;
using crayon::browser_history::kMaxHistoryEntries;
using crayon::browser_history::kMaxRecentlyClosed;
using crayon::browser_history::LoadHistoryFromFile;
using crayon::browser_history::SaveHistoryToFile;
using crayon::browser_history::SerializeHistory;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

// ---------- Recording ----------

bool RecordAndFind() {
  HistoryStore store;
  const auto id = store.RecordVisit("https://a.test/", "A 页面", 1000);
  CHECK(id != 0);
  const auto* entry = store.Find(id);
  CHECK(entry != nullptr);
  CHECK(entry->title == "A 页面");
  CHECK(entry->visited_at == 1000);
  return true;
}

bool ValidationMatrix() {
  HistoryStore store;
  HistoryError error = HistoryError::kInvalidUrl;
  CHECK(store.RecordVisit("javascript:alert(1)", "x", 1, &error) == 0);
  CHECK(error == HistoryError::kInvalidUrl);
  CHECK(store.RecordVisit("https://a.test/\x01", "x", 1, &error) == 0);
  CHECK(error == HistoryError::kInvalidUrl);
  CHECK(store.RecordVisit("https://a.test/", std::string(513, 't'), 1,
                          &error) == 0);
  CHECK(error == HistoryError::kInvalidTitle);
  return true;
}

bool CapacityEvictsOldest() {
  HistoryStore store;
  for (std::size_t i = 0; i < kMaxHistoryEntries + 5; ++i) {
    CHECK(store.RecordVisit("https://site.test/" + std::to_string(i), "t",
                            100 + i) != 0);
  }
  CHECK(store.entries().size() == kMaxHistoryEntries);
  // The five oldest entries were evicted.
  CHECK(store.entries().front().visited_at == 105);
  return true;
}

// ---------- Ephemeral ----------

bool EphemeralRefusesEverything() {
  HistoryStore store(/*ephemeral=*/true);
  HistoryError error = HistoryError::kInvalidUrl;
  CHECK(store.RecordVisit("https://a.test/", "A", 1, &error) == 0);
  CHECK(error == HistoryError::kEphemeral);
  CHECK(!store.RecordClosedTab("https://a.test/", "A", 2, &error));
  CHECK(error == HistoryError::kEphemeral);
  // Persistence is refused too.
  HistoryCodecError codec_error = HistoryCodecError::kIoFailure;
  CHECK(!SaveHistoryToFile(store, "/tmp/crayon-history-should-not-exist.txt",
                           &codec_error));
  CHECK(codec_error == HistoryCodecError::kEphemeralRefused);
  return true;
}

// ---------- Recently closed ----------

bool RecentlyClosedStackBoundedAndOrdered() {
  HistoryStore store;
  for (std::size_t i = 0; i < kMaxRecentlyClosed + 3; ++i) {
    CHECK(store.RecordClosedTab("https://tab.test/" + std::to_string(i),
                                "tab", 200 + i));
  }
  CHECK(store.recently_closed_count() == kMaxRecentlyClosed);
  // Newest first on restore; the three oldest were dropped.
  const auto restored = store.RestoreRecentlyClosed();
  CHECK(restored.has_value());
  CHECK(restored->closed_at == 200 + kMaxRecentlyClosed + 2);
  store.ClearAll();
  CHECK(!store.RestoreRecentlyClosed().has_value());
  return true;
}

// ---------- Deletion ----------

bool DeleteRangeBoundaries() {
  HistoryStore store;
  store.RecordVisit("https://a.test/", "a", 10);
  store.RecordVisit("https://b.test/", "b", 20);
  store.RecordVisit("https://c.test/", "c", 30);
  HistoryError error = HistoryError::kInvalidUrl;
  CHECK(store.DeleteRange(25, 15, &error) == 0);  // inverted range
  CHECK(error == HistoryError::kInvalidRange);
  CHECK(store.entries().size() == 3);             // untouched
  CHECK(store.DeleteRange(10, 20, &error) == 2);  // inclusive endpoints
  CHECK(store.entries().size() == 1);
  CHECK(store.entries().front().visited_at == 30);
  CHECK(store.DeleteRange(0, 5) == 0);            // empty range
  return true;
}

bool DeleteUrlAndClearAll() {
  HistoryStore store;
  store.RecordVisit("https://dup.test/", "a", 1);
  store.RecordVisit("https://dup.test/", "b", 2);
  store.RecordVisit("https://keep.test/", "c", 3);
  CHECK(store.DeleteUrl("https://dup.test/") == 2);
  CHECK(store.entries().size() == 1);
  store.RecordClosedTab("https://x.test/", "x", 9);
  store.ClearAll();
  CHECK(store.entries().empty());
  CHECK(store.recently_closed_count() == 0);
  return true;
}

// ---------- Search ----------

bool SearchNewestFirstAndBounded() {
  HistoryStore store;
  store.RecordVisit("https://old.test/crayon", "old", 1);
  store.RecordVisit("https://new.test/", "Crayon new", 2);
  const auto matches = store.Search("CRAYON");
  CHECK(matches.size() == 2);
  CHECK(matches.front().visited_at == 2);  // newest first
  CHECK(store.Search("").empty());
  CHECK(store.Search("absent").empty());
  return true;
}

// ---------- Codec ----------

bool RoundTripPreservesEntries() {
  HistoryStore store;
  store.RecordVisit("https://a.test/?q=1&r=2", "标题 一", 111);
  store.RecordVisit("https://b.test/", "Second", 222);
  const auto restored = DeserializeHistory(SerializeHistory(store));
  CHECK(restored.has_value());
  CHECK(restored->entries().size() == 2);
  CHECK(restored->entries().front().title == "标题 一");
  CHECK(restored->entries().back().visited_at == 222);
  return true;
}

bool CorruptionMatrixFailsClosed() {
  HistoryStore store;
  store.RecordVisit("https://a.test/", "a", 1);
  const std::string good = SerializeHistory(store);
  HistoryCodecError error = HistoryCodecError::kIoFailure;
  CHECK(!DeserializeHistory("CRAYON-HISTORY v0\n", &error).has_value());
  CHECK(error == HistoryCodecError::kBadHeader);
  CHECK(!DeserializeHistory(good.substr(0, good.size() - 2), &error)
            .has_value());
  CHECK(!DeserializeHistory(std::string("CRAYON-HISTORY v1\nZ 1 1 1\na\nb\n"),
                            &error)
            .has_value());
  CHECK(error == HistoryCodecError::kUnknownRecordType);
  CHECK(!DeserializeHistory(
            std::string("CRAYON-HISTORY v1\nV 1 99999 5\nt\nu r l\n"), &error)
            .has_value());
  CHECK(error == HistoryCodecError::kLengthOverflow);
  CHECK(!DeserializeHistory(
            std::string("CRAYON-HISTORY v1\nV 1 1 14\nt\njavascript:bad\n"),
            &error)
            .has_value());
  CHECK(error == HistoryCodecError::kContentRejected);
  return true;
}

bool FileRoundTripAndMissingFile() {
  const std::string path =
      std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR")
                                                   : "/tmp") +
      "/crayon-history-test-v1.txt";
  HistoryStore store;
  store.RecordVisit("https://a.test/", "a", 7);
  HistoryCodecError error = HistoryCodecError::kIoFailure;
  CHECK(SaveHistoryToFile(store, path, &error));
  const auto loaded = LoadHistoryFromFile(path, &error);
  CHECK(loaded.has_value());
  CHECK(loaded->entries().size() == 1);
  std::ifstream staging(path + ".tmp");
  CHECK(!staging.good());
  std::remove(path.c_str());
  CHECK(!LoadHistoryFromFile("/nonexistent/crayon-hist-none.txt", &error)
             .has_value());
  CHECK(error == HistoryCodecError::kIoFailure);
  return true;
}

}  // namespace

int main() {
  if (!RecordAndFind() || !ValidationMatrix() || !CapacityEvictsOldest() ||
      !EphemeralRefusesEverything() || !RecentlyClosedStackBoundedAndOrdered() ||
      !DeleteRangeBoundaries() || !DeleteUrlAndClearAll() ||
      !SearchNewestFirstAndBounded() || !RoundTripPreservesEntries() ||
      !CorruptionMatrixFailsClosed() || !FileRoundTripAndMissingFile()) {
    return 1;
  }
  return 0;
}
