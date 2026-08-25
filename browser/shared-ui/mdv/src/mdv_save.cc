#include "crayon/browser_mdv/mdv_save.h"

namespace crayon::browser_mdv_save {
namespace {
using crayon::browser_mdv::HasMarkdownSuffix;
using crayon::browser_mdv::kMaxEntryPathLen;
}  // namespace

void MdvSaveController::RecordLoadedFile(const std::string& path, std::uint64_t size,
                                         std::uint64_t mtime) {
  baseline_.path = path;
  baseline_.size = size;
  baseline_.mtime = mtime;
  baseline_.valid = true;
}

void MdvSaveController::ClearLoadedFile() {
  baseline_ = FileBaseline{};
  state_ = SaveState::kIdle;
  residual_temp_path_.clear();
}

SaveState MdvSaveController::RunAtomicWrite(const std::string& target_path,
                                            const std::string& bytes) {
  std::string temp_path;
  if (hooks_.write_temp == nullptr ||
      hooks_.write_temp(target_path, bytes, &temp_path) != 0) {
    state_ = SaveState::kFailedTempWrite;
    return state_;
  }
  if (hooks_.rename == nullptr || hooks_.rename(temp_path, target_path) != 0) {
    // Best-effort cleanup; a failed cleanup must surface the residual
    // path (never claim success silently).
    if (hooks_.remove == nullptr || hooks_.remove(temp_path) != 0) {
      residual_temp_path_ = temp_path;
      state_ = SaveState::kFailedResidual;
    } else {
      state_ = SaveState::kFailedRename;
    }
    return state_;
  }
  state_ = SaveState::kSucceeded;
  return state_;
}

SaveState MdvSaveController::Save(SaveKind kind, const std::string& target_path,
                                  const std::string& bytes) {
  residual_temp_path_.clear();
  // Target shape validation for both paths (§4 matrix minus the
  // existence requirement — saving may create or overwrite).
  if (!HasMarkdownSuffix(target_path)) {
    state_ = SaveState::kFailedInvalidTarget;
    return state_;
  }
  if (target_path.size() > kMaxEntryPathLen) {
    state_ = SaveState::kFailedInvalidTarget;
    return state_;
  }
  if (target_path.find("..") != std::string::npos) {
    state_ = SaveState::kFailedInvalidTarget;
    return state_;
  }
  bool has_control = false;
  for (const char c : target_path) {
    const auto raw = static_cast<unsigned char>(c);
    if (raw < 0x20 || raw == 0x7F) {
      has_control = true;
      break;
    }
  }
  if (has_control) {
    state_ = SaveState::kFailedInvalidTarget;
    return state_;
  }

  if (kind == SaveKind::kWriteBack) {
    if (!baseline_.valid || baseline_.path != target_path || hooks_.stat_file == nullptr) {
      state_ = SaveState::kFailedStat;
      return state_;
    }
    std::uint64_t size = 0;
    std::uint64_t mtime = 0;
    if (hooks_.stat_file(target_path, &size, &mtime) != 0) {
      state_ = SaveState::kFailedStat;
      return state_;
    }
    if (size != baseline_.size || mtime != baseline_.mtime) {
      // External modification: never overwrite silently (MD-006).
      state_ = SaveState::kFailedConflict;
      return state_;
    }
  }

  const SaveState result = RunAtomicWrite(target_path, bytes);
  if (result == SaveState::kSucceeded) {
    baseline_.path = target_path;
    baseline_.size = bytes.size();
    baseline_.mtime = 0;  // unknown until the next stat; drift check
    baseline_.valid = true;
    baseline_.mtime_known = false;
  }
  return result;
}

}  // namespace crayon::browser_mdv_save
