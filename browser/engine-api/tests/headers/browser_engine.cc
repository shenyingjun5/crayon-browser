#include "crayon/browser_engine/browser_engine.h"

int HeaderBrowserEngineCompiles() {
  return sizeof(crayon::browser_engine::EngineEventSink*) > 0 ? 0 : 1;
}
