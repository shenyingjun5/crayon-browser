#include "browser/media_host/alloy_cast_controller.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <variant>

namespace crayon::browser::cef_shell::media_host {
namespace {

namespace cast_view = ::crayon::browser_cast_view;
namespace mh = ::crayon::cef_shell::ipc::media_host;

bool BoundedText(const std::string& value) {
  if (value.empty() || value.size() > cast_view::kCastSelectionTitleBytes)
    return false;
  for (std::size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i++]);
    std::uint32_t codepoint = first;
    unsigned remaining = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      codepoint = first & 0x1f;
      remaining = 1;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      codepoint = first & 0x0f;
      remaining = 2;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      codepoint = first & 7;
      remaining = 3;
      minimum = 0x10000;
    } else if (first >= 0x80) {
      return false;
    }
    if (remaining > value.size() - i) return false;
    while (remaining-- > 0) {
      const auto next = static_cast<unsigned char>(value[i++]);
      if ((next & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint < 0x20 ||
        (codepoint >= 0x7f && codepoint <= 0x9f) || codepoint == 0x061c ||
        (codepoint >= 0x200e && codepoint <= 0x200f) ||
        (codepoint >= 0x2028 && codepoint <= 0x202e) ||
        (codepoint >= 0x2066 && codepoint <= 0x2069))
      return false;
  }
  return true;
}

bool UiToken(const std::string& value) {
  return !value.empty() && value.size() <= cast_view::kCastSelectionIdBytes &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                  c == '.' || c == ':';
         });
}

cast_view::CastDraftPhase Phase(ipc_v2::DraftPhase value) {
  switch (value) {
    case ipc_v2::DraftPhase::kChoosing:
      return cast_view::CastDraftPhase::kChoosing;
    case ipc_v2::DraftPhase::kConnecting:
      return cast_view::CastDraftPhase::kConnecting;
    case ipc_v2::DraftPhase::kPreparing:
      return cast_view::CastDraftPhase::kPreparing;
    case ipc_v2::DraftPhase::kPrepared:
      return cast_view::CastDraftPhase::kPrepared;
    case ipc_v2::DraftPhase::kCommitting:
      return cast_view::CastDraftPhase::kCommitting;
    case ipc_v2::DraftPhase::kCommitted:
      return cast_view::CastDraftPhase::kCommitted;
    case ipc_v2::DraftPhase::kFailed:
      return cast_view::CastDraftPhase::kFailed;
    case ipc_v2::DraftPhase::kExpired:
      return cast_view::CastDraftPhase::kExpired;
    case ipc_v2::DraftPhase::kCancelled:
      return cast_view::CastDraftPhase::kChoosing;
  }
  return cast_view::CastDraftPhase::kFailed;
}

cast_view::CastSelectionRoute Route(ipc_v2::DraftRoute value) {
  switch (value) {
    case ipc_v2::DraftRoute::kDirect:
      return cast_view::CastSelectionRoute::kDirect;
    case ipc_v2::DraftRoute::kRelay:
      return cast_view::CastSelectionRoute::kRelay;
    case ipc_v2::DraftRoute::kNone:
      return cast_view::CastSelectionRoute::kNone;
  }
  return cast_view::CastSelectionRoute::kNone;
}

cast_view::CastFailureReason Reason(ipc_v2::DraftReason value) {
  using In = ipc_v2::DraftReason;
  using Out = cast_view::CastFailureReason;
  switch (value) {
    case In::kNone: return Out::kNone;
    case In::kCredentials: return Out::kCredentials;
    case In::kProtection: return Out::kProtection;
    case In::kRecognized: return Out::kRecognized;
    case In::kUnrecognized: return Out::kUnrecognized;
    case In::kRedirectRefused: return Out::kRedirectRefused;
    case In::kUpstreamRejected: return Out::kUpstreamRejected;
    case In::kAddressRejected: return Out::kAddressRejected;
    case In::kDns: return Out::kDns;
    case In::kConnect: return Out::kConnect;
    case In::kTimeout: return Out::kTimeout;
    case In::kTransport: return Out::kTransport;
    case In::kInvalidTarget: return Out::kInvalidTarget;
  }
  return Out::kNone;
}

}  // namespace

