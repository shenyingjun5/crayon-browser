#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "resource_ids.h"

namespace {

constexpr int kExpectedArgumentCount = 6;
constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;
constexpr std::size_t kProductNameCapacity = 128;
constexpr std::string_view kForbiddenFixtureFactory = "BuildFixtureSnapshot";
constexpr std::string_view kForbiddenFixtureBody =
    u8"这是内置 Markdown 查看器的**只读**示例文档。";
constexpr std::wstring_view kForbiddenWideFixtureBody =
    L"这是内置 Markdown 查看器的**只读**示例文档。";

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

}  // namespace

int wmain(int argument_count, wchar_t *arguments[]) {
  if (argument_count != kExpectedArgumentCount) {
    std::cerr << "Expected executable, resource module and runtime manifest "
                 "and content/media-host arguments\n";
    return 1;
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
  if (!RuntimeFilesExist(executable, manifest)) {
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
  FreeLibrary(module);

  if (!product_name_exists || !icons_exist) {
    std::cerr << "Product name or app icon resource is missing\n";
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
