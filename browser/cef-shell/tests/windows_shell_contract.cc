#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "resource_ids.h"

namespace {

constexpr int kExpectedArgumentCount = 7;
constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;
constexpr std::size_t kProductNameCapacity = 128;
constexpr std::string_view kForbiddenFixtureFactory = "BuildFixtureSnapshot";
constexpr std::string_view kForbiddenFixtureBody =
    u8"这是内置 Markdown 查看器的**只读**示例文档。";
constexpr std::wstring_view kForbiddenWideFixtureBody =
    L"这是内置 Markdown 查看器的**只读**示例文档。";
constexpr std::array<std::wstring_view, 3> kSupportedLocales = {
    L"en-US", L"zh-CN", L"zh-TW"};
constexpr std::array<std::wstring_view, 4> kLocaleSuffixes = {
    L"", L"_FEMININE", L"_MASCULINE", L"_NEUTER"};

template <typename Character>
bool ContainsBytes(const std::vector<char> &binary,
                   std::basic_string_view<Character> value) {
  const auto *begin = reinterpret_cast<const char *>(value.data());
  const std::size_t size = value.size() * sizeof(Character);
  return std::search(binary.begin(), binary.end(), begin, begin + size) !=
         binary.end();
}

bool ExcludesMdvFixture(const std::filesystem::path &executable) {
  std::ifstream input(executable, std::ios::binary | std::ios::ate);
  if (!input) {
    std::cerr << "Browser executable cannot be scanned\n";
    return false;
  }
  const auto end = input.tellg();
  if (end <= 0) {
    std::cerr << "Browser executable is empty\n";
    return false;
  }
  std::vector<char> binary(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!input.read(binary.data(), static_cast<std::streamsize>(binary.size()))) {
    std::cerr << "Browser executable scan was incomplete\n";
    return false;
  }
  if (ContainsBytes(binary, kForbiddenFixtureFactory) ||
      ContainsBytes(binary, kForbiddenFixtureBody) ||
      ContainsBytes(binary, kForbiddenWideFixtureBody)) {
    std::cerr << "Browser executable contains a forbidden MDV fixture\n";
    return false;
  }
  return true;
}

bool RuntimeFilesExist(const std::filesystem::path &executable,
                       const std::filesystem::path &manifest) {
  std::ifstream input(manifest);
  if (!input) {
    std::cerr << "Runtime manifest is missing\n";
    return false;
  }

  std::string entry;
  std::size_t count = 0;
  while (std::getline(input, entry)) {
    if (entry.empty()) {
      continue;
    }
    const std::filesystem::path relative(entry);
    if (relative.is_absolute() || relative.has_parent_path()) {
      std::cerr << "Runtime manifest contains an unsafe entry\n";
      return false;
    }
    if (!std::filesystem::exists(executable.parent_path() / relative)) {
      std::cerr << "CEF runtime dependency is missing: " << entry << '\n';
      return false;
    }
    ++count;
  }
  return count > 0;
}

bool HasIcon(HMODULE module, int resource_id, int size) {
  HICON icon =
      static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(resource_id),
                                    IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
  if (!icon) {
    return false;
  }
  DestroyIcon(icon);
  return true;
}

bool LocaleDirectoryIsClosed(const std::filesystem::path &locale_directory,
                             std::wstring_view configuration,
                             bool report_errors) {
  if (!std::filesystem::is_directory(locale_directory)) {
    if (report_errors) {
      std::cerr << "CEF locale directory is missing\n";
    }
    return false;
  }

  std::vector<std::wstring> expected;
  for (const std::wstring_view locale : kSupportedLocales) {
    for (const std::wstring_view suffix : kLocaleSuffixes) {
      expected.emplace_back(std::wstring(locale) + std::wstring(suffix) +
                            L".pak");
    }
  }
  std::sort(expected.begin(), expected.end());

  std::vector<std::wstring> actual;
  for (const auto &entry : std::filesystem::directory_iterator(locale_directory)) {
    if (!entry.is_regular_file() || entry.is_symlink()) {
      if (report_errors) {
        std::cerr << "CEF locale directory contains an unsafe entry\n";
      }
      return false;
    }
    if (entry.path().extension() == L".pak") {
      actual.push_back(entry.path().filename().wstring());
    }
  }
  std::sort(actual.begin(), actual.end());

  for (const std::wstring &required : expected) {
    if (!std::binary_search(actual.begin(), actual.end(), required)) {
      if (report_errors) {
        std::wcerr << L"Required CEF locale resource is missing: " << required
                   << L'\n';
      }
      return false;
    }
  }
  if (configuration == L"Release" && actual != expected) {
    if (report_errors) {
      std::cerr
          << "Release package contains unsupported CEF locale resources\n";
    }
    return false;
  }
  return true;
}

bool LocaleResourcesAreClosed(const std::filesystem::path &executable,
                              std::wstring_view configuration) {
  return LocaleDirectoryIsClosed(executable.parent_path() / L"locales",
                                 configuration, true);
}

bool LocaleClosureNegativeContract() {
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      (L"crayon-windows-locale-contract-" +
       std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  if (!std::filesystem::create_directory(directory)) {
    std::cerr << "Could not create locale contract fixture\n";
    return false;
  }
  struct FixtureCleanup final {
    std::filesystem::path directory;
    ~FixtureCleanup() {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
    }
  } cleanup{directory};

  std::vector<std::wstring> expected;
  for (const std::wstring_view locale : kSupportedLocales) {
    for (const std::wstring_view suffix : kLocaleSuffixes) {
      expected.emplace_back(std::wstring(locale) + std::wstring(suffix) +
                            L".pak");
    }
  }
  for (const std::wstring &name : expected) {
    std::ofstream(directory / name, std::ios::binary).put('\0');
  }
  if (!LocaleDirectoryIsClosed(directory, L"Release", false)) {
    return false;
  }

  const std::filesystem::path required = directory / expected.front();
  if (!std::filesystem::remove(required) ||
      LocaleDirectoryIsClosed(directory, L"Release", false)) {
    return false;
  }
  std::ofstream(required, std::ios::binary).put('\0');
  std::ofstream(directory / L"fr.pak", std::ios::binary).put('\0');
  return !LocaleDirectoryIsClosed(directory, L"Release", false) &&
         LocaleDirectoryIsClosed(directory, L"Debug", false);
}

bool HasLocalizedProductName(HMODULE module, LANGID language) {
  constexpr int kStringTableBlock = (IDS_CRAYON_PRODUCT_NAME >> 4) + 1;
  return FindResourceExW(module, RT_STRING,
                         MAKEINTRESOURCEW(kStringTableBlock), language) !=
         nullptr;
}

}  // namespace

int wmain(int argument_count, wchar_t *arguments[]) {
  if (argument_count != kExpectedArgumentCount) {
    std::cerr << "Expected executable, resource module and runtime manifest "
                 "and content/media-host arguments\n";
    return 1;
  }
  if (!LocaleClosureNegativeContract()) {
    std::cerr << "CEF locale closure negative contract failed\n";
    return 10;
  }

  const std::filesystem::path executable(arguments[1]);
  const std::filesystem::path resource_module(arguments[2]);
  const std::filesystem::path manifest(arguments[3]);
  const std::filesystem::path content_host(arguments[4]);
  const std::filesystem::path media_host(arguments[5]);
  DWORD binary_type = 0;
  if (!std::filesystem::is_regular_file(executable) ||
      !GetBinaryTypeW(executable.c_str(), &binary_type) ||
      binary_type != SCS_64BIT_BINARY) {
    std::cerr << "Browser executable is missing or is not Windows x64\n";
    return 2;
  }
  if (!RuntimeFilesExist(executable, manifest) ||
      !LocaleResourcesAreClosed(executable, arguments[6])) {
    return 3;
  }
  if (!ExcludesMdvFixture(executable)) {
    return 9;
  }
  if (!std::filesystem::is_regular_file(content_host) ||
      !GetBinaryTypeW(content_host.c_str(), &binary_type) ||
      binary_type != SCS_64BIT_BINARY) {
    std::cerr << "Bundled content host is missing or is not Windows x64\n";
    return 7;
  }
  if (!std::filesystem::is_regular_file(media_host) ||
      !GetBinaryTypeW(media_host.c_str(), &binary_type) ||
      binary_type != SCS_64BIT_BINARY) {
    std::cerr << "Bundled media host is missing or is not Windows x64\n";
    return 8;
  }

  HMODULE module =
      LoadLibraryExW(resource_module.c_str(), nullptr,
                     LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (!module) {
    std::cerr << "Browser resource module cannot be opened as a data image\n";
    return 4;
  }

  wchar_t product_name[kProductNameCapacity] = {};
  const bool product_name_exists =
      LoadStringW(module, IDS_CRAYON_PRODUCT_NAME, product_name,
                  static_cast<int>(std::size(product_name))) > 0;
  const bool icons_exist =
      HasIcon(module, IDI_CRAYON_APP, kMainIconSize) &&
      HasIcon(module, IDI_CRAYON_APP_SMALL, kSmallIconSize);
  const bool localized_product_names_exist =
      HasLocalizedProductName(module, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) &&
      HasLocalizedProductName(
          module, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) &&
      HasLocalizedProductName(
          module, MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL));
  FreeLibrary(module);

  if (!product_name_exists || !localized_product_names_exist || !icons_exist) {
    std::cerr << "Localized product name or app icon resource is missing\n";
    return 5;
  }

  HMODULE executable_module = LoadLibraryExW(resource_module.c_str(), nullptr,
                                             DONT_RESOLVE_DLL_REFERENCES);
  if (!executable_module || !GetProcAddress(executable_module, "RunWinMain")) {
    if (executable_module) {
      FreeLibrary(executable_module);
    }
    std::cerr << "CEF bootstrap export RunWinMain is missing\n";
    return 6;
  }
  FreeLibrary(executable_module);
  return 0;
}
