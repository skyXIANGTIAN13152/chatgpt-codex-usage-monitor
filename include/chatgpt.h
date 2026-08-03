#pragma once

#include "common.h"
#include <unordered_set>

namespace monitor {

struct ChatGptApplication {
  std::wstring packageName;
  std::wstring packageFamilyName;
  std::wstring displayName;
  std::wstring aumid;
};

struct ChatGptInstance {
  DWORD processId = 0;
  std::wstring packageFamilyName;
  HWND mainWindow = nullptr;
};

std::vector<ChatGptApplication> FindInstalledChatGptApplications();
std::optional<ChatGptInstance> FindRunningChatGpt();
std::optional<ChatGptInstance> FindRunningChatGptByPid(DWORD processId);
bool IsValidChatGptTopLevelWindow(HWND hwnd, DWORD primaryPid,
                                  std::wstring_view packageFamilyName);
std::vector<HWND> FindChatGptTopLevelWindows(DWORD primaryPid,
                                             std::wstring_view packageFamilyName);
std::optional<ChatGptInstance> ActivateChatGpt(const ChatGptApplication& app,
                                               std::wstring* error);
bool ActivateChatGptWindow(HWND hwnd);

class ChatGptLifecycleMonitor {
 public:
  ChatGptLifecycleMonitor() = default;
  ~ChatGptLifecycleMonitor();
  ChatGptLifecycleMonitor(const ChatGptLifecycleMonitor&) = delete;
  ChatGptLifecycleMonitor& operator=(const ChatGptLifecycleMonitor&) = delete;

  bool Start(HWND notificationWindow, const ChatGptInstance& instance);
  void Stop();
  bool HasAnyWindow() const;
  HWND BestWindow() const;
  DWORD ProcessId() const { return instance_.processId; }
  const std::wstring& PackageFamilyName() const { return instance_.packageFamilyName; }

 private:
  static void CALLBACK WinEventCallback(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                        LONG objectId, LONG childId,
                                        DWORD eventThread, DWORD eventTime);
  static void CALLBACK ProcessWaitCallback(void* context, BOOLEAN timedOut);
  ChatGptInstance instance_;
  HWND notificationWindow_ = nullptr;
  HANDLE processHandle_ = nullptr;
  HANDLE processWait_ = nullptr;
  HWINEVENTHOOK eventHook_ = nullptr;
};

}  // namespace monitor