AlloyCastController::AlloyCastController(MediaHostAdapter* adapter,
                                         SnapshotSink sink,
                                         std::string media_fallback,
                                         std::string device_fallback,
                                         Clock clock)
    : adapter_(adapter),
      sink_(std::move(sink)),
      media_fallback_(std::move(media_fallback)),
      device_fallback_(std::move(device_fallback)),
      clock_(std::move(clock)) {
  if (!BoundedText(media_fallback_)) media_fallback_ = "Video";
  if (!BoundedText(device_fallback_)) device_fallback_ = "Device";
  if (!clock_) {
    clock_ = [] {
      return static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count());
    };
  }
}

bool AlloyCastController::Current(const Context& value) const {
  return context_ && *context_ == value;
}

bool AlloyCastController::BindContext(Context context) {
  if (!active_ || !adapter_ || context.browser_session == 0 ||
      context.profile_id.empty() || context.tab_id == 0 ||
      context.navigation_id == 0 || context.generation == 0)
    return false;
  if (Current(context))
    return true;
  if (context_) {
    if (snapshot_.session_generation)
      static_cast<void>(
          adapter_->RequestStopCast(*snapshot_.session_generation));
    static_cast<void>(adapter_->CloseTab(context_->tab_id,
                                         context_->generation));
  }
  ResetProjection();
  context_ = std::move(context);
  snapshot_.context = *context_;
  const bool admitted = adapter_->AdvanceNavigation(
      context_->tab_id, context_->navigation_id, context_->generation);
  snapshot_.compatible = admitted && adapter_->supports_drafts() &&
                         adapter_->supports_connect();
  if (snapshot_.compatible) {
    static_cast<void>(RequestPlayers(0, 0));
    static_cast<void>(adapter_->RequestDiscovery(mh::DiscoveryAction::kStart));
    static_cast<void>(RequestDevices(std::nullopt, 0));
  }
  Emit();
  if (!admitted) context_.reset();
  return admitted;
}

bool AlloyCastController::RequestPlayers(std::uint64_t revision,
                                         std::uint16_t offset) {
  if (!context_ || player_request_pending_ ||
      offset >= cast_view::kCastSelectionCapacity)
    return false;
  const auto request = adapter_->RequestPlayerPage(
      context_->tab_id, context_->navigation_id, context_->generation,
      revision, offset,
      static_cast<std::uint16_t>(cast_view::kCastSelectionPageSize));
  if (!request)
    return false;
  requested_player_offset_ = offset;
  player_request_pending_ = true;
  return true;
}

bool AlloyCastController::RequestDevices(
    std::optional<std::uint64_t> revision, std::uint16_t offset) {
  if (device_request_pending_ ||
      offset >= cast_view::kCastSelectionCapacity ||
      !adapter_->RequestDevicePage(revision, offset))
    return false;
  requested_device_offset_ = offset;
  device_request_pending_ = true;
  return true;
}

bool AlloyCastController::SendDraft(
    ipc_v2::DraftAction action, std::optional<ipc_v2::DraftMediaRef> media,
    std::string device_id) {
  if (!context_ || draft_request_pending_)
    return false;
  const bool open = action == ipc_v2::DraftAction::kOpen;
  const auto request = adapter_->RequestDraft(
      action, context_->profile_id, context_->tab_id, context_->navigation_id,
      context_->generation, open ? 0 : snapshot_.draft_id,
      open ? 0 : snapshot_.draft_revision, media, std::move(device_id));
  draft_request_pending_ = request.has_value();
  return draft_request_pending_;
}

