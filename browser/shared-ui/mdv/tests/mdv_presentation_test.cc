// MRT-16 / MR-010: presentation state machine - section split rules,
// reversible phases, bounded navigation and revision-forced exit.

#include "crayon/browser_mdv/mdv_presentation.h"

#include <cstdlib>
#include <iostream>

namespace {

using crayon::browser_markdown::CollectOutlineFacts;
using crayon::browser_mdv::MdvPresentationModel;
using crayon::browser_mdv::PresentationMove;
using crayon::browser_mdv::PresentationPhase;

#define CHECK(condition)                                    \
  do {                                                      \
    if (!(condition)) {                                     \
      std::cerr << "CHECK failed: " #condition " at line "  \
                << __LINE__ << std::endl;                   \
      return EXIT_FAILURE;                                  \
    }                                                       \
  } while (false)

MdvPresentationModel BuildFrom(const std::string& markdown, std::uint64_t revision) {
    const auto facts = CollectOutlineFacts(markdown, 1, revision);
    return MdvPresentationModel::Build(facts.headings, revision);
}

int CheckSectionSplitRules() {
    const auto model = BuildFrom(
        "# Top\n\nintro\n\n## Sub\n\nbody\n\n### Deep\n\ninner\n\n## Tail\n\n", 1);
    // h3 is intra-section structure: 3 sections, not 4.
    CHECK(model.section_count() == 3u);
    CHECK(model.sections()[0].level == 1);
    CHECK(model.sections()[1].level == 2);
    CHECK(model.sections()[2].level == 2);
    CHECK(model.sections()[0].start_ordinal == 0u);
    // Ordinals count all headings (h3 included): 0,1,2,3 - sections start at 0,1,3.
    CHECK(model.sections()[2].start_ordinal == 3u);
    return EXIT_SUCCESS;
}

int CheckThematicBreakNeverSplits() {
    const auto model = BuildFrom("# A\n\n---\n\n# B\n\n***\n\n", 1);
    CHECK(model.section_count() == 2u);
    return EXIT_SUCCESS;
}

int CheckHeadinglessDocumentIsSingleSection() {
    const auto model = BuildFrom("just plain text\n\nmore text\n", 1);
    CHECK(model.section_count() == 1u);
    CHECK(model.sections()[0].level == 0);
    CHECK(model.sections()[0].title.empty());

    const auto empty = BuildFrom("", 1);
    CHECK(empty.section_count() == 1u);
    return EXIT_SUCCESS;
}

int CheckEnterExitMatrix() {
    auto model = BuildFrom("# A\n\n## B\n\n", 1);
    CHECK(model.Enter());
    CHECK(model.phase() == PresentationPhase::Presenting);
    CHECK(!model.Enter());  // idempotent
    CHECK(model.phase() == PresentationPhase::Presenting);
    CHECK(model.Exit());
    CHECK(model.phase() == PresentationPhase::Normal);
    CHECK(!model.Exit());  // idempotent
    CHECK(model.phase() == PresentationPhase::Normal);
    return EXIT_SUCCESS;
}

int CheckNavigationBoundsAndNoWrap() {
    auto model = BuildFrom("# A\n\n## B\n\n## C\n\n", 1);
    // Normal phase: navigation is a no-op.
    CHECK(model.Move(PresentationMove::Next) == 0u);
    CHECK(model.GoTo(2) == 0u);
    CHECK(model.Enter());
    CHECK(model.Move(PresentationMove::Next) == 1u);
    CHECK(model.Move(PresentationMove::Next) == 2u);
    CHECK(model.Move(PresentationMove::Next) == 2u);  // clamped at end
    CHECK(model.Move(PresentationMove::Previous) == 1u);
    CHECK(model.Move(PresentationMove::First) == 0u);
    CHECK(model.Move(PresentationMove::Previous) == 0u);  // clamped at start
    CHECK(model.Move(PresentationMove::Last) == 2u);
    CHECK(model.GoTo(99) == 2u);  // clamped
    CHECK(model.GoTo(0) == 0u);
    return EXIT_SUCCESS;
}

int CheckRevisionChangeForcesExitAndResetsIndex() {
    auto model = BuildFrom("# A\n\n## B\n\n", 1);
    CHECK(model.Enter());
    CHECK(model.Move(PresentationMove::Next) == 1u);
    CHECK(model.source_revision() == 1u);

    // The document was edited: rebuild with a new revision.
    auto changed = BuildFrom("# A v2\n\n## B\n\n## C\n\n", 2);
    CHECK(changed.phase() == PresentationPhase::Normal);
    CHECK(changed.section_index() == 0u);
    CHECK(changed.source_revision() == 2u);
    return EXIT_SUCCESS;
}

int CheckReEnterKeepsSectionIndex() {
    auto model = BuildFrom("# A\n\n## B\n\n## C\n\n", 1);
    CHECK(model.Enter());
    CHECK(model.Move(PresentationMove::Last) == 2u);
    CHECK(!model.Enter());  // idempotent: index kept
    CHECK(model.section_index() == 2u);
    CHECK(model.Exit());
    CHECK(model.Enter());
    CHECK(model.section_index() == 2u);  // re-enter does not reset per contract
    return EXIT_SUCCESS;
}

int CheckSingleSectionPresentation() {
    auto model = BuildFrom("no headings at all\n", 1);
    CHECK(model.Enter());
    CHECK(model.Move(PresentationMove::Next) == 0u);
    CHECK(model.Move(PresentationMove::Last) == 0u);
    CHECK(model.GoTo(5) == 0u);
    return EXIT_SUCCESS;
}

}  // namespace

int main() {
    return CheckSectionSplitRules() + CheckThematicBreakNeverSplits() +
           CheckHeadinglessDocumentIsSingleSection() + CheckEnterExitMatrix() +
           CheckNavigationBoundsAndNoWrap() +
           CheckRevisionChangeForcesExitAndResetsIndex() +
           CheckReEnterKeepsSectionIndex() + CheckSingleSectionPresentation();
}
