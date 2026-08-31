#include "renderer/page_snapshot_collector/cef_page_snapshot_renderer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_dom.h"
#include "include/wrapper/cef_helpers.h"
#include "ipc/page_snapshot_cef_message.h"
#include "renderer/page_snapshot_collector/page_snapshot_collector.h"

namespace crayon::browser::cef_shell::renderer {
namespace {

using browser_engine::BrowserUrl;
using browser_engine::SnapshotFact;
using browser_engine::SnapshotFactKind;
using ::crayon::cef_shell::renderer::CollectResult;
using ::crayon::cef_shell::renderer::PageSnapshotCollector;
using ::crayon::cef_shell::renderer::PageSnapshotCollectorSink;
using ::crayon::cef_shell::renderer::RendererFact;

constexpr std::size_t kMaxSnapshotVisitedDomNodes = 65536;
constexpr std::size_t kMaxSnapshotDomDepth = 128;

std::string Lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string BoundedUtf8(std::string value, std::size_t max_bytes) {
  if (value.size() <= max_bytes) return value;
  std::size_t end = max_bytes;
  while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) {
    --end;
  }
  value.resize(end);
  return value;
}

std::string SingleLine(std::string value, std::size_t max_bytes) {
  std::replace_if(
      value.begin(), value.end(),
      [](char ch) { return ch == '\n' || ch == '\r' || ch == '\t'; }, ' ');
  return BoundedUtf8(std::move(value), max_bytes);
}

std::optional<std::uint32_t> PositiveOrdinal(const CefString &value) {
  const std::string text = value.ToString();
  if (text.empty()) return std::nullopt;
  std::uint32_t ordinal = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), ordinal);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      ordinal == 0) {
    return std::nullopt;
  }
  return ordinal;
}

std::optional<std::uint32_t> ListOrdinal(CefRefPtr<CefDOMNode> node) {
  if (auto explicit_value =
          PositiveOrdinal(node->GetElementAttribute("value"))) {
    return explicit_value;
  }
  std::uint32_t ordinal = 1;
  auto parent = node->GetParent();
  if (parent && parent->IsElement()) {
    if (auto start = PositiveOrdinal(parent->GetElementAttribute("start"))) {
      ordinal = *start;
    }
  }
  for (auto sibling = node->GetPreviousSibling(); sibling;
       sibling = sibling->GetPreviousSibling()) {
    if (sibling->IsElement() &&
        Lower(sibling->GetElementTagName().ToString()) == "li") {
      if (ordinal == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
      }
      ++ordinal;
    }
  }
  return ordinal;
}

std::optional<std::uint8_t> ListDepth(CefRefPtr<CefDOMNode> node) {
  std::uint8_t depth = 0;
  for (auto ancestor = node->GetParent(); ancestor;
       ancestor = ancestor->GetParent()) {
    if (!ancestor->IsElement()) continue;
    const std::string tag = Lower(ancestor->GetElementTagName().ToString());
    if (tag == "ol" || tag == "ul") {
      if (depth == 8) return std::nullopt;
      ++depth;
    }
  }
  return depth == 0 ? std::nullopt : std::optional<std::uint8_t>(depth);
}

bool Hidden(CefRefPtr<CefDOMNode> node) {
  if (!node || !node->IsElement()) return true;
  const CefRect bounds = node->GetElementBounds();
  if (bounds.width <= 0 || bounds.height <= 0 ||
      node->HasElementAttribute("hidden") ||
      Lower(node->GetElementAttribute("aria-hidden").ToString()) == "true") {
    return true;
  }
  const std::string style =
      Lower(node->GetElementAttribute("style").ToString());
  return style.find("display:none") != std::string::npos ||
         style.find("display: none") != std::string::npos ||
         style.find("visibility:hidden") != std::string::npos ||
         style.find("visibility: hidden") != std::string::npos;
}

bool CollectTableRows(CefRefPtr<CefDOMNode> node,
                      std::vector<std::vector<std::string>> *rows,
                      std::size_t depth, std::size_t *remaining_nodes) {
  if (depth > kMaxSnapshotDomDepth) return false;
  for (auto current = node; current && rows->size() < 256;
       current = current->GetNextSibling()) {
    if (*remaining_nodes == 0) return false;
    --*remaining_nodes;
    if (!current->IsElement() || Hidden(current)) continue;
    const std::string tag = Lower(current->GetElementTagName().ToString());
    if (tag == "tr") {
      std::vector<std::string> cells;
      for (auto cell = current->GetFirstChild(); cell && cells.size() < 32;
           cell = cell->GetNextSibling()) {
        if (*remaining_nodes == 0) return false;
        --*remaining_nodes;
        if (!cell->IsElement() || Hidden(cell)) continue;
        const std::string cell_tag =
            Lower(cell->GetElementTagName().ToString());
        if (cell_tag == "td" || cell_tag == "th") {
          cells.push_back(
              BoundedUtf8(cell->GetElementInnerText().ToString(), 1024));
        }
      }
      if (!cells.empty()) rows->push_back(std::move(cells));
    } else if (current->HasChildren()) {
      if (!CollectTableRows(current->GetFirstChild(), rows, depth + 1,
                            remaining_nodes)) {
        return false;
      }
    }
  }
  return true;
}

