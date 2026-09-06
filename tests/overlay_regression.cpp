#include "overlay_window.h"
#include <chrono>
#include <iostream>
#include <objbase.h>
#include <dwrite.h>

namespace monitor {
// Exercise the actual window controller, without using live account data or
// writing the user's settings. All native windows belong to this test process.
struct OverlayWindowTestAccess {
  static const Settings& Config(const OverlayWindow& window) { return window.settings_; }
  static DataStatus Status(const OverlayWindow& window) { return window.displayStatus_; }
  static bool InFlight(const OverlayWindow& window) { return window.appServer_.RequestInFlight(); }
  static bool Hidden(const OverlayWindow& window) { return window.hidden_; }
  static bool Dual(const OverlayWindow& window) { return window.ShowsFiveHourQuota(); }
  static double Previous(const OverlayWindow& window) { return window.previousRemaining_; }
  static const std::wstring& Tooltip(const OverlayWindow& window) { return window.quotaTooltip_; }
  static bool Apply(OverlayWindow& window, std::string_view json) {
    std::string error;
    auto snapshot = ParseRateLimitMessage(json, &error);
    if (!snapshot) return false;
    window.ApplySnapshot(std::move(*snapshot));
    return true;
  }
  static bool FullPercentFits(OverlayWindow& window) {
    window.EnsureGraphicsResources();
    if (!window.writeFactory_ || !window.textLarge_) return false;
    IDWriteTextLayout* layout = nullptr;
    const float width = SingleQuotaPercentWidth(100);
    if (FAILED(window.writeFactory_->CreateTextLayout(L"100%", 4, window.textLarge_,
                                                       width, 38.0f, &layout))) return false;
    DWRITE_TEXT_METRICS metrics{};
    const HRESULT result = layout->GetMetrics(&metrics);
    layout->Release();
    return SUCCEEDED(result) && metrics.widthIncludingTrailingWhitespace <= width;
  }
  static bool ShiftedRingTextFits(OverlayWindow& window) {
    window.EnsureGraphicsResources();
    if (!window.writeFactory_ || !window.textSmall_ || !window.textValue_) return false;
    // Tightest case: compact dual ring, with a three-digit percentage.
    const float infoWidth = 280.0f - 12.0f -
        (CalculateHudRingLeft(94.0f, true) + 56.0f + 4.0f);
    auto fits = [&](const wchar_t* text, IDWriteTextFormat* format, float width) {
      IDWriteTextLayout* layout = nullptr;
      if (FAILED(window.writeFactory_->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)),
              format, 500.0f, 30.0f, &layout))) return false;
      DWRITE_TEXT_METRICS metrics{};
      const HRESULT result = layout->GetMetrics(&metrics);
      layout->Release();
      if (SUCCEEDED(result) && metrics.widthIncludingTrailingWhitespace > width) {
        std::cerr << "Ring text width " << metrics.widthIncludingTrailingWhitespace
                  << " exceeds available " << width << '\n';
      }
      return SUCCEEDED(result) && metrics.widthIncludingTrailingWhitespace <= width;
    };
    return fits(L"WEEK", window.textSmall_, 32.0f) &&
        fits(L"5H", window.textSmall_, 32.0f) &&
        fits(L"100%", window.textValue_, infoWidth - 37.0f) &&
        fits(L"Reset 7d 23h", window.textSmall_, infoWidth) &&
        fits(L"09/13 03:22", window.textSmall_, infoWidth);
  }
  static EnergyVisualState Energy(const OverlayWindow& window) { return window.energyState_.visual; }
  static const RateLimitSnapshot& Snapshot(const OverlayWindow& window) { return *window.snapshot_; }
  static std::wstring StatusText(OverlayWindow& window) {
    window.demoMode_ = false;
    auto result = window.CurrentStatusLine();
    window.demoMode_ = true;
    return result;
  }
  static void Fail(OverlayWindow& window) {
    window.ApplyServerError({AppServerErrorKind::Network, L"Test network error"});
  }
  static void StartFakeRequest(OverlayWindow& window) {
    window.demoMode_ = false;
    window.StartCodex();
    window.demoMode_ = true;
  }
};
}  // namespace monitor

namespace {
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (predicate()) return true;
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
  } while (GetTickCount64() < deadline);
  return predicate();
}
}

