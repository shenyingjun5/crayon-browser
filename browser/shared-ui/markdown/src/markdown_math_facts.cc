#include "crayon/browser_markdown/markdown_math_facts.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <string_view>
#include <utility>

#include "markdown_internal.h"

namespace crayon::browser_markdown {
namespace {

struct Candidate final {
  ExtensionNodeKind kind = ExtensionNodeKind::kInline;
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string source;
  std::string fallback;
};

bool IsAsciiWord(char value) {
  const unsigned char c = static_cast<unsigned char>(value);
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

bool IsAsciiSpace(char value) { return value == ' ' || value == '\t'; }

bool IsLineBlank(std::string_view line) {
  return std::all_of(line.begin(), line.end(), IsAsciiSpace);
}

bool IsEscaped(std::string_view input, std::size_t offset) {
  std::size_t slashes = 0;
  while (offset > slashes && input[offset - slashes - 1] == '\\') {
    ++slashes;
  }
  return slashes % 2 != 0;
}

std::size_t Utf8CodePointBytes(unsigned char lead) {
  if ((lead & 0x80) == 0) {
    return 1;
  }
  if ((lead & 0xe0) == 0xc0) {
    return 2;
  }
  if ((lead & 0xf0) == 0xe0) {
    return 3;
  }
  return 4;
}

bool WithinMathTokenBudget(std::string_view source) {
  std::size_t tokens = 0;
  for (std::size_t index = 0; index < source.size();) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    if (value < 0x80 && std::isspace(value) != 0) {
      ++index;
      continue;
    }
    if (++tokens > kMaxMathTokens) {
      return false;
    }
    if (source[index] == '\\') {
      ++index;
      if (index == source.size()) {
        continue;
      }
      const unsigned char escaped = static_cast<unsigned char>(source[index]);
      if (std::isalpha(escaped) != 0 || escaped == '@') {
        while (index < source.size()) {
          const unsigned char command =
              static_cast<unsigned char>(source[index]);
          if (std::isalpha(command) == 0 && command != '@') {
            break;
          }
          ++index;
        }
      } else {
        index += Utf8CodePointBytes(escaped);
      }
      continue;
    }
    index += Utf8CodePointBytes(value);
  }
  return true;
}

std::size_t LineEnd(std::string_view input, std::size_t start) {
  const std::size_t end = input.find('\n', start);
  return end == std::string_view::npos ? input.size() : end;
}

std::size_t NextLine(std::string_view input, std::size_t end) {
  return end < input.size() ? end + 1 : input.size();
}

std::string_view TrimAscii(std::string_view value) {
  std::size_t left = 0;
  while (left < value.size() && IsAsciiSpace(value[left])) {
    ++left;
  }
  std::size_t right = value.size();
  while (right > left && IsAsciiSpace(value[right - 1])) {
    --right;
  }
  return value.substr(left, right - left);
}

void CollectBlockCandidates(std::string_view input,
                            std::vector<Candidate>* candidates,
                            std::vector<bool>* occupied) {
  std::size_t line_start = 0;
  while (line_start < input.size()) {
    const std::size_t line_end = LineEnd(input, line_start);
    const std::string_view line =
        input.substr(line_start, line_end - line_start);
    std::size_t indent = 0;
    while (indent < line.size() && line[indent] == ' ' && indent < 4) {
      ++indent;
    }
    if (indent > 3 || line.substr(indent, 2) != "$$") {
      line_start = NextLine(input, line_end);
      continue;
    }

    const std::string_view after_open = line.substr(indent + 2);
    if (IsLineBlank(after_open)) {
      const std::size_t content_begin = NextLine(input, line_end);
      std::size_t closing_start = content_begin;
      bool crossed_blank = false;
      bool found = false;
      while (closing_start < input.size()) {
        const std::size_t closing_end = LineEnd(input, closing_start);
        const std::string_view closing =
            input.substr(closing_start, closing_end - closing_start);
        std::size_t closing_indent = 0;
        while (closing_indent < closing.size() &&
               closing[closing_indent] == ' ' && closing_indent < 4) {
          ++closing_indent;
        }
        if (closing_indent <= 3 && closing.substr(closing_indent, 2) == "$$" &&
            IsLineBlank(closing.substr(closing_indent + 2))) {
          found = true;
          const std::size_t content_end =
              closing_start > content_begin ? closing_start - 1 : closing_start;
          if (!crossed_blank && content_end > content_begin) {
            Candidate candidate;
            candidate.kind = ExtensionNodeKind::kBlock;
            candidate.begin = line_start;
            candidate.end = closing_end;
            candidate.source = std::string(
                input.substr(content_begin, content_end - content_begin));
            candidate.fallback = std::string(
                input.substr(candidate.begin, candidate.end - candidate.begin));
            candidates->push_back(std::move(candidate));
            std::fill(occupied->begin() + line_start,
                      occupied->begin() + closing_end, true);
          }
          line_start = NextLine(input, closing_end);
          break;
        }
        if (IsLineBlank(closing)) {
          crossed_blank = true;
        }
        closing_start = NextLine(input, closing_end);
      }
      if (found) {
        continue;
      }
    } else {
      std::size_t closing = std::string_view::npos;
      for (std::size_t index = indent + 2; index + 1 < line.size(); ++index) {
        if (line[index] == '$' && line[index + 1] == '$' &&
            !IsEscaped(line, index)) {
          if (closing != std::string_view::npos) {
            closing = std::string_view::npos;
            break;
          }
          closing = index;
          ++index;
        }
      }
      if (closing != std::string_view::npos &&
          IsLineBlank(line.substr(closing + 2))) {
        const std::string_view source =
            TrimAscii(line.substr(indent + 2, closing - indent - 2));
        if (!source.empty()) {
          Candidate candidate;
          candidate.kind = ExtensionNodeKind::kBlock;
          candidate.begin = line_start;
          candidate.end = line_end;
          candidate.source = std::string(source);
          candidate.fallback = std::string(line);
          candidates->push_back(std::move(candidate));
          std::fill(occupied->begin() + line_start,
                    occupied->begin() + line_end, true);
        }
      }
    }
    line_start = NextLine(input, line_end);
  }
}

void CollectInlineCandidates(std::string_view input,
                             const std::vector<bool>& occupied,
                             std::vector<Candidate>* candidates) {
  std::size_t index = 0;
  while (index < input.size()) {
    if (occupied[index] || input[index] != '$' || IsEscaped(input, index) ||
        (index + 1 < input.size() && input[index + 1] == '$') ||
        (index > 0 && IsAsciiWord(input[index - 1])) ||
        index + 1 >= input.size() ||
        std::isspace(static_cast<unsigned char>(input[index + 1])) != 0) {
      ++index;
      continue;
    }
    std::size_t closing = index + 1;
    bool invalid = false;
    for (; closing < input.size() && input[closing] != '\n'; ++closing) {
      if (occupied[closing]) {
        invalid = true;
        break;
      }
      if (input[closing] != '$' || IsEscaped(input, closing)) {
        continue;
      }
      if (closing + 1 < input.size() && input[closing + 1] == '$') {
        invalid = true;
        break;
      }
      break;
    }
    if (!invalid && closing < input.size() && input[closing] == '$' &&
        closing > index + 1 &&
        std::isspace(static_cast<unsigned char>(input[closing - 1])) == 0 &&
        (closing + 1 == input.size() || !IsAsciiWord(input[closing + 1]))) {
      Candidate candidate;
      candidate.kind = ExtensionNodeKind::kInline;
      candidate.begin = index;
      candidate.end = closing + 1;
      candidate.source =
          std::string(input.substr(index + 1, closing - index - 1));
      candidate.fallback =
          std::string(input.substr(index, closing - index + 1));
      candidates->push_back(std::move(candidate));
      index = closing + 1;
      continue;
    }
    ++index;
  }
}

bool CandidateWithinBudgets(const Candidate& candidate) {
  if (candidate.source.empty() ||
      candidate.source.size() > kMaxMathSourceBytes) {
    return false;
  }
  if (!WithinMathTokenBudget(candidate.source)) {
    return false;
  }
  std::size_t depth = 0;
  for (std::size_t index = 0; index < candidate.source.size(); ++index) {
    const char value = candidate.source[index];
    if (value == '\\') {
      ++index;
      continue;
    }
    if (value == '{' && ++depth > kMaxMathBraceDepth) {
      return false;
    }
    if (value == '}' && depth > 0) {
      --depth;
    }
  }
  return true;
}

void AppendFixedHex(std::string* output, std::uint64_t value) {
  char buffer[16];
  const auto converted =
      std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
  const std::size_t length = static_cast<std::size_t>(converted.ptr - buffer);
  output->append(16 - length, '0');
  output->append(buffer, length);
}

std::string MakeMathNodeId(std::uint64_t document_generation,
                           std::uint64_t source_revision, std::size_t ordinal) {
  std::string id = "m-";
  AppendFixedHex(&id, document_generation);
  id.push_back('-');
  AppendFixedHex(&id, source_revision);
  id.push_back('-');
  AppendFixedHex(&id, static_cast<std::uint64_t>(ordinal));
  return id;
}

struct ActiveMath final {
  bool open = false;
  ExtensionNodeKind kind = ExtensionNodeKind::kInline;
  bool root_block = false;
  std::size_t first_source_offset = std::numeric_limits<std::size_t>::max();
};

struct ParserContext final {
  const char* source = nullptr;
  std::size_t source_size = 0;
  std::size_t container_depth = 0;
  ActiveMath active;
  std::vector<std::pair<ExtensionNodeKind, std::size_t>> confirmed;
};

bool IsContainer(MD_BLOCKTYPE type) {
  return type == MD_BLOCK_QUOTE || type == MD_BLOCK_UL || type == MD_BLOCK_OL ||
         type == MD_BLOCK_LI || type == MD_BLOCK_TABLE ||
         type == MD_BLOCK_THEAD || type == MD_BLOCK_TBODY ||
         type == MD_BLOCK_TR || type == MD_BLOCK_TH || type == MD_BLOCK_TD ||
         type == MD_BLOCK_CODE;
}

int EnterBlock(MD_BLOCKTYPE type, void*, void* user_data) {
  auto* context = static_cast<ParserContext*>(user_data);
  if (IsContainer(type)) {
    ++context->container_depth;
  }
  return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void*, void* user_data) {
  auto* context = static_cast<ParserContext*>(user_data);
  if (IsContainer(type) && context->container_depth > 0) {
    --context->container_depth;
  }
  return 0;
}

int EnterSpan(MD_SPANTYPE type, void*, void* user_data) {
  if (type != MD_SPAN_LATEXMATH && type != MD_SPAN_LATEXMATH_DISPLAY) {
    return 0;
  }
  auto* context = static_cast<ParserContext*>(user_data);
  context->active = {};
  context->active.open = true;
  context->active.kind = type == MD_SPAN_LATEXMATH ? ExtensionNodeKind::kInline
                                                   : ExtensionNodeKind::kBlock;
  context->active.root_block = context->container_depth == 0;
  return 0;
}

int LeaveSpan(MD_SPANTYPE type, void*, void* user_data) {
  if (type != MD_SPAN_LATEXMATH && type != MD_SPAN_LATEXMATH_DISPLAY) {
    return 0;
  }
  auto* context = static_cast<ParserContext*>(user_data);
  if (context->active.open &&
      context->active.first_source_offset !=
          std::numeric_limits<std::size_t>::max() &&
      (context->active.kind != ExtensionNodeKind::kBlock ||
       context->active.root_block)) {
    context->confirmed.emplace_back(context->active.kind,
                                    context->active.first_source_offset);
  }
  context->active = {};
  return 0;
}

int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE, void* user_data) {
  auto* context = static_cast<ParserContext*>(user_data);
  if (type != MD_TEXT_LATEXMATH || !context->active.open || text == nullptr) {
    return 0;
  }
  const auto source_address = reinterpret_cast<std::uintptr_t>(context->source);
  const auto text_address = reinterpret_cast<std::uintptr_t>(text);
  if (text_address < source_address ||
      text_address - source_address >= context->source_size) {
    return 0;
  }
  context->active.first_source_offset =
      std::min(context->active.first_source_offset,
               static_cast<std::size_t>(text_address - source_address));
  return 0;
}

}  // namespace

