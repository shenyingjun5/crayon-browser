#include "browser/window/alloy_page_tools.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <utility>

namespace crayon::browser::cef_shell::window {

namespace {

constexpr double kCefZoomStep = 1.2;
constexpr std::size_t kMaximumWindowsPathLength = 32767;

double CefZoomLevel(int factor) {
  return std::log(static_cast<double>(factor) / 100.0) / std::log(kCefZoomStep);
}

bool IsValidAuthorizedPdfPath(const std::wstring &path) {
  if (path.empty() || path.find(L'\0') != std::wstring::npos ||
      path.size() > kMaximumWindowsPathLength)
    return false;
  const std::filesystem::path parsed(path);
  if (!parsed.is_absolute())
    return false;
  std::wstring extension = parsed.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t value) { return std::towlower(value); });
  return extension == L".pdf";
}

} // namespace

AlloyPageTools::AlloyPageTools(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefWindow> window,
                               std::string profile_id)
    : browser_(std::move(browser)), window_(std::move(window)),
      profile_id_(std::move(profile_id)) {}

bool AlloyPageTools::OnNavigation(std::uint64_t generation) {
  if (!active_ || generation == 0 || generation <= navigation_generation_)
    return false;
  navigation_generation_ = generation;
  if (browser_ && find_.active())
    browser_->GetHost()->StopFinding(true);
  find_.EndFind();
  CancelPdf();
  return true;
}

bool AlloyPageTools::StartFind(std::uint64_t generation,
                               const std::string &query, bool case_sensitive) {
  if (!IsCurrent(generation) || !browser_ ||
      !find_.StartFind(query, case_sensitive)) {
    return false;
  }
  browser_->GetHost()->Find(query, true, case_sensitive, false);
  return true;
}

bool AlloyPageTools::UpdateFind(std::uint64_t generation,
                                const std::string &query) {
  if (!IsCurrent(generation) || !browser_ || !find_.UpdateQuery(query))
    return false;
  browser_->GetHost()->Find(query, true, find_.case_sensitive(), false);
  return true;
}

bool AlloyPageTools::FindNext(std::uint64_t generation) {
  if (!IsCurrent(generation) || !browser_ || !find_.FindNext())
    return false;
  browser_->GetHost()->Find(find_.query(), true, find_.case_sensitive(), true);
  return true;
}

bool AlloyPageTools::FindPrevious(std::uint64_t generation) {
  if (!IsCurrent(generation) || !browser_ || !find_.FindPrevious())
    return false;
  browser_->GetHost()->Find(find_.query(), false, find_.case_sensitive(), true);
  return true;
}

bool AlloyPageTools::EndFind(std::uint64_t generation) {
  if (!IsCurrent(generation) || !browser_ || !find_.active())
    return false;
  browser_->GetHost()->StopFinding(true);
  find_.EndFind();
  return true;
}

bool AlloyPageTools::SetZoom(std::uint64_t generation, int factor) {
  return IsCurrent(generation) && zoom_.SetZoom(factor) && ApplyZoom();
}

bool AlloyPageTools::ZoomIn(std::uint64_t generation) {
  return IsCurrent(generation) && zoom_.ZoomIn() && ApplyZoom();
}

bool AlloyPageTools::ZoomOut(std::uint64_t generation) {
  return IsCurrent(generation) && zoom_.ZoomOut() && ApplyZoom();
}

bool AlloyPageTools::ResetZoom(std::uint64_t generation) {
  if (!IsCurrent(generation) || !browser_)
    return false;
  zoom_.Reset();
  return ApplyZoom();
}

bool AlloyPageTools::EnterFullscreen(std::uint64_t generation) {
  if (!IsCurrent(generation) || !window_ || !fullscreen_.RequestEnter())
    return false;
  window_->SetFullscreen(true);
  return true;
}

bool AlloyPageTools::ExitFullscreen(std::uint64_t generation) {
  if (!IsCurrent(generation) || !window_ || !fullscreen_.RequestExit())
    return false;
  if (browser_ && browser_->GetHost()->IsFullscreen())
    browser_->GetHost()->ExitFullscreen(true);
  window_->SetFullscreen(false);
  return true;
}

