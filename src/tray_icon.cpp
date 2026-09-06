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
  std::wstring tip = L"Codex 额度 · " + DataStatusText(displayStatus_);
  const CodexQuotaWindows windows = QuotaWindows();
  if (const RateWindow* primary = windows.weekly ? windows.weekly : PrimaryQuotaWindow()) {
    wchar_t percent[32]{};
    const wchar_t* label = windows.weekly ? L"周" : L"当前";
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
              hidden_ ? L"恢复额度窗口" : demoMode_ ? L"最小化预览" : L"最小化到托盘");
  if (demoMode_) {
    HMENU preview = CreatePopupMenu();
    AppendMenuW(preview, MF_STRING, kTrayPreviewNormal, L"充盈蓝光 · 周 68% / 5H 86%");
    AppendMenuW(preview, MF_STRING, kTrayPreviewWarning, L"红灯警戒 · 周 34% / 5H 22%");
    AppendMenuW(preview, MF_STRING, kTrayPreviewStone, L"能量耗尽 · 0% 石化");
    submenu(preview, L"演示状态（不读取真实额度）");
  } else {
    AppendMenuW(menu, MF_STRING | (appServer_.RequestInFlight() ? MF_GRAYED : 0),
                kTrayRefresh, appServer_.RequestInFlight() ? L"正在刷新…" : L"立即刷新");
    AppendMenuW(menu, MF_STRING, kTrayUsage, L"打开 Codex Usage 页面");
    AppendMenuW(menu, MF_STRING, kTrayOpenChatGpt, L"打开 ChatGPT");
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  HMENU form = CreatePopupMenu();
  choice(form, kTrayViewRing, L"光能圆环", settings_.progressDisplayMode == ProgressDisplayMode::Ring);
  choice(form, kTrayViewBar, L"光能进度条", settings_.progressDisplayMode == ProgressDisplayMode::Bar);
  AppendMenuW(form, MF_SEPARATOR, 0, nullptr);
  choice(form, kTrayQuotaBoth, L"自动适配额度（单 / 双）", settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour);
  choice(form, kTrayQuotaWeekly, L"只显示周额度", settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyOnly);
  AppendMenuW(form, MF_SEPARATOR, 0, nullptr);
  choice(form, kTrayDisplayBoth, L"胸灯 + 额度", settings_.displayMode == IndicatorDisplayMode::Both);
  choice(form, kTrayDisplayProgress, L"仅额度", settings_.displayMode == IndicatorDisplayMode::ProgressOnly);
  choice(form, kTrayDisplayEnergy, L"仅胸灯与数字", settings_.displayMode == IndicatorDisplayMode::EnergyOnly);
  submenu(form, L"显示内容与形式");

  HMENU size = CreatePopupMenu();
  choice(size, kTrayLayoutCompact, L"紧凑布局", settings_.sizeMode == HudSizeMode::Compact);
  choice(size, kTrayLayoutStandard, L"标准布局", settings_.sizeMode == HudSizeMode::Standard);
  choice(size, kTrayLayoutExpanded, L"展开布局", settings_.sizeMode == HudSizeMode::Expanded);
  AppendMenuW(size, MF_SEPARATOR, 0, nullptr);
  wchar_t scaleLabel[64]{};
  swprintf_s(scaleLabel, L"整体缩放：%d%%", settings_.scalePercent);
  AppendMenuW(size, MF_STRING | MF_GRAYED, 0, scaleLabel);
  AppendMenuW(size, MF_STRING | (settings_.scalePercent <= kMinHudScalePercent ? MF_GRAYED : 0),
              kTrayScaleDown, L"整体缩小 5%");
  AppendMenuW(size, MF_STRING | (settings_.scalePercent >= kMaxHudScalePercent ? MF_GRAYED : 0),
              kTrayScaleUp, L"整体放大 5%");
  AppendMenuW(size, MF_STRING, kTrayScaleReset, L"恢复默认缩放（80%）");
  submenu(size, L"尺寸与布局");

  HMENU theme = CreatePopupMenu();
  AppendMenuW(theme, MF_STRING | (settings_.themeEnabled ? MF_CHECKED : 0), kTrayTheme,
              L"迪迦 · 光之生命体主题");
  AppendMenuW(theme, MF_STRING | (settings_.energy.glow ? MF_CHECKED : 0), kTrayGlow, L"轻微光晕");
  AppendMenuW(theme, MF_STRING | (settings_.energy.stoneAtZero ? MF_CHECKED : 0), kTrayStone, L"0% 胸甲与灯石化");
  AppendMenuW(theme, MF_SEPARATOR, 0, nullptr);
  choice(theme, kTrayBlinkGentle, L"胸灯闪烁：温和", settings_.energy.blinkStyle == BlinkStyle::Gentle);
  choice(theme, kTrayBlinkStandard, L"胸灯闪烁：标准", settings_.energy.blinkStyle == BlinkStyle::Standard);
  choice(theme, kTrayBlinkFast, L"胸灯闪烁：激进", settings_.energy.blinkStyle == BlinkStyle::Aggressive);
  AppendMenuW(theme, MF_SEPARATOR, 0, nullptr);
  wchar_t threshold[64]{};
  swprintf_s(threshold, L"剩余 ≤ %d%% 时闪烁（仅胸灯）", settings_.energy.warningThreshold);
  AppendMenuW(theme, MF_STRING | MF_GRAYED, 0, threshold);
  AppendMenuW(theme, MF_STRING | (settings_.energy.warningThreshold <= 10 ? MF_GRAYED : 0),
              kTrayThresholdDown, L"降低闪烁阈值 5%");
  AppendMenuW(theme, MF_STRING | (settings_.energy.warningThreshold >= 80 ? MF_GRAYED : 0),
              kTrayThresholdUp, L"提高闪烁阈值 5%");
  submenu(theme, L"主题与胸灯效果");
  if (!demoMode_) AppendMenuW(menu, MF_STRING | (settings_.followChatGpt ? MF_CHECKED : 0),
                             kTrayFollow, L"跟随 ChatGPT 窗口");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayAbout, demoMode_ ? L"关于精修预览" : L"关于");
  AppendMenuW(menu, MF_STRING, kTrayExit, demoMode_ ? L"关闭预览（正式版不受影响）" : L"退出额度显示器");
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

}  // namespace monitor
