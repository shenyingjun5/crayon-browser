// MRT-16: local presentation mode state machine (v1 contract lives in
// docs/current/presentation-contract.md). Sections split on level <= 2
// headings from the MRT-10 facts layer; CommonMark thematic breaks never
// split. The model holds no document content and persists nothing.

#include <cstdint>
#include <vector>

#include "crayon/browser_markdown/markdown_outline_facts.h"

namespace crayon::browser_mdv {

/// Maximum sections tracked (inherits the facts-layer budget).
inline constexpr std::size_t kMaxPresentationSections = 512;

/// Closed presentation phases.
enum class PresentationPhase {
    Normal,
    Presenting,
};

/// Section navigation moves (no wrap: presentation paging does not loop).
enum class PresentationMove {
    First,
    Last,
    Next,
    Previous,
};

/// One section: the starting heading fact plus the document-relative
/// ordinal the UI resolves against the rendered h1..h6 order.
struct MdvSection final {
    /// Section start heading level (1..=6); 0 for a heading-less document.
    int level = 0;
    /// Starting heading ordinal in document order; 0 when the section has
    /// no start heading (content before the first heading is part of the
    /// first section).
    std::uint32_t start_ordinal = 0;
    /// Starting heading text for the accessible name; empty for a
    /// heading-less document.
    std::string title;
};

/// Bounded presentation state machine.
class MdvPresentationModel final {
 public:
    /// Splits the document into sections from outline facts.
    static MdvPresentationModel Build(
        const std::vector<crayon::browser_markdown::OutlineHeading>& headings,
        std::uint64_t source_revision);

    [[nodiscard]]     PresentationPhase phase() const noexcept { return phase_; }

    [[nodiscard]]     std::size_t section_count() const noexcept { return sections_.size(); }

    [[nodiscard]]     std::size_t section_index() const noexcept { return index_; }

    [[nodiscard]]     const std::vector<MdvSection>& sections() const noexcept { return sections_; }

    /// The revision this model was built from; a mismatch means the
    /// document changed and the presentation must have been exited.
    [[nodiscard]]     std::uint64_t source_revision() const noexcept { return revision_; }

    /// Normal -> Presenting. Idempotent: re-entering keeps the current
    /// section index. Fails (returns false) only if already presenting.
    bool Enter();

    /// Presenting -> Normal. Idempotent.
    bool Exit();

    /// Forces Exit and resets the index; called on document revision change.
    void OnDocumentChanged();

    /// Bounded section navigation. No-op in Normal phase; clamped at both
    /// ends (no wrap). Returns the new index.
    std::size_t Move(PresentationMove move);

    /// Jumps to a section index, clamped to the valid range. No-op in
    /// Normal phase.
    std::size_t GoTo(std::size_t index);

 private:
    MdvPresentationModel() = default;

    std::vector<MdvSection> sections_;
    PresentationPhase phase_ = PresentationPhase::Normal;
    std::size_t index_ = 0;
    std::uint64_t revision_ = 0;
};

}  // namespace crayon::browser_mdv
