#include "crayon/browser_engine/snapshot.h"

int SnapshotHeaderCompiles() {
  return sizeof(crayon::browser_engine::SnapshotStreamSink*) > 0 ? 0 : 1;
}
