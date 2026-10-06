#include "overlay_window.h"
#include <iostream>
#include <objbase.h>

namespace monitor {
// All HWNDs and fake servers are owned by this test. Preview creation and
// cleanup prevent the test from reading credentials or saving live settings.
struct OverlayWindowTestAccess {
  static void Start(OverlayWindow& window) {
    window.snapshot_.reset();
    window.previousRemaining_ = 101.0;
    window.demoMode_ = false;
    window.StartCodex();
    window.demoMode_ = true;
  }
  static void Preview(OverlayWindow& window) { window.demoMode_ = true; }
  static DataStatus Status(const OverlayWindow& window) { return window.displayStatus_; }
  static DWORD ServerPid(const OverlayWindow& window) { return window.appServer_.ProcessId(); }
  static WPARAM Generation(const OverlayWindow& window) { return window.serverGeneration_; }
  static bool InFlight(const OverlayWindow& window) { return window.appServer_.RequestInFlight(); }
  static int Failures(const OverlayWindow& window) { return window.consecutiveFailures_; }
  static double Remaining(const OverlayWindow& window) {
    const auto* quota = window.LimitingQuotaWindow();
    return quota ? quota->remainingPercent : -1;
  }
  static void AuthenticationError(OverlayWindow& window) {
    window.demoMode_ = false;
    window.ApplyServerError({AppServerErrorKind::Unauthorized, L"Test expired login"});
    window.demoMode_ = true;
  }
  static void Refresh(OverlayWindow& window) {
    window.demoMode_ = false;
    window.RequestRefresh(true);
    window.demoMode_ = true;
  }
  static void RetryTimer(OverlayWindow& window) {
    window.demoMode_ = false;
    SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    window.demoMode_ = true;
  }
  static void Dispatch(const MSG& message) {
    // Only server callbacks and their timers need live scheduling. Keep move,
    // paint, and menu messages in preview mode so they cannot save settings.
    const bool serverMessage = message.message == WM_MONITOR_SNAPSHOT ||
        message.message == WM_MONITOR_SERVER_ERROR ||
        (message.message == WM_TIMER && (message.wParam == 1 || message.wParam == 3));
    wchar_t className[256]{};
    GetClassNameW(message.hwnd, className, static_cast<int>(std::size(className)));
    auto* window = serverMessage && std::wstring(className) == kPreviewWindowClass
        ? reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(message.hwnd, GWLP_USERDATA)) : nullptr;
    if (window) window->demoMode_ = false;
    TranslateMessage(&message);
    DispatchMessageW(&message);
    if (window) window->demoMode_ = true;
  }
};
}  // namespace monitor

namespace {
using namespace monitor;
using Access = OverlayWindowTestAccess;
int checks = 0, failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
  std::cerr << "FAIL " << __LINE__ << " : " #condition "\n"; } } while (false)

template<class Predicate>
bool PumpUntil(Predicate predicate, DWORD timeout) {
  const ULONGLONG deadline = GetTickCount64() + timeout;
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message != WM_QUIT) {
        Access::Dispatch(message);
      }
    }
    if (predicate()) return true;
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
  } while (GetTickCount64() < deadline);
  return predicate();
}

