#include "browser/mdv/cef_mdv_handler.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include "crayon/browser_markdown_runtime/highlight_extension.h"
#include "crayon/browser_markdown_runtime/katex_extension.h"
#include "crayon/browser_markdown_runtime/mermaid_extension.h"
#include "crayon/browser_mdv/mdv_images.h"
#include "crayon/browser_mdv/mdv_page.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/wrapper/cef_helpers.h"

namespace crayon::browser::cef_shell::mdv {
namespace {

using crayon::browser_mdv::ClassifyMdvRequest;
using crayon::browser_mdv::kMaxLocalImageBytes;
using crayon::browser_mdv::kMdvCsp;
using crayon::browser_mdv::kMdvHost;
using crayon::browser_mdv::kMdvScheme;
using crayon::browser_mdv::MdvPageSnapshot;
using crayon::browser_mdv::MdvPageStrings;
using crayon::browser_mdv::MdvRequestParts;
using crayon::browser_mdv::RenderMdvDocument;

constexpr char kHtmlMimeType[] = "text/html";
constexpr char kCssMimeType[] = "text/css";
constexpr char kJsMimeType[] = "text/javascript";
constexpr char kTextMimeType[] = "text/plain";
constexpr char kFontMimeType[] = "font/woff2";
constexpr char kUtf8Charset[] = "utf-8";

std::filesystem::path FilesystemPath(const std::string& path_utf8) {
#if defined(_WIN32)
  return std::filesystem::u8path(path_utf8);
#else
  return std::filesystem::path(path_utf8);
#endif
}

class MdvMemoryResourceHandler final : public CefResourceHandler {
 public:
  MdvMemoryResourceHandler(int status_code, std::string status_text,
                           std::string mime_type, std::string body)
      : status_code_(status_code),
        status_text_(std::move(status_text)),
        mime_type_(std::move(mime_type)),
        body_(std::move(body)) {}

  bool Open(CefRefPtr<CefRequest> request, bool& handle_request,
            CefRefPtr<CefCallback> callback) override {
    static_cast<void>(request);
    static_cast<void>(callback);
    handle_request = true;
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString& redirect_url) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(redirect_url);
    response->SetStatus(status_code_);
    response->SetStatusText(status_text_);
    response->SetMimeType(mime_type_);
    if (mime_type_ != kFontMimeType) {
      response->SetCharset(kUtf8Charset);
    }
    CefResponse::HeaderMap headers;
    headers.emplace("Cache-Control", "no-store");
    // Contract CSP (MDV-01 §2): byte-for-byte the shared constant.
    headers.emplace("Content-Security-Policy", kMdvCsp);
    headers.emplace("Cross-Origin-Resource-Policy", "same-origin");
    headers.emplace("Referrer-Policy", "no-referrer");
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("X-Frame-Options", "DENY");
    response->SetHeaderMap(headers);
    response_length = static_cast<int64_t>(body_.size());
  }

