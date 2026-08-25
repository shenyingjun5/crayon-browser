// MDV-06: save semantics and external-modification conflict model
// (MDV-01 §9).
//
// Two save paths — write-back to the loaded file and save-as to a new
// location — both through an atomic plan (same-directory temp file,
// then rename).  Before a write-back the target is re-statted and
// compared against the (size, mtime) baseline recorded at load time;
// any drift is a conflict and the model refuses to overwrite silently.
// Failures are explicit: the temp file is cleaned up best-effort and a
// failed cleanup reports the residual path.
//
// All IO goes through injected hooks; the model itself performs no
// syscalls.  Thread contract: single-threaded, UI thread only.
#pragma once

#include <cstdint>
#include <string>

#include "crayon/browser_mdv/mdv_entry_guard.h"

namespace crayon::browser_mdv_save {

/// Closed save paths.
enum class SaveKind { kWriteBack = 0, kSaveAs };

/// Closed save outcomes and failures; stable and data-free except the
/// residual temp path, which the user must be told about.
enum class SaveState {
    kIdle = 0,
    kSucceeded,
    kFailedConflict,      // external modification detected (write-back)
    kFailedInvalidTarget, // save-as target violated the §4 matrix
    kFailedStat,          // pre-save stat failed
    kFailedTempWrite,     // temp file creation/write failed (disk full, ACL)
    kFailedRename,        // rename failed (permissions, cross-device)
    kFailedResidual       // rename failed AND temp cleanup failed
};

/// Injected IO hooks (all return 0 on success, non-zero on failure;
/// `stat_file` writes size/mtime and returns 0 only when the file
/// exists).
struct SaveIoHooks {
    int (*stat_file)(const std::string& path, std::uint64_t* size, std::uint64_t* mtime);
    int (*write_temp)(const std::string& target_path, const std::string& bytes,
                      std::string* temp_path);
    int (*rename)(const std::string& temp_path, const std::string& target_path);
    int (*remove)(const std::string& path);
};

/// The (size, mtime) baseline recorded at load time.
struct FileBaseline {
    std::string path;
    std::uint64_t size = 0;
    std::uint64_t mtime = 0;
    /// False right after a save (mtime unknown until the next stat).
    bool mtime_known = false;
    bool valid = false;
};

/// Save controller over injected IO.
class MdvSaveController final {
public:
    explicit MdvSaveController(const SaveIoHooks& hooks) : hooks_(hooks) {}

    /// Records the baseline for a loaded file (write-back target).
    void RecordLoadedFile(const std::string& path, std::uint64_t size, std::uint64_t mtime);

    /// Clears the baseline (document closed).
    void ClearLoadedFile();

    /// Runs the atomic save.  Write-back re-stats against the baseline
    /// and fails with `kFailedConflict` on drift; save-as validates the
    /// target with the §4 matrix minus the existence requirement and
    /// may overwrite an existing `.md`.  `bytes` is the content to
    /// persist.  On success the baseline moves to the saved file.
    SaveState Save(SaveKind kind, const std::string& target_path, const std::string& bytes);

    SaveState state() const { return state_; }
    const FileBaseline& baseline() const { return baseline_; }
    const std::string& residual_temp_path() const { return residual_temp_path_; }

private:
    SaveState RunAtomicWrite(const std::string& target_path, const std::string& bytes);

    SaveIoHooks hooks_;
    FileBaseline baseline_;
    SaveState state_ = SaveState::kIdle;
    std::string residual_temp_path_;
};

}  // namespace crayon::browser_mdv_save