bool AlloyCastController::HandleIntent(
    const cast_view::CastSelectionIntent& intent) {
  if (!active_ || !Current(intent.context) ||
      intent.view_revision != snapshot_.view_revision ||
      intent.draft_id != snapshot_.draft_id ||
      intent.draft_revision != snapshot_.draft_revision ||
      intent.session_generation != snapshot_.session_generation)
    return false;
  cast_view::CastSelectionPresentation guard;
  guard.BindContext(snapshot_.context);
  if (!guard.Apply(snapshot_) || !guard.Allows(intent, clock_())) return false;
  if (snapshot_.session_generation &&
      intent.kind == cast_view::CastIntentKind::kOpen) {
    snapshot_.picker_open = true;
    Emit();
    return true;
  }
  if (snapshot_.session_generation &&
      intent.kind == cast_view::CastIntentKind::kCancel) {
    snapshot_.picker_open = false;
    Emit();
    return true;
  }
  switch (intent.kind) {
    case cast_view::CastIntentKind::kOpen:
      pending_open_media_.reset();
      return SendDraft(ipc_v2::DraftAction::kOpen);
    case cast_view::CastIntentKind::kOpenForMedia:
      if (!intent.media) return false;
      pending_open_media_ = intent.media;
      if (SendDraft(ipc_v2::DraftAction::kOpen)) return true;
      pending_open_media_.reset();
      return false;
    case cast_view::CastIntentKind::kCancel:
      return SendDraft(ipc_v2::DraftAction::kCancel);
    case cast_view::CastIntentKind::kSelectMedia:
      return intent.media && SendDraft(
          ipc_v2::DraftAction::kSelectMedia,
          ipc_v2::DraftMediaRef{intent.media->instance_id,
                                intent.media->source_revision});
    case cast_view::CastIntentKind::kSelectDevice:
      return SendDraft(ipc_v2::DraftAction::kSelectDevice, std::nullopt,
                       intent.device_id);
    case cast_view::CastIntentKind::kMediaPage:
      if (intent.page_offset >= all_media_.size() && !all_media_.empty())
        return false;
      snapshot_.media_offset = intent.page_offset;
      Emit();
      return true;
    case cast_view::CastIntentKind::kDevicePage:
      if (intent.page_offset >= all_devices_.size() && !all_devices_.empty())
        return false;
      snapshot_.device_offset = intent.page_offset;
      Emit();
      return true;
    case cast_view::CastIntentKind::kRefreshDevices:
      all_devices_.clear();
      device_revision_ = 0;
      device_request_pending_ = false;
      return adapter_->RequestDiscovery(mh::DiscoveryAction::kRefresh) &&
             RequestDevices(std::nullopt, 0);
    case cast_view::CastIntentKind::kLookupCode:
      if (code_request_id_) return false;
      code_request_id_ = adapter_->RequestResolveCastCode(intent.cast_code);
      return code_request_id_.has_value();
    case cast_view::CastIntentKind::kConnectDevice:
      return SendDraft(ipc_v2::DraftAction::kConnect);
    case cast_view::CastIntentKind::kPrepare:
      return SendDraft(ipc_v2::DraftAction::kPrepare);
    case cast_view::CastIntentKind::kConfirmReplacement:
      return SendDraft(ipc_v2::DraftAction::kConfirmReplacement);
    case cast_view::CastIntentKind::kCommit:
      return SendDraft(ipc_v2::DraftAction::kCommit);
    case cast_view::CastIntentKind::kStop:
      return intent.session_generation &&
             adapter_->RequestStopCast(*intent.session_generation);
    case cast_view::CastIntentKind::kPause:
      return intent.session_generation &&
             adapter_->RequestControlCast(*intent.session_generation,
                                          mh::CastControlAction::kPause,
                                          std::nullopt).has_value();
    case cast_view::CastIntentKind::kResume:
      return intent.session_generation &&
             adapter_->RequestControlCast(*intent.session_generation,
                                          mh::CastControlAction::kPlay,
                                          std::nullopt).has_value();
  }
  return false;
}

