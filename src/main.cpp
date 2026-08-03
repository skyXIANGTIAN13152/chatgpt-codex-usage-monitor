#include "overlay_window.h"
#include "logging.h"
#include <shellapi.h>
#include <winrt/base.h>

namespace monitor {
namespace {

HANDLE g_windowEvent = nullptr;

void CALLBACK LaunchWindowEvent(HWINEVENTHOOK, DWORD, HWND, LONG objectId,
                                LONG, DWORD, DWORD) {
  if (objectId == OBJID_WINDOW && g_windowEvent) SetEvent(g_windowEvent);
}

HWND WaitForChatGptWindow(const ChatGptInstance& instance, DWORD timeoutMs) {
  auto windows = FindChatGptTopLevelWindows(instance.processId, instance.packageFamilyName);
  if (!windows.empty()) return windows.front();
  g_windowEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_windowEvent) return nullptr;
  HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr,
                                       LaunchWindowEvent, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  HWND result = nullptr;
  while (GetTickCount64() < deadline) {
    const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(deadline - GetTickCount64(), 5000));
    const DWORD wait = MsgWaitForMultipleObjects(1, &g_windowEvent, FALSE, remaining, QS_ALLINPUT);
    if (wait == WAIT_OBJECT_0) {
      windows = FindChatGptTopLevelWindows(instance.processId, instance.packageFamilyName);
      if (!windows.empty()) { result = windows.front(); break; }
    } else if (wait == WAIT_OBJECT_0 + 1) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    } else if (wait == WAIT_TIMEOUT) {
      windows = FindChatGptTopLevelWindows(instance.processId, instance.packageFamilyName);
      if (!windows.empty()) { result = windows.front(); break; }
    } else {
      break;
    }
  }
  if (hook) UnhookWinEvent(hook);
  CloseHandle(g_windowEvent);
  g_windowEvent = nullptr;
  return result;
}

std::vector<std::wstring> Arguments() {
  int count = 0;
  LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::wstring> args;
  if (raw) {
    for (int i = 1; i < count; ++i) args.emplace_back(raw[i]);
    LocalFree(raw);
  }
  return args;
}

bool HasArgument(const std::vector<std::wstring>& args, std::wstring_view value) {
  return std::any_of(args.begin(), args.end(), [value](const auto& item) {
    return _wcsicmp(item.c_str(), std::wstring(value).c_str()) == 0;
  });
}

std::optional<DWORD> MonitorPid(const std::vector<std::wstring>& args) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (_wcsicmp(args[i].c_str(), L"--monitor") == 0) {
      wchar_t* end = nullptr;
      const unsigned long value = wcstoul(args[i + 1].c_str(), &end, 10);
      if (end && *end == L'\0' && value > 0) return static_cast<DWORD>(value);
    }
  }
  return std::nullopt;
}

std::optional<ChatGptInstance> LaunchOrFindChatGpt(std::wstring* error) {
  if (auto running = FindRunningChatGpt()) {
    if (running->mainWindow) ActivateChatGptWindow(running->mainWindow);
    return running;
  }
  std::vector<ChatGptApplication> applications = FindInstalledChatGptApplications();
  if (applications.empty()) {
    applications.push_back({L"OpenAI.Codex", L"OpenAI.Codex_2p2nqsd0c76g0",
                            L"ChatGPT", L"OpenAI.Codex_2p2nqsd0c76g0!App"});
    applications.push_back({L"OpenAI.ChatGPT-Desktop", L"OpenAI.ChatGPT-Desktop_2p2nqsd0c76g0",
                            L"ChatGPT", L"OpenAI.ChatGPT-Desktop_2p2nqsd0c76g0!App"});
    applications.push_back({L"OpenAI.ChatGPT", L"OpenAI.ChatGPT_2p2nqsd0c76g0",
                            L"ChatGPT", L"OpenAI.ChatGPT_2p2nqsd0c76g0!App"});
  }
  std::wstring lastError;
  for (const auto& application : applications) {
    if (auto instance = ActivateChatGpt(application, &lastError)) {
      instance->mainWindow = WaitForChatGptWindow(*instance, 30000);
      return instance;
    }
  }
  if (error) {
    *error = L"未检测到可启动的 Microsoft Store / MSIX 版 ChatGPT。\n\n"
             L"程序已动态枚举软件包并尝试候选 AUMID，但没有找到有效安装。";
    if (!lastError.empty()) *error += L"\n\n最后错误：" + lastError;
  }
  return std::nullopt;
}

}  // namespace
}  // namespace monitor

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  using namespace monitor;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  const std::vector<std::wstring> args = Arguments();
  Settings settings = LoadSettings();
  if (HasArgument(args, L"--debug")) settings.debugLogging = true;
  Logger::Instance().Initialize(settings.debugLogging);

  if (HasArgument(args, L"--check-environment")) {
    int result = 0;
    if (FindInstalledChatGptApplications().empty() && !FindRunningChatGpt()) result |= 1;
    if (!CodexAppServer::LocateCodexExecutable()) result |= 2;
    return result;
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (!mutex) return 2;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
      PostMessageW(existing, WM_MONITOR_SHOW, 0, 0);
      if (auto running = FindRunningChatGpt(); running && running->mainWindow) {
        ActivateChatGptWindow(running->mainWindow);
      }
    }
    CloseHandle(mutex);
    return 0;
  }

  const bool demoMode = HasArgument(args, L"--demo");
  ChatGptInstance chatGpt;
  if (!demoMode) {
    std::optional<ChatGptInstance> instanceInfo;
    if (const auto pid = MonitorPid(args)) {
      instanceInfo = FindRunningChatGptByPid(*pid);
      if (!instanceInfo) {
        MessageBoxW(nullptr, L"--monitor 指定的进程不是正在运行的 ChatGPT。",
                    kProductName, MB_OK | MB_ICONERROR);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
      }
      instanceInfo->mainWindow = WaitForChatGptWindow(*instanceInfo, 15000);
    } else {
      std::wstring error;
      instanceInfo = LaunchOrFindChatGpt(&error);
      if (!instanceInfo) {
        MessageBoxW(nullptr, error.c_str(), kProductName, MB_OK | MB_ICONERROR);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 4;
      }
    }
    chatGpt = std::move(*instanceInfo);
  }

  OverlayWindow overlay(instance, std::move(settings), std::move(chatGpt), demoMode);
  if (!overlay.Create()) {
    MessageBoxW(nullptr, L"无法创建额度 HUD。", kProductName, MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 5;
  }
  const int result = overlay.RunMessageLoop();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return result;
}
