// MRT-10 outline model: normalized tree depth, keyboard navigation bounds,
// accessible labels and graceful empty handling.

#include "crayon/browser_mdv/mdv_outline.h"

#include <cstdlib>
#include <iostream>

namespace {

using crayon::browser_markdown::OutlineFactsResult;
using crayon::browser_markdown::CollectOutlineFacts;
using crayon::browser_mdv::MdvOutlineEntry;
using crayon::browser_mdv::MdvOutlineModel;
using crayon::browser_mdv::OutlineMove;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << "CHECK failed: " #condition " at line "  \
                << __LINE__ << std::endl;                   \
      return EXIT_FAILURE;                                  \
    }                                                       \
  } while (false)

MdvOutlineModel BuildFrom(const std::string& markdown) {
  const OutlineFactsResult facts = CollectOutlineFacts(markdown, 0x1, 0x2);
  return MdvOutlineModel::Build(facts.headings);
}

int CheckNormalizedDepths() {
  const auto model = BuildFrom(
      "# A\n\n## B\n\n### C\n\n## D\n\n#### E\n\n# F\n\n### G\n\n");
  CHECK(model.size() == 7u);
  const int expected_depths[] = {0, 1, 2, 1, 2, 0, 1};
  for (std::size_t index = 0; index < model.size(); ++index) {
    CHECK(model.entries()[index].tree_depth == expected_depths[index]);
  }
  return EXIT_SUCCESS;
}

int CheckLevelJumpFlattensToNestByOne() {
  const auto model = BuildFrom("# A\n\n### C\n\n#### D\n\n## B\n\n");
  CHECK(model.size() == 4u);
  CHECK(model.entries()[0].tree_depth == 0);
  // h1 -> h3 jumps two levels but nests by one only.
  CHECK(model.entries()[1].tree_depth == 1);
  CHECK(model.entries()[2].tree_depth == 2);
  CHECK(model.entries()[3].tree_depth == 1);
  return EXIT_SUCCESS;
}

int CheckKeyboardNavigationBounds() {
  const auto model = BuildFrom("# A\n\n## B\n\n## C\n\n");
  CHECK(model.size() == 3u);
  CHECK(model.Move(OutlineMove::First, 2) == 0u);
  CHECK(model.Move(OutlineMove::Last, 0) == 2u);
  CHECK(model.Move(OutlineMove::Next, 0) == 1u);
  CHECK(model.Move(OutlineMove::Next, 2) == 2u);   // clamped at end
  CHECK(model.Move(OutlineMove::Previous, 2) == 1u);
  CHECK(model.Move(OutlineMove::Previous, 0) == 0u);  // clamped at start
  // Out-of-range index behaves as "none selected".
  CHECK(model.Move(OutlineMove::First, 99) == 0u);
  CHECK(model.Move(OutlineMove::Next, 99) == 2u);
  CHECK(model.Move(OutlineMove::Previous, 99) == 0u);
  return EXIT_SUCCESS;
}

int CheckAccessibleLabelCarriesTextAndLevel() {
  const auto model = BuildFrom("## Setup\n\n");
  CHECK(model.AccessibleLabel(0) == "Setup (level 2)");
  CHECK(model.AccessibleLabel(1).empty());
  return EXIT_SUCCESS;
}

int CheckEmptyAndSingleEntryModels() {
  const auto empty = BuildFrom("plain text only\n\n");
  CHECK(empty.empty());
  CHECK(empty.Move(OutlineMove::Next, 0) == 0u);
  CHECK(empty.AccessibleLabel(0).empty());

  const auto single = BuildFrom("# Only\n\n");
  CHECK(single.size() == 1u);
  CHECK(single.Move(OutlineMove::Next, 0) == 0u);
  CHECK(single.Move(OutlineMove::Previous, 0) == 0u);
  CHECK(single.entries()[0].tree_depth == 0);
  return EXIT_SUCCESS;
}

int CheckOrdinalMatchesRenderedHeadingOrder() {
  // Ordinals must be 0..n-1 in document order so the page can resolve
  // scroll targets via querySelectorAll('h1,h2,h3,h4,h5,h6')[ordinal].
  const auto model = BuildFrom("## Two\n\n# One\n\n### Three\n\n");
  CHECK(model.size() == 3u);
  for (std::size_t index = 0; index < model.size(); ++index) {
    CHECK(model.entries()[index].ordinal == index);
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main() {
  return CheckNormalizedDepths() + CheckLevelJumpFlattensToNestByOne() +
         CheckKeyboardNavigationBounds() +
         CheckAccessibleLabelCarriesTextAndLevel() +
         CheckEmptyAndSingleEntryModels() +
         CheckOrdinalMatchesRenderedHeadingOrder();
}