bool AlloyCastController::OpenForMedia(cast_view::CastMediaRef media) {
  if (!active_ || !context_ || media.instance_id == 0 ||
      media.source_revision == 0) {
    return false;
  }
  cast_view::CastSelectionIntent intent;
  intent.kind = cast_view::CastIntentKind::kOpenForMedia;
  intent.context = snapshot_.context;
  intent.view_revision = snapshot_.view_revision;
  intent.draft_id = snapshot_.draft_id;
  intent.draft_revision = snapshot_.draft_revision;
  intent.session_generation = snapshot_.session_generation;
  intent.media = media;
  return HandleIntent(intent);
}

void AlloyCastController::DrainPlayers(bool* changed) {
  for (auto& page : adapter_->DrainPlayerPages(8)) {
    player_request_pending_ = false;
    if (!context_ || page.offset != requested_player_offset_) continue;
    if (page.status == ipc_v2::PlayerPageStatus::kStale ||
        page.offset != all_media_.size()) {
      all_media_.clear();
      player_revision_ = 0;
      static_cast<void>(RequestPlayers(0, 0));
      *changed = true;
      continue;
    }
    player_revision_ = page.snapshot_revision;
    const bool duplicate_player = std::any_of(
        page.players.begin(), page.players.end(), [&](const auto& player) {
          return std::any_of(all_media_.begin(), all_media_.end(),
                             [&](const auto& existing) {
            return existing.ref.instance_id == player.instance_id;
          }) || std::count_if(page.players.begin(), page.players.end(),
                              [&](const auto& other) {
            return other.instance_id == player.instance_id;
          }) != 1;
        });
    if (duplicate_player) {
      all_media_.clear();
      player_revision_ = 0;
      *changed = true;
      continue;
    }
    for (const auto& player : page.players) {
      if (all_media_.size() >= cast_view::kCastSelectionCapacity) break;
      const bool selectable =
          player.source_kind == ipc_v2::PlayerSourceKind::kHttpUrl &&
          player.has_video && player.visible && !player.eme_encrypted;
      all_media_.push_back(
          {{player.instance_id, player.source_revision},
           BoundedText(player.redacted_origin) ? player.redacted_origin
                                               : media_fallback_,
           selectable});
    }
    if (page.next_offset &&
        *page.next_offset < cast_view::kCastSelectionCapacity)
      static_cast<void>(RequestPlayers(player_revision_, *page.next_offset));
    *changed = true;
  }
}

void AlloyCastController::ApplyDraft(const ipc_v2::DraftStateReply& state,
                                     bool* changed) {
  if (!context_ || state.context.profile_id != context_->profile_id ||
      state.context.tab_id != context_->tab_id ||
      state.context.navigation_id != context_->navigation_id ||
      state.context.tab_generation != context_->generation)
    return;
  last_draft_ = state;
  snapshot_.draft_id = state.draft_id;
  snapshot_.draft_revision = state.draft_revision;
  snapshot_.phase = Phase(state.phase);
  snapshot_.picker_open = state.phase != ipc_v2::DraftPhase::kCancelled;
  snapshot_.device_connected = state.device_connected;
  snapshot_.replacement_confirmation_required =
      state.replacement_confirmation_required;
  snapshot_.route = Route(state.route);
  snapshot_.reason = Reason(state.reason);
  snapshot_.prepared_until_ms = state.prepared_until_ms.value_or(0);
  snapshot_.session_generation = state.session_generation;
  snapshot_.playback_paused = false;
  snapshot_.selected_media.reset();
  if (state.media) {
    const auto found = std::find_if(all_media_.begin(), all_media_.end(),
                                    [&](const auto& media) {
      return media.ref.instance_id == state.media->instance_id &&
             media.ref.source_revision == state.media->source_revision;
    });
    if (found != all_media_.end()) snapshot_.selected_media = *found;
  }
  snapshot_.selected_device.reset();
  const auto device = std::find_if(all_devices_.begin(), all_devices_.end(),
                                   [&](const auto& value) {
    return value.id == state.device_id;
  });
  if (device != all_devices_.end()) snapshot_.selected_device = *device;
  *changed = true;
}

