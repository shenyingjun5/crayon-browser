#include "crayon/browser_markdown/markdown_extension_facts.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <utility>

#include "markdown_internal.h"

namespace crayon::browser_markdown {
namespace {

bool IsKnownKind(ExtensionNodeKind kind) {
  switch (kind) {
    case ExtensionNodeKind::kInline:
    case ExtensionNodeKind::kBlock:
    case ExtensionNodeKind::kFence:
    case ExtensionNodeKind::kContainer:
      return true;
  }
  return false;
}

bool IsValidSelection(const std::vector<ExtensionMatcher>& selection) {
  if (selection.size() > kMaxExtensionMatchers) {
    return false;
  }
  for (std::size_t i = 0; i < selection.size(); ++i) {
    if (!IsKnownKind(selection[i].kind) ||
        !IsValidExtensionMatcherToken(selection[i].token)) {
      return false;
    }
    for (std::size_t earlier = 0; earlier < i; ++earlier) {
      if (selection[earlier].kind == selection[i].kind &&
          selection[earlier].token == selection[i].token) {
        return false;
      }
    }
  }
  return true;
}

void AppendFixedHex(std::string* output, std::uint64_t value,
                    std::size_t width) {
  char buffer[16];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
  const std::size_t length = static_cast<std::size_t>(result.ptr - buffer);
  output->append(width - length, '0');
  output->append(buffer, length);
}

std::string MakeNodeId(std::uint64_t document_generation,
                       std::uint64_t source_revision,
                       std::size_t candidate_ordinal) {
  std::string node_id = "n-";
  node_id.reserve(52);
  AppendFixedHex(&node_id, document_generation, 16);
  node_id.push_back('-');
  AppendFixedHex(&node_id, source_revision, 16);
  node_id.push_back('-');
  AppendFixedHex(&node_id, static_cast<std::uint64_t>(candidate_ordinal), 16);
  return node_id;
}

struct ActiveFence {
  bool open = false;
  bool selected = false;
  bool source_over_budget = false;
  std::size_t candidate_ordinal = 0;
  std::string matcher;
  std::string source;
};

struct FactParserContext {
  std::vector<std::string> fence_matchers;
  std::uint64_t document_generation = 0;
  std::uint64_t source_revision = 0;
  std::size_t next_candidate_ordinal = 0;
  std::size_t total_source_bytes = 0;
  ExtensionFactsStatus status = ExtensionFactsStatus::kComplete;
  ActiveFence active;
  std::vector<ExtensionNode> nodes;
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* user_data) {
  if (type != MD_BLOCK_CODE) {
    return 0;
  }
  auto* context = static_cast<FactParserContext*>(user_data);
  const auto* code = static_cast<const MD_BLOCK_CODE_DETAIL*>(detail);
  context->active = {};
  context->active.open = true;
  context->active.candidate_ordinal = context->next_candidate_ordinal++;
  if (code == nullptr || code->fence_char == 0 || code->info.text == nullptr ||
      code->info.size == 0 || code->info.size > kMaxExtensionMatcherBytes) {
    return 0;
  }
  context->active.matcher.assign(code->info.text, code->info.size);
  context->active.selected = std::binary_search(context->fence_matchers.begin(),
                                                context->fence_matchers.end(),
                                                context->active.matcher);
  if (context->active.selected && context->nodes.size() >= kMaxExtensionNodes) {
    context->active.selected = false;
    context->status = ExtensionFactsStatus::kBudgetExceeded;
  }
  return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void* detail, void* user_data) {
  static_cast<void>(detail);
  if (type != MD_BLOCK_CODE) {
    return 0;
  }
  auto* context = static_cast<FactParserContext*>(user_data);
  ActiveFence active = std::move(context->active);
  context->active = {};
  if (!active.open || !active.selected) {
    return 0;
  }
  if (active.source_over_budget ||
      context->nodes.size() >= kMaxExtensionNodes ||
      active.source.size() >
          kMaxTotalExtensionSourceBytes - context->total_source_bytes) {
    context->status = ExtensionFactsStatus::kBudgetExceeded;
    return 0;
  }

  ExtensionNode node;
  node.kind = ExtensionNodeKind::kFence;
  node.node_id = MakeNodeId(context->document_generation,
                            context->source_revision, active.candidate_ordinal);
  node.matcher = std::move(active.matcher);
  node.source_utf8 = std::move(active.source);
  node.source_bytes = node.source_utf8.size();
  node.source_revision = context->source_revision;
  context->total_source_bytes += node.source_bytes;
  context->nodes.push_back(std::move(node));
  return 0;
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* user_data) {
  static_cast<void>(type);
  static_cast<void>(detail);
  static_cast<void>(user_data);
  return 0;
}

int LeaveSpan(MD_SPANTYPE type, void* detail, void* user_data) {
  static_cast<void>(type);
  static_cast<void>(detail);
  static_cast<void>(user_data);
  return 0;
}

int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* user_data) {
  auto* context = static_cast<FactParserContext*>(user_data);
  if (type != MD_TEXT_CODE || !context->active.open ||
      !context->active.selected || context->active.source_over_budget) {
    return 0;
  }
  const std::size_t bytes = static_cast<std::size_t>(size);
  if (bytes > kMaxExtensionSourceBytesPerNode - context->active.source.size()) {
    context->active.source.clear();
    context->active.source_over_budget = true;
    return 0;
  }
  context->active.source.append(text, bytes);
  return 0;
}

ExtensionFactsStatus CollectFenceFacts(
    const std::string& normalized, std::uint64_t document_generation,
    std::uint64_t source_revision,
    const std::vector<ExtensionMatcher>& selection,
    std::vector<ExtensionNode>* nodes) {
  FactParserContext context;
  for (const ExtensionMatcher& matcher : selection) {
    if (matcher.kind == ExtensionNodeKind::kFence) {
      context.fence_matchers.push_back(matcher.token);
    }
  }
  if (context.fence_matchers.empty()) {
    return ExtensionFactsStatus::kComplete;
  }
  std::sort(context.fence_matchers.begin(), context.fence_matchers.end());
  context.document_generation = document_generation;
  context.source_revision = source_revision;

  MD_PARSER parser{};
  parser.flags = internal::kParserFlags;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = Text;
  const int result =
      md_parse(normalized.data(), static_cast<MD_SIZE>(normalized.size()),
               &parser, &context);
  if (result != 0) {
    nodes->clear();
    return ExtensionFactsStatus::kParserFailure;
  }
  *nodes = std::move(context.nodes);
  return context.status;
}

}  // namespace

