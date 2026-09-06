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

void TestDataAge() {
  using namespace std::chrono;
  const system_clock::time_point now{seconds(1900000000)};
  CHECK(monitor::FormatDataAge({}, now) == L"never");
  CHECK(monitor::FormatDataAge(now, now) == L"now");
  CHECK(monitor::FormatDataAge(now - seconds(59), now) == L"now");
  CHECK(monitor::FormatDataAge(now - minutes(1), now) == L"1m ago");
  CHECK(monitor::FormatDataAge(now - minutes(59), now) == L"59m ago");
  CHECK(monitor::FormatDataAge(now - hours(1), now) == L"1h ago");
  CHECK(monitor::FormatDataAge(now - hours(23), now) == L"23h ago");
  CHECK(monitor::FormatDataAge(now - hours(24), now) == L"1d ago");
  CHECK(monitor::FormatDataAge(now + hours(2), now) == L"now");
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
  CHECK(empty && empty->windows.empty());
  CHECK(empty && empty->status == monitor::DataStatus::DataUnavailable);

  auto sparse = monitor::ParseRateLimitMessage(
      R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":30,"windowDurationMins":300,"resetsAt":1893456000}}}})",
      &error);
  CHECK(sparse && sparse->sparseUpdate);
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

void TestQuotaWindowSelection() {
  monitor::RateLimitSnapshot snapshot;
  monitor::RateWindow modelWeekly;
  modelWeekly.bucketId = "model-x";
  modelWeekly.windowDurationMins = 10080;
  modelWeekly.remainingPercent = 91;
  snapshot.windows.push_back(modelWeekly);

  monitor::RateWindow weekly;
  weekly.bucketId = "codex";
  weekly.windowName = "secondary";
  weekly.windowDurationMins = 10080;
  weekly.remainingPercent = 64;
  snapshot.windows.push_back(weekly);

  monitor::RateWindow fiveHour;
  fiveHour.bucketId = "codex";
  fiveHour.windowName = "primary";
  fiveHour.windowDurationMins = 300;
  fiveHour.remainingPercent = 37;
  snapshot.windows.push_back(fiveHour);

  const monitor::CodexQuotaWindows selected =
      monitor::SelectCodexQuotaWindows(snapshot);
  CHECK(selected.weekly != nullptr);
  CHECK(selected.fiveHour != nullptr);
  CHECK(selected.weekly && selected.weekly->bucketId == "codex");
  CHECK(selected.weekly && selected.weekly->remainingPercent == 64);
  CHECK(selected.fiveHour && selected.fiveHour->remainingPercent == 37);
  CHECK(monitor::ClassifyQuotaWindow(weekly) == monitor::QuotaWindowKind::Weekly);
  CHECK(monitor::ClassifyQuotaWindow(fiveHour) == monitor::QuotaWindowKind::FiveHour);
  CHECK(monitor::ClassifyQuotaWindow(monitor::RateWindow{}) ==
        monitor::QuotaWindowKind::Other);

  monitor::RateLimitSnapshot reversed;
  reversed.windows = {fiveHour, weekly};
  const monitor::CodexQuotaWindows reversedSelection =
      monitor::SelectCodexQuotaWindows(reversed);
  CHECK(reversedSelection.weekly && reversedSelection.weekly->remainingPercent == 64);
  CHECK(reversedSelection.fiveHour && reversedSelection.fiveHour->remainingPercent == 37);

  monitor::RateLimitSnapshot weeklyOnly;
  weeklyOnly.windows.push_back(weekly);
  const monitor::CodexQuotaWindows single =
      monitor::SelectCodexQuotaWindows(weeklyOnly);
  CHECK(single.weekly != nullptr);
  CHECK(single.fiveHour == nullptr);

  monitor::RateLimitSnapshot update;
  update.sparseUpdate = true;
  update.status = monitor::DataStatus::Live;
  update.receivedAt = update.lastSuccessAt = std::chrono::system_clock::now();
  fiveHour.remainingPercent = 22;
  update.windows.push_back(fiveHour);
  const monitor::RateLimitSnapshot merged =
      monitor::MergeSparseRateLimitSnapshot(reversed, update);
  const monitor::CodexQuotaWindows mergedWindows =
      monitor::SelectCodexQuotaWindows(merged);
  CHECK(merged.windows.size() == 2);
  CHECK(mergedWindows.weekly && mergedWindows.weekly->remainingPercent == 64);
  CHECK(mergedWindows.fiveHour && mergedWindows.fiveHour->remainingPercent == 22);
  CHECK(!merged.sparseUpdate);
}

void TestProQuotaAdaptation() {
  using namespace monitor;
  std::string error;
  const char* proJson = R"({"result":{"rateLimitsByLimitId":{
    "codex":{"limitId":"codex","planType":"prolite","primary":{"usedPercent":2,"windowDurationMins":10080},"secondary":null},
    "codex_bengalfox":{"limitId":"codex_bengalfox","limitName":"Spark","planType":"not-main","credits":{"unlimited":true},
      "primary":{"usedPercent":0,"windowDurationMins":300},"secondary":{"usedPercent":0,"windowDurationMins":10080}}
  }}})";
  auto pro = ParseRateLimitMessage(proJson, &error);
  CHECK(pro.has_value());
  if (!pro) return;
  CHECK(pro->status == DataStatus::Live);
  CHECK(pro->planType == "prolite");
  CHECK(!pro->credits.unlimited);
  auto selected = SelectCodexQuotaWindows(*pro);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 98);
  CHECK(selected.weekly && selected.weekly->bucketId == "codex");
  CHECK(!selected.fiveHour && !selected.other);
  // Bucket order and a zero main quota must never make Spark look preferable.
  std::reverse(pro->windows.begin(), pro->windows.end());
  selected = SelectCodexQuotaWindows(*pro);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 98);
  CHECK(!selected.fiveHour);
  for (auto& window : pro->windows) if (window.bucketId == "codex") window.remainingPercent = 0;
  selected = SelectCodexQuotaWindows(*pro);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 0);
  CHECK(!selected.fiveHour);

  std::erase_if(pro->windows, [](const RateWindow& window) { return window.bucketId == "codex"; });
  selected = SelectCodexQuotaWindows(*pro);
  CHECK(!selected.weekly && !selected.fiveHour && !selected.other);
  CHECK(!HasCodexQuotaUpdate(*pro));
  auto sparkOnly = ParseRateLimitMessage(R"({"result":{"rateLimitsByLimitId":{
    "codex_bengalfox":{"primary":{"usedPercent":0,"windowDurationMins":300},"secondary":{"usedPercent":0,"windowDurationMins":10080}}
  }}})", &error);
  CHECK(sparkOnly && sparkOnly->status == DataStatus::DataUnavailable);

  auto plus = ParseRateLimitMessage(R"({"result":{"rateLimits":{"limitId":"codex","planType":"plus",
    "primary":{"usedPercent":80,"windowDurationMins":300},"secondary":{"usedPercent":30,"windowDurationMins":10080}}}})", &error);
  CHECK(plus.has_value());
  if (!plus) return;
  auto sparse = ParseRateLimitMessage(R"({"method":"account/rateLimits/updated","params":{"rateLimits":{
    "limitId":"codex","primary":{"usedPercent":2,"windowDurationMins":10080},"secondary":null,"planType":"prolite"}}})", &error);
  CHECK(sparse && sparse->updatedWindows.size() == 2);
  if (!sparse) return;
  auto upgraded = MergeSparseRateLimitSnapshot(*plus, *sparse);
  selected = SelectCodexQuotaWindows(upgraded);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 98);
  CHECK(!selected.fiveHour);
  CHECK(upgraded.windows.size() == 1 && upgraded.updatedWindows.empty());
  CHECK(upgraded.planType == "prolite");
  // Omitted is not null: partial data keeps the other period.
  auto partial = ParseRateLimitMessage(R"({"method":"account/rateLimits/updated","params":{"rateLimits":{
    "limitId":"codex","primary":{"usedPercent":81,"windowDurationMins":300}}}})", &error);
  CHECK(partial.has_value());
  if (!partial) return;
  auto merged = MergeSparseRateLimitSnapshot(*plus, *partial);
  selected = SelectCodexQuotaWindows(merged);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 70);
  CHECK(selected.fiveHour && selected.fiveHour->remainingPercent == 19);
  auto removed = ParseRateLimitMessage(R"({"method":"account/rateLimits/updated","params":{"rateLimits":{
    "limitId":"codex","primary":null}}})", &error);
  CHECK(removed && removed->windows.empty() && HasCodexQuotaUpdate(*removed));
  if (!removed) return;
  merged = MergeSparseRateLimitSnapshot(*plus, *removed);
  selected = SelectCodexQuotaWindows(merged);
  CHECK(selected.weekly && selected.weekly->remainingPercent == 70);
  CHECK(!selected.fiveHour);

  // Authoritative empty/null main data never revives the legacy or Spark view.
  auto emptyMulti = ParseRateLimitMessage(R"({"result":{"rateLimitsByLimitId":{},"rateLimits":{
    "limitId":"codex","primary":{"usedPercent":0,"windowDurationMins":300}}}})", &error);
  CHECK(emptyMulti && emptyMulti->windows.empty() && emptyMulti->status == DataStatus::DataUnavailable);
  auto noMain = ParseRateLimitMessage(R"({"result":{"rateLimitsByLimitId":{
    "codex":{"primary":null,"secondary":null},"codex_bengalfox":{"primary":{"usedPercent":0,"windowDurationMins":10080}}
  }}})", &error);
  CHECK(noMain && noMain->status == DataStatus::DataUnavailable);
  auto invalidUsed = ParseRateLimitMessage(R"({"result":{"rateLimits":{"primary":{"usedPercent":"","windowDurationMins":10080}}}})", &error);
  CHECK(invalidUsed && invalidUsed->windows.empty() && invalidUsed->status == DataStatus::DataUnavailable);
  CHECK(!ParseRateLimitMessage(R"({"result":{"somethingElse":true}})", &error));
  auto legacy = ParseRateLimitMessage(R"({"result":{"secondary":{"usedPercent":10,"windowDurationMins":10080}}})", &error);
  CHECK(legacy && SelectCodexQuotaWindows(*legacy).weekly);
  CHECK(legacy && !SelectCodexQuotaWindows(*legacy).fiveHour);
  auto fiveOnly = ParseRateLimitMessage(R"({"result":{"rateLimits":{"primary":{"usedPercent":10,"windowDurationMins":300}}}})", &error);
  CHECK(fiveOnly && !SelectCodexQuotaWindows(*fiveOnly).weekly && SelectCodexQuotaWindows(*fiveOnly).fiveHour);
  auto unknown = ParseRateLimitMessage(R"({"result":{"rateLimits":{"primary":{"usedPercent":10,"windowDurationMins":60}}}})", &error);
  CHECK(unknown && SelectCodexQuotaWindows(*unknown).other);
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
  CHECK(monitor::CalculateHudRingLeft(94, true) == 126);
  CHECK(monitor::CalculateHudRingLeft(16, false) == 16);
  CHECK(monitor::CalculateHudRingLeft(140, true) == 140);
  CHECK(monitor::SingleQuotaPercentWidth(100) == 84.0f);
  CHECK(monitor::SingleQuotaPercentWidth(99.5) == 84.0f);
  CHECK(monitor::SingleQuotaPercentWidth(99.49) == 64.0f);
  CHECK(monitor::SingleQuotaPercentWidth(0) == 64.0f);
  // Every visual button center resolves to the same control at fractional
  // user scales and monitor DPIs. The reset-time row stays non-interactive.
  for (UINT dpi : {96u, 120u, 144u, 192u}) {
    for (float uiScale : {0.60f, 0.80f, 0.85f, 1.0f, 1.40f}) {
      const float scale = dpi / 96.0f * uiScale;
      const float ringGlowLeft = (monitor::CalculateHudRingLeft(94, true) - 0.6f) * scale;
      CHECK(ringGlowLeft > monitor::kHudArtworkRight * scale);
      const int width = static_cast<int>(std::lround(280 * scale));
      const int height = static_cast<int>(std::lround(90 * scale));
      const auto layout = monitor::CalculateHudControlLayout(width / scale, height / scale);
      const int y = static_cast<int>(std::lround((layout.settings.top + layout.settings.bottom) * .5f * scale));
      const int settingsX = static_cast<int>(std::lround((layout.settings.left + layout.settings.right) * .5f * scale));
      const int minimizeX = static_cast<int>(std::lround((layout.minimize.left + layout.minimize.right) * .5f * scale));
      CHECK(monitor::HitTestHudControl(settingsX, y, width, height, dpi, uiScale) == HudControl::Settings);
      CHECK(monitor::HitTestHudControl(minimizeX, y, width, height, dpi, uiScale) == HudControl::Minimize);
      CHECK(monitor::HitTestHudControl(minimizeX, static_cast<int>(58 * scale), width, height, dpi, uiScale) == HudControl::None);
    }
  }
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

  // The same controls remain aligned when the whole HUD is scaled to 80%.
  CHECK(monitor::HitTestHudControl(196, 58, 240, 72, 96, 0.8f) == HudControl::Settings);
  CHECK(monitor::HitTestHudControl(216, 58, 240, 72, 96, 0.8f) == HudControl::Minimize);
  CHECK(monitor::HitTestHudControl(175, 58, 240, 72, 96, 0.8f) == HudControl::None);

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
  {
    const monitor::CodexQuotaWindows windows =
        monitor::SelectCodexQuotaWindows(context.last);
    CHECK(windows.weekly && windows.weekly->remainingPercent == 30);
    CHECK(windows.fiveHour && windows.fiveHour->remainingPercent == 55);
  }
  run(L"pro-weekly", true, monitor::AppServerErrorKind::Protocol);
  {
    const auto windows = monitor::SelectCodexQuotaWindows(context.last);
    CHECK(windows.weekly && windows.weekly->bucketId == "codex");
    CHECK(windows.weekly && windows.weekly->remainingPercent == 98);
    CHECK(!windows.fiveHour);
  }
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

