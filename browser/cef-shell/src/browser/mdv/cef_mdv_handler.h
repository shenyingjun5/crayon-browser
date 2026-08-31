#ifndef CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_
#define CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_

#include <memory>

#include "crayon/browser_mdv/mdv_page.h"
#include "include/cef_scheme.h"

namespace crayon::browser::cef_shell::mdv {

using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv::MdvPageStrings;

/// Thread-safe store of the page snapshot the factory renders per
/// request.  The Browser-process entry controller swaps snapshots after
/// a gated load; the IO thread reads them.
class MdvRuntimeState {
 public:
  MdvRuntimeState();
  explicit MdvRuntimeState(MdvPageSnapshot initial);
  ~MdvRuntimeState();
  MdvRuntimeState(const MdvRuntimeState&) = delete;
  MdvRuntimeState& operator=(const MdvRuntimeState&) = delete;

  void SetSnapshot(MdvPageSnapshot snapshot);
  [[nodiscard]] MdvPageSnapshot snapshot() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Registers the crayon://mdv scheme handler factory (domain "mdv").
// The document body is rendered per request from `state` (initially an empty
// snapshot until a user opens a document); stylesheet and script are fixed.
// `strings` come from the platform string resources.
// Must be called on the CEF UI thread during OnContextInitialized,
// after the new-tab factory.
bool RegisterMdvSchemeHandlerFactory(
    MdvPageStrings strings, const std::shared_ptr<MdvRuntimeState>& state);

}  // namespace crayon::browser::cef_shell::mdv

#endif  // CRAYON_BROWSER_CEF_SHELL_SRC_BROWSER_MDV_CEF_MDV_HANDLER_H_
