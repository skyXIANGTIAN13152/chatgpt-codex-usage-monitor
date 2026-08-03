#include "chatgpt.h"
#include "codex_app_server.h"
#include "energy_indicator.h"
#include "hud_controls.h"
#include "rate_limits.h"
#include <windows.h>
#include <cmath>
#include <iostream>
#include <psapi.h>
#include <string>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition) do { ++checks; if (!(condition)) { \
  ++failures; std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << " : " #condition "\n"; \
} } while (false)

void TestRemainingPercent() {
  CHECK(monitor::RemainingPercent(0) == 100);
  CHECK(monitor::RemainingPercent(45) == 55);
  CHECK(monitor::RemainingPercent(100) == 0);
  CHECK(monitor::RemainingPercent(-20) == 100);
  CHECK(monitor::RemainingPercent(130) == 0);
}

void TestParsing() {
  std::string error;
  auto normal = monitor::ParseRateLimitMessage(
      R"({"id":6,"result":{"rateLimits":{"limitId":"codex","limitName":null,"primary":{"usedPercent":25,"windowDurationMins":15,"resetsAt":1730947200},"secondary":null},"credits":{"hasCredits":true,"balance":7.5},"planType":"plus","newFutureField":{"x":1}}})", &error);
  CHECK(normal.has_value());
  CHECK(normal && normal->windows.size() == 1);
  CHECK(normal && normal->windows[0].remainingPercent == 75);
  CHECK(normal && normal->credits.balance && *normal->credits.balance == 7.5);
  CHECK(normal && normal->planType == "plus");

  auto missing = monitor::ParseRateLimitMessage(
      R"({"result":{"rateLimits":{"limitId":"codex","primary":null,"secondary":{"usedPercent":50,"windowDurationMins":10080,"resetsAt":"2030-01-01T00:00:00Z"}}}})", &error);
  CHECK(missing && missing->windows.size() == 1);
  CHECK(missing && missing->windows[0].windowName == "secondary");
  CHECK(missing && missing->windows[0].remainingPercent == 50);

  auto multi = monitor::ParseRateLimitMessage(
      R"({"result":{"rateLimits":{"limitId":"legacy","primary":{"usedPercent":1}},"rateLimitsByLimitId":{"codex":{"limitId":"codex","primary":{"usedPercent":40,"windowDurationMins":300,"resetsAt":1893456000},"secondary":{"usedPercent":60,"windowDurationMins":10080,"resetsAt":1893542400}},"model-x":{"limitId":"model-x","limitName":"Model X","primary":{"usedPercent":10,"resetsAt":1893456000000}}},"rateLimitResetCredits":{"availableCount":2,"credits":[]}}})", &error);
  CHECK(multi && multi->windows.size() == 3);
  CHECK(multi && multi->credits.resetCreditsAvailable && *multi->credits.resetCreditsAvailable == 2);

  auto unlimited = monitor::ParseRateLimitMessage(
      R"({"result":{"rateLimits":{"usedPercent":0},"credits":{"unlimited":true}}})", &error);
  CHECK(unlimited && unlimited->credits.unlimited);

  auto old = monitor::ParseRateLimitMessage(
      R"({"result":{"primary":{"usedPercent":"75","windowDurationMins":60,"resetsAt":"1893456000"},"unknown":123}})", &error);
  CHECK(old && old->windows.size() == 1);
  CHECK(old && old->windows[0].remainingPercent == 25);

  auto invalid = monitor::ParseRateLimitMessage("{bad", &error);
  CHECK(!invalid.has_value());
  CHECK(!error.empty());
  auto empty = monitor::ParseRateLimitMessage(R"({"result":{"rateLimits":{"primary":null}}})", &error);
  CHECK(!empty.has_value());
}

void TestResetAndStale() {
  monitor::JsonValue unixSeconds(1893456000.0);
  monitor::JsonValue unixMillis(1893456000000.0);
  monitor::JsonValue iso(std::string("2030-01-01T11:00:00+11:00"));
  auto a = monitor::ParseResetTime(unixSeconds);
  auto b = monitor::ParseResetTime(unixMillis);
  auto c = monitor::ParseResetTime(iso);
  CHECK(a && b && c);
  CHECK(a && b && *a == *b);
  CHECK(a && c && *a == *c);

  monitor::RateLimitSnapshot snapshot;
  const auto now = std::chrono::system_clock::now();
  snapshot.lastSuccessAt = now - std::chrono::minutes(10);
  CHECK(monitor::IsSnapshotStale(snapshot, now, std::chrono::minutes(5)));
  snapshot.lastSuccessAt = now - std::chrono::seconds(10);
  CHECK(!monitor::IsSnapshotStale(snapshot, now, std::chrono::minutes(5)));
  CHECK(!monitor::DataStatusText(monitor::DataStatus::NetworkUnavailable).empty());
}

