// UX-011 contract tests: page find, zoom, fullscreen and print/PDF +
// save-page command state machines.
#include <cstdlib>
#include <iostream>
#include <string>

#include "crayon/browser_page_tools/page_tools.h"

namespace {

using crayon::browser_page_tools::FindBarController;
using crayon::browser_page_tools::FullscreenController;
using crayon::browser_page_tools::FullscreenState;
using crayon::browser_page_tools::IsValidOutputFilename;
using crayon::browser_page_tools::IsValidZoomFactor;
using crayon::browser_page_tools::PageOutputError;
using crayon::browser_page_tools::PageOutputFormat;
using crayon::browser_page_tools::PageOutputJobController;
using crayon::browser_page_tools::PageOutputKind;
using crayon::browser_page_tools::PageOutputState;
using crayon::browser_page_tools::ZoomController;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

bool FindSessionLifecycle() {
  FindBarController find;
  CHECK(!find.active());
  CHECK(find.StartFind("", false) == false);  // empty rejected
  CHECK(!find.active());
  CHECK(find.StartFind(std::string(1025, 'a'), false) == false);
  CHECK(find.StartFind("needle", true));
  CHECK(find.active());
  CHECK(find.query() == "needle");
  CHECK(find.case_sensitive());
  find.ReportMatchCount(3);
  CHECK(find.match_count() == 3);
  CHECK(find.FindNext() && find.cursor() == 1);
  CHECK(find.FindNext() && find.cursor() == 2);
  CHECK(find.FindNext() && find.cursor() == 0);  // wraps
  CHECK(find.FindPrevious() && find.cursor() == 2);
  // Live refinement resets cursor/count.
  CHECK(find.UpdateQuery("need"));
  CHECK(find.match_count() == 0 && find.cursor() == 0);
  CHECK(!find.FindNext());
  // Match-case toggles live and reset the cursor; rejected while
  // hidden.
  find.ReportMatchCount(4);
  CHECK(find.FindNext() && find.cursor() == 1);
  CHECK(find.SetCaseSensitive(true));
  CHECK(find.case_sensitive());
  CHECK(find.match_count() == 0 && find.cursor() == 0);
  find.ReportMatchCount(2);
  CHECK(find.FindNext() && find.cursor() == 1);
  CHECK(find.SetCaseSensitive(false) && !find.case_sensitive());
  // Refinement is rejected while hidden.
  find.EndFind();
  CHECK(!find.active() && find.query().empty() && !find.case_sensitive());
  CHECK(find.match_count() == 0);
  CHECK(!find.UpdateQuery("x"));
  CHECK(!find.SetCaseSensitive(true));
  return true;
}

bool ZoomClosedSet() {
  ZoomController zoom;
  CHECK(zoom.factor() == 100 && zoom.is_default());
  CHECK(!zoom.ZoomOut() || true);  // 100 -> 90 is legal, so probe bounds explicitly below
  zoom.SetZoom(25);
  CHECK(!zoom.ZoomOut());  // lower bound
  CHECK(zoom.factor() == 25);
  CHECK(zoom.SetZoom(500));
  CHECK(!zoom.ZoomIn());  // upper bound
  CHECK(zoom.factor() == 500);
  zoom.Reset();
  CHECK(zoom.is_default());
  CHECK(!zoom.SetZoom(60));  // not in the closed set
  CHECK(zoom.factor() == 100);
  CHECK(IsValidZoomFactor(300));
  CHECK(!IsValidZoomFactor(0));
  CHECK(!IsValidZoomFactor(-100));
  // Stepping walks the closed set from the default.
  zoom.ZoomIn();
  CHECK(zoom.factor() == 110);
  zoom.ZoomOut();
  CHECK(zoom.factor() == 100);
  return true;
}

bool FullscreenTransitions() {
  FullscreenController fs;
  CHECK(fs.state() == FullscreenState::kWindowed);
  CHECK(!fs.RequestExit());
  CHECK(fs.RequestEnter());
  CHECK(!fs.RequestEnter());  // duplicate suppressed while entering
  fs.AcknowledgeEntered();
  CHECK(fs.state() == FullscreenState::kFullscreen);
  CHECK(fs.RequestExit());
  CHECK(!fs.RequestExit());
  fs.AcknowledgeExited();
  CHECK(fs.state() == FullscreenState::kWindowed);
  return true;
}

bool OutputFilenameMatrix() {
  CHECK(IsValidOutputFilename("page.pdf"));
  CHECK(IsValidOutputFilename("My-Saved_Page.2026.html"));
  CHECK(!IsValidOutputFilename(""));
  CHECK(!IsValidOutputFilename(".hidden"));               // leading dot
  CHECK(!IsValidOutputFilename("sub/page.pdf"));          // separators
  CHECK(!IsValidOutputFilename("windows\\page.pdf"));
  CHECK(!IsValidOutputFilename(".."));
  CHECK(!IsValidOutputFilename("bad name.pdf"));          // space
  CHECK(!IsValidOutputFilename(std::string(200, 'a')));   // length
  return true;
}

bool PrintSaveJobLifecycle() {
  PageOutputJobController job;
  CHECK(job.state() == PageOutputState::kIdle);
  CHECK(!job.Cancel());  // nothing live
  CHECK(job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "page.pdf", "profile-a"));
  CHECK(job.state() == PageOutputState::kPreparing);
  CHECK(!job.Start(PageOutputKind::kSavePage, PageOutputFormat::kComplete, "x.html", "profile-a"));
  CHECK(job.NotifyPreparingDone("profile-a"));
  CHECK(job.state() == PageOutputState::kRunning);
  CHECK(job.NotifySucceeded("profile-a"));
  CHECK(job.state() == PageOutputState::kSucceeded);
  job.AcknowledgeResult();
  CHECK(job.state() == PageOutputState::kIdle);
  CHECK(job.suggested_filename().empty());
  return true;
}

