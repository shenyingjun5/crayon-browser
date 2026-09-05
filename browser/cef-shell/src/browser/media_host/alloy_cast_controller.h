#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "browser/media_host/media_host_adapter.h"
#include "crayon/browser_cast_view/cast_selection.h"

namespace crayon::browser::cef_shell::media_host {

// UI-thread projection for the candidate Alloy host. Runtime/MediaHostAdapter
// retain ownership of eligibility, drafts, routes and cast sessions.
class AlloyCastController final {
 public:
  using Snapshot = ::crayon::browser_cast_view::CastSelectionSnapshot;
  using SnapshotSink = std::function<void(Snapshot)>;
  using Clock = std::function<std::uint64_t()>;

  AlloyCastController(MediaHostAdapter* adapter, SnapshotSink sink,
                      std::string media_fallback,
                      std::string device_fallback, Clock clock = {});

  bool BindContext(::crayon::browser_cast_view::CastViewContext context);
  void Tick();
  bool HandleIntent(
      const ::crayon::browser_cast_view::CastSelectionIntent& intent);
  bool OpenForMedia(::crayon::browser_cast_view::CastMediaRef media);
  void Shutdown();

  const Snapshot& snapshot() const noexcept { return snapshot_; }

 private:
  using Context = ::crayon::browser_cast_view::CastViewContext;
  using MediaChoice = ::crayon::browser_cast_view::CastMediaChoice;
  using DeviceChoice = ::crayon::browser_cast_view::CastDeviceChoice;

  bool Current(const Context& context) const;
  bool RequestPlayers(std::uint64_t revision, std::uint16_t offset);
  bool RequestDevices(std::optional<std::uint64_t> revision,
                      std::uint16_t offset);
  bool SendDraft(ipc_v2::DraftAction action,
                 std::optional<ipc_v2::DraftMediaRef> media = std::nullopt,
                 std::string device_id = {});
  void DrainPlayers(bool* changed);
  void DrainDrafts(bool* changed);
  void DrainCast(bool* changed);
  void ApplyDraft(const ipc_v2::DraftStateReply& state, bool* changed);
  void RebuildPages();
  void Emit();
  void ResetProjection();

  MediaHostAdapter* adapter_ = nullptr;
  SnapshotSink sink_;
  std::string media_fallback_;
  std::string device_fallback_;
  Clock clock_;
  std::optional<Context> context_;
  Snapshot snapshot_;
  std::vector<MediaChoice> all_media_;
  std::vector<DeviceChoice> all_devices_;
  std::optional<::crayon::browser_cast_view::CastMediaRef> pending_open_media_;
  std::optional<ipc_v2::DraftStateReply> last_draft_;
  std::optional<std::string> code_request_id_;
  std::uint64_t player_revision_ = 0;
  std::uint64_t device_revision_ = 0;
  std::uint16_t requested_player_offset_ = 0;
  std::uint16_t requested_device_offset_ = 0;
  bool player_request_pending_ = false;
  bool device_request_pending_ = false;
  bool draft_request_pending_ = false;
  bool active_ = true;
};

}  // namespace crayon::browser::cef_shell::media_host
