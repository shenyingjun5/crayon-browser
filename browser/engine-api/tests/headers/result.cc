#include "crayon/browser_engine/result.h"

int HeaderResultCompiles() {
  return crayon::browser_engine::CommandResult::Accepted().accepted() ? 0 : 1;
}
