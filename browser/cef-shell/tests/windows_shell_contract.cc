#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "resource_ids.h"

namespace {

constexpr int kExpectedArgumentCount = 3;
constexpr int kMainIconSize = 32;
constexpr int kSmallIconSize = 16;
constexpr std::size_t kProductNameCapacity = 128;

bool RuntimeFilesExist(const std::filesystem::path& executable,
                       const std::filesystem::path& manifest) {
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

int wmain(int argument_count, wchar_t* arguments[]) {
  if (argument_count != kExpectedArgumentCount) {
    std::cerr << "Expected executable and runtime manifest arguments\n";
    return 1;
  }

  const std::filesystem::path executable(arguments[1]);
  const std::filesystem::path manifest(arguments[2]);
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

  HMODULE module =
      LoadLibraryExW(executable.c_str(), nullptr,
                     LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (!module) {
    std::cerr << "Browser executable cannot be opened as a data image\n";
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
  return 0;
}
