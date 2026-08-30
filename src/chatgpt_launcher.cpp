#include "chatgpt.h"
#include <shobjidl.h>

namespace monitor {

std::optional<ChatGptInstance> ActivateChatGpt(const ChatGptApplication& app,
                                               std::wstring* error) {
  IApplicationActivationManager* manager = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
  if (FAILED(hr)) {
    if (error) *error = L"Unable to create the Windows application activation manager.";
    return std::nullopt;
  }
  DWORD pid = 0;
  hr = manager->ActivateApplication(app.aumid.c_str(), nullptr, AO_NONE, &pid);
  manager->Release();
  if (FAILED(hr) || !pid) {
    if (error) {
      wchar_t buffer[128]{};
      swprintf_s(buffer, L"Failed to launch ChatGPT (HRESULT 0x%08X).", static_cast<unsigned>(hr));
      *error = buffer;
    }
    return std::nullopt;
  }
  ChatGptInstance instance;
  instance.processId = pid;
  instance.packageFamilyName = app.packageFamilyName;
  return instance;
}

bool ActivateChatGptWindow(HWND hwnd) {
  if (!IsWindow(hwnd)) return false;
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  else ShowWindow(hwnd, SW_SHOW);
  return SetForegroundWindow(hwnd) != FALSE;
}

}  // namespace monitor

