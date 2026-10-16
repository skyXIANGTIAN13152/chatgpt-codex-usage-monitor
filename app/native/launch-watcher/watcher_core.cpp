#include "watcher_core.h"
#include "json.h"
#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace tiga::launch {
std::optional<Settings> parse_settings(std::string_view json) {
  constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};
  if (json.starts_with(utf8_bom)) json.remove_prefix(utf8_bom.size());
  const auto parsed = monitor::ParseJson(json);
  if (!parsed || !parsed.value.IsObject()) return std::nullopt;
  const auto boolean = [&parsed](std::string_view key) {
    const auto* value = parsed.value.Find(key);
    return value && value->AsBool().value_or(false);
  };
  return Settings{boolean("launchWithCodex"), boolean("launchWithClaude")};
}

std::wstring normalized_path(std::wstring_view path) {
  std::wstring result{path};
  std::replace(result.begin(), result.end(), L'/', L'\\');
  if (result.starts_with(L"\\\\?\\UNC\\")) result = L"\\\\" + result.substr(8);
  else if (result.starts_with(L"\\\\?\\")) result.erase(0, 4);
  result = std::filesystem::path{result}.lexically_normal().wstring();
  while (result.size() > 3 && result.back() == L'\\') result.pop_back();
  std::transform(result.begin(), result.end(), result.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return result;
}

bool is_within_directory(std::wstring_view file, std::wstring_view directory) {
  const auto candidate = normalized_path(file);
  auto root = normalized_path(directory);
  if (candidate.empty() || root.empty()) return false;
  if (candidate == root) return true;
  if (root.back() != L'\\') root += L'\\';
  return candidate.starts_with(root);
}

std::optional<Provider> classify_process(std::wstring_view image_path,
                                       std::wstring_view app_directory) {
  // An unreadable image path must not fall back to its basename: that could
  // mistake the bundled CLI for an external app and start a feedback loop.
  if (image_path.empty() || is_within_directory(image_path, app_directory)) return std::nullopt;
  const auto path = normalized_path(image_path);
  const auto separator = path.find_last_of(L'\\');
  const auto name = path.substr(separator == std::wstring::npos ? 0 : separator + 1);
  if (name == L"codex.exe" || name == L"chatgpt.exe") return Provider::codex;
  if (name == L"claude.exe") return Provider::claude;
  return std::nullopt;
}

std::wstring quote_argument(std::wstring_view argument) {
  std::wstring result{L"\""};
  std::size_t backslashes{0};
  for (const auto character : argument) {
    if (character == L'\\') { ++backslashes; continue; }
    result.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
    backslashes = 0;
    if (character == L'"') result += L'\\';
    result += character;
  }
  result.append(backslashes * 2, L'\\');
  return result + L'"';
}

std::vector<Provider> RisingEdges::update(Settings settings, Presence current) {
  current.codex = settings.launch_with_codex && current.codex;
  current.claude = settings.launch_with_claude && current.claude;
  std::vector<Provider> result;
  if (current.codex && !previous_.codex) result.push_back(Provider::codex);
  if (current.claude && !previous_.claude) result.push_back(Provider::claude);
  previous_ = current;
  return result;
}
}  // namespace tiga::launch