void TestSydneyDst() {
  DYNAMIC_TIME_ZONE_INFORMATION zone{};
  bool found = false;
  for (DWORD index = 0;; ++index) {
    const DWORD result = EnumDynamicTimeZoneInformation(index, &zone);
    if (result == ERROR_NO_MORE_ITEMS) break;
    if (result != ERROR_SUCCESS) continue;
    if (_wcsicmp(zone.TimeZoneKeyName, L"AUS Eastern Standard Time") == 0) { found = true; break; }
  }
  CHECK(found);
  if (!found) return;
  SYSTEMTIME january{2026, 1, 0, 15, 0, 0, 0, 0};
  SYSTEMTIME july{2026, 7, 0, 15, 0, 0, 0, 0};
  SYSTEMTIME janLocal{}, julLocal{};
  CHECK(SystemTimeToTzSpecificLocalTimeEx(&zone, &january, &janLocal));
  CHECK(SystemTimeToTzSpecificLocalTimeEx(&zone, &july, &julLocal));
  CHECK(janLocal.wHour == 11);
  CHECK(julLocal.wHour == 10);
}

void TestEnergyState() {
  monitor::EnergyIndicatorConfig config;
  auto s100 = monitor::ComputeEnergyState(100.0, config);
  auto s51 = monitor::ComputeEnergyState(51.0, config);
  auto s50 = monitor::ComputeEnergyState(50.0, config);
  auto s30 = monitor::ComputeEnergyState(30.0, config);
  auto s10 = monitor::ComputeEnergyState(10.0, config);
  auto s1 = monitor::ComputeEnergyState(1.0, config);
  auto s0 = monitor::ComputeEnergyState(0.0, config);
  auto unavailable = monitor::ComputeEnergyState(std::nullopt, config);
  CHECK(s100.visual == monitor::EnergyVisualState::NormalBlue && s100.fullCycleMs == 0);
  CHECK(s51.visual == monitor::EnergyVisualState::NormalBlue && s51.fullCycleMs == 0);
  CHECK(s50.visual == monitor::EnergyVisualState::WarningRedBlink && s50.fullCycleMs == 1200);
  CHECK(s30.fullCycleMs == 900);
  CHECK(s10.visual == monitor::EnergyVisualState::CriticalRedFastBlink && s10.fullCycleMs == 400);
  CHECK(s1.fullCycleMs == 250);
  CHECK(s0.visual == monitor::EnergyVisualState::Stone && s0.fullCycleMs == 0);
  CHECK(unavailable.visual == monitor::EnergyVisualState::DataUnavailable && unavailable.fullCycleMs == 0);
  CHECK(s1.fullCycleMs / 2 >= 100);
}

void TestHudInteractions() {
  using monitor::HudControl;
  // Compact layout at 100% DPI: the entire visible dash area minimizes.
  CHECK(monitor::HitTestHudControl(250, 73, 300, 90, 96) == HudControl::Settings);
  CHECK(monitor::HitTestHudControl(261, 73, 300, 90, 96) == HudControl::Minimize);
  CHECK(monitor::HitTestHudControl(271, 73, 300, 90, 96) == HudControl::Minimize);
  CHECK(monitor::HitTestHudControl(235, 73, 300, 90, 96) == HudControl::Settings);
  CHECK(monitor::HitTestHudControl(220, 73, 300, 90, 96) == HudControl::None);

  // The same DIP positions must map to the same controls at 150% DPI.
  CHECK(monitor::HitTestHudControl(390, 109, 450, 135, 144) == HudControl::Settings);
  CHECK(monitor::HitTestHudControl(392, 109, 450, 135, 144) == HudControl::Minimize);
  CHECK(monitor::HitTestHudControl(415, 109, 450, 135, 144) == HudControl::Minimize);

  // Some shells emit both mouse-up and NIN_SELECT. Repeating restore stays visible.
  bool hidden = true;
  hidden = monitor::HiddenAfterTrayPrimaryActivation(hidden);
  CHECK(!hidden);
  hidden = monitor::HiddenAfterTrayPrimaryActivation(hidden);
  CHECK(!hidden);
}

LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

void TestLifecyclePrimitives() {
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpfnWndProc = TestWindowProc;
  wc.lpszClassName = L"MonitorLifecycleTestWindow";
  RegisterClassW(&wc);
  HWND window = CreateWindowW(wc.lpszClassName, L"test", WS_OVERLAPPEDWINDOW,
                              100, 100, 500, 300, nullptr, nullptr, wc.hInstance, nullptr);
  CHECK(window != nullptr);
  CHECK(monitor::IsValidChatGptTopLevelWindow(window, GetCurrentProcessId(), {}));
  ShowWindow(window, SW_MINIMIZE);
  CHECK(monitor::IsValidChatGptTopLevelWindow(window, GetCurrentProcessId(), {}));
  ShowWindow(window, SW_HIDE);
  CHECK(monitor::IsValidChatGptTopLevelWindow(window, GetCurrentProcessId(), {}));
  DestroyWindow(window);
  CHECK(!monitor::IsValidChatGptTopLevelWindow(window, GetCurrentProcessId(), {}));

  const wchar_t* name = L"Local\\ChatGPTCodexUsageMonitor.TestMutex";
  HANDLE first = CreateMutexW(nullptr, FALSE, name);
  HANDLE second = CreateMutexW(nullptr, FALSE, name);
  CHECK(first && second && GetLastError() == ERROR_ALREADY_EXISTS);
  if (second) CloseHandle(second);
  if (first) CloseHandle(first);
}

