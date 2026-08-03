#include "chatgpt.h"
#include "logging.h"
#include <appmodel.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <cwctype>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Management.Deployment.h>

namespace monitor {
namespace {

std::wstring Lower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return value;
}

bool Contains(std::wstring_view haystack, std::wstring_view needle) {
  return Lower(std::wstring(haystack)).find(Lower(std::wstring(needle))) != std::wstring::npos;
}

std::wstring ProcessPackageFamily(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  UINT32 length = 0;
  LONG result = GetPackageFamilyName(process, &length, nullptr);
  if (result != ERROR_INSUFFICIENT_BUFFER || length == 0) {
    CloseHandle(process);
    return {};
  }
  std::wstring family(length, L'\0');
  result = GetPackageFamilyName(process, &length, family.data());
  CloseHandle(process);
  if (result != ERROR_SUCCESS) return {};
  if (!family.empty() && family.back() == L'\0') family.pop_back();
  return family;
}

std::wstring ProcessImageName(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  std::wstring path(32768, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) size = 0;
  CloseHandle(process);
  path.resize(size);
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool LooksLikeChatGptProcess(DWORD pid, std::wstring_view expectedFamily) {
  const std::wstring family = ProcessPackageFamily(pid);
  if (!expectedFamily.empty() && _wcsicmp(family.c_str(), std::wstring(expectedFamily).c_str()) == 0) {
    return true;
  }
  if (Contains(family, L"chatgpt")) return true;
  const std::wstring image = ProcessImageName(pid);
  return _wcsicmp(image.c_str(), L"ChatGPT.exe") == 0 ||
         (Contains(image, L"chatgpt") && !Contains(image, kProductName));
}

struct WindowSearchContext {
  DWORD primaryPid = 0;
  std::wstring family;
  std::vector<HWND>* windows = nullptr;
};

BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM parameter) {
  auto* context = reinterpret_cast<WindowSearchContext*>(parameter);
  if (IsValidChatGptTopLevelWindow(hwnd, context->primaryPid, context->family)) {
    context->windows->push_back(hwnd);
  }
  return TRUE;
}

}  // namespace

std::vector<ChatGptApplication> FindInstalledChatGptApplications() {
  std::vector<ChatGptApplication> result;
  try {
    winrt::Windows::Management::Deployment::PackageManager manager;
    for (const auto& package : manager.FindPackagesForUser(L"")) {
      const auto id = package.Id();
      const std::wstring name = id.Name().c_str();
      const std::wstring family = id.FamilyName().c_str();
      std::wstring display;
      try { display = package.DisplayName().c_str(); } catch (...) {}
      const bool candidate = Contains(name, L"chatgpt") || Contains(family, L"chatgpt") ||
                             Contains(display, L"chatgpt") || Contains(name, L"openai") ||
                             Contains(family, L"openai");
      if (!candidate) continue;
      try {
        for (const auto& entry : package.GetAppListEntriesAsync().get()) {
          const std::wstring entryDisplay = entry.DisplayInfo().DisplayName().c_str();
          const std::wstring aumid = entry.AppUserModelId().c_str();
          const bool isChatGptEntry = Contains(entryDisplay, L"chatgpt") ||
                                      Contains(display, L"chatgpt") ||
                                      Contains(name, L"chatgpt") ||
                                      Contains(family, L"chatgpt");
          if (!isChatGptEntry) continue;
          ChatGptApplication app;
          app.packageName = name;
          app.packageFamilyName = family;
          app.displayName = entryDisplay.empty() ? display : entryDisplay;
          app.aumid = aumid;
          if (!app.aumid.empty()) result.push_back(std::move(app));
        }
      } catch (...) {
        LogWarning(L"ChatGPT package found, but its application list could not be read.");
      }
    }
  } catch (const winrt::hresult_error& error) {
    LogWarning(std::wstring(L"MSIX package enumeration failed: ") + error.message().c_str());
  } catch (...) {
    LogWarning(L"MSIX package enumeration failed with an unknown error.");
  }
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return a.aumid < b.aumid;
  });
  result.erase(std::unique(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return _wcsicmp(a.aumid.c_str(), b.aumid.c_str()) == 0;
  }), result.end());
  return result;
}

bool IsValidChatGptTopLevelWindow(HWND hwnd, DWORD primaryPid,
                                  std::wstring_view packageFamilyName) {
  if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (!pid || (pid != primaryPid && !LooksLikeChatGptProcess(pid, packageFamilyName))) return false;
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)) return false;
  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) return false;
  if (IsIconic(hwnd)) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement)) rect = placement.rcNormalPosition;
  }
  return rect.right - rect.left >= 80 && rect.bottom - rect.top >= 40;
}

std::vector<HWND> FindChatGptTopLevelWindows(DWORD primaryPid,
                                             std::wstring_view packageFamilyName) {
  std::vector<HWND> windows;
  WindowSearchContext context{primaryPid, std::wstring(packageFamilyName), &windows};
  EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&context));
  return windows;
}

std::optional<ChatGptInstance> FindRunningChatGptByPid(DWORD processId) {
  if (!processId || !LooksLikeChatGptProcess(processId, {})) return std::nullopt;
  ChatGptInstance instance;
  instance.processId = processId;
  instance.packageFamilyName = ProcessPackageFamily(processId);
  const auto windows = FindChatGptTopLevelWindows(processId, instance.packageFamilyName);
  if (!windows.empty()) instance.mainWindow = windows.front();
  return instance;
}

std::optional<ChatGptInstance> FindRunningChatGpt() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  std::optional<ChatGptInstance> found;
  if (Process32FirstW(snapshot, &entry)) {
    do {
      const std::wstring image = entry.szExeFile;
      if ((_wcsicmp(image.c_str(), L"ChatGPT.exe") == 0 || Contains(image, L"chatgpt")) &&
          !Contains(image, kProductName)) {
        ChatGptInstance instance;
        instance.processId = entry.th32ProcessID;
        instance.packageFamilyName = ProcessPackageFamily(instance.processId);
        const auto windows = FindChatGptTopLevelWindows(instance.processId,
                                                        instance.packageFamilyName);
        if (!windows.empty()) instance.mainWindow = windows.front();
        found = std::move(instance);
        if (found->mainWindow) break;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return found;
}

}  // namespace monitor