std::optional<SnapshotFact> FactFor(CefRefPtr<CefDOMNode> node,
                                    CefRefPtr<CefDOMDocument> document,
                                    std::size_t depth,
                                    std::size_t *remaining_nodes,
                                    bool *capacity_exceeded) {
  if (Hidden(node)) return std::nullopt;
  const std::string tag = Lower(node->GetElementTagName().ToString());
  SnapshotFact fact;
  if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
    fact.kind = SnapshotFactKind::kHeading;
    fact.level = static_cast<std::uint8_t>(tag[1] - '0');
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
  } else if (tag == "p") {
    fact.kind = SnapshotFactKind::kParagraph;
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
  } else if (tag == "li") {
    fact.kind = SnapshotFactKind::kListItem;
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
    auto parent = node->GetParent();
    fact.ordered = parent && parent->IsElement() &&
                   Lower(parent->GetElementTagName().ToString()) == "ol";
    auto list_depth = ListDepth(node);
    if (!list_depth) return std::nullopt;
    fact.depth = *list_depth;
    if (fact.ordered) {
      fact.ordinal = ListOrdinal(node);
      if (!fact.ordinal) return std::nullopt;
    }
  } else if (tag == "a") {
    fact.kind = SnapshotFactKind::kLink;
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
    auto url = BrowserUrl::TryParse(
        document->GetCompleteURL(node->GetElementAttribute("href")).ToString());
    if (!url) return std::nullopt;
    fact.url = std::move(*url);
  } else if (tag == "img") {
    fact.kind = SnapshotFactKind::kImage;
    fact.text = BoundedUtf8(node->GetElementAttribute("alt").ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
    auto url = BrowserUrl::TryParse(
        document->GetCompleteURL(node->GetElementAttribute("src")).ToString());
    if (!url) return std::nullopt;
    fact.url = std::move(*url);
  } else if (tag == "pre") {
    fact.kind = SnapshotFactKind::kCodeBlock;
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotCodeBytes);
  } else if (tag == "blockquote") {
    fact.kind = SnapshotFactKind::kQuote;
    fact.text = BoundedUtf8(node->GetElementInnerText().ToString(),
                            browser_engine::kMaxSnapshotFactTextBytes);
  } else if (tag == "hr") {
    fact.kind = SnapshotFactKind::kDivider;
  } else if (tag == "table") {
    std::vector<std::vector<std::string>> rows;
    if (!CollectTableRows(node->GetFirstChild(), &rows, depth + 1,
                          remaining_nodes)) {
      *capacity_exceeded = true;
      return std::nullopt;
    }
    if (rows.empty() || rows.front().empty()) return std::nullopt;
    const std::size_t columns = rows.front().size();
    if (std::any_of(rows.begin(), rows.end(), [columns](const auto &row) {
          return row.size() != columns;
        })) {
      return std::nullopt;
    }
    fact.kind = SnapshotFactKind::kTable;
    fact.table_columns = static_cast<std::uint16_t>(columns);
    for (auto &row : rows) {
      for (auto &cell : row) fact.table_cells.push_back(std::move(cell));
    }
  } else {
    return std::nullopt;
  }
  return browser_engine::IsValid(fact)
             ? std::optional<SnapshotFact>(std::move(fact))
             : std::nullopt;
}