MathFactsResult CollectMathExtensionFacts(const std::string& input,
                                          std::uint64_t document_generation,
                                          std::uint64_t source_revision) {
  MathFactsResult result;
  if (input.size() > kMaxInputBytes) {
    result.render_status = RenderStatus::kInputTooLarge;
    return result;
  }
  result.normalized_markdown = internal::NormalizeInput(input);
  if (!IsValidUtf8(result.normalized_markdown)) {
    result.render_status = RenderStatus::kInvalidUtf8;
    result.normalized_markdown.clear();
    return result;
  }

  std::vector<Candidate> candidates;
  std::vector<bool> occupied(result.normalized_markdown.size(), false);
  CollectBlockCandidates(result.normalized_markdown, &candidates, &occupied);
  CollectInlineCandidates(result.normalized_markdown, occupied, &candidates);
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right) {
              return left.begin < right.begin;
            });
  if (candidates.empty()) {
    return result;
  }

  ParserContext context;
  context.source = result.normalized_markdown.data();
  context.source_size = result.normalized_markdown.size();
  MD_PARSER parser{};
  parser.flags = internal::kParserFlags | MD_FLAG_LATEXMATHSPANS;
  parser.enter_block = EnterBlock;
  parser.leave_block = LeaveBlock;
  parser.enter_span = EnterSpan;
  parser.leave_span = LeaveSpan;
  parser.text = Text;
  if (md_parse(result.normalized_markdown.data(),
               static_cast<MD_SIZE>(result.normalized_markdown.size()), &parser,
               &context) != 0) {
    result.facts_status = ExtensionFactsStatus::kParserFailure;
    return result;
  }

  std::size_t total_source_bytes = 0;
  std::size_t ordinal = 0;
  for (const Candidate& candidate : candidates) {
    const auto confirmed = std::find_if(
        context.confirmed.begin(), context.confirmed.end(),
        [&](const auto& item) {
          return item.first == candidate.kind &&
                 item.second >= candidate.begin && item.second < candidate.end;
        });
    if (confirmed == context.confirmed.end()) {
      ++ordinal;
      continue;
    }
    context.confirmed.erase(confirmed);
    if (!CandidateWithinBudgets(candidate) ||
        result.facts.size() >= kMaxExtensionNodes ||
        candidate.source.size() >
            kMaxTotalExtensionSourceBytes - total_source_bytes) {
      result.facts_status = ExtensionFactsStatus::kBudgetExceeded;
      ++ordinal;
      continue;
    }
    MathExtensionFact fact;
    fact.node.kind = candidate.kind;
    fact.node.node_id =
        MakeMathNodeId(document_generation, source_revision, ordinal);
    fact.node.matcher = candidate.kind == ExtensionNodeKind::kInline
                            ? "math-inline"
                            : "math-block";
    fact.node.source_utf8 = candidate.source;
    fact.node.source_bytes = candidate.source.size();
    fact.node.source_revision = source_revision;
    fact.source_begin = candidate.begin;
    fact.source_end = candidate.end;
    fact.fallback_utf8 = candidate.fallback;
    total_source_bytes += candidate.source.size();
    result.facts.push_back(std::move(fact));
    ++ordinal;
  }
  return result;
}

}  // namespace crayon::browser_markdown