bool IsValidExtensionMatcherToken(const std::string& token) {
  if (token.empty() || token.size() > kMaxExtensionMatcherBytes) {
    return false;
  }
  const auto is_lower_or_digit = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  };
  if (!is_lower_or_digit(static_cast<unsigned char>(token.front()))) {
    return false;
  }
  return std::all_of(token.begin(), token.end(), [&](char value) {
    const unsigned char c = static_cast<unsigned char>(value);
    return is_lower_or_digit(c) || c == '-' || c == '_' || c == '.' || c == '+';
  });
}

MarkdownRenderPlan RenderMarkdownPlan(
    const std::string& input, std::uint64_t document_generation,
    std::uint64_t source_revision,
    const std::vector<ExtensionMatcher>& selection) {
  MarkdownRenderPlan plan;
  plan.document_generation = document_generation;
  plan.source_revision = source_revision;
  plan.safe_html = RenderMarkdownToSafeHtml(input, &plan.render_status);
  if (plan.render_status != RenderStatus::kOk || selection.empty()) {
    return plan;
  }
  if (!IsValidSelection(selection)) {
    plan.facts_status = ExtensionFactsStatus::kInvalidSelection;
    return plan;
  }

  const std::string normalized = internal::NormalizeInput(input);
  plan.facts_status =
      CollectFenceFacts(normalized, document_generation, source_revision,
                        selection, &plan.extension_nodes);
  return plan;
}

}  // namespace crayon::browser_markdown
