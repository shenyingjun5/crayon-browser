#include "crayon/browser_markdown/markdown_outline_facts.h"

#include <charconv>
#include <string>

#include "markdown_internal.h"

namespace crayon::browser_markdown {
namespace {

constexpr std::string_view kTruncatedMarker = "...";

/// Per-document walk state; one heading is open at a time.
struct OutlineParserContext final {
  std::uint64_t document_generation = 0;
  std::uint64_t source_revision = 0;
  bool active = false;
  int active_level = 0;
  bool text_over_budget = false;
  std::string text;
  OutlineFactsStatus status = OutlineFactsStatus::kComplete;
  std::vector<OutlineHeading> headings;
};

void AppendFixedHex(std::string* output, std::uint64_t value,
                    std::size_t width) {
  char buffer[16];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
  const std::size_t length = static_cast<std::size_t>(result.ptr - buffer);
  output->append(width - length, '0');
  output->append(buffer, length);
}

std::string MakeAnchor(std::uint64_t document_generation,
                       std::uint64_t source_revision,
                       std::uint32_t ordinal) {
  std::string anchor = "h-";
  anchor.reserve(44);
  AppendFixedHex(&anchor, document_generation, 16);
  anchor.push_back('-');
  AppendFixedHex(&anchor, source_revision, 16);
  anchor.push_back('-');
  AppendFixedHex(&anchor, ordinal, 4);
  return anchor;
}

int OutlineEnterBlock(MD_BLOCKTYPE type, void* detail, void* user_data) {
  if (type != MD_BLOCK_H) {
    return 0;
  }
  auto* context = static_cast<OutlineParserContext*>(user_data);
  if (context->headings.size() >= kMaxOutlineHeadings) {
    // Bounded graceful truncation: the first 512 headings form the outline.
    context->active = false;
    return 0;
  }
  const auto* header = static_cast<const MD_BLOCK_H_DETAIL*>(detail);
  context->active = true;
  context->active_level = header->level;
  context->text_over_budget = false;
  context->text.clear();
  return 0;
}

int OutlineLeaveBlock(MD_BLOCKTYPE type, void* detail, void* user_data) {
  static_cast<void>(detail);
  if (type != MD_BLOCK_H) {
    return 0;
  }
  auto* context = static_cast<OutlineParserContext*>(user_data);
  if (!context->active ||
      context->headings.size() >= kMaxOutlineHeadings) {
    context->active = false;
    return 0;
  }
  OutlineHeading heading;
  heading.level = context->active_level;
  heading.ordinal = static_cast<std::uint32_t>(context->headings.size());
  heading.anchor = MakeAnchor(context->document_generation,
                              context->source_revision, heading.ordinal);
  if (context->text_over_budget) {
    heading.text = context->text.substr(
        0, kMaxOutlineHeadingTextBytes - kTruncatedMarker.size());
    heading.text += kTruncatedMarker;
  } else {
    heading.text = context->text;
  }
  context->headings.push_back(std::move(heading));
  context->active = false;
  context->text.clear();
  return 0;
}

int OutlineEnterSpan(MD_SPANTYPE type, void* detail, void* user_data) {
  static_cast<void>(type);
  static_cast<void>(detail);
  static_cast<void>(user_data);
  return 0;
}

int OutlineLeaveSpan(MD_SPANTYPE type, void* detail, void* user_data) {
  static_cast<void>(type);
  static_cast<void>(detail);
  static_cast<void>(user_data);
  return 0;
}

int OutlineText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
                void* user_data) {
  static_cast<void>(type);
  auto* context = static_cast<OutlineParserContext*>(user_data);
  if (!context->active || context->text_over_budget) {
    return 0;
  }
  const std::size_t bytes = static_cast<std::size_t>(size);
  const std::size_t budget =
      kMaxOutlineHeadingTextBytes - context->text.size();
  if (bytes <= budget) {
    context->text.append(text, bytes);
    return 0;
  }
  // Keep the bounded prefix; the leave-block callback appends the marker.
  context->text.append(text, budget);
  context->text_over_budget = true;
  return 0;
}

}  // namespace

OutlineFactsResult CollectOutlineFacts(const std::string& input,
                                       std::uint64_t document_generation,
                                       std::uint64_t source_revision) {
  OutlineFactsResult result;
  const std::string normalized = internal::NormalizeInput(input);
  OutlineParserContext context;
  context.document_generation = document_generation;
  context.source_revision = source_revision;

  MD_PARSER parser{};
  parser.flags = internal::kParserFlags;
  parser.enter_block = OutlineEnterBlock;
  parser.leave_block = OutlineLeaveBlock;
  parser.enter_span = OutlineEnterSpan;
  parser.leave_span = OutlineLeaveSpan;
  parser.text = OutlineText;

  const int parse_result =
      md_parse(normalized.data(), static_cast<MD_SIZE>(normalized.size()),
               &parser, &context);
  if (parse_result != 0) {
    result.status = OutlineFactsStatus::kParserFailure;
    return result;
  }
  result.status = context.status;
  result.headings = std::move(context.headings);
  return result;
}

}  // namespace crayon::browser_markdown
