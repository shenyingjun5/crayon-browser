// MDV-06 contract tests (MD-006): atomic save plan, external
// modification conflict, save-as matrix, failure reporting with
// residual temp paths.
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "crayon/browser_mdv/mdv_save.h"

namespace {

using crayon::browser_mdv_save::FileBaseline;
using crayon::browser_mdv_save::MdvSaveController;
using crayon::browser_mdv_save::SaveIoHooks;
using crayon::browser_mdv_save::SaveKind;
using crayon::browser_mdv_save::SaveState;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << __FILE__ << ':' << __LINE__              \
                << " CHECK failed: " << #condition << '\n'; \
      return false;                                         \
    }                                                       \
  } while (false)

/// Scripted fake IO: per-path (size, mtime) table, recorded operations
/// and injectable failure points.
struct FakeIo {
    std::string temp_written;
    std::string rename_from;
    std::string rename_to;
    bool fail_write = false;
    bool fail_rename = false;
    bool fail_remove = false;
    bool remove_called = false;

    static int stat_fn(const std::string& path, std::uint64_t* size, std::uint64_t* mtime) {
        auto* self = current();
        const auto it = self->files.find(path);
        if (it == self->files.end()) {
            return 1;
        }
        *size = it->second.first;
        *mtime = it->second.second;
        return 0;
    }

    static int write_fn(const std::string& target, const std::string& bytes,
                        std::string* temp_path) {
        auto* self = current();
        if (self->fail_write) {
            return 1;
        }
        self->temp_written = bytes;
        *temp_path = target + ".tmp-4242";
        return 0;
    }

    static int rename_fn(const std::string& from, const std::string& to) {
        auto* self = current();
        if (self->fail_rename) {
            return 1;
        }
        self->rename_from = from;
        self->rename_to = to;
        return 0;
    }

    static int remove_fn(const std::string& path) {
        auto* self = current();
        self->remove_called = true;
        if (self->fail_remove) {
            self->residual = path;
            return 1;
        }
        return 0;
    }

    std::string residual;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> files;

    SaveIoHooks hooks() const {
        return SaveIoHooks{&FakeIo::stat_fn, &FakeIo::write_fn, &FakeIo::rename_fn,
                           &FakeIo::remove_fn};
    }
    static FakeIo* current() { return active_; }
    static FakeIo* active_;
};
FakeIo* FakeIo::active_ = nullptr;

bool HappyWriteBack() {
    FakeIo io;
    FakeIo::active_ = &io;
    io.files["/n/a.md"] = {12, 1000};  // matches the load baseline
    MdvSaveController controller(io.hooks());
    controller.RecordLoadedFile("/n/a.md", 12, 1000);
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "hello world!") ==
          SaveState::kSucceeded);
    CHECK(io.rename_to == "/n/a.md" && io.rename_from == "/n/a.md.tmp-4242");
    CHECK(io.temp_written == "hello world!");
    // Baseline moved to the saved content; mtime unknown until re-stat.
    CHECK(controller.baseline().size == 12);
    CHECK(!controller.baseline().mtime_known);
    CHECK(controller.residual_temp_path().empty());
    return true;
}

bool ExternalModificationConflict() {
    FakeIo io;
    FakeIo::active_ = &io;
    MdvSaveController controller(io.hooks());
    controller.RecordLoadedFile("/n/a.md", 12, 1000);
    // The file changed on disk since load.
    io.files["/n/a.md"] = {99, 2000};
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "mine") ==
          SaveState::kFailedConflict);
    CHECK(io.temp_written.empty());  // nothing written, no half state
    // Restoring the on-disk state lets the retry succeed.
    io.files["/n/a.md"] = {12, 1000};
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "mine") == SaveState::kSucceeded);
    // A disappeared file also conflicts (stat fails).
    io.files.clear();
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "mine") == SaveState::kFailedStat);
    return true;
}

bool SaveAsMatrix() {
    FakeIo io;
    FakeIo::active_ = &io;
    MdvSaveController controller(io.hooks());
    // New location without a baseline: allowed, no conflict check.
    CHECK(controller.Save(SaveKind::kSaveAs, "/n/new.md", "content") == SaveState::kSucceeded);
    // Bad suffix / traversal / control chars / oversize path rejected.
    CHECK(controller.Save(SaveKind::kSaveAs, "/n/new.txt", "c") ==
          SaveState::kFailedInvalidTarget);
    CHECK(controller.Save(SaveKind::kSaveAs, "/n/../x.md", "c") ==
          SaveState::kFailedInvalidTarget);
    std::string bad = "/n/a\x01";
    bad += ".md";
    CHECK(controller.Save(SaveKind::kSaveAs, bad, "c") == SaveState::kFailedInvalidTarget);
    // Write-back without a recorded baseline fails closed.
    MdvSaveController fresh(io.hooks());
    CHECK(fresh.Save(SaveKind::kWriteBack, "/n/a.md", "c") == SaveState::kFailedStat);
    return true;
}

bool FailureReportingAndResidual() {
    FakeIo io;
    FakeIo::active_ = &io;
    io.files["/n/a.md"] = {2, 5};
    MdvSaveController controller(io.hooks());
    controller.RecordLoadedFile("/n/a.md", 2, 5);
    // Temp write failure: explicit error, nothing renamed.
    io.fail_write = true;
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "x") ==
          SaveState::kFailedTempWrite);
    io.fail_write = false;
    // Rename failure with successful cleanup: no residual.
    io.fail_rename = true;
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "x") == SaveState::kFailedRename);
    CHECK(io.remove_called && controller.residual_temp_path().empty());
    // Rename failure with failed cleanup: residual path reported.
    io.fail_remove = true;
    CHECK(controller.Save(SaveKind::kWriteBack, "/n/a.md", "x") ==
          SaveState::kFailedResidual);
    CHECK(controller.residual_temp_path() == "/n/a.md.tmp-4242");
    return true;
}

}  // namespace

int main() {
    const bool ok = HappyWriteBack() && ExternalModificationConflict() && SaveAsMatrix() &&
                    FailureReportingAndResidual();
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::cout << "mdv_save_test passed\n";
    return EXIT_SUCCESS;
}
