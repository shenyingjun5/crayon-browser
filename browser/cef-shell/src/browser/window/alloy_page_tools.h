#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "crayon/browser_page_tools/page_tools.h"
#include "include/cef_browser.h"
#include "include/cef_find_handler.h"
#include "include/views/cef_window.h"

namespace crayon::browser::cef_shell::window {

struct AlloyPageToolCapabilities final {
  bool find = true;
  bool zoom = true;
  bool fullscreen = true;
  bool system_print = true;
  bool print_to_pdf = true;
  bool picture_in_picture = false;
};

/// CEF Alloy adapter for commands scoped to one current content view.
/// Thread contract: CEF UI thread only.
class AlloyPageTools final : public CefFindHandler, public CefPdfPrintCallback {
public:
  using PdfCompletion = std::function<void(bool)>;

  AlloyPageTools(CefRefPtr<CefBrowser> browser, CefRefPtr<CefWindow> window,
                 std::string profile_id);

  bool OnNavigation(std::uint64_t generation);
  bool StartFind(std::uint64_t generation, const std::string &query,
                 bool case_sensitive);
  bool UpdateFind(std::uint64_t generation, const std::string &query);
  bool FindNext(std::uint64_t generation);
  bool FindPrevious(std::uint64_t generation);
  bool EndFind(std::uint64_t generation);

  bool SetZoom(std::uint64_t generation, int factor);
  bool ZoomIn(std::uint64_t generation);
  bool ZoomOut(std::uint64_t generation);
  bool ResetZoom(std::uint64_t generation);

  bool EnterFullscreen(std::uint64_t generation);
  bool ExitFullscreen(std::uint64_t generation);
  bool OnContentFullscreenChange(bool fullscreen);
  void OnWindowFullscreenTransition(bool is_completed);

  bool Print(std::uint64_t generation);
  bool PrintToPdf(std::uint64_t generation, const std::wstring &authorized_path,
                  const std::string &suggested_filename,
                  PdfCompletion completion);
  void AcknowledgeOutput();

  void OnFindResult(CefRefPtr<CefBrowser> browser, int identifier, int count,
                    const CefRect &selection_rect, int active_match_ordinal,
                    bool final_update) override;
  void OnPdfPrintFinished(const CefString &path, bool ok) override;

  const AlloyPageToolCapabilities &capabilities() const noexcept {
    return capabilities_;
  }
  const browser_page_tools::FindBarController &find() const noexcept {
    return find_;
  }
  const browser_page_tools::ZoomController &zoom() const noexcept {
    return zoom_;
  }
  const browser_page_tools::FullscreenController &fullscreen() const noexcept {
    return fullscreen_;
  }
  const browser_page_tools::PageOutputJobController &output() const noexcept {
    return output_;
  }
  bool Shutdown();

private:
  bool IsCurrent(std::uint64_t generation) const noexcept;
  bool ApplyZoom();
  void CancelPdf();

  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefWindow> window_;
  std::string profile_id_;
  AlloyPageToolCapabilities capabilities_;
  browser_page_tools::FindBarController find_;
  browser_page_tools::ZoomController zoom_;
  browser_page_tools::FullscreenController fullscreen_;
  browser_page_tools::PageOutputJobController output_;
  PdfCompletion pdf_completion_;
  std::wstring pdf_path_;
  std::uint64_t navigation_generation_ = 0;
  std::uint64_t pdf_generation_ = 0;
  bool active_ = true;

  IMPLEMENT_REFCOUNTING(AlloyPageTools);
};

} // namespace crayon::browser::cef_shell::window