bool AlloyPageTools::OnContentFullscreenChange(bool fullscreen) {
  if (!active_ || !window_ || navigation_generation_ == 0)
    return false;
  if (fullscreen) {
    if (!fullscreen_.RequestEnter())
      return false;
  } else if (!fullscreen_.RequestExit()) {
    return false;
  }
  window_->SetFullscreen(fullscreen);
  return true;
}

void AlloyPageTools::OnWindowFullscreenTransition(bool is_completed) {
  if (!active_ || !window_ || !is_completed)
    return;
  if (window_->IsFullscreen())
    fullscreen_.AcknowledgeEntered();
  else
    fullscreen_.AcknowledgeExited();
}

bool AlloyPageTools::Print(std::uint64_t generation) {
  if (!IsCurrent(generation) || !browser_)
    return false;
  browser_->GetHost()->Print();
  return true;
}

bool AlloyPageTools::PrintToPdf(std::uint64_t generation,
                                const std::wstring &authorized_path,
                                const std::string &suggested_filename,
                                PdfCompletion completion) {
  if (!IsCurrent(generation) || !browser_ || !completion ||
      !IsValidAuthorizedPdfPath(authorized_path) || pdf_completion_ ||
      !output_.Start(browser_page_tools::PageOutputKind::kPrintToPdf,
                     browser_page_tools::PageOutputFormat::kPdf,
                     suggested_filename, profile_id_) ||
      !output_.NotifyPreparingDone(profile_id_)) {
    return false;
  }
  pdf_generation_ = generation;
  pdf_path_ = authorized_path;
  pdf_completion_ = std::move(completion);
  CefPdfPrintSettings settings;
  browser_->GetHost()->PrintToPDF(authorized_path, settings, this);
  return true;
}

void AlloyPageTools::AcknowledgeOutput() { output_.AcknowledgeResult(); }

void AlloyPageTools::OnFindResult(CefRefPtr<CefBrowser> browser, int, int count,
                                  const CefRect &, int, bool) {
  if (!active_ || !find_.active() || !browser_ || !browser ||
      browser->GetIdentifier() != browser_->GetIdentifier())
    return;
  find_.ReportMatchCount(count < 0 ? 0 : static_cast<std::size_t>(count));
}

void AlloyPageTools::OnPdfPrintFinished(const CefString &path, bool ok) {
  if (!active_ || !pdf_completion_ ||
      pdf_generation_ != navigation_generation_ ||
      path.ToWString() != pdf_path_) {
    return;
  }
  if (ok)
    static_cast<void>(output_.NotifySucceeded(profile_id_));
  else
    output_.NotifyFailed(browser_page_tools::PageOutputError::kEngineFailed,
                         profile_id_);
  PdfCompletion completion = std::exchange(pdf_completion_, PdfCompletion{});
  pdf_generation_ = 0;
  pdf_path_.clear();
  completion(ok);
}

bool AlloyPageTools::Shutdown() {
  if (!active_)
    return true;
  active_ = false;
  if (browser_ && find_.active())
    browser_->GetHost()->StopFinding(true);
  find_.EndFind();
  CancelPdf();
  browser_ = nullptr;
  window_ = nullptr;
  profile_id_.clear();
  navigation_generation_ = 0;
  return true;
}

bool AlloyPageTools::IsCurrent(std::uint64_t generation) const noexcept {
  return active_ && generation != 0 && generation == navigation_generation_;
}

bool AlloyPageTools::ApplyZoom() {
  if (!browser_)
    return false;
  browser_->GetHost()->SetZoomLevel(CefZoomLevel(zoom_.factor()));
  return true;
}

void AlloyPageTools::CancelPdf() {
  if (!pdf_completion_)
    return;
  if (output_.state() == browser_page_tools::PageOutputState::kPreparing ||
      output_.state() == browser_page_tools::PageOutputState::kRunning) {
    static_cast<void>(output_.Cancel());
  }
  PdfCompletion completion = std::exchange(pdf_completion_, PdfCompletion{});
  pdf_generation_ = 0;
  pdf_path_.clear();
  completion(false);
  output_.AcknowledgeResult();
}

} // namespace crayon::browser::cef_shell::window
