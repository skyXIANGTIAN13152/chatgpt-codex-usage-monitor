#include "overlay_window.h"
#include "../resources/resource.h"
#include <shellapi.h>

namespace monitor {
namespace {

const wchar_t* HudSizeModeLabel(HudSizeMode mode) {
  if (mode == HudSizeMode::Compact) return L"Information layout: Compact";
  if (mode == HudSizeMode::Expanded) return L"Information layout: Expanded";
  return L"Information layout: Standard";
}

}  // namespace

void OverlayWindow::AddTrayIcon() {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data.uCallbackMessage = WM_MONITOR_TRAY;
  data.hIcon = static_cast<HICON>(LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
  wcscpy_s(data.szTip, kProductName);
  // Explorer can restart independently of this process. Remove any stale
  // registration first, then add the icon again to the fresh notification area.
  Shell_NotifyIconW(NIM_DELETE, &data);
  trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
  if (!trayIconAdded_) {
    SetTimer(hwnd_, kTrayRetryTimer, 1000, nullptr);
    return;
  }
  data.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &data);
  KillTimer(hwnd_, kTrayRetryTimer);
}

void OverlayWindow::RemoveTrayIcon() {
  KillTimer(hwnd_, kTrayRetryTimer);
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &data);
  trayIconAdded_ = false;
}

void OverlayWindow::UpdateTrayTooltip() {
  if (!trayIconAdded_) {
    AddTrayIcon();
    if (!trayIconAdded_) return;
  }
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  data.uFlags = NIF_TIP;
  std::wstring tip = L"Codex quota · " + DataStatusText(displayStatus_);
  const CodexQuotaWindows windows = QuotaWindows();
  if (const RateWindow* primary = windows.weekly ? windows.weekly : PrimaryQuotaWindow()) {
    wchar_t percent[32]{};
    const wchar_t* label = windows.weekly ? L"Weekly" : L"Current";
    swprintf_s(percent, L" · %s %.0f%%", label, primary->remainingPercent);
    tip += percent;
  }
  if (ShowsFiveHourQuota()) {
    wchar_t percent[32]{};
    swprintf_s(percent, L" · 5H %.0f%%", windows.fiveHour->remainingPercent);
    tip += percent;
  }
  wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
  if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
    trayIconAdded_ = false;
    AddTrayIcon();
    if (trayIconAdded_) Shell_NotifyIconW(NIM_MODIFY, &data);
  }
}

void OverlayWindow::ShowTrayMenu(POINT point) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kTrayShowHide,
              hidden_ ? L"Restore quota window" : L"Minimize to tray");
  AppendMenuW(menu, MF_STRING, kTrayRefresh, L"Refresh now");
  AppendMenuW(menu, MF_STRING, kTrayUsage, L"Open Codex Usage page");
  AppendMenuW(menu, MF_STRING, kTrayOpenChatGpt, L"Open ChatGPT");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Settings");
  AppendMenuW(menu, MF_STRING | (settings_.themeEnabled ? MF_CHECKED : 0), kTrayTheme,
              L"Tiga energy-indicator theme");
  const wchar_t* display = settings_.displayMode == IndicatorDisplayMode::Both
      ? L"Display: Quota visual + energy indicator"
      : settings_.displayMode == IndicatorDisplayMode::ProgressOnly
          ? L"Display: Quota visual only"
          : L"Display: Energy indicator only";
  AppendMenuW(menu, MF_STRING, kTrayDisplayMode, display);
  const wchar_t* progressDisplay = settings_.progressDisplayMode == ProgressDisplayMode::Ring
      ? L"Quota visual: Light-energy rings"
      : L"Quota visual: Light-energy bars";
  AppendMenuW(menu, MF_STRING, kTrayProgressDisplayMode, progressDisplay);
  const wchar_t* quotaDisplay =
      settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour
          ? L"Quota windows: Weekly + 5 hours"
          : L"Quota windows: Weekly only";
  AppendMenuW(menu, MF_STRING, kTrayQuotaDisplayMode, quotaDisplay);
  AppendMenuW(menu, MF_STRING | (settings_.followChatGpt ? MF_CHECKED : 0), kTrayFollow,
              L"Follow the ChatGPT window");
  AppendMenuW(menu, MF_STRING | (settings_.energy.glow ? MF_CHECKED : 0),
              kTrayGlow, L"Subtle glow");
  AppendMenuW(menu, MF_STRING | (settings_.energy.stoneAtZero ? MF_CHECKED : 0),
              kTrayStone, L"Stone effect at 0%");
  const wchar_t* style = settings_.energy.blinkStyle == BlinkStyle::Gentle
      ? L"Blink style: Gentle"
      : settings_.energy.blinkStyle == BlinkStyle::Standard
          ? L"Blink style: Standard"
          : L"Blink style: Intense";
  AppendMenuW(menu, MF_STRING, kTrayBlinkStyle, style);
  wchar_t threshold[64]{};
  swprintf_s(threshold, L"Blink threshold: %d%% (left - / right +)",
             settings_.energy.warningThreshold);
  AppendMenuW(menu, MF_STRING, kTrayThresholdDown, threshold);
  AppendMenuW(menu, MF_STRING, kTrayThresholdUp, L"Increase blink threshold by 5%");
  wchar_t scaleLabel[64]{};
  swprintf_s(scaleLabel, L"Overall scale: %d%%", settings_.scalePercent);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, scaleLabel);
  AppendMenuW(menu, MF_STRING, kTrayScaleDown, L"Reduce overall size by 5%");
  AppendMenuW(menu, MF_STRING, kTrayScaleUp, L"Increase overall size by 5%");
  AppendMenuW(menu, MF_STRING, kTrayScaleReset, L"Restore default overall size");
  AppendMenuW(menu, MF_STRING, kTraySizeMode, HudSizeModeLabel(settings_.sizeMode));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayAbout, L"About");
  AppendMenuW(menu, MF_STRING, kTrayExit, L"Exit usage monitor");
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
}

}  // namespace monitor