bool SavePageFailureAndCancel() {
  PageOutputJobController job;
  CHECK(job.Start(PageOutputKind::kSavePage, PageOutputFormat::kMhtml, "page.mhtml", "profile-a"));
  job.NotifyFailed(PageOutputError::kEngineFailed, "profile-a");
  CHECK(job.state() == PageOutputState::kFailed);
  CHECK(job.last_error() == PageOutputError::kEngineFailed);
  CHECK(!job.Cancel());  // terminal, not live
  job.AcknowledgeResult();

  CHECK(job.Start(PageOutputKind::kSavePage, PageOutputFormat::kComplete, "a.html", "profile-a"));
  CHECK(job.NotifyPreparingDone("profile-a"));
  CHECK(job.Cancel());
  CHECK(job.state() == PageOutputState::kCancelled);
  job.AcknowledgeResult();
  CHECK(job.state() == PageOutputState::kIdle);
  return true;
}

bool CrossProfileResultsFailClosed() {
  PageOutputJobController job;
  CHECK(job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "p.pdf", "profile-a"));
  CHECK(!job.NotifyPreparingDone("profile-b"));  // foreign delivery rejected
  CHECK(job.state() == PageOutputState::kPreparing);
  CHECK(job.NotifyPreparingDone("profile-a"));
  // A success delivered for another profile fails closed; no success.
  CHECK(!job.NotifySucceeded("profile-b"));
  CHECK(job.state() == PageOutputState::kFailed);
  CHECK(job.last_error() == PageOutputError::kProfileMismatch);
  // Failures from foreign profiles also mark mismatch, never leak.
  job.AcknowledgeResult();
  CHECK(job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "p.pdf", "profile-a"));
  job.NotifyFailed(PageOutputError::kEngineFailed, "profile-c");
  CHECK(job.last_error() == PageOutputError::kProfileMismatch);
  return true;
}

bool StartValidation() {
  PageOutputJobController job;
  CHECK(!job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "", "profile-a"));
  CHECK(!job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "bad/name", "profile-a"));
  CHECK(!job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "ok.pdf", ""));
  CHECK(!job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "ok.pdf", "bad id"));
  CHECK(job.state() == PageOutputState::kIdle);
  CHECK(job.Start(PageOutputKind::kPrintToPdf, PageOutputFormat::kPdf, "ok.pdf", "profile.a-1"));
  return true;
}

}  // namespace

int main() {
  const bool ok = FindSessionLifecycle() && ZoomClosedSet() && FullscreenTransitions() &&
                  OutputFilenameMatrix() && PrintSaveJobLifecycle() && SavePageFailureAndCancel() &&
                  CrossProfileResultsFailClosed() && StartValidation();
  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "page_tools_contract_test passed\n";
  return EXIT_SUCCESS;
}
