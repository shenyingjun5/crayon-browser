#include "crayon/browser_engine/event_sink.h"

int HeaderEventSinkCompiles() {
  return sizeof(crayon::browser_engine::EngineEventSink*) > 0 ? 0 : 1;
}