void AlloyCastController::DrainDrafts(bool* changed) {
  for (const auto& state : adapter_->DrainDraftStates(8)) {
    draft_request_pending_ = false;
    ApplyDraft(state, changed);
    if (pending_open_media_ && state.phase == ipc_v2::DraftPhase::kChoosing) {
      const auto media = *pending_open_media_;
      pending_open_media_.reset();
      static_cast<void>(SendDraft(
          ipc_v2::DraftAction::kSelectMedia,
          ipc_v2::DraftMediaRef{media.instance_id, media.source_revision}));
    }
  }
}

void AlloyCastController::DrainCast(bool* changed) {
  for (const auto& message : adapter_->DrainCast(16)) {
    if (const auto* page = std::get_if<mh::DevicePageReply>(&message)) {
      device_request_pending_ = false;
      if (page->offset != requested_device_offset_) continue;
      if (page->offset == 0) {
        all_devices_.clear();
        device_revision_ = page->snapshot_revision;
      }
      if (page->snapshot_revision != device_revision_ ||
          page->offset != all_devices_.size()) {
        all_devices_.clear();
        device_revision_ = 0;
        static_cast<void>(RequestDevices(std::nullopt, 0));
        *changed = true;
        continue;
      }
      const bool invalid_device = std::any_of(
          page->devices.begin(), page->devices.end(), [&](const auto& device) {
            return !UiToken(device.device_id) ||
                   std::any_of(all_devices_.begin(), all_devices_.end(),
                               [&](const auto& existing) {
                     return existing.id == device.device_id;
                   }) ||
                   std::count_if(page->devices.begin(), page->devices.end(),
                                 [&](const auto& other) {
                     return other.device_id == device.device_id;
                   }) != 1;
          });
      if (invalid_device) {
        all_devices_.clear();
        device_revision_ = 0;
        *changed = true;
        continue;
      }
      for (const auto& device : page->devices) {
        if (all_devices_.size() >= cast_view::kCastSelectionCapacity) break;
        all_devices_.push_back(
            {device.device_id,
             BoundedText(device.display_name) ? device.display_name
                                              : device_fallback_,
             device.state == mh::DeviceState::kReady});
      }
      if (page->next_offset &&
          *page->next_offset < cast_view::kCastSelectionCapacity)
        static_cast<void>(RequestDevices(device_revision_, *page->next_offset));
      *changed = true;
      continue;
    }
    if (const auto* code = std::get_if<mh::ResolveCastCodeReply>(&message)) {
      if (!code_request_id_ || code->request_id != *code_request_id_) continue;
      code_request_id_.reset();
      if (code->device && code->device->state == mh::DeviceState::kReady) {
        if (!UiToken(code->device->device_id)) continue;
        auto found = std::find_if(all_devices_.begin(), all_devices_.end(),
                                  [&](const auto& value) {
          return value.id == code->device->device_id;
        });
        if (found == all_devices_.end() &&
            all_devices_.size() < cast_view::kCastSelectionCapacity) {
          all_devices_.push_back(
              {code->device->device_id,
               BoundedText(code->device->display_name)
                   ? code->device->display_name
                   : device_fallback_,
               true});
        }
        static_cast<void>(SendDraft(ipc_v2::DraftAction::kSelectDevice,
                                    std::nullopt, code->device->device_id));
        *changed = true;
      }
      continue;
    }
    if (const auto* events = std::get_if<mh::SessionEventsReply>(&message)) {
      for (const auto& event : events->events) {
        if (!snapshot_.session_generation ||
            event.session_generation != *snapshot_.session_generation)
          continue;
        if (event.phase == mh::SessionPhase::kTerminated) {
          snapshot_.session_generation.reset();
          snapshot_.phase = cast_view::CastDraftPhase::kChoosing;
        }
        snapshot_.playback_paused =
            event.playback == mh::SessionPlayback::kPaused;
        *changed = true;
      }
    }
  }
}