  bool Read(void* data_out, int bytes_to_read, int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override {
    static_cast<void>(callback);
    bytes_read = 0;
    if (cancelled_ || data_out == nullptr || bytes_to_read <= 0 ||
        offset_ >= body_.size()) {
      return false;
    }
    const std::size_t available = body_.size() - offset_;
    const std::size_t requested = static_cast<std::size_t>(bytes_to_read);
    const std::size_t length = std::min(available, requested);
    std::memcpy(data_out, body_.data() + offset_, length);
    offset_ += length;
    bytes_read = static_cast<int>(length);
    return true;
  }

  void Cancel() override { cancelled_ = true; }

 private:
  const int status_code_;
  const std::string status_text_;
  const std::string mime_type_;
  const std::string body_;
  std::size_t offset_ = 0;
  bool cancelled_ = false;

  IMPLEMENT_REFCOUNTING(MdvMemoryResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(MdvMemoryResourceHandler);
};

/// Maps a validated image path to its response mime type.
std::string MimeForImage(const std::string& path) {
  const auto dot = path.find_last_of('.');
  std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (ext == "png") return "image/png";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "gif") return "image/gif";
  if (ext == "webp") return "image/webp";
  if (ext == "bmp") return "image/bmp";
  if (ext == "svg") return "image/svg+xml";
  return {};
}

/// Reads at most `kMaxLocalImageBytes + 1` bytes; oversized or unreadable
/// files return empty (the handler maps that to 404).
std::string ReadImageBytes(const std::string& path_utf8) {
  std::ifstream file(FilesystemPath(path_utf8), std::ios::binary);
  if (!file.is_open()) {
    return {};
  }
  std::string bytes;
  bytes.assign(std::istreambuf_iterator<char>(file),
               std::istreambuf_iterator<char>());
  if (bytes.size() > kMaxLocalImageBytes) {
    return {};
  }
  return bytes;
}

class MdvSchemeHandlerFactory final : public CefSchemeHandlerFactory {
 public:
  MdvSchemeHandlerFactory(MdvPageStrings strings,
                          std::shared_ptr<const MdvRuntimeState> state,
                          std::shared_ptr<const crayon::browser_markdown_runtime::
                                                    RuntimeAssetBundle>
                              highlight_assets,
                          std::shared_ptr<const crayon::browser_markdown_runtime::
                                                    RuntimeAssetBundle>
                              katex_assets,
                          std::shared_ptr<const crayon::browser_markdown_runtime::
                                                    RuntimeAssetBundle>
                              mermaid_assets,
                          std::string stylesheet, std::string script)
      : strings_(std::move(strings)),
        state_(std::move(state)),
        highlight_assets_(std::move(highlight_assets)),
        katex_assets_(std::move(katex_assets)),
        mermaid_assets_(std::move(mermaid_assets)),
        stylesheet_(std::move(stylesheet)),
        script_(std::move(script)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();
    static_cast<void>(browser);
    static_cast<void>(frame);
    if (!request || scheme_name.ToString() != kMdvScheme) {
      return nullptr;
    }
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) {
      return nullptr;
    }
    const bool has_credentials = !CefString(&parts.username).empty() ||
                                 !CefString(&parts.password).empty();
    const MdvRequestParts request_parts{
        request->GetMethod().ToString(),
        CefString(&parts.scheme).ToString(),
        CefString(&parts.host).ToString(),
        CefString(&parts.path).ToString(),
        has_credentials,
        !CefString(&parts.port).empty(),
        request->GetURL().ToString().find('?') != std::string::npos,
        request->GetURL().ToString().find('#') != std::string::npos,
    };
    const auto route = ClassifyMdvRequest(request_parts);

