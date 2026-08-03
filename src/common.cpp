#include "common.h"
#include <shlobj.h>

namespace monitor {

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty()) return {};
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return {};
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), count);
  return result;
}

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0,
                                        nullptr, nullptr);
  if (count <= 0) return {};
  std::string result(static_cast<size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), count,
                      nullptr, nullptr);
  return result;
}

std::wstring GetLocalAppDataDirectory() {
  PWSTR path = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
    return L".";
  }
  std::wstring result(path);
  CoTaskMemFree(path);
  return result;
}

std::wstring JoinPath(std::wstring_view left, std::wstring_view right) {
  if (left.empty()) return std::wstring(right);
  std::wstring result(left);
  if (result.back() != L'\\' && result.back() != L'/') result.push_back(L'\\');
  result.append(right);
  return result;
}

}  // namespace monitor