void AlloyCastController::RebuildPages() {
  const auto build_page = [](const auto& all, std::uint16_t* offset,
                             auto* page) {
    if (all.empty()) {
      *offset = 0;
      page->clear();
      return;
    }
    if (*offset >= all.size())
      *offset = static_cast<std::uint16_t>(
          ((all.size() - 1) / cast_view::kCastSelectionPageSize) *
          cast_view::kCastSelectionPageSize);
    const auto end = std::min<std::size_t>(
        all.size(), *offset + cast_view::kCastSelectionPageSize);
    page->assign(all.begin() + *offset, all.begin() + end);
  };
  snapshot_.media_total = static_cast<std::uint16_t>(all_media_.size());
  snapshot_.eligible_count = static_cast<std::uint16_t>(std::count_if(
      all_media_.begin(), all_media_.end(),
      [](const auto& media) { return media.selectable; }));
  snapshot_.device_total = static_cast<std::uint16_t>(all_devices_.size());
  if (last_draft_) {
    snapshot_.selected_media.reset();
    if (last_draft_->media) {
      const auto media = std::find_if(all_media_.begin(), all_media_.end(),
                                      [&](const auto& value) {
        return value.ref.instance_id == last_draft_->media->instance_id &&
               value.ref.source_revision ==
                   last_draft_->media->source_revision;
      });
      if (media != all_media_.end()) snapshot_.selected_media = *media;
    }
    snapshot_.selected_device.reset();
    const auto device = std::find_if(all_devices_.begin(), all_devices_.end(),
                                     [&](const auto& value) {
      return value.id == last_draft_->device_id;
    });
    if (device != all_devices_.end()) snapshot_.selected_device = *device;
  }
  build_page(all_media_, &snapshot_.media_offset, &snapshot_.media);
  build_page(all_devices_, &snapshot_.device_offset, &snapshot_.devices);
}

void AlloyCastController::Emit() {
  if (!context_) return;
  RebuildPages();
  if (snapshot_.view_revision != std::numeric_limits<std::uint64_t>::max())
    ++snapshot_.view_revision;
  if (sink_) sink_(snapshot_);
}

void AlloyCastController::Tick() {
  if (!active_ || !adapter_ || !context_) return;
  adapter_->Tick();
  bool changed = false;
  DrainPlayers(&changed);
  DrainDrafts(&changed);
  DrainCast(&changed);
  const bool compatible = adapter_->supports_drafts() &&
                          adapter_->supports_connect();
  if (snapshot_.compatible != compatible) {
    snapshot_.compatible = compatible;
    if (!compatible) {
      player_request_pending_ = device_request_pending_ = false;
      draft_request_pending_ = false;
      code_request_id_.reset();
      pending_open_media_.reset();
    }
    changed = true;
  }
  if (changed) Emit();
}

void AlloyCastController::ResetProjection() {
  all_media_.clear();
  all_devices_.clear();
  pending_open_media_.reset();
  last_draft_.reset();
  code_request_id_.reset();
  player_revision_ = device_revision_ = 0;
  requested_player_offset_ = requested_device_offset_ = 0;
  player_request_pending_ = device_request_pending_ = false;
  draft_request_pending_ = false;
  snapshot_ = {};
}

void AlloyCastController::Shutdown() {
  if (!active_) return;
  active_ = false;
  if (adapter_) {
    static_cast<void>(adapter_->RequestDiscovery(mh::DiscoveryAction::kStop));
    if (snapshot_.session_generation)
      static_cast<void>(
          adapter_->RequestStopCast(*snapshot_.session_generation));
  }
  if (adapter_ && context_)
    static_cast<void>(adapter_->CloseTab(context_->tab_id,
                                         context_->generation));
  ResetProjection();
  context_.reset();
  sink_ = {};
  adapter_ = nullptr;
}

}  // namespace crayon::browser::cef_shell::media_host