int Integration(const wchar_t* fakePath) {
  _wputenv_s(L"CODEX_EXECUTABLE", fakePath);
  struct Context {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    int snapshots = 0;
    int errors = 0;
    monitor::RateLimitSnapshot last;
    monitor::AppServerError lastError;
  } context;

  auto run = [&](const wchar_t* scenario, bool expectSnapshot,
                 monitor::AppServerErrorKind expectedError, int minSnapshots = 1) {
    ResetEvent(context.event);
    context.snapshots = context.errors = 0;
    _wputenv_s(L"FAKE_CODEX_SCENARIO", scenario);
    monitor::CodexAppServer server;
    server.Start([&](monitor::RateLimitSnapshot value) {
      context.last = std::move(value); ++context.snapshots; SetEvent(context.event);
    }, [&](monitor::AppServerError error) {
      context.lastError = std::move(error); ++context.errors; SetEvent(context.event);
    });
    WaitForSingleObject(context.event, 3000);
    if (std::wstring(scenario) == L"notification") Sleep(100);
    if (std::wstring(scenario) == L"timeout") {
      server.MarkRequestTimedOut();
      CHECK(context.snapshots == 0);
    } else if (expectSnapshot) {
      CHECK(context.snapshots >= minSnapshots);
      CHECK(!context.last.windows.empty());
    } else {
      CHECK(context.errors > 0);
      CHECK(context.lastError.kind == expectedError);
    }
    server.Stop();
  };
  run(L"normal", true, monitor::AppServerErrorKind::Protocol);
  CHECK(context.last.windows[0].remainingPercent == 55);
  run(L"zero", true, monitor::AppServerErrorKind::Protocol);
  CHECK(context.last.windows[0].remainingPercent == 0);
  run(L"double", true, monitor::AppServerErrorKind::Protocol);
  CHECK(context.last.windows.size() == 2);
  run(L"unlimited", true, monitor::AppServerErrorKind::Protocol);
  CHECK(context.last.credits.unlimited);
  run(L"credits", true, monitor::AppServerErrorKind::Protocol);
  CHECK(context.last.credits.balance && *context.last.credits.balance == 12.5);
  run(L"401", false, monitor::AppServerErrorKind::Unauthorized);
  run(L"429", false, monitor::AppServerErrorKind::RateLimited);
  run(L"timeout", false, monitor::AppServerErrorKind::Timeout);
  run(L"close", false, monitor::AppServerErrorKind::Closed);
  run(L"notification", true, monitor::AppServerErrorKind::Protocol, 2);
  CloseHandle(context.event);
  std::cout << "Integration checks complete\n";
  return failures ? 1 : 0;
}

int Benchmark(const wchar_t* fakePath) {
  _wputenv_s(L"CODEX_EXECUTABLE", fakePath);
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"normal");
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::atomic<int> snapshots{0};
  std::atomic<int> errors{0};
  monitor::CodexAppServer server;
  const auto startup = std::chrono::steady_clock::now();
  if (!server.Start([&](monitor::RateLimitSnapshot) {
        ++snapshots; SetEvent(event);
      }, [&](monitor::AppServerError) {
        ++errors; SetEvent(event);
      })) return 2;
  if (WaitForSingleObject(event, 3000) != WAIT_OBJECT_0 || snapshots.load() == 0) return 3;
  const auto firstMs = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - startup).count();
  double totalMs = 0.0;
  int measured = 0;
  for (int i = 0; i < 10; ++i) {
    ResetEvent(event);
    const auto start = std::chrono::steady_clock::now();
    if (!server.RequestRateLimits()) { Sleep(5); --i; continue; }
    if (WaitForSingleObject(event, 3000) != WAIT_OBJECT_0) break;
    totalMs += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    ++measured;
  }
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, server.ProcessId());
  if (process) GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                    sizeof(memory));
  if (process) CloseHandle(process);
  std::cout << "first_refresh_ms=" << firstMs << '\n'
            << "mean_refresh_ms=" << (measured ? totalMs / measured : -1.0) << '\n'
            << "app_server_working_set_mb=" << memory.WorkingSetSize / 1048576.0 << '\n'
            << "app_server_private_mb=" << memory.PrivateUsage / 1048576.0 << '\n'
            << "samples=" << measured << " errors=" << errors.load() << '\n';
  server.Stop();
  CloseHandle(event);
  return measured == 10 && errors.load() == 0 ? 0 : 4;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 3 && std::wstring(argv[1]) == L"--integration") return Integration(argv[2]);
  if (argc >= 3 && std::wstring(argv[1]) == L"--benchmark") return Benchmark(argv[2]);
  TestRemainingPercent();
  TestParsing();
  TestResetAndStale();
  TestSydneyDst();
  TestEnergyState();
  TestHudInteractions();
  TestLifecyclePrimitives();
  std::cout << "Checks: " << checks << ", failures: " << failures << '\n';
  return failures ? 1 : 0;
}
