#pragma once

#include "chatgpt.h"
#include "codex_app_server.h"
#include "settings.h"
#include "hud_controls.h"

struct ID2D1Factory;
struct ID2D1HwndRenderTarget;
struct ID2D1Bitmap;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IWICImagingFactory;

namespace monitor {

class OverlayWindow {
 public:
  OverlayWindow(HINSTANCE instance, Settings settings, ChatGptInstance chatGpt,
                bool demoMode = false);
  ~OverlayWindow();
  bool Create();
  int RunMessageLoop();
  HWND Handle() const { return hwnd_; }

  void OnTrayCommand(UINT command);
  void ShowTrayMenu(POINT point);
  void AddTrayIcon();
  void RemoveTrayIcon();
  void UpdateTrayTooltip();

 private:
  friend struct OverlayWindowTestAccess;
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Paint();
  void DrawTributeLightBackground(float width, float height);
  void DrawThemedPanelImage(float width, float height);
  void EnsureThemeBitmap();
  void EnsureGraphicsResources();
  void DiscardGraphicsResources();
  void DrawEnergyIndicator(float left, float top, float width, float height);
  void StartCodex();
  void RequestRefresh(bool userInitiated);
  void ApplySnapshot(RateLimitSnapshot snapshot);
  void ApplyServerError(AppServerError error);
  void ConfigureBlinkTimer();
  void ScheduleNextRefresh(bool success);
  void AuditChatGptWindows();
  void FollowChatGptWindow();
  void PositionInitially();
  void ClampToWorkArea(RECT* rect) const;
  void SaveWindowPosition();
  void ResizeForMode();
  void SetHidden(bool hidden);
  void UpdateTooltips();
  void RefreshDataAge();
  CodexQuotaWindows QuotaWindows() const;
  const RateWindow* PrimaryQuotaWindow() const;
  const RateWindow* LimitingQuotaWindow() const;
  bool ShowsFiveHourQuota() const;
  void NotifyThresholds(double previous, double current);
  void OpenUsagePage();
  void OpenChatGpt();
  void OpenSettingsMenu();
  std::wstring CurrentStatusLine() const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  Settings settings_;
  ChatGptInstance chatGpt_;
  ChatGptLifecycleMonitor lifecycle_;
  CodexAppServer appServer_;
  std::optional<RateLimitSnapshot> snapshot_;
  DataStatus displayStatus_ = DataStatus::Connecting;
  EnergyIndicatorState energyState_;
  bool blinkOn_ = true;
  bool hidden_ = false;
  bool demoMode_ = false;
  bool workingSetTrimmed_ = false;
  int consecutiveFailures_ = 0;
  double previousRemaining_ = 101.0;
  std::vector<int> notifiedThresholds_;
  HANDLE networkNotification_ = nullptr;
  UINT taskbarCreatedMessage_ = 0;
  bool trayIconAdded_ = false;
  HWND tooltip_ = nullptr;
  std::wstring quotaTooltip_;
  HudControl hoveredControl_ = HudControl::None;

  ID2D1Factory* d2dFactory_ = nullptr;
  ID2D1HwndRenderTarget* renderTarget_ = nullptr;
  ID2D1Bitmap* chestBitmap_ = nullptr;
  ID2D1Bitmap* chestWarningOnBitmap_ = nullptr;
  ID2D1Bitmap* chestWarningOffBitmap_ = nullptr;
  ID2D1Bitmap* chestStoneBitmap_ = nullptr;
  IWICImagingFactory* wicFactory_ = nullptr;
  bool themeBitmapLoadAttempted_ = false;
  IDWriteFactory* writeFactory_ = nullptr;
  IDWriteTextFormat* textSmall_ = nullptr;
  IDWriteTextFormat* textMedium_ = nullptr;
  IDWriteTextFormat* textLarge_ = nullptr;
  IDWriteTextFormat* textRing_ = nullptr;
  IDWriteTextFormat* textValue_ = nullptr;
};

enum TrayCommand : UINT {
  kTrayShowHide = 2001,
  kTrayRefresh,
  kTrayUsage,
  kTrayOpenChatGpt,
  kTrayDisplayMode,
  kTrayProgressDisplayMode,
  kTrayQuotaDisplayMode,
  kTrayFollow,
  kTrayTheme,
  kTrayGlow,
  kTrayStone,
  kTrayBlinkStyle,
  kTrayThresholdDown,
  kTrayThresholdUp,
  kTraySizeMode,
  kTrayAbout,
  kTrayExit,
  kTrayScaleDown,
  kTrayScaleUp,
  kTrayScaleReset,
  kTrayViewRing = 2100,
  kTrayViewBar,
  kTrayQuotaBoth,
  kTrayQuotaWeekly,
  kTrayDisplayBoth,
  kTrayDisplayProgress,
  kTrayDisplayEnergy,
  kTrayLayoutCompact,
  kTrayLayoutStandard,
  kTrayLayoutExpanded,
  kTrayBlinkGentle,
  kTrayBlinkStandard,
  kTrayBlinkFast,
  kTrayPreviewNormal,
  kTrayPreviewWarning,
  kTrayPreviewStone,
};

}  // namespace monitor
