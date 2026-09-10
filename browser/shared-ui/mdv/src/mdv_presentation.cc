#include "crayon/browser_mdv/mdv_presentation.h"

namespace crayon::browser_mdv {
namespace {

constexpr int kSectionSplitMaxLevel = 2;

}  // namespace

MdvPresentationModel MdvPresentationModel::Build(
    const std::vector<crayon::browser_markdown::OutlineHeading>& headings,
    std::uint64_t source_revision) {
    MdvPresentationModel model;
    model.revision_ = source_revision;
    bool saw_split_heading = false;
    for (const auto& heading : headings) {
        if (model.sections_.size() >= kMaxPresentationSections) {
            break;
        }
        if (heading.level <= kSectionSplitMaxLevel) {
            MdvSection section;
            section.level = heading.level;
            section.start_ordinal = heading.ordinal;
            section.title = heading.text;
            model.sections_.push_back(std::move(section));
            saw_split_heading = true;
        }
    }
    if (!saw_split_heading) {
        // Contract §1.4: a document without any level <= 2 heading is a
        // single-section document covering everything.
        MdvSection only;
        only.level = 0;
        only.start_ordinal = 0;
        // Empty title: the UI substitutes the document title for the
        // accessible name (contract §4).
        model.sections_.push_back(std::move(only));
    }
    return model;
}

bool MdvPresentationModel::Enter() {
    if (phase_ == PresentationPhase::Presenting) {
        return false;  // idempotent: keep the current section index
    }
    phase_ = PresentationPhase::Presenting;
    return true;
}

bool MdvPresentationModel::Exit() {
    if (phase_ == PresentationPhase::Normal) {
        return false;  // idempotent
    }
    phase_ = PresentationPhase::Normal;
    return true;
}

void MdvPresentationModel::OnDocumentChanged() {
    phase_ = PresentationPhase::Normal;
    index_ = 0;
}

std::size_t MdvPresentationModel::Move(PresentationMove move) {
    if (phase_ != PresentationPhase::Presenting || sections_.empty()) {
        return index_;
    }
    const std::size_t last = sections_.size() - 1;
    switch (move) {
        case PresentationMove::First:
            index_ = 0;
            break;
        case PresentationMove::Last:
            index_ = last;
            break;
        case PresentationMove::Next:
            if (index_ < last) {
                ++index_;
            }
            break;
        case PresentationMove::Previous:
            if (index_ > 0) {
                --index_;
            }
            break;
    }
    return index_;
}

std::size_t MdvPresentationModel::GoTo(std::size_t index) {
    if (phase_ != PresentationPhase::Presenting || sections_.empty()) {
        return index_;
    }
    const std::size_t last = sections_.size() - 1;
    index_ = index > last ? last : index;
    return index_;
}

}  // namespace crayon::browser_mdv
