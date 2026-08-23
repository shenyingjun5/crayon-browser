#include "crayon/browser_page_tools/page_tools.h"

#include <algorithm>

namespace crayon::browser_page_tools {
namespace {

constexpr int kZoomFactorCount = static_cast<int>(sizeof(kZoomFactors) / sizeof(kZoomFactors[0]));

bool IsValidProfileToken(const std::string& id) {
  if (id.empty() || id.size() > 128) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.';
  });
}

}  // namespace

bool IsValidZoomFactor(int factor) noexcept {
  for (int i = 0; i < kZoomFactorCount; ++i) {
    if (kZoomFactors[i] == factor) {
      return true;
    }
  }
  return false;
}

bool FindBarController::ApplyQuery(const std::string& query) {
  if (query.empty() || query.size() > kMaxFindQueryLen) {
    return false;
  }
  query_ = query;
  match_count_ = 0;
  cursor_ = 0;
  return true;
}

bool FindBarController::StartFind(const std::string& query, bool case_sensitive) {
  if (!ApplyQuery(query)) {
    return false;
  }
  case_sensitive_ = case_sensitive;
  active_ = true;
  return true;
}

bool FindBarController::UpdateQuery(const std::string& query) {
  if (!active_) {
    return false;
  }
  return ApplyQuery(query);
}

bool FindBarController::SetCaseSensitive(bool case_sensitive) {
  if (!active_) {
    return false;
  }
  case_sensitive_ = case_sensitive;
  match_count_ = 0;
  cursor_ = 0;
  return true;
}

void FindBarController::ReportMatchCount(std::size_t count) {
  match_count_ = count;
  if (cursor_ >= count) {
    cursor_ = count == 0 ? 0 : count - 1;
  }
}

bool FindBarController::FindNext() {
  if (!active_ || match_count_ == 0) {
    return false;
  }
  cursor_ = (cursor_ + 1) % match_count_;
  return true;
}

bool FindBarController::FindPrevious() {
  if (!active_ || match_count_ == 0) {
    return false;
  }
  cursor_ = cursor_ == 0 ? match_count_ - 1 : cursor_ - 1;
  return true;
}

void FindBarController::EndFind() {
  active_ = false;
  case_sensitive_ = false;
  query_.clear();
  match_count_ = 0;
  cursor_ = 0;
}

bool ZoomController::ZoomIn() {
  for (int i = 0; i < kZoomFactorCount; ++i) {
    if (kZoomFactors[i] == factor_ && i + 1 < kZoomFactorCount) {
      factor_ = kZoomFactors[i + 1];
      return true;
    }
  }
  return false;
}

bool ZoomController::ZoomOut() {
  for (int i = 0; i < kZoomFactorCount; ++i) {
    if (kZoomFactors[i] == factor_ && i > 0) {
      factor_ = kZoomFactors[i - 1];
      return true;
    }
  }
  return false;
}

bool ZoomController::SetZoom(int factor) {
  if (!IsValidZoomFactor(factor)) {
    return false;
  }
  factor_ = factor;
  return true;
}

void ZoomController::Reset() {
  factor_ = 100;
}

bool FullscreenController::RequestEnter() {
  if (state_ != FullscreenState::kWindowed) {
    return false;
  }
  state_ = FullscreenState::kEntering;
  return true;
}

bool FullscreenController::RequestExit() {
  if (state_ != FullscreenState::kFullscreen) {
    return false;
  }
  state_ = FullscreenState::kExiting;
  return true;
}

void FullscreenController::AcknowledgeEntered() {
  if (state_ == FullscreenState::kEntering) {
    state_ = FullscreenState::kFullscreen;
  }
}

void FullscreenController::AcknowledgeExited() {
  if (state_ == FullscreenState::kExiting) {
    state_ = FullscreenState::kWindowed;
  }
}

bool IsValidOutputFilename(const std::string& name) {
  if (name.empty() || name.size() > kMaxFilenameLen || name[0] == '.') {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.';
  });
}

bool PageOutputJobController::DeliveringFor(const std::string& profile_id) const {
  return profile_id == profile_;
}

bool PageOutputJobController::Start(PageOutputKind kind,
                                    PageOutputFormat format,
                                    const std::string& suggested_filename,
                                    const std::string& profile_id) {
  if (state_ != PageOutputState::kIdle) {
    return false;
  }
  if (!IsValidOutputFilename(suggested_filename) || !IsValidProfileToken(profile_id)) {
    return false;
  }
  kind_ = kind;
  format_ = format;
  filename_ = suggested_filename;
  profile_ = profile_id;
  state_ = PageOutputState::kPreparing;
  return true;
}

bool PageOutputJobController::NotifyPreparingDone(const std::string& profile_id) {
  if (state_ != PageOutputState::kPreparing || !DeliveringFor(profile_id)) {
    return false;
  }
  state_ = PageOutputState::kRunning;
  return true;
}

void PageOutputJobController::NotifyFailed(PageOutputError error, const std::string& profile_id) {
  if (state_ == PageOutputState::kIdle || state_ == PageOutputState::kSucceeded) {
    return;
  }
  // Failures for another profile's job close the job with a mismatch
  // marker instead of leaking state across profiles.
  if (!DeliveringFor(profile_id)) {
    last_error_ = PageOutputError::kProfileMismatch;
  } else {
    last_error_ = error;
  }
  state_ = PageOutputState::kFailed;
}

bool PageOutputJobController::NotifySucceeded(const std::string& profile_id) {
  if (state_ != PageOutputState::kRunning || !DeliveringFor(profile_id)) {
    if (state_ == PageOutputState::kRunning) {
      last_error_ = PageOutputError::kProfileMismatch;
      state_ = PageOutputState::kFailed;
    }
    return false;
  }
  state_ = PageOutputState::kSucceeded;
  return true;
}

bool PageOutputJobController::Cancel() {
  if (state_ != PageOutputState::kPreparing && state_ != PageOutputState::kRunning) {
    return false;
  }
  state_ = PageOutputState::kCancelled;
  return true;
}

void PageOutputJobController::AcknowledgeResult() {
  if (state_ == PageOutputState::kSucceeded || state_ == PageOutputState::kFailed ||
      state_ == PageOutputState::kCancelled) {
    state_ = PageOutputState::kIdle;
    filename_.clear();
    profile_.clear();
  }
}

}  // namespace crayon::browser_page_tools