int LiveQuotaProbe() {
  struct Context {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::optional<monitor::RateLimitSnapshot> snapshot;
    std::optional<monitor::AppServerError> error;
  } context;
  monitor::CodexAppServer server;
  if (!server.Start([&](monitor::RateLimitSnapshot value) {
        context.snapshot = std::move(value);
        SetEvent(context.event);
      }, [&](monitor::AppServerError value) {
        context.error = std::move(value);
        SetEvent(context.event);
      })) {
    if (context.event) CloseHandle(context.event);
    return 2;
  }
  const DWORD wait = WaitForSingleObject(context.event, 15000);
  server.Stop();
  if (wait != WAIT_OBJECT_0 || !context.snapshot) {
    if (context.error) std::wcerr << context.error->message << L'\n';
    if (context.event) CloseHandle(context.event);
    return 3;
  }
  const monitor::CodexQuotaWindows windows =
      monitor::SelectCodexQuotaWindows(*context.snapshot);
  auto printWindow = [](const wchar_t* name, const monitor::RateWindow* window) {
    if (!window) {
      std::wcout << name << L"=not_returned\n";
      return;
    }
    std::wcout << name << L"_bucket=" << monitor::Utf8ToWide(window->bucketId) << L'\n'
               << name << L"_duration_mins=" << window->windowDurationMins << L'\n'
               << name << L"_remaining_percent=" << window->remainingPercent << L'\n';
  };
  printWindow(L"weekly", windows.weekly);
  printWindow(L"five_hour", windows.fiveHour);
  if (context.event) CloseHandle(context.event);
  return windows.weekly || windows.fiveHour || windows.other ? 0 : 4;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc >= 3 && std::wstring(argv[1]) == L"--integration") return Integration(argv[2]);
  if (argc >= 3 && std::wstring(argv[1]) == L"--benchmark") return Benchmark(argv[2]);
  if (argc >= 2 && std::wstring(argv[1]) == L"--live-quota-probe") return LiveQuotaProbe();
  TestRemainingPercent();
  TestDataAge();
  TestParsing();
  TestResetAndStale();
  TestQuotaWindowSelection();
  TestProQuotaAdaptation();
  TestSydneyDst();
  TestEnergyState();
  TestHudInteractions();
  TestLifecyclePrimitives();
  std::cout << "Checks: " << checks << ", failures: " << failures << '\n';
  return failures ? 1 : 0;
}
