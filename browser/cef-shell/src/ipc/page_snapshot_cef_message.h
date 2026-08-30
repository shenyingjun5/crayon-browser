#pragma once

#include <optional>

#include "crayon/browser_engine/snapshot.h"
#include "include/cef_process_message.h"

namespace crayon::browser::cef_shell::snapshot_ipc {

inline constexpr char kRequestMessageName[] = "crayon.snapshot.request.v1";
inline constexpr char kCancelMessageName[] = "crayon.snapshot.cancel.v1";
inline constexpr char kChunkMessageName[] = "crayon.snapshot.chunk.v1";
inline constexpr char kTerminalMessageName[] = "crayon.snapshot.terminal.v1";

CefRefPtr<CefProcessMessage> CreateRequestMessage(
    const browser_engine::SnapshotRequest& request);
CefRefPtr<CefProcessMessage> CreateCancelMessage(
    const browser_engine::SnapshotRequestId& request_id);
CefRefPtr<CefProcessMessage> CreateChunkMessage(
    const browser_engine::SnapshotChunk& chunk);
CefRefPtr<CefProcessMessage> CreateTerminalMessage(
    const browser_engine::SnapshotTerminal& terminal);

std::optional<browser_engine::SnapshotRequest> ReadRequestMessage(
    CefRefPtr<CefProcessMessage> message);
std::optional<browser_engine::SnapshotRequestId> ReadCancelMessage(
    CefRefPtr<CefProcessMessage> message);
std::optional<browser_engine::SnapshotChunk> ReadChunkMessage(
    CefRefPtr<CefProcessMessage> message);
std::optional<browser_engine::SnapshotTerminal> ReadTerminalMessage(
    CefRefPtr<CefProcessMessage> message);

}  // namespace crayon::browser::cef_shell::snapshot_ipc
