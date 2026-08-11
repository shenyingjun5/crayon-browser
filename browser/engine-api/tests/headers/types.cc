#include "crayon/browser_engine/types.h"

int HeaderTypesCompile() {
  return crayon::browser_engine::kMaxBrowserUrlBytes > 0 ? 0 : 1;
}
