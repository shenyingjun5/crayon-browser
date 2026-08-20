#include "crayon/browser_bookmarks/bookmark_codec.h"

#include <cstdio>
#include <fstream>
#include <string_view>
#include <vector>

namespace crayon::browser_bookmarks {

namespace {

constexpr std::string_view kHeader = "CRAYON-BOOKMARKS v1\n";
constexpr std::size_t kMaxDigits = 10;  // 32-bit length fields.

void SetError(BookmarkCodecError* error, BookmarkCodecError value) noexcept {
  if (error != nullptr) {
    *error = value;
  }
}

void SerializeSubtree(const BookmarkStore& store,
                      std::uint64_t folder_id,
                      std::size_t depth,
                      std::string& out) {
  for (const std::uint64_t child : store.ChildrenOf(folder_id)) {
    const BookmarkNode* node = store.Find(child);
    if (node == nullptr) {
      continue;
    }
    if (node->kind == BookmarkKind::kFolder) {
      out += "F " + std::to_string(depth) + " " +
             std::to_string(node->title.size()) + "\n" + node->title + "\n";
      SerializeSubtree(store, child, depth + 1, out);
    } else {
      out += "B " + std::to_string(depth) + " " +
             std::to_string(node->title.size()) + " " +
             std::to_string(node->url.size()) + "\n" + node->title + "\n" +
             node->url + "\n";
    }
  }
}

/// Minimal cursor over the serialized document.
class Parser final {
 public:
  explicit Parser(std::string_view document) : document_(document) {}

  bool ConsumeHeader() {
    if (document_.substr(0, kHeader.size()) != kHeader) {
      return false;
    }
    position_ = kHeader.size();
    return true;
  }

  bool AtEnd() const { return position_ == document_.size(); }

  /// Parses a non-negative bounded number followed by one space or newline.
  bool ReadNumber(std::size_t* value) {
    std::size_t parsed = 0;
    std::size_t digits = 0;
    while (position_ < document_.size() && digits < kMaxDigits) {
      const char c = document_[position_];
      if (c < '0' || c > '9') {
        break;
      }
      parsed = parsed * 10 + static_cast<std::size_t>(c - '0');
      ++position_;
      ++digits;
    }
    if (digits == 0 || position_ >= document_.size()) {
      return false;
    }
    const char terminator = document_[position_];
    if (terminator != ' ' && terminator != '\n') {
      return false;
    }
    ++position_;
    *value = parsed;
    return true;
  }

  bool ReadRecordKind(char* kind) {
    if (position_ + 2 > document_.size()) {
      return false;
    }
    *kind = document_[position_];
    if (document_[position_ + 1] != ' ') {
      return false;
    }
    position_ += 2;
    return true;
  }

  /// Reads exactly `length` payload bytes followed by a newline.
  bool ReadPayload(std::size_t length, std::string* out) {
    if (position_ >= document_.size()) {
      return false;
    }
    const std::size_t remaining = document_.size() - position_;
    if (length + 1 > remaining || document_[position_ + length] != '\n') {
      return false;
    }
    *out = std::string(document_.substr(position_, length));
    position_ += length + 1;
    return true;
  }

