#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "crayon/browser_engine/types.h"
#include "crayon/browser_history/history_codec.h"
#include "crayon/browser_history/history_store.h"
#include "crayon/browser_history_view/history_page_state_machine.h"

namespace crayon::browser::cef_shell::window {

enum class AlloyHistoryResult {
  kSuccess = 0,
  kInvalidInput,
  kNotFound,
  kRejected,
  kEphemeral,
  kStaleNavigation,
  kInactive,
};

class AlloyHistory final {
public:
  struct Callbacks final {
    std::function<bool(const std::string &)> open_new_tab;
  };

  AlloyHistory(browser_engine::ProfileId profile_id, bool ephemeral,
               Callbacks callbacks);

  bool BeginNavigation(std::uint64_t generation);
  AlloyHistoryResult CommitNavigation(std::uint64_t generation,
                                      std::string url, std::string title,
                                      std::uint64_t visited_at);
  AlloyHistoryResult RecordClosedTab(std::string url, std::string title,
                                     std::uint64_t closed_at);
  AlloyHistoryResult RestoreRecentlyClosed();
  std::size_t DeleteRange(std::uint64_t from, std::uint64_t to);
  std::size_t DeleteUrl(const std::string &url);
  bool ClearAll();
  bool Search(const std::string &query);
  bool Import(const std::string &document,
              browser_history::HistoryCodecError *error = nullptr);
  std::string Export() const;
  bool Shutdown();

  const browser_history::HistoryStore &store() const noexcept { return store_; }
  const browser_history_view::HistoryPageStateMachine &view() const noexcept {
    return view_;
  }
  const browser_engine::ProfileId &profile_id() const noexcept {
    return profile_id_;
  }

private:
  bool RefreshAll();
  static std::vector<browser_history_view::HistoryProjection>
  Project(const std::vector<browser_history::HistoryEntry> &entries);

  browser_engine::ProfileId profile_id_;
  Callbacks callbacks_;
  browser_history::HistoryStore store_;
  browser_history_view::HistoryPageStateMachine view_;
  std::uint64_t navigation_generation_ = 0;
  bool navigation_pending_ = false;
  bool active_ = true;
};

} // namespace crayon::browser::cef_shell::window
