#include "overlay_window.h"
#include "../resources/resource.h"
#include <shellapi.h>

namespace monitor {
namespace {

const wchar_t* HudSizeModeLabel(HudSizeMode mode) {
  if (mode == HudSizeMode::Compact) return L"信息布局：紧凑";
  if (mode == HudSizeMode::Expanded) return L"信息布局：展开";
  return L"信息布局：标准";
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
  Shell_NotifyIconW(NIM_ADD, &data);
  data.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void OverlayWindow::RemoveTrayIcon() {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &data);
}

void OverlayWindow::UpdateTrayTooltip() {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = hwnd_;
  data.uID = 1;
  data.uFlags = NIF_TIP;
  std::wstring tip = L"Codex 额度 · " + DataStatusText(displayStatus_);
  if (const RateWindow* window = SelectedWindow()) {
    wchar_t percent[24]{};
    swprintf_s(percent, L" %.0f%%", window->remainingPercent);
    tip += percent;
  }
  wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

void OverlayWindow::ShowTrayMenu(POINT point) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kTrayShowHide,
              hidden_ ? L"恢复额度窗口" : L"最小化到托盘");
  AppendMenuW(menu, MF_STRING, kTrayRefresh, L"立即刷新");
  AppendMenuW(menu, MF_STRING, kTrayUsage, L"打开 Codex Usage 页面");
  AppendMenuW(menu, MF_STRING, kTrayOpenChatGpt, L"打开 ChatGPT");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"设置");
  AppendMenuW(menu, MF_STRING | (settings_.themeEnabled ? MF_CHECKED : 0), kTrayTheme,
              L"迪迦能量指示器主题");
  const wchar_t* display = settings_.displayMode == IndicatorDisplayMode::Both ? L"显示：进度条 + 能量指示器" :
                           settings_.displayMode == IndicatorDisplayMode::ProgressOnly ? L"显示：仅进度条" :
                           L"显示：仅能量指示器";
  AppendMenuW(menu, MF_STRING, kTrayDisplayMode, display);
  if (snapshot_ && snapshot_->windows.size() > 1) {
    AppendMenuW(menu, MF_STRING, kTrayNextWindow, L"切换主要/次要额度窗口");
  }
  AppendMenuW(menu, MF_STRING | (settings_.followChatGpt ? MF_CHECKED : 0), kTrayFollow,
              L"跟随 ChatGPT 窗口");
  AppendMenuW(menu, MF_STRING | (settings_.energy.glow ? MF_CHECKED : 0), kTrayGlow, L"轻微光晕");
  AppendMenuW(menu, MF_STRING | (settings_.energy.stoneAtZero ? MF_CHECKED : 0), kTrayStone, L"0% 石化效果");
  const wchar_t* style = settings_.energy.blinkStyle == BlinkStyle::Gentle ? L"闪烁风格：温和" :
                         settings_.energy.blinkStyle == BlinkStyle::Standard ? L"闪烁风格：标准" :
                         L"闪烁风格：激进";
  AppendMenuW(menu, MF_STRING, kTrayBlinkStyle, style);
  wchar_t threshold[64]{};
  swprintf_s(threshold, L"闪烁阈值：%d%%（左减 / 右加）", settings_.energy.warningThreshold);
  AppendMenuW(menu, MF_STRING, kTrayThresholdDown, threshold);
  AppendMenuW(menu, MF_STRING, kTrayThresholdUp, L"提高闪烁阈值 5%");
  wchar_t scaleLabel[64]{};
  swprintf_s(scaleLabel, L"整体缩放：%d%%", settings_.scalePercent);
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, scaleLabel);
  AppendMenuW(menu, MF_STRING, kTrayScaleDown, L"整体缩小 5%");
  AppendMenuW(menu, MF_STRING, kTrayScaleUp, L"整体放大 5%");
  AppendMenuW(menu, MF_STRING, kTrayScaleReset, L"恢复默认整体大小");
  AppendMenuW(menu, MF_STRING, kTraySizeMode, HudSizeModeLabel(settings_.sizeMode));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kTrayAbout, L"关于");
  AppendMenuW(menu, MF_STRING, kTrayExit, L"退出额度显示器");
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
}

}  // namespace monitor
