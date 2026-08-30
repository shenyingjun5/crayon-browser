#include "crayon/browser_engine/action_discovery.h"

int HeaderActionDiscoveryCompiles() {
  return crayon::browser_engine::kMaxDiscoveryHints > 0 &&
                 crayon::browser_engine::kMaxDiscoveryCandidates > 0
             ? 0
             : 1;
}
