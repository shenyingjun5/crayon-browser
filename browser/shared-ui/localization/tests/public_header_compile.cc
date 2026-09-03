#include "crayon/browser_localization/locale_catalog.h"
#include "crayon/browser_localization/locale_snapshot.h"

int main() {
  const crayon::browser::localization::LocaleSnapshot snapshot;
  const crayon::browser::localization::LocaleCatalog catalog(snapshot.locale);
  return snapshot.tag == "en-US" && catalog.locale() == snapshot.locale ? 0 : 1;
}
