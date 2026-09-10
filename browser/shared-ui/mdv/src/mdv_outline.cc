#include "crayon/browser_mdv/mdv_outline.h"

#include <algorithm>
#include <vector>

namespace crayon::browser_mdv {
namespace {

std::string AriaLevelSuffix(int level) {
  return " (level " + std::to_string(level) + ")";
}

}  // namespace

MdvOutlineModel MdvOutlineModel::Build(
    const std::vector<crayon::browser_markdown::OutlineHeading>& headings) {
  MdvOutlineModel model;
  model.entries_.reserve(std::min(headings.size(), kMaxOutlineEntries));
  // Level stack of open ancestors; depth = stack size after closing every
  // ancestor that is not a strict parent. Level jumps (h1 -> h3) nest by
  // one only, so pathological skips cannot create deep trees.
  std::vector<int> open_levels;
  for (const auto& heading : headings) {
    if (model.entries_.size() >= kMaxOutlineEntries) {
      break;
    }
    while (!open_levels.empty() && open_levels.back() >= heading.level) {
      open_levels.pop_back();
    }
    MdvOutlineEntry entry;
    entry.anchor = heading.anchor;
    entry.text = heading.text;
    entry.level = heading.level;
    entry.ordinal = heading.ordinal;
    entry.tree_depth = static_cast<int>(open_levels.size());
    open_levels.push_back(heading.level);
    model.entries_.push_back(std::move(entry));
  }
  return model;
}

std::size_t MdvOutlineModel::Move(OutlineMove move,
                                  std::size_t current_index) const {
  if (entries_.empty()) {
    return 0;
  }
  const std::size_t last = entries_.size() - 1;
  const bool valid = current_index < entries_.size();
  switch (move) {
    case OutlineMove::First:
      return 0;
    case OutlineMove::Last:
      return last;
    case OutlineMove::Next:
      if (!valid || current_index == last) {
        return valid ? current_index : last;
      }
      return current_index + 1;
    case OutlineMove::Previous:
      if (!valid || current_index == 0) {
        return valid ? current_index : 0;
      }
      return current_index - 1;
  }
  return valid ? current_index : 0;
}

std::string MdvOutlineModel::AccessibleLabel(std::size_t index) const {
  if (index >= entries_.size()) {
    return {};
  }
  return entries_[index].text + AriaLevelSuffix(entries_[index].level);
}

}  // namespace crayon::browser_mdv
