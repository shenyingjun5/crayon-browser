#include "crayon/browser_engine/adapter.h"

int HeaderAdapterCompiles() {
  return sizeof(crayon::browser_engine::BrowserEngineAdapter*) > 0 ? 0 : 1;
}
