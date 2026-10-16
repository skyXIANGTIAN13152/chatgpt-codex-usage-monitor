#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tiga::launch {
enum class Provider { codex, claude };
struct Settings { bool launch_with_codex{false}; bool launch_with_claude{false}; };
struct Presence { bool codex{false}; bool claude{false}; };

std::optional<Settings> parse_settings(std::string_view json);
std::wstring normalized_path(std::wstring_view path);
bool is_within_directory(std::wstring_view file, std::wstring_view directory);
std::optional<Provider> classify_process(std::wstring_view image_path,
                                       std::wstring_view app_directory);
std::wstring quote_argument(std::wstring_view argument);

class RisingEdges {
 public:
  std::vector<Provider> update(Settings settings, Presence current);
 private:
  Presence previous_{};
};
}  // namespace tiga::launch