int wmain(int argc, wchar_t** argv) {
  using namespace monitor;
  using Access = OverlayWindowTestAccess;
  if (argc != 2) return 2;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  _wputenv_s(L"CODEX_EXECUTABLE", argv[1]);
  _wputenv_s(L"FAKE_CODEX_SCENARIO", L"notification-timeout");
  _wputenv_s(L"MONITOR_DEMO_REMAINING", L"55");
  _wputenv_s(L"MONITOR_DEMO_FIVE_HOUR_REMAINING", L"72");
  {
    Settings settings;
    settings.energy.glow = false;
    OverlayWindow window(GetModuleHandleW(nullptr), settings, {}, true);
    CHECK(window.Create());
    if (!window.Handle()) return 3;
    ShowWindow(window.Handle(), SW_HIDE);

    CHECK(std::wstring(kPreviewWindowClass) != kWindowClass);
    CHECK(std::wstring(kPreviewMutexName) != kMutexName);
    CHECK(Access::FullPercentFits(window));
    CHECK(Access::ShiftedRingTextFits(window));
    RECT before{}, after{};
    GetWindowRect(window.Handle(), &before);
    window.OnTrayCommand(kTrayShowHide);
    CHECK(Access::Hidden(window));
    CHECK(IsIconic(window.Handle()));
    SendMessageW(window.Handle(), WM_MONITOR_SHOW, 0, 0);
    CHECK(!Access::Hidden(window));
    CHECK(!IsIconic(window.Handle()));
    GetWindowRect(window.Handle(), &after);
    CHECK(EqualRect(&before, &after));
    // Duplicate activation messages restore rather than toggle the HUD away.
    SendMessageW(window.Handle(), WM_MONITOR_SHOW, 0, 0);
    CHECK(!Access::Hidden(window));
    ShowWindow(window.Handle(), SW_MINIMIZE);
    CHECK(Access::Hidden(window));
    ShowWindow(window.Handle(), SW_RESTORE);
    CHECK(!Access::Hidden(window));
    ShowWindow(window.Handle(), SW_HIDE);
    window.OnTrayCommand(kTrayViewRing);
    CHECK(Access::Config(window).progressDisplayMode == ProgressDisplayMode::Ring);
    window.OnTrayCommand(kTrayViewBar);
    CHECK(Access::Config(window).progressDisplayMode == ProgressDisplayMode::Bar);
    window.OnTrayCommand(kTrayQuotaWeekly);
    CHECK(Access::Config(window).quotaDisplayMode == QuotaDisplayMode::WeeklyOnly);
    window.OnTrayCommand(kTrayQuotaBoth);
    CHECK(Access::Config(window).quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour);
    window.OnTrayCommand(kTrayLayoutExpanded);
    CHECK(Access::Config(window).sizeMode == HudSizeMode::Expanded);
    window.OnTrayCommand(kTrayLayoutCompact);
    for (int i = 0; i < 30; ++i) window.OnTrayCommand(kTrayScaleDown);
    CHECK(Access::Config(window).scalePercent == kMinHudScalePercent);
    for (int i = 0; i < 30; ++i) window.OnTrayCommand(kTrayScaleUp);
    CHECK(Access::Config(window).scalePercent == kMaxHudScalePercent);
    window.OnTrayCommand(kTrayScaleReset);
    CHECK(Access::Config(window).scalePercent == kDefaultHudScalePercent);

    window.OnTrayCommand(kTrayPreviewWarning);
    CHECK(Access::Energy(window) == EnergyVisualState::WarningRedBlink);
    CHECK(Access::StatusText(window).starts_with(L"WARN"));
    // Threshold adjustments update text and lamp together, not hardcoded 30%.
    for (int i = 0; i < 7; ++i) window.OnTrayCommand(kTrayThresholdDown);
    CHECK(Access::Energy(window) == EnergyVisualState::NormalBlue);
    CHECK(Access::StatusText(window).starts_with(L"LIVE"));
    window.OnTrayCommand(kTrayPreviewStone);
    CHECK(Access::Energy(window) == EnergyVisualState::Stone);
    CHECK(Access::StatusText(window).starts_with(L"STONE"));
    Access::Fail(window);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    window.OnTrayCommand(kTrayGlow);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    CHECK(Access::StatusText(window) == L"NETWORK · now");
    window.OnTrayCommand(kTrayPreviewNormal);

    const char* dualJson = R"({"result":{"rateLimits":{"limitId":"codex",
      "primary":{"usedPercent":14,"windowDurationMins":300,"resetsAt":1893456000},
      "secondary":{"usedPercent":32,"windowDurationMins":10080,"resetsAt":1893542400}}}})";
    const char* proJson = R"({"result":{"rateLimitsByLimitId":{
      "codex":{"primary":{"usedPercent":2,"windowDurationMins":10080,"resetsAt":1893542400},"secondary":null},
      "codex_bengalfox":{"primary":{"usedPercent":100,"windowDurationMins":300},"secondary":{"usedPercent":100,"windowDurationMins":10080}}
    }}})";
    window.OnTrayCommand(kTrayScaleUp); // Keep the user's non-preset 85% scale.
    for (UINT view : {kTrayViewRing, kTrayViewBar}) {
      window.OnTrayCommand(view);
      CHECK(Access::Apply(window, dualJson));
      CHECK(Access::Dual(window));
      GetWindowRect(window.Handle(), &before);
      CHECK(Access::Apply(window, proJson));
      GetWindowRect(window.Handle(), &after);
      CHECK(EqualRect(&before, &after));
      CHECK(Access::Config(window).scalePercent == 85);
      CHECK(Access::Config(window).quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour);
      CHECK(!Access::Dual(window));
      CHECK(Access::Previous(window) == 98);
      CHECK(Access::Energy(window) == EnergyVisualState::NormalBlue);
      CHECK(Access::Tooltip(window).find(L"The main quota currently returns only a weekly period") != std::wstring::npos);
      CHECK(Access::Tooltip(window).find(L"bright inner ring") == std::wstring::npos);
      // Returning to a dual-period plan restores its inner ring/lower bar.
      CHECK(Access::Apply(window, dualJson));
      CHECK(Access::Dual(window));
      window.OnTrayCommand(kTrayQuotaWeekly);
      CHECK(!Access::Dual(window));
      window.OnTrayCommand(kTrayQuotaBoth);
      CHECK(Access::Dual(window));
    }
    CHECK(Access::Apply(window, proJson));
    const auto lastMainRead = Access::Snapshot(window).lastSuccessAt;
    CHECK(Access::Apply(window, R"({"method":"account/rateLimits/updated","params":{"rateLimits":{
      "limitId":"codex_bengalfox","primary":{"usedPercent":100,"windowDurationMins":300}}}})"));
    CHECK(Access::Snapshot(window).lastSuccessAt == lastMainRead);
    CHECK(Access::Previous(window) == 98);
    CHECK(!Access::Dual(window));
    CHECK(Access::Energy(window) == EnergyVisualState::NormalBlue);

    CHECK(Access::Apply(window, R"({"result":{"rateLimits":{"primary":{"usedPercent":100,"windowDurationMins":10080},"secondary":null}}})"));
    CHECK(Access::Energy(window) == EnergyVisualState::Stone);
    CHECK(Access::Apply(window, R"({"method":"account/rateLimits/updated","params":{"rateLimits":{"primary":null}}})"));
    CHECK(Access::Status(window) == DataStatus::DataUnavailable);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    CHECK(Access::Previous(window) == 101); // No false 0% exhaustion alert.
    CHECK(Access::StatusText(window).starts_with(L"N/A"));
    window.OnTrayCommand(kTrayGlow);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    CHECK(Access::Apply(window, proJson));
    Access::Fail(window);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    CHECK(!Access::Dual(window));
    CHECK(Access::Tooltip(window).find(L"last successful read") != std::wstring::npos);
    CHECK(Access::Apply(window, R"({"result":{"rateLimitsByLimitId":{
      "codex_bengalfox":{"primary":{"usedPercent":0,"windowDurationMins":300},"secondary":{"usedPercent":0,"windowDurationMins":10080}}
    }}})"));
    CHECK(Access::Status(window) == DataStatus::DataUnavailable);
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    CHECK(!Access::Dual(window));
    CHECK(Access::Apply(window, dualJson));

    // Start's first request receives only an unrelated sparse notification.
    // It must preserve both the weekly cache and the initial 15 s watchdog.
    Access::StartFakeRequest(window);
    CHECK(Access::InFlight(window));
    CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::Live; }, 3000));
    CHECK(Access::InFlight(window));
    const auto quota = SelectCodexQuotaWindows(Access::Snapshot(window));
    CHECK(quota.weekly && quota.weekly->remainingPercent == 68);
    CHECK(quota.fiveHour && quota.fiveHour->remainingPercent == 22);
    CHECK(PumpUntil([&] { return Access::Status(window) == DataStatus::Offline; }, 17000));
    CHECK(!Access::InFlight(window));
    CHECK(Access::Energy(window) == EnergyVisualState::DataUnavailable);
    DestroyWindow(window.Handle());
  }
  if (SUCCEEDED(apartment)) CoUninitialize();
  std::cout << "Overlay checks: " << checks << ", failures: " << failures << '\n';
  return failures ? 1 : 0;
}
