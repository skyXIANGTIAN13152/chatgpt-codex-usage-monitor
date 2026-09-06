#include "overlay_window.h"
#include "../resources/resource.h"
#include <shellapi.h>

namespace monitor {
void OverlayWindow::AddTrayIcon() {
  if (demoMode_) return;
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
  if (demoMode_) return;
  KillTimer(hwnd_, kTrayRetryTimer);
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &data);
  trayIconAdded_ = false;
}

void OverlayWindow::UpdateTrayTooltip() {
  if (demoMode_) return;
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
    const wchar_t* label = windows.weekly ? L"Week" : L"Current";
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
  if (!menu) return;
  const auto choice = [](HMENU target, UINT command, const wchar_t* label, bool selected) {
    AppendMenuW(target, MF_STRING | (selected ? MF_CHECKED : 0), command, label);
    MENUITEMINFOW item{sizeof(item)};
    item.fMask = MIIM_FTYPE;
    item.fType = MFT_RADIOCHECK;
    SetMenuItemInfoW(target, command, FALSE, &item);
  };
  const auto submenu = [&](HMENU child, const wchar_t* label) {
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
  };
  AppendMenuW(menu, MF_STRING, kTrayShowHide,
              hidden_ ? L"Restore quota window" : demoMode_ ? L"Minimize preview" : L"Minimize to tray");
  if (demoMode_) {
    HMENU preview = CreatePopupMenu();
    AppendMenuW(preview, MF_STRING, kTrayPreviewNormal, L"Blue energy · Week 68% / 5H 86%");
    AppendMenuW(preview, MF_STRING, kTrayPreviewWarning, L"Red warning · Week 34% / 5H 22%");
    AppendMenuW(preview, MF_STRING, kTrayPreviewStone, L"Energy depleted · Stone at 0%");
    submenu(preview, L"Sample state (no live quota data)");
  } else {
    AppendMenuW(menu, MF_STRING | (appServer_.RequestInFlight() ? MF_GRAYED : 0),
                kTrayRefresh, appServer_.RequestInFlight() ? L"Refreshing…" : L"Refresh now");
    AppendMenuW(menu, MF_STRING, kTrayUsage, L"Open Codex Usage page");
    AppendMenuW(menu, MF_STRING, kTrayOpenChatGpt, L"Open ChatGPT");
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  HMENU form = CreatePopupMenu();
  choice(form, kTrayViewRing, L"Light-energy rings", settings_.progressDisplayMode == ProgressDisplayMode::Ring);
  choice(form, kTrayViewBar, L"Light-energy bars", settings_.progressDisplayMode == ProgressDisplayMode::Bar);
  AppendMenuW(form, MF_SEPARATOR, 0, nullptr);
  choice(form, kTrayQuotaBoth, L"Auto quota (single / dual)", settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour);
  choice(form, kTrayQuotaWeekly, L"Weekly only", settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyOnly);
  AppendMenuW(form, MF_SEPARATOR, 0, nullptr);
  choice(form, kTrayDisplayBoth, L"Chest lamp + quota", settings_.displayMode == IndicatorDisplayMode::Both);
  choice(form, kTrayDisplayProgress, L"Quota only", settings_.displayMode == IndicatorDisplayMode::ProgressOnly);
  choice(form, kTrayDisplayEnergy, L"Chest lamp and values", settings_.displayMode == IndicatorDisplayMode::EnergyOnly);
  submenu(form, L"Display and appearance");

  HMENU size = CreatePopupMenu();
  choice(size, kTrayLayoutCompact, L"Compact layout", settings_.sizeMode == HudSizeMode::Compact);
  choice(size, kTrayLayoutStandard, L"Standard layout", settings_.sizeMode == HudSizeMode::Standard);
  choice(size, kTrayLayoutExpanded, L"Expanded layout", settings_.sizeMode == HudSizeMode::Expanded);
  AppendMenuW(size, MF_SEPARATOR, 0, nullptr);
  wchar_t scaleLabel[64]{};
  swprintf_s(scaleLabel, L"Overall scale: %d%%", settings_.scalePercent);
  AppendMenuW(size, MF_STRING | MF_GRAYED, 0, scaleLabel);
  AppendMenuW(size, MF_STRING | (settings_.scalePercent <= kMinHudScalePercent ? MF_GRAYED : 0),
              kTrayScaleDown, L"Decrease overall scale by 5%");
  AppendMenuW(size, MF_STRING | (settings_.scalePercent >= kMaxHudScalePercent ? MF_GRAYED : 0),
              kTrayScaleUp, L"Increase overall scale by 5%");
  AppendMenuW(size, MF_STRING, kTrayScaleReset, L"Restore default scale (80%)");
  submenu(size, L"Size and layout");

  HMENU theme = CreatePopupMenu();
  AppendMenuW(theme, MF_STRING | (settings_.themeEnabled ? MF_CHECKED : 0), kTrayTheme,
              L"Tiga · Life of light theme");
  AppendMenuW(theme, MF_STRING | (settings_.energy.glow ? MF_CHECKED : 0), kTrayGlow, L"Subtle glow");
  AppendMenuW(theme, MF_STRING | (settings_.energy.stoneAtZero ? MF_CHECKED : 0), kTrayStone, L"Petrify chest and lamp at 0%");
  AppendMenuW(theme, MF_SEPARATOR, 0, nullptr);
  choice(theme, kTrayBlinkGentle, L"Chest lamp blink: Gentle", settings_.energy.blinkStyle == BlinkStyle::Gentle);
  choice(theme, kTrayBlinkStandard, L"Chest lamp blink: Standard", settings_.energy.blinkStyle == BlinkStyle::Standard);
  choice(theme, kTrayBlinkFast, L"Chest lamp blink: Fast", settings_.energy.blinkStyle == BlinkStyle::Aggressive);
  AppendMenuW(theme, MF_SEPARATOR, 0, nullptr);
  wchar_t threshold[64]{};
  swprintf_s(threshold, L"Blink at remaining <= %d%% (chest lamp only)", settings_.energy.warningThreshold);
  AppendMenuW(theme, MF_STRING | MF_GRAYED, 0, threshold);
  AppendMenuW(theme, MF_STRING | (settings_.energy.warningThreshold <= 10 ? MF_GRAYED : 0),
              kTrayThresholdDown, L"Lower blink threshold by 5%");
  AppendMenuW(theme, MF_STRING | (settings_.energy.warningThreshold >= 80 ? MF_GRAYED : 0),
              kTrayThresholdUp, L"Raise blink threshold by 5%");
  submenu(theme, L"Theme and chest lamp");
  if (!demoMode_) AppendMenuW(menu, MF_STRING | (settings_.followChatGpt ? MF_CHECKED : 0),
                             kTrayFollow, L"Follow the ChatGPT window");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayAbout, demoMode_ ? L"About this preview" : L"About");
  AppendMenuW(menu, MF_STRING, kTrayExit, demoMode_ ? L"Close preview (installed monitor unaffected)" : L"Exit quota monitor");
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

}  // namespace monitor