    std::string body;
    std::string mime_type = kTextMimeType;
    switch (route.kind) {
      case crayon::browser_mdv::MdvResourceKind::kDocument:
        mime_type = kHtmlMimeType;
        if (route.include_body) {
          body = RenderMdvDocument(state_->snapshot(), strings_);
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kStylesheet:
        mime_type = kCssMimeType;
        if (route.include_body) {
          body = stylesheet_;
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kScript:
        mime_type = kJsMimeType;
        if (route.include_body) {
          body = script_;
        }
        break;
      case crayon::browser_mdv::MdvResourceKind::kRuntimeAsset: {
        const auto& bundle = route.runtime_namespace == "katex"
                                 ? katex_assets_
                             : route.runtime_namespace == "mermaid"
                                 ? mermaid_assets_
                                 : highlight_assets_;
        if (!bundle) {
          return new MdvMemoryResourceHandler(404, "Not Found", kTextMimeType,
                                              {});
        }
        const auto found = std::find_if(
            bundle->resources.begin(), bundle->resources.end(),
            [&](const auto& asset) {
              return asset.resource_id == route.runtime_resource_id;
            });
        if (found == bundle->resources.end()) {
          return new MdvMemoryResourceHandler(404, "Not Found", kTextMimeType,
                                              {});
        }
        using ContentType =
            crayon::browser_markdown_runtime::RuntimeAssetContentType;
        switch (found->content_type) {
          case ContentType::kJavaScript:
            mime_type = kJsMimeType;
            break;
          case ContentType::kCss:
            mime_type = kCssMimeType;
            break;
          case ContentType::kFont:
            mime_type = kFontMimeType;
            break;
          case ContentType::kUnknown:
          case ContentType::kWasm:
          case ContentType::kJson:
            return new MdvMemoryResourceHandler(404, "Not Found", kTextMimeType,
                                                {});
        }
        if (route.include_body) {
          body = found->bytes;
        }
        break;
      }
      case crayon::browser_mdv::MdvResourceKind::kImage: {
        // Opaque validated local image: read on demand, bounded.
        const auto snapshot = state_->snapshot();
        const auto& images = snapshot.local_images;
        std::string image_body;
        std::string image_mime;
        if (route.image_index < images.size()) {
          image_mime = MimeForImage(images[route.image_index]);
          image_body = ReadImageBytes(images[route.image_index]);
        }
        if (image_body.empty()) {
          return new MdvMemoryResourceHandler(404, "Not Found", kTextMimeType,
                                              {});
        }
        return new MdvMemoryResourceHandler(200, "OK", image_mime,
                                            std::move(image_body));
      }
      case crayon::browser_mdv::MdvResourceKind::kMethodNotAllowed:
        break;
      case crayon::browser_mdv::MdvResourceKind::kNotFound:
        break;
    }
    return new MdvMemoryResourceHandler(route.status_code,
                                        StatusText(route.status_code),
                                        mime_type, std::move(body));
  }

 private:
  static std::string StatusText(int status_code) {
    switch (status_code) {
      case 200:
        return "OK";
      case 404:
        return "Not Found";
      case 405:
        return "Method Not Allowed";
      default:
        return "Error";
    }
  }

  const MdvPageStrings strings_;
  const std::shared_ptr<const MdvRuntimeState> state_;
  const std::shared_ptr<const crayon::browser_markdown_runtime::
                            RuntimeAssetBundle>
      highlight_assets_;
  const std::shared_ptr<const crayon::browser_markdown_runtime::
                            RuntimeAssetBundle>
      katex_assets_;
  const std::shared_ptr<const crayon::browser_markdown_runtime::
                            RuntimeAssetBundle>
      mermaid_assets_;
  const std::string stylesheet_;
  const std::string script_;

  IMPLEMENT_REFCOUNTING(MdvSchemeHandlerFactory);
  DISALLOW_COPY_AND_ASSIGN(MdvSchemeHandlerFactory);
};

}  // namespace

struct MdvRuntimeState::Impl {
  mutable std::mutex mutex;
  MdvPageSnapshot snapshot;
};

MdvRuntimeState::MdvRuntimeState() : MdvRuntimeState(MdvPageSnapshot{}) {}

MdvRuntimeState::MdvRuntimeState(MdvPageSnapshot initial)
    : impl_(std::make_unique<Impl>()) {
  impl_->snapshot = std::move(initial);
}

MdvRuntimeState::~MdvRuntimeState() = default;

void MdvRuntimeState::SetSnapshot(MdvPageSnapshot snapshot) {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->snapshot = std::move(snapshot);
}

MdvPageSnapshot MdvRuntimeState::snapshot() const {
  const std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->snapshot;
}

bool RegisterMdvSchemeHandlerFactory(
    MdvPageStrings strings, const std::shared_ptr<MdvRuntimeState>& state) {
  const auto catalog_result =
      crayon::browser_markdown_runtime::BuildHighlightAssetCatalog();
  if (catalog_result.status != crayon::browser_markdown_runtime::
                                   AssetCatalogBuildStatus::kReady ||
      !catalog_result.catalog) {
    return false;
  }
  auto highlight_assets = catalog_result.catalog->FindCompatible(
      crayon::browser_markdown_runtime::kHighlightAssetManifestId,
      crayon::browser_markdown_runtime::kHighlightExtensionId,
      crayon::browser_markdown_runtime::kHighlightExtensionVersion);
  if (!highlight_assets) {
    return false;
  }
  const auto katex_catalog =
      crayon::browser_markdown_runtime::BuildKatexAssetCatalog();
  if (katex_catalog.status != crayon::browser_markdown_runtime::
                                  AssetCatalogBuildStatus::kReady ||
      !katex_catalog.catalog) {
    return false;
  }
  auto katex_assets = katex_catalog.catalog->FindCompatible(
      crayon::browser_markdown_runtime::kKatexAssetManifestId,
      crayon::browser_markdown_runtime::kKatexInlineExtensionId,
      crayon::browser_markdown_runtime::kKatexExtensionVersion);
  if (!katex_assets) {
    return false;
  }
  const auto mermaid_catalog =
      crayon::browser_markdown_runtime::BuildMermaidAssetCatalog();
  if (mermaid_catalog.status != crayon::browser_markdown_runtime::
                                    AssetCatalogBuildStatus::kReady ||
      !mermaid_catalog.catalog) {
    return false;
  }
  auto mermaid_assets = mermaid_catalog.catalog->FindCompatible(
      crayon::browser_markdown_runtime::kMermaidAssetManifestId,
      crayon::browser_markdown_runtime::kMermaidExtensionId,
      crayon::browser_markdown_runtime::kMermaidExtensionVersion);
  if (!mermaid_assets) {
    return false;
  }
  return CefRegisterSchemeHandlerFactory(
      kMdvScheme, kMdvHost,
      new MdvSchemeHandlerFactory(std::move(strings), state,
                                  std::move(highlight_assets),
                                  std::move(katex_assets),
                                  std::move(mermaid_assets),
                                  crayon::browser_mdv::RenderMdvStylesheet(),
                                  crayon::browser_mdv::RenderMdvScript()));
}

}  // namespace crayon::browser::cef_shell::mdv
