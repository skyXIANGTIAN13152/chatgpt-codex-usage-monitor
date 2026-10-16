#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include "watcher_core.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {
using tiga::launch::Provider;
constexpr DWORD poll_interval_ms{2500};
constexpr std::uintmax_t maximum_settings_bytes{256 * 1024};

class UniqueHandle {
 public:
  explicit UniqueHandle(HANDLE value = nullptr) noexcept : value_{value} {}
  ~UniqueHandle() { close(); }
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& other) noexcept : value_{std::exchange(other.value_, nullptr)} {}
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this != &other) { close(); value_ = std::exchange(other.value_, nullptr); }
    return *this;
  }
  HANDLE get() const noexcept { return value_; }
  explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
 private:
  void close() noexcept { if (*this) CloseHandle(value_); }
  HANDLE value_{};
};

// Adapted from ProcessImageName in the existing src/chatgpt_locator.cpp.
// Keep the full path, so the monitor's own embedded CLIs can be excluded.
std::wstring process_image_path(DWORD process_id) {
  const UniqueHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)};
  if (!process) return {};
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) return {};
  path.resize(size);
  return path;
}

std::optional<tiga::launch::Presence> running_providers(std::wstring_view app_directory) {
  // Toolhelp enumeration adapted from FindRunningChatGpt in the same old file.
  const UniqueHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
  tiga::launch::Presence presence{};
  if (!snapshot) return std::nullopt;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.get(), &entry)) return std::nullopt;
  do {
    if (entry.th32ProcessID == GetCurrentProcessId()) continue;
    const auto name = tiga::launch::normalized_path(entry.szExeFile);
    if (name != L"codex.exe" && name != L"chatgpt.exe" && name != L"claude.exe") continue;
    const auto provider = tiga::launch::classify_process(process_image_path(entry.th32ProcessID), app_directory);
    if (provider == Provider::codex) presence.codex = true;
    if (provider == Provider::claude) presence.claude = true;
    if (presence.codex && presence.claude) break;
  } while (Process32NextW(snapshot.get(), &entry));
  return presence;
}

std::optional<tiga::launch::Settings> read_settings(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum_settings_bytes) return std::nullopt;
  std::ifstream stream{path, std::ios::binary};
  if (!stream) return std::nullopt;
  const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  return tiga::launch::parse_settings(text);
}

std::wstring mutex_name(std::wstring_view settings_path) {
  // Stable per-settings-path identity, including across application updates.
  std::uint64_t hash{14695981039346656037ULL};
  for (const auto character : tiga::launch::normalized_path(settings_path)) {
    hash ^= static_cast<std::uint64_t>(character);
    hash *= 1099511628211ULL;
  }
  return L"Local\\TigaQuotaLaunchWatcher-" + std::to_wstring(hash);
}

void launch_app(const std::filesystem::path& app, Provider provider) {
  auto command = tiga::launch::quote_argument(app.wstring()) +
                 (provider == Provider::codex ? L" --trigger=codex" : L" --trigger=claude");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION information{};
  const auto directory = app.parent_path().wstring();
  if (!CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr, directory.c_str(), &startup, &information)) {
    OutputDebugStringW(L"Tiga launch watcher could not start the monitor.\n");
    return;
  }
  const UniqueHandle thread{information.hThread};
  const UniqueHandle process{information.hProcess};
}

int run() {
  int count{0};
  const auto free_arguments = [](wchar_t** value) { if (value) LocalFree(value); };
  const std::unique_ptr<wchar_t*, decltype(free_arguments)> arguments{
      CommandLineToArgvW(GetCommandLineW(), &count), free_arguments};
  if (!arguments) return 2;
  std::filesystem::path settings_path;
  std::filesystem::path app;
  for (int index = 1; index + 1 < count; index += 2) {
    const std::wstring_view key{arguments.get()[index]};
    if (key == L"--settings") settings_path = arguments.get()[index + 1];
    else if (key == L"--app") app = arguments.get()[index + 1];
    else return 2;
  }
  if (count != 5 || !settings_path.is_absolute() || !app.is_absolute()) return 2;
  const auto mutex_id = mutex_name(settings_path.wstring());
  const UniqueHandle mutex{CreateMutexW(nullptr, FALSE, mutex_id.c_str())};
  if (!mutex) return 3;
  if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
  const auto app_directory = app.parent_path().wstring();
  tiga::launch::RisingEdges edges;
  unsigned unreadable_attempts{0};
  for (;;) {
    const auto settings = read_settings(settings_path);
    if (!settings) {
      // Atomic settings replacement can briefly make the file unavailable.
      if (++unreadable_attempts >= 3) return 3;
    } else {
      unreadable_attempts = 0;
      if (!settings->launch_with_codex && !settings->launch_with_claude) return 0;
      if (const auto presence = running_providers(app_directory)) {
        // Consume both presence edges, but wake the shared monitor only once.
        const auto triggers = edges.update(*settings, *presence);
        if (!triggers.empty()) launch_app(app, triggers.front());
      }
    }
    Sleep(poll_interval_ms);
  }
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try { return run(); }
  catch (const std::exception&) {
    OutputDebugStringW(L"Tiga launch watcher stopped after an unexpected error.\n");
    return 3;
  }
}