bool Walk(CefRefPtr<CefDOMNode> node, CefRefPtr<CefDOMDocument> document,
          PageSnapshotCollector *collector, std::uint64_t navigation_id,
          const std::string &frame_id, std::size_t depth,
          std::size_t *remaining_nodes) {
  if (depth > kMaxSnapshotDomDepth) return false;
  for (auto current = node; current && collector->active();
       current = current->GetNextSibling()) {
    if (*remaining_nodes == 0) return false;
    --*remaining_nodes;
    const bool hidden = current->IsElement() && Hidden(current);
    bool owns_subtree = false;
    if (current->IsElement() && !hidden) {
      bool capacity_exceeded = false;
      if (auto fact = FactFor(current, document, depth, remaining_nodes,
                              &capacity_exceeded)) {
        owns_subtree = fact->kind == SnapshotFactKind::kTable;
        collector->Observe(RendererFact{std::move(*fact), navigation_id,
                                        frame_id, true, true, true});
      }
      if (capacity_exceeded) return false;
    }
    if (!hidden && !owns_subtree && current->HasChildren()) {
      if (!Walk(current->GetFirstChild(), document, collector, navigation_id,
                frame_id, depth + 1, remaining_nodes)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

class CefPageSnapshotRenderer::Session final
    : public CefDOMVisitor,
      public PageSnapshotCollectorSink {
 public:
  Session(CefPageSnapshotRenderer *owner, CefRefPtr<CefFrame> frame,
          browser_engine::SnapshotRequest request)
      : owner_(owner),
        frame_(std::move(frame)),
        request_(std::move(request)),
        collector_(*this) {}

  void Visit(CefRefPtr<CefDOMDocument> document) override {
    CEF_REQUIRE_RENDERER_THREAD();
    if (!document || !frame_ || !frame_->IsMain()) {
      Reject(browser_engine::EngineErrorCode::kInvalidState);
      return;
    }
    auto url = BrowserUrl::TryParse(frame_->GetURL().ToString());
    if (!url) {
      Reject(browser_engine::EngineErrorCode::kNavigationFailed);
      return;
    }
    const std::string frame_id = frame_->GetIdentifier().ToString();
    std::string title = SingleLine(document->GetTitle().ToString(), 512);
    if (title.empty()) title = BoundedUtf8(url->value(), 512);
    browser_engine::SnapshotDocumentMetadata metadata{std::move(*url),
                                                      std::move(title)};
    if (collector_.Start(request_, frame_id, std::move(metadata)) !=
        CollectResult::kAccepted) {
      Reject(browser_engine::EngineErrorCode::kInvalidState);
      return;
    }
    std::size_t remaining_nodes = kMaxSnapshotVisitedDomNodes;
    if (!Walk(document->GetBody(), document, &collector_,
              request_.navigation_id.value(), frame_id, 0, &remaining_nodes)) {
      collector_.RejectCapacity();
    }
    if (collector_.active()) collector_.Finish();
    Complete();
  }

  void Cancel() {
    if (collector_.active()) collector_.Cancel();
    Complete();
  }

  void TearDown() {
    collector_.TearDown();
    owner_ = nullptr;
    frame_ = nullptr;
  }

  int browser_id() const {
    return frame_ ? frame_->GetBrowser()->GetIdentifier() : 0;
  }
  const std::string &request_id() const { return request_.request_id.value(); }

  void OnRendererSnapshotChunk(
      const browser_engine::SnapshotChunk &chunk) override {
    if (frame_)
      frame_->SendProcessMessage(PID_BROWSER,
                                 snapshot_ipc::CreateChunkMessage(chunk));
  }

  void OnRendererSnapshotTerminal(
      const browser_engine::SnapshotTerminal &terminal) override {
    if (frame_)
      frame_->SendProcessMessage(PID_BROWSER,
                                 snapshot_ipc::CreateTerminalMessage(terminal));
  }

 private:
  void Reject(browser_engine::EngineErrorCode error) {
    OnRendererSnapshotTerminal(browser_engine::SnapshotTerminal{
        request_.request_id, request_.tab_id, request_.navigation_id,
        browser_engine::SnapshotTerminalStatus::kRejected, error});
    Complete();
  }

  void Complete() {
    if (owner_) owner_->CompleteSession(request_.request_id.value());
  }

  CefPageSnapshotRenderer *owner_;
  CefRefPtr<CefFrame> frame_;
  browser_engine::SnapshotRequest request_;
  PageSnapshotCollector collector_;

  IMPLEMENT_REFCOUNTING(Session);
  DISALLOW_COPY_AND_ASSIGN(Session);
};

CefPageSnapshotRenderer::CefPageSnapshotRenderer() = default;
CefPageSnapshotRenderer::~CefPageSnapshotRenderer() = default;

bool CefPageSnapshotRenderer::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (source_process != PID_BROWSER || !browser || !frame || !frame->IsMain()) {
    return false;
  }
  if (message && message->GetName() == snapshot_ipc::kRequestMessageName) {
    auto request = snapshot_ipc::ReadRequestMessage(message);
    if (!request || sessions_.count(request->request_id.value()) != 0)
      return true;
    CefRefPtr<Session> session = new Session(this, frame, std::move(*request));
    const std::string request_id = session->request_id();
    sessions_.emplace(request_id, session);
    frame->VisitDOM(session);
    return true;
  }
  if (message && message->GetName() == snapshot_ipc::kCancelMessageName) {
    auto request_id = snapshot_ipc::ReadCancelMessage(message);
    if (!request_id) return true;
    const auto found = sessions_.find(request_id->value());
    if (found != sessions_.end()) {
      CefRefPtr<Session> session = found->second;
      session->Cancel();
    }
    return true;
  }
  return false;
}

void CefPageSnapshotRenderer::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (!browser || !frame || !frame->IsMain()) return;
  OnBrowserDestroyed(browser);
}

void CefPageSnapshotRenderer::OnBrowserDestroyed(
    CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (!browser) return;
  for (auto iterator = sessions_.begin(); iterator != sessions_.end();) {
    if (iterator->second->browser_id() == browser->GetIdentifier()) {
      iterator->second->TearDown();
      iterator = sessions_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void CefPageSnapshotRenderer::CompleteSession(const std::string &request_id) {
  const auto found = sessions_.find(request_id);
  if (found != sessions_.end()) sessions_.erase(found);
}

}  // namespace crayon::browser::cef_shell::renderer