 private:
  std::string_view document_;
  std::size_t position_ = 0;
};

}  // namespace

std::string SerializeBookmarks(const BookmarkStore& store) {
  std::string out(kHeader);
  SerializeSubtree(store, BookmarkStore::kRootId, 0, out);
  return out;
}

std::optional<BookmarkStore> DeserializeBookmarks(
    const std::string& document,
    BookmarkCodecError* error) {
  if (document.size() > kMaxBookmarkFileBytes) {
    SetError(error, BookmarkCodecError::kLengthOverflow);
    return std::nullopt;
  }
  Parser parser(document);
  if (!parser.ConsumeHeader()) {
    SetError(error, BookmarkCodecError::kBadHeader);
    return std::nullopt;
  }
  BookmarkStore store;
  std::vector<std::uint64_t> folder_stack{BookmarkStore::kRootId};
  while (!parser.AtEnd()) {
    char kind = '\0';
    if (!parser.ReadRecordKind(&kind)) {
      SetError(error, BookmarkCodecError::kTruncated);
      return std::nullopt;
    }
    std::size_t depth = 0;
    if (!parser.ReadNumber(&depth)) {
      SetError(error, BookmarkCodecError::kTruncated);
      return std::nullopt;
    }
    if (depth >= folder_stack.size() || depth >= kMaxTreeDepth) {
      SetError(error, BookmarkCodecError::kDepthJump);
      return std::nullopt;
    }
    folder_stack.resize(depth + 1);
    const std::uint64_t parent = folder_stack.back();
    std::size_t title_len = 0;
    std::string title;
    std::string url;
    std::uint64_t new_folder = 0;
    if (kind == 'F') {
      if (!parser.ReadNumber(&title_len) || title_len > kMaxTitleBytes) {
        SetError(error, BookmarkCodecError::kLengthOverflow);
        return std::nullopt;
      }
      if (!parser.ReadPayload(title_len, &title)) {
        SetError(error, BookmarkCodecError::kTruncated);
        return std::nullopt;
      }
      BookmarkError store_error = BookmarkError::kUnknownId;
      new_folder = store.AddFolder(parent, title, &store_error);
    } else if (kind == 'B') {
      std::size_t url_len = 0;
      if (!parser.ReadNumber(&title_len) || !parser.ReadNumber(&url_len) ||
          title_len > kMaxTitleBytes || url_len > kMaxUrlBytes) {
        SetError(error, BookmarkCodecError::kLengthOverflow);
        return std::nullopt;
      }
      if (!parser.ReadPayload(title_len, &title) ||
          !parser.ReadPayload(url_len, &url)) {
        SetError(error, BookmarkCodecError::kTruncated);
        return std::nullopt;
      }
      BookmarkError store_error = BookmarkError::kUnknownId;
      if (store.AddBookmark(parent, title, url, &store_error) == 0) {
        SetError(error, BookmarkCodecError::kContentRejected);
        return std::nullopt;
      }
    } else {
      SetError(error, BookmarkCodecError::kUnknownRecordType);
      return std::nullopt;
    }
    if (kind == 'F') {
      if (new_folder == 0) {
        SetError(error, BookmarkCodecError::kContentRejected);
        return std::nullopt;
      }
      folder_stack.push_back(new_folder);
    }
  }
  return store;
}

bool SaveBookmarksToFile(const BookmarkStore& store,
                         const std::string& path,
                         BookmarkCodecError* error) {
  const std::string staging = path + ".tmp";
  {
    std::ofstream out(staging, std::ios::binary | std::ios::trunc);
    if (!out) {
      SetError(error, BookmarkCodecError::kIoFailure);
      return false;
    }
    out << SerializeBookmarks(store);
    if (!out.good()) {
      SetError(error, BookmarkCodecError::kIoFailure);
      return false;
    }
  }
  if (std::rename(staging.c_str(), path.c_str()) != 0) {
    std::remove(staging.c_str());
    SetError(error, BookmarkCodecError::kIoFailure);
    return false;
  }
  return true;
}

std::optional<BookmarkStore> LoadBookmarksFromFile(
    const std::string& path,
    BookmarkCodecError* error) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    SetError(error, BookmarkCodecError::kIoFailure);
    return std::nullopt;
  }
  const std::streamsize size = in.tellg();
  if (size < 0 ||
      static_cast<std::uintmax_t>(size) > kMaxBookmarkFileBytes) {
    SetError(error, BookmarkCodecError::kLengthOverflow);
    return std::nullopt;
  }
  in.seekg(0);
  std::string document(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !in.read(document.data(), size)) {
    SetError(error, BookmarkCodecError::kIoFailure);
    return std::nullopt;
  }
  return DeserializeBookmarks(document, error);
}

}  // namespace crayon::browser_bookmarks
