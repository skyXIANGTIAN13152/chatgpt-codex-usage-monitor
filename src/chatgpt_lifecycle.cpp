#include "chatgpt.h"

namespace monitor {
namespace {
std::atomic<ChatGptLifecycleMonitor*> g_lifecycle{nullptr};
}

ChatGptLifecycleMonitor::~ChatGptLifecycleMonitor() { Stop(); }

bool ChatGptLifecycleMonitor::Start(HWND notificationWindow,
                                    const ChatGptInstance& instance) {
  Stop();
  instance_ = instance;
  notificationWindow_ = notificationWindow;
  processHandle_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, instance.processId);
  if (processHandle_) {
    RegisterWaitForSingleObject(&processWait_, processHandle_, ProcessWaitCallback, this,
                                INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD);
  }
  g_lifecycle.store(this);
  eventHook_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE,
                               nullptr, WinEventCallback, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  return processHandle_ != nullptr || eventHook_ != nullptr;
}

void ChatGptLifecycleMonitor::Stop() {
  ChatGptLifecycleMonitor* expected = this;
  g_lifecycle.compare_exchange_strong(expected, nullptr);
  if (eventHook_) {
    UnhookWinEvent(eventHook_);
    eventHook_ = nullptr;
  }
  if (processWait_) {
    UnregisterWaitEx(processWait_, INVALID_HANDLE_VALUE);
    processWait_ = nullptr;
  }
  SafeCloseHandle(processHandle_);
  notificationWindow_ = nullptr;
}

void CALLBACK ChatGptLifecycleMonitor::WinEventCallback(HWINEVENTHOOK, DWORD event,
                                                        HWND hwnd, LONG objectId,
                                                        LONG childId, DWORD, DWORD) {
  ChatGptLifecycleMonitor* monitor = g_lifecycle.load();
  if (!monitor || !monitor->notificationWindow_ || objectId != OBJID_WINDOW || childId != 0) return;
  DWORD pid = 0;
  if (hwnd) GetWindowThreadProcessId(hwnd, &pid);
  if (pid != monitor->instance_.processId &&
      !IsValidChatGptTopLevelWindow(hwnd, monitor->instance_.processId,
                                    monitor->instance_.packageFamilyName)) return;
  PostMessageW(monitor->notificationWindow_, WM_MONITOR_WINDOW_EVENT,
               static_cast<WPARAM>(event), reinterpret_cast<LPARAM>(hwnd));
}

void CALLBACK ChatGptLifecycleMonitor::ProcessWaitCallback(void* context, BOOLEAN) {
  auto* monitor = static_cast<ChatGptLifecycleMonitor*>(context);
  if (monitor && monitor->notificationWindow_) {
    PostMessageW(monitor->notificationWindow_, WM_MONITOR_PROCESS_EXIT, 0, 0);
  }
}

bool ChatGptLifecycleMonitor::HasAnyWindow() const {
  return !FindChatGptTopLevelWindows(instance_.processId,
                                     instance_.packageFamilyName).empty();
}

HWND ChatGptLifecycleMonitor::BestWindow() const {
  const auto windows = FindChatGptTopLevelWindows(instance_.processId,
                                                  instance_.packageFamilyName);
  for (HWND window : windows) if (IsWindowVisible(window)) return window;
  return windows.empty() ? nullptr : windows.front();
}

}  // namespace monitor