void AutomaticRecovery(const wchar_t* scenario) {
  _wputenv_s(L"FAKE_CODEX_SCENARIO", scenario);
  Settings settings;
  OverlayWindow window(GetModuleHandleW(nullptr), settings, {}, true);
  CHECK(window.Create());
  if (!window.Handle()) return;
  ShowWindow(window.Handle(), SW_HIDE);
  Access::Start(window);
  const DWORD oldServer = Access::ServerPid(window);
  const WPARAM oldGeneration = Access::Generation(window);
  CHECK(oldServer != 0);
  CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::NotLoggedIn &&
                              !Access::InFlight(window); }, 3000));
  // The old fake captures the expired state at startup. Only a newly launched
  // server can observe the refreshed login, just like the actual failure.
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"normal");
  CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::Live; }, 3500));
  CHECK(Access::ServerPid(window) != oldServer);
  CHECK(Access::Remaining(window) == 55);
  CHECK(Access::Failures(window) == 0);
  // An old reader may have already posted data before Stop() joins it. Neither
  // a delayed authentication error nor an old quota may overwrite recovery.
  const DWORD recoveredServer = Access::ServerPid(window);
  SendMessageW(window.Handle(), WM_MONITOR_SERVER_ERROR, oldGeneration,
      reinterpret_cast<LPARAM>(new AppServerError{AppServerErrorKind::Unauthorized, L"Old login error"}));
  auto oldSnapshot = ParseRateLimitMessage(
      R"({"result":{"rateLimits":{"primary":{"usedPercent":99,"windowDurationMins":300}}}})", nullptr);
  CHECK(oldSnapshot.has_value());
  if (oldSnapshot) {
    SendMessageW(window.Handle(), WM_MONITOR_SNAPSHOT, oldGeneration,
        reinterpret_cast<LPARAM>(new RateLimitSnapshot(std::move(*oldSnapshot))));
  }
  CHECK(Access::Status(window) == DataStatus::Live);
  CHECK(Access::ServerPid(window) == recoveredServer);
  CHECK(Access::Remaining(window) == 55);
  CHECK(Access::Failures(window) == 0);
  Access::Preview(window);
  DestroyWindow(window.Handle());
}

void PersistentSignOut() {
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"401");
  Settings settings;
  OverlayWindow window(GetModuleHandleW(nullptr), settings, {}, true);
  CHECK(window.Create());
  if (!window.Handle()) return;
  ShowWindow(window.Handle(), SW_HIDE);
  Access::Start(window);
  const DWORD firstServer = Access::ServerPid(window);
  CHECK(PumpUntil([&] { return Access::ServerPid(window) != firstServer &&
                              Access::Status(window) == DataStatus::NotLoggedIn &&
                              !Access::InFlight(window); }, 3500));
  const DWORD retryServer = Access::ServerPid(window);
  // A real sign-out must not cause a new child process every second.
  CHECK(!PumpUntil([&] { return Access::ServerPid(window) != retryServer; }, 2200));
  CHECK(Access::Status(window) == DataStatus::NotLoggedIn);
  CHECK(Access::Failures(window) == 1);
  // The scheduled retry also reloads a login that arrives after the quick
  // recovery. Deliver its timer now rather than sleeping through the backoff.
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"normal");
  Access::RetryTimer(window);
  CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::Live; }, 3000));
  CHECK(Access::ServerPid(window) != retryServer);
  CHECK(Access::Failures(window) == 0);
  // Clicking Refresh bypasses the backoff for another expired connection.
  const DWORD beforeManualRefresh = Access::ServerPid(window);
  Access::AuthenticationError(window);
  Access::Refresh(window);
  CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::Live; }, 3000));
  CHECK(Access::ServerPid(window) != beforeManualRefresh);
  // After a successful read, a later expiry gets its own automatic recovery.
  const DWORD liveServer = Access::ServerPid(window);
  Access::AuthenticationError(window);
  CHECK(PumpUntil([&] { return Access::ServerPid(window) != liveServer &&
                              Access::Status(window) == DataStatus::Live; }, 3500));
  Access::Preview(window);
  DestroyWindow(window.Handle());
}

void RateLimitDoesNotRestart() {
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"429");
  Settings settings;
  OverlayWindow window(GetModuleHandleW(nullptr), settings, {}, true);
  CHECK(window.Create());
  if (!window.Handle()) return;
  ShowWindow(window.Handle(), SW_HIDE);
  Access::Start(window);
  const DWORD server = Access::ServerPid(window);
  CHECK(PumpUntil([&] { return Access::Failures(window) > 0 &&
                              !Access::InFlight(window); }, 3000));
  CHECK(!PumpUntil([&] { return Access::ServerPid(window) != server; }, 1200));
  CHECK(Access::Status(window) != DataStatus::NotLoggedIn);
  Access::Preview(window);
  DestroyWindow(window.Handle());
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  _wputenv_s(L"CODEX_EXECUTABLE", argv[1]);
  AutomaticRecovery(L"401");
  AutomaticRecovery(L"signed-out");
  PersistentSignOut();
  RateLimitDoesNotRestart();
  if (SUCCEEDED(apartment)) CoUninitialize();
  std::cout << "Authentication recovery checks: " << checks << ", failures: " << failures << '\n';
  return failures ? 1 : 0;
}
