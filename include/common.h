#pragma once

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace monitor {

constexpr wchar_t kProductName[] = L"ChatGPTCodexUsageMonitor";
constexpr wchar_t kWindowClass[] = L"ChatGPTCodexUsageMonitor.Hud";
constexpr wchar_t kMutexName[] = L"Local\\ChatGPTCodexUsageMonitor.Monitor";

constexpr UINT WM_MONITOR_SNAPSHOT = WM_APP + 1;
constexpr UINT WM_MONITOR_SERVER_ERROR = WM_APP + 2;
constexpr UINT WM_MONITOR_WINDOW_EVENT = WM_APP + 3;
constexpr UINT WM_MONITOR_PROCESS_EXIT = WM_APP + 4;
constexpr UINT WM_MONITOR_NETWORK_CHANGE = WM_APP + 5;
constexpr UINT WM_MONITOR_TRAY = WM_APP + 6;
constexpr UINT WM_MONITOR_SHOW = WM_APP + 7;

inline void SafeCloseHandle(HANDLE& handle) {
  if (handle && handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
    handle = nullptr;
  }
}

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);
std::wstring GetLocalAppDataDirectory();
std::wstring JoinPath(std::wstring_view left, std::wstring_view right);

}  // namespace monitor

