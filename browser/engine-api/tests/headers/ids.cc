#include "crayon/browser_engine/ids.h"

int HeaderIdsCompile() {
  return crayon::browser_engine::kMaxOpaqueIdBytes > 0 ? 0 : 1;
}
