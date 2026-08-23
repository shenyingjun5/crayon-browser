#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace crayon::browser_page_tools {

/// Maximum find query length, in bytes.
inline constexpr std::size_t kMaxFindQueryLen = 1024;
/// Maximum suggested output filename length, in bytes.
inline constexpr std::size_t kMaxFilenameLen = 128;
/// Zoom factors as whole percentages; the closed set stepped by
/// ZoomIn/ZoomOut.
inline constexpr int kZoomFactors[] = {
    25, 33, 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400, 500};

/// Reports whether `factor` is a member of the closed zoom set.
bool IsValidZoomFactor(int factor) noexcept;

/// In-page find session view model (UX-011: state correct, cancel leaves
/// no residue).  Match counting itself belongs to the engine; this class
/// owns the query, options and the visible match cursor.
/// Thread contract: single-threaded, UI thread only.
class FindBarController final {
 public:
  FindBarController() = default;

  /// Starts or restarts a find.  Empty or oversized queries are rejected
  /// without touching the active session.
  bool StartFind(const std::string& query, bool case_sensitive);
  /// Live query refinement while the bar is open.
  bool UpdateQuery(const std::string& query);

  /// Toggles match-case while the bar is open; resets the cursor so
  /// the engine restarts the search under the new option.
  bool SetCaseSensitive(bool case_sensitive);

  /// Engine-reported match count for the current query; the cursor is
  /// clamped into range.
  void ReportMatchCount(std::size_t count);

  /// Steps to the next/previous match (wraps); reports the cursor or
  /// `false` when there are no matches.
  bool FindNext();
  bool FindPrevious();

  /// Closes the bar and clears query, options and cursor (no residue).
  void EndFind();

  bool active() const noexcept { return active_; }
  const std::string& query() const noexcept { return query_; }
  bool case_sensitive() const noexcept { return case_sensitive_; }
  std::size_t match_count() const noexcept { return match_count_; }
  std::size_t cursor() const noexcept { return cursor_; }

 private:
  bool ApplyQuery(const std::string& query);

  bool active_{false};
  bool case_sensitive_{false};
  std::string query_;
  std::size_t match_count_{0};
  std::size_t cursor_{0};
};

/// Zoom view model over the closed factor set (UX-011: state correct).
/// Thread contract: single-threaded, UI thread only.
class ZoomController final {
 public:
  ZoomController() = default;

  /// Steps one factor up/down; at the bounds the request fails and the
  /// factor is unchanged.
  bool ZoomIn();
  bool ZoomOut();
  /// Sets an arbitrary factor from the closed set.
  bool SetZoom(int factor);
  void Reset();

  int factor() const noexcept { return factor_; }
  bool is_default() const noexcept { return factor_ == 100; }

 private:
  int factor_{100};
};

/// Fullscreen view model.  Chrome exposes only enter/exit toggles; the
/// transitional states let the UI suppress duplicate commands while the
/// engine animation is in flight.
enum class FullscreenState { kWindowed = 0, kEntering, kFullscreen, kExiting };

/// Thread contract: single-threaded, UI thread only.
class FullscreenController final {
 public:
  FullscreenController() = default;

  bool RequestEnter();
  bool RequestExit();
  /// Engine acknowledgements for the transitional states.
  void AcknowledgeEntered();
  void AcknowledgeExited();

  FullscreenState state() const noexcept { return state_; }

 private:
  FullscreenState state_{FullscreenState::kWindowed};
};

/// Closed page-output kinds (print/PDF and save-page share one job
/// pipeline).
enum class PageOutputKind { kPrintToPdf = 0, kSavePage };
/// Closed save-page formats.
enum class PageOutputFormat { kPdf = 0, kComplete, kHtmlOnly, kMhtml };
/// Closed job states.
enum class PageOutputState { kIdle = 0, kPreparing, kRunning, kSucceeded, kFailed, kCancelled };
/// Closed failure causes; never carries paths or page data.
enum class PageOutputError { kEngineFailed, kFilenameRejected, kProfileMismatch };

/// Validates a suggested output filename: non-empty, closed charset
/// `[A-Za-z0-9._-]`, no path separators, no `..` prefix, bounded length.
bool IsValidOutputFilename(const std::string& name);

/// Print/PDF and save-page job view model (UX-011: cancel without
/// residue, failures explicit, output paths controlled, no cross-profile
/// leakage).  A job is bound to the profile id token that created it;
/// results delivered for another profile fail closed with
/// `kProfileMismatch` and no output is written.
/// Thread contract: single-threaded, UI thread only.
class PageOutputJobController final {
 public:
  PageOutputJobController() = default;

  /// Starts a job.  The filename must pass `IsValidOutputFilename` and
  /// the profile must be a non-empty token; everything else fails
  /// without leaving the idle state.
  bool Start(PageOutputKind kind,
             PageOutputFormat format,
             const std::string& suggested_filename,
             const std::string& profile_id);

  /// Engine lifecycle: preparing finished / progress.  Delivering
  /// success for a different profile than the job's fails closed.
  bool NotifyPreparingDone(const std::string& profile_id);
  void NotifyFailed(PageOutputError error, const std::string& profile_id);
  bool NotifySucceeded(const std::string& profile_id);

  /// Cancels a live job; cancelling an idle/terminal job is rejected
  /// (the caller acknowledges terminals separately).
  bool Cancel();
  /// Returns to idle from a terminal state.
  void AcknowledgeResult();

  PageOutputState state() const noexcept { return state_; }
  PageOutputKind kind() const noexcept { return kind_; }
  PageOutputFormat format() const noexcept { return format_; }
  PageOutputError last_error() const noexcept { return last_error_; }
  const std::string& suggested_filename() const noexcept { return filename_; }

 private:
  bool DeliveringFor(const std::string& profile_id) const;

  PageOutputState state_{PageOutputState::kIdle};
  PageOutputKind kind_{PageOutputKind::kPrintToPdf};
  PageOutputFormat format_{PageOutputFormat::kPdf};
  PageOutputError last_error_{PageOutputError::kEngineFailed};
  std::string filename_;
  std::string profile_;
};

}  // namespace crayon::browser_page_tools
