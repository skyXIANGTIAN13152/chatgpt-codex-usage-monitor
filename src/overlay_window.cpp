#include <winsock2.h>
#include <ws2ipdef.h>
#include "overlay_window.h"
#include "hud_controls.h"
#include "logging.h"
#include "../resources/resource.h"
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <windowsx.h>
#include <cmath>
#include <commctrl.h>

namespace monitor {
namespace {

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kBlinkTimer = 2;
constexpr UINT_PTR kRequestTimeoutTimer = 3;
constexpr UINT_PTR kLifecycleDebounceTimer = 4;
constexpr UINT_PTR kFollowCoalesceTimer = 5;
constexpr UINT_PTR kDataAgeTimer = 7;

template <typename T>
void Release(T*& value) {
  if (value) { value->Release(); value = nullptr; }
}

void CALLBACK InterfaceChanged(void* context, PMIB_IPINTERFACE_ROW,
                               MIB_NOTIFICATION_TYPE type) {
  if (type != MibInitialNotification && context) {
    PostMessageW(static_cast<HWND>(context), WM_MONITOR_NETWORK_CHANGE, 0, 0);
  }
}

int LogicalWidth(HudSizeMode mode, ProgressDisplayMode progressDisplayMode) {
  if (progressDisplayMode == ProgressDisplayMode::Ring) {
    if (mode == HudSizeMode::Compact) return 280;
    if (mode == HudSizeMode::Expanded) return 380;
    return 330;
  }
  if (mode == HudSizeMode::Compact) return 300;
  if (mode == HudSizeMode::Expanded) return 400;
  return 350;
}

int LogicalHeight(HudSizeMode mode) {
  if (mode == HudSizeMode::Compact) return 90;
  if (mode == HudSizeMode::Expanded) return 145;
  return 115;
}

float HudScale(int percent) {
  return static_cast<float>(std::clamp(percent, kMinHudScalePercent,
                                       kMaxHudScalePercent)) / 100.0f;
}

}  // namespace

OverlayWindow::OverlayWindow(HINSTANCE instance, Settings settings,
                             ChatGptInstance chatGpt, bool demoMode)
    : instance_(instance), settings_(std::move(settings)), chatGpt_(std::move(chatGpt)),
      demoMode_(demoMode) {}

OverlayWindow::~OverlayWindow() {
  appServer_.Stop();
  lifecycle_.Stop();
  DiscardGraphicsResources();
  Release(textSmall_);
  Release(textMedium_);
  Release(textLarge_);
  Release(textRing_);
  Release(textValue_);
  Release(writeFactory_);
  Release(wicFactory_);
  Release(d2dFactory_);
}

bool OverlayWindow::Create() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_DBLCLKS;
  windowClass.hInstance = instance_;
  windowClass.lpfnWndProc = WindowProc;
  windowClass.lpszClassName = demoMode_ ? kPreviewWindowClass : kWindowClass;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hIcon = static_cast<HICON>(LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  windowClass.hIconSm = static_cast<HICON>(LoadImageW(
      instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR | LR_SHARED));
  if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
  if (!windowClass.hIconSm) windowClass.hIconSm = windowClass.hIcon;
  RegisterClassExW(&windowClass);

  DWORD exStyle = WS_EX_LAYERED;
  exStyle |= demoMode_ ? WS_EX_APPWINDOW : (WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
  if (settings_.alwaysOnTop) exStyle |= WS_EX_TOPMOST;
  hwnd_ = CreateWindowExW(exStyle, windowClass.lpszClassName,
                          demoMode_ ? L"额度显示器 · 精修预览（示例数据）" : kProductName, WS_POPUP,
                          0, 0, 350, 115, nullptr, nullptr, instance_, this);
  if (!hwnd_) return false;
  SetLayeredWindowAttributes(hwnd_, 0, 242, LWA_ALPHA);
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  ResizeForMode();
  PositionInitially();
  taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
  if (!demoMode_) {
    AddTrayIcon();
    NotifyIpInterfaceChange(AF_UNSPEC, InterfaceChanged, hwnd_, FALSE, &networkNotification_);
  }
  UpdateTooltips();

  if (demoMode_) {
    RateLimitSnapshot demo;
    demo.status = DataStatus::Live;
    demo.receivedAt = demo.lastSuccessAt = std::chrono::system_clock::now();
    demo.planType = "Demo";
    RateWindow primary;
    primary.bucketId = "codex";
    primary.bucketName = "codex";
    primary.windowName = "secondary"; // Weekly fixture matches the regular protocol slot.
    double demoRemaining = 55.0;
    wchar_t demoValue[32]{};
    if (const DWORD length = GetEnvironmentVariableW(L"MONITOR_DEMO_REMAINING", demoValue,
                                static_cast<DWORD>(std::size(demoValue)));
        length > 0 && length < std::size(demoValue)) {
      wchar_t* end = nullptr;
      const double parsed = wcstod(demoValue, &end);
      if (end != demoValue && *end == L'\0' && std::isfinite(parsed)) {
        demoRemaining = std::clamp(parsed, 0.0, 100.0);
      }
    }
    primary.usedPercent = 100.0 - demoRemaining;
    primary.remainingPercent = demoRemaining;
    primary.windowDurationMins = 10080;
    primary.resetsAt = std::chrono::system_clock::now() + std::chrono::hours(158);
    demo.windows.push_back(primary);
    RateWindow secondary = primary;
    secondary.windowName = "primary";
    double fiveHourRemaining = 72.0;
    wchar_t fiveHourValue[32]{};
    if (const DWORD length = GetEnvironmentVariableW(L"MONITOR_DEMO_FIVE_HOUR_REMAINING", fiveHourValue,
                                static_cast<DWORD>(std::size(fiveHourValue)));
        length > 0 && length < std::size(fiveHourValue)) {
      wchar_t* end = nullptr;
      const double parsed = wcstod(fiveHourValue, &end);
      if (end != fiveHourValue && *end == L'\0' && std::isfinite(parsed)) {
        fiveHourRemaining = std::clamp(parsed, 0.0, 100.0);
      }
    }
    secondary.usedPercent = 100.0 - fiveHourRemaining;
    secondary.remainingPercent = fiveHourRemaining;
    secondary.windowDurationMins = 300;
    secondary.resetsAt = std::chrono::system_clock::now() + std::chrono::hours(3);
    wchar_t proFixture[2]{};
    GetEnvironmentVariableW(L"MONITOR_DEMO_PRO_ACCOUNT", proFixture, 2);
    if (proFixture[0] == L'1') {
      // Main bucket has only weekly data; Spark still supplies a full pair.
      demo.windows.front().windowName = "primary";
      secondary.bucketId = "codex_bengalfox";
      secondary.remainingPercent = 100;
      secondary.usedPercent = 0;
      demo.windows.push_back(secondary);
      secondary.windowName = "secondary";
      secondary.windowDurationMins = 10080;
      demo.windows.push_back(secondary);
    } else {
      demo.windows.push_back(secondary);
    }
    demo.credits.present = true;
    demo.credits.balance = 12.5;
    ApplySnapshot(std::move(demo));
  } else {
    lifecycle_.Start(hwnd_, chatGpt_);
    AuditChatGptWindows();
    StartCodex();
  }
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  if (!demoMode_) SetTimer(hwnd_, kDataAgeTimer, 30000, nullptr);
  UpdateWindow(hwnd_);
  return true;
}

int OverlayWindow::RunMessageLoop() {
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                                           LPARAM lparam) {
  OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<OverlayWindow*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  return self ? self->HandleMessage(message, wparam, lparam)
              : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT OverlayWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
    AddTrayIcon();
    UpdateTrayTooltip();
    return 0;
  }
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      // Taskbar/assistive activation can restore the preview with ShowWindow
      // rather than SC_RESTORE. Keep menu labels and the blink timer in sync.
      if (demoMode_ && hidden_ != (wparam == SIZE_MINIMIZED)) {
        hidden_ = wparam == SIZE_MINIMIZED;
        hoveredControl_ = HudControl::None;
        ConfigureBlinkTimer();
      }
      break;
    case WM_MOUSEACTIVATE:
      return demoMode_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
      RECT client{};
      GetClientRect(hwnd_, &client);
      const HudControl hovered = HitTestHudControl(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
          client.right, client.bottom, GetDpiForWindow(hwnd_), HudScale(settings_.scalePercent));
      if (hoveredControl_ != hovered) {
        hoveredControl_ = hovered;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&tracking);
      return 0;
    }
    case WM_NCMOUSEMOVE: {
      // Most of the frameless HUD is a draggable caption. Relay its movement
      // in client coordinates so quota details can still appear on hover.
      if (tooltip_) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        MSG relay{hwnd_, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y)};
        SendMessageW(tooltip_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relay));
      }
      TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
      TrackMouseEvent(&tracking);
      if (hoveredControl_ != HudControl::None) {
        hoveredControl_ = HudControl::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      break;
    }
    case WM_NCMOUSELEAVE:
      if (tooltip_) SendMessageW(tooltip_, TTM_POP, 0, 0);
      [[fallthrough]];
    case WM_MOUSELEAVE:
      if (hoveredControl_ != HudControl::None) {
        hoveredControl_ = HudControl::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      break;
    case WM_SETCURSOR:
      if (LOWORD(lparam) == HTCLIENT && hoveredControl_ != HudControl::None) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    case WM_NCHITTEST: {
      const LRESULT base = DefWindowProcW(hwnd_, message, wparam, lparam);
      if (base != HTCLIENT) return base;
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd_, &point);
      RECT client{};
      GetClientRect(hwnd_, &client);
      if (HitTestHudControl(point.x, point.y, client.right, client.bottom,
                            GetDpiForWindow(hwnd_), HudScale(settings_.scalePercent)) != HudControl::None) return HTCLIENT;
      return HTCAPTION;
    }
    case WM_LBUTTONUP: {
      RECT client{};
      GetClientRect(hwnd_, &client);
      const int x = GET_X_LPARAM(lparam);
      const int y = GET_Y_LPARAM(lparam);
      const HudControl control = HitTestHudControl(x, y, client.right, client.bottom,
                                                   GetDpiForWindow(hwnd_),
                                                   HudScale(settings_.scalePercent));
      if (control == HudControl::Minimize) {
        SetHidden(true);
      } else if (control == HudControl::Settings) {
        OpenSettingsMenu();
      } else {
        RequestRefresh(true);
      }
      return 0;
    }
    case WM_LBUTTONDBLCLK:
    case WM_NCLBUTTONDBLCLK:
      SetHidden(true);
      return 0;
    case WM_NCLBUTTONUP:
      if (wparam == HTCAPTION) RequestRefresh(true);
      return 0;
    case WM_RBUTTONUP:
    case WM_NCRBUTTONUP: {
      POINT point{};
      GetCursorPos(&point);
      ShowTrayMenu(point);
      return 0;
    }
    case WM_EXITSIZEMOVE:
      SaveWindowPosition();
      return 0;
    case WM_DISPLAYCHANGE: {
      RECT rect{};
      GetWindowRect(hwnd_, &rect);
      ClampToWorkArea(&rect);
      SetWindowPos(hwnd_, nullptr, rect.left, rect.top, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      // A monitor hot-plug can keep the old HWND render target alive even
      // though the window is now on a different DPI/virtual screen. Rebuild
      // the target and reapply the persisted logical HUD size.
      ResizeForMode();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      if (suggested) {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      // The suggested rectangle is DPI-aware, but may not match the user's
      // selected HUD scale. ResizeForMode recomputes it from the new DPI and
      // also discards the old Direct2D target.
      ResizeForMode();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_POWERBROADCAST:
      if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
        RequestRefresh(false);
      }
      return TRUE;
    case WM_TIMER:
      if (wparam == kBlinkTimer) {
        blinkOn_ = !blinkOn_;
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (wparam == kRefreshTimer) {
        KillTimer(hwnd_, kRefreshTimer);
        RequestRefresh(false);
      } else if (wparam == kRequestTimeoutTimer) {
        KillTimer(hwnd_, kRequestTimeoutTimer);
        if (appServer_.RequestInFlight()) {
          appServer_.MarkRequestTimedOut();
          ApplyServerError({AppServerErrorKind::Timeout, L"读取 Codex 额度超时。"});
        }
      } else if (wparam == kLifecycleDebounceTimer) {
        KillTimer(hwnd_, kLifecycleDebounceTimer);
        if (!demoMode_ && !lifecycle_.HasAnyWindow()) DestroyWindow(hwnd_);
      } else if (wparam == kFollowCoalesceTimer) {
        KillTimer(hwnd_, kFollowCoalesceTimer);
        FollowChatGptWindow();
      } else if (wparam == kTrayRetryTimer) {
        AddTrayIcon();
        if (trayIconAdded_) UpdateTrayTooltip();
      } else if (wparam == kDataAgeTimer) {
        RefreshDataAge();
      }
      return 0;
    case WM_MONITOR_SNAPSHOT: {
      std::unique_ptr<RateLimitSnapshot> snapshot(reinterpret_cast<RateLimitSnapshot*>(lparam));
      if (snapshot) ApplySnapshot(std::move(*snapshot));
      return 0;
    }
    case WM_MONITOR_SERVER_ERROR: {
      std::unique_ptr<AppServerError> error(reinterpret_cast<AppServerError*>(lparam));
      if (error) ApplyServerError(std::move(*error));
      return 0;
    }
    case WM_MONITOR_NETWORK_CHANGE:
      RequestRefresh(false);
      return 0;
    case WM_MONITOR_WINDOW_EVENT:
      if (wparam == EVENT_OBJECT_LOCATIONCHANGE && settings_.followChatGpt) {
        SetTimer(hwnd_, kFollowCoalesceTimer, 40, nullptr);
      }
      AuditChatGptWindows();
      return 0;
    case WM_MONITOR_PROCESS_EXIT:
      AuditChatGptWindows();
      return 0;
    case WM_MONITOR_SHOW:
      SetHidden(false);
      return 0;
    case WM_MONITOR_TRAY:
      if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == NIN_SELECT ||
          LOWORD(lparam) == NIN_KEYSELECT) {
        SetHidden(HiddenAfterTrayPrimaryActivation(hidden_));
      } else if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
        POINT point{};
        GetCursorPos(&point);
        ShowTrayMenu(point);
      }
      return 0;
    case WM_COMMAND:
      OnTrayCommand(LOWORD(wparam));
      return 0;
    case WM_CLOSE:
      DestroyWindow(hwnd_);
      return 0;
    case WM_SYSCOMMAND:
      if (demoMode_ && ((wparam & 0xfff0) == SC_RESTORE ||
                        (wparam & 0xfff0) == SC_MINIMIZE)) {
        SetHidden((wparam & 0xfff0) == SC_MINIMIZE);
        return 0;
      }
      break;
    case WM_DESTROY:
      SaveWindowPosition();
      RemoveTrayIcon();
      if (networkNotification_) {
        CancelMibChangeNotify2(networkNotification_);
        networkNotification_ = nullptr;
      }
      lifecycle_.Stop();
      appServer_.Stop();
      if (tooltip_) {
        DestroyWindow(tooltip_);
        tooltip_ = nullptr;
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void OverlayWindow::EnsureGraphicsResources() {
  if (!d2dFactory_) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_);
  }
  if (!writeFactory_) {
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&writeFactory_));
    if (writeFactory_) {
      writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
          DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"zh-CN", &textSmall_);
      writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
          DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN", &textMedium_);
      writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
          DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"zh-CN", &textLarge_);
      writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
          DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-CN", &textRing_);
      writeFactory_->CreateTextFormat(L"Segoe UI Variable", nullptr,
          DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN", &textValue_);
      if (textValue_) textValue_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      if (textRing_) textRing_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      if (textMedium_) textMedium_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      if (textLarge_) textLarge_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      if (textSmall_) {
        textSmall_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(textSmall_, &ellipsis))) {
          textSmall_->SetTrimming(&trimming, ellipsis);
          Release(ellipsis);
        }
      }
    }
  }
  if (renderTarget_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const D2D1_SIZE_U pixelSize = renderTarget_->GetPixelSize();
    float targetDpiX = 0.0f;
    float targetDpiY = 0.0f;
    renderTarget_->GetDpi(&targetDpiX, &targetDpiY);
    const float windowDpi = static_cast<float>(GetDpiForWindow(hwnd_));
    const bool sizeMismatch = client.right > 0 && client.bottom > 0 &&
        (pixelSize.width != static_cast<UINT>(client.right) ||
         pixelSize.height != static_cast<UINT>(client.bottom));
    const bool dpiMismatch = windowDpi > 0.0f &&
        (std::fabs(targetDpiX - windowDpi) > 0.5f ||
         std::fabs(targetDpiY - windowDpi) > 0.5f);
    if (sizeMismatch || dpiMismatch) DiscardGraphicsResources();
  }
  if (!renderTarget_ && d2dFactory_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                     D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN,
                                                      D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd_,
            D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom)),
            D2D1_PRESENT_OPTIONS_IMMEDIATELY), &renderTarget_);
    if (renderTarget_) {
      const float dpi = static_cast<float>(GetDpiForWindow(hwnd_));
      renderTarget_->SetDpi(dpi, dpi);
    }
  }
}

void OverlayWindow::DiscardGraphicsResources() {
  Release(chestStoneBitmap_);
  Release(chestWarningOffBitmap_);
  Release(chestWarningOnBitmap_);
  Release(chestBitmap_);
  themeBitmapLoadAttempted_ = false;
  Release(renderTarget_);
}

void OverlayWindow::EnsureThemeBitmap() {
  if (chestBitmap_ || themeBitmapLoadAttempted_ || !renderTarget_) return;
  themeBitmapLoadAttempted_ = true;

  if (!wicFactory_ &&
      FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory_)))) {
    return;
  }

  auto loadBitmap = [&](UINT resourceId, ID2D1Bitmap** bitmap) {
    HRSRC resource = FindResourceW(
        instance_, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(instance_, resource);
    const DWORD byteCount = SizeofResource(instance_, resource);
    BYTE* bytes = loaded ? static_cast<BYTE*>(LockResource(loaded)) : nullptr;
    if (!bytes || byteCount == 0) return false;

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT result = wicFactory_->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromMemory(bytes, byteCount);
    if (SUCCEEDED(result)) {
      result = wicFactory_->CreateDecoderFromStream(
          stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(result)) result = wicFactory_->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) {
      result = converter->Initialize(
          frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
          nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(result)) {
      result = renderTarget_->CreateBitmapFromWicBitmap(
          converter, nullptr, bitmap);
    }
    Release(converter);
    Release(frame);
    Release(decoder);
    Release(stream);
    return SUCCEEDED(result) && *bitmap;
  };

  loadBitmap(IDR_TIGA_CHEST_REFERENCE_PNG, &chestBitmap_);
  loadBitmap(IDR_TIGA_CHEST_WARNING_ON_PNG, &chestWarningOnBitmap_);
  loadBitmap(IDR_TIGA_CHEST_WARNING_OFF_PNG, &chestWarningOffBitmap_);
  loadBitmap(IDR_TIGA_CHEST_STONE_PNG, &chestStoneBitmap_);
}

void OverlayWindow::DrawThemedPanelImage(float width, float height) {
  if (!renderTarget_) return;
  EnsureThemeBitmap();
  if (!chestBitmap_) return;

  const D2D1_SIZE_F sourceSize = chestBitmap_->GetSize();
  if (sourceSize.width <= 0.0f || sourceSize.height <= 0.0f) return;

  // Preserve the supplied artwork. Only scaling, positioning and a soft edge
  // fade are applied so the rectangular source blends into the compact HUD.
  const float imageHeight = std::min(76.0f, height - 3.0f);
  const float imageWidth = imageHeight * sourceSize.width / sourceSize.height;
  const float centerX = 46.0f;
  const D2D1_RECT_F destination = D2D1::RectF(
      centerX - imageWidth * 0.5f, 1.0f,
      centerX + imageWidth * 0.5f, 1.0f + imageHeight);

  D2D1_GRADIENT_STOP fadeStops[] = {
      {0.00f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.00f)},
      {0.56f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.00f)},
      {0.82f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.72f)},
      {1.00f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.00f)},
  };
  ID2D1GradientStopCollection* fadeCollection = nullptr;
  ID2D1RadialGradientBrush* fadeBrush = nullptr;
  ID2D1Layer* fadeLayer = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          fadeStops, static_cast<UINT32>(std::size(fadeStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &fadeCollection))) {
    renderTarget_->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(centerX, 34.0f), D2D1::Point2F(0.0f, 0.0f),
            72.0f, 53.0f),
        fadeCollection, &fadeBrush);
  }
  renderTarget_->CreateLayer(nullptr, &fadeLayer);

  // Preserve the lamp and upper armor, but dissolve the rectangular bitmap's
  // bottom edge into the same background light instead of ending at a seam.
  const D2D1_GRADIENT_STOP bottomStops[] = {
      {0.00f, D2D1::ColorF(1, 1, 1, 1.00f)},
      {0.62f, D2D1::ColorF(1, 1, 1, 1.00f)},
      {0.78f, D2D1::ColorF(1, 1, 1, 0.82f)},
      {0.91f, D2D1::ColorF(1, 1, 1, 0.34f)},
      {1.00f, D2D1::ColorF(1, 1, 1, 0.00f)},
  };
  ID2D1GradientStopCollection* bottomCollection = nullptr;
  ID2D1LinearGradientBrush* bottomBrush = nullptr;
  ID2D1Layer* bottomLayer = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          bottomStops, static_cast<UINT32>(std::size(bottomStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &bottomCollection))) {
    renderTarget_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
        D2D1::Point2F(0, destination.top), D2D1::Point2F(0, destination.bottom)),
        bottomCollection, &bottomBrush);
  }
  renderTarget_->CreateLayer(nullptr, &bottomLayer);

  if (fadeBrush && fadeLayer) {
    D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::RectF(
        0.5f, 0.5f, std::min(width - 0.5f, kHudArtworkRight), height - 0.5f);
    parameters.opacityBrush = fadeBrush;
    renderTarget_->PushLayer(parameters, fadeLayer);
  }
  if (bottomBrush && bottomLayer) {
    D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::RectF(
        0.5f, 0.5f, std::min(width - 0.5f, kHudArtworkRight), height - 0.5f);
    parameters.opacityBrush = bottomBrush;
    renderTarget_->PushLayer(parameters, bottomLayer);
  }

  ID2D1Bitmap* artwork = chestBitmap_;
  float artworkOpacity = 0.82f;
  if (energyState_.visual == EnergyVisualState::WarningRedBlink ||
      energyState_.visual == EnergyVisualState::CriticalRedFastBlink) {
    if (blinkOn_ && chestWarningOnBitmap_) {
      artwork = chestWarningOnBitmap_;
      artworkOpacity = 0.82f;
    } else if (chestWarningOffBitmap_) {
      artwork = chestWarningOffBitmap_;
      artworkOpacity = 0.82f;
    }
  } else if (energyState_.visual == EnergyVisualState::Stone &&
             chestStoneBitmap_) {
    artwork = chestStoneBitmap_;
    artworkOpacity = 0.86f;
  } else if (energyState_.visual == EnergyVisualState::DataUnavailable &&
             chestWarningOffBitmap_) {
    artwork = chestWarningOffBitmap_;
    artworkOpacity = 0.52f;
  }
  renderTarget_->DrawBitmap(
      artwork, destination, artworkOpacity,
      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

  if (bottomBrush && bottomLayer) renderTarget_->PopLayer();
  if (fadeBrush && fadeLayer) renderTarget_->PopLayer();
  Release(bottomLayer);
  Release(bottomBrush);
  Release(bottomCollection);
  Release(fadeLayer);
  Release(fadeBrush);
  Release(fadeCollection);
}

void OverlayWindow::DrawTributeLightBackground(float width, float height) {
  if (!renderTarget_) return;
  const bool stone = energyState_.visual == EnergyVisualState::Stone;
  const bool unavailable = energyState_.visual == EnergyVisualState::DataUnavailable;
  const bool warning = energyState_.visual == EnergyVisualState::WarningRedBlink ||
                       energyState_.visual == EnergyVisualState::CriticalRedFastBlink;
  const bool neutral = stone || unavailable;
  const float strength = neutral ? 0.24f : 1.0f;
  const float focusX = settings_.displayMode == IndicatorDisplayMode::ProgressOnly
      ? width * 0.22f : 46.0f;
  const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
      D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 10.0f, 10.0f);

  // One light source, anchored to the chest: softly merge the artwork into
  // the panel rather than adding a separate badge, rails or another frame.
  auto bloom = [&](D2D1_POINT_2F center, float radiusX, float radiusY,
                    const D2D1_GRADIENT_STOP* stops, UINT32 count) {
    ID2D1GradientStopCollection* collection = nullptr;
    ID2D1RadialGradientBrush* brush = nullptr;
    if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
            stops, count, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection))) {
      renderTarget_->CreateRadialGradientBrush(
          D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0),
                                               radiusX, radiusY), collection, &brush);
    }
    if (brush) renderTarget_->FillRoundedRectangle(panel, brush);
    Release(brush);
    Release(collection);
  };
  const D2D1_COLOR_F energy = neutral ? D2D1::ColorF(0.64f, 0.67f, 0.71f)
      : warning ? D2D1::ColorF(0.95f, 0.26f, 0.39f)
                : D2D1::ColorF(0.27f, 0.69f, 0.94f);
  const D2D1_GRADIENT_STOP lightStops[] = {
      {0.00f, D2D1::ColorF(energy.r, energy.g, energy.b, 0.19f * strength)},
      {0.35f, D2D1::ColorF(energy.r, energy.g, energy.b, 0.085f * strength)},
      {0.72f, D2D1::ColorF(energy.r, energy.g, energy.b, 0.025f * strength)},
      {1.00f, D2D1::ColorF(energy.r, energy.g, energy.b, 0.0f)},
  };
  bloom(D2D1::Point2F(focusX, 36.0f), 116.0f, 68.0f,
        lightStops, static_cast<UINT32>(std::size(lightStops)));

  // A broad, low-contrast reflection under the lower chest follows its
  // silhouette. No visible curve or separate ornament crosses the footer.
  const D2D1_COLOR_F reflection = neutral ? D2D1::ColorF(0.60f, 0.63f, 0.67f)
      : warning ? D2D1::ColorF(0.82f, 0.26f, 0.35f)
                : D2D1::ColorF(0.68f, 0.60f, 0.84f);
  const D2D1_GRADIENT_STOP reflectionStops[] = {
      {0.00f, D2D1::ColorF(reflection.r, reflection.g, reflection.b, 0.080f * strength)},
      {0.42f, D2D1::ColorF(reflection.r, reflection.g, reflection.b, 0.033f * strength)},
      {1.00f, D2D1::ColorF(reflection.r, reflection.g, reflection.b, 0.0f)},
  };
  bloom(D2D1::Point2F(focusX + 6.0f, std::min(height - 18.0f, 67.0f)),
        68.0f, 29.0f, reflectionStops, static_cast<UINT32>(std::size(reflectionStops)));
}

void OverlayWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(hwnd_, &paint);
  EnsureGraphicsResources();
  if (!renderTarget_) { EndPaint(hwnd_, &paint); return; }
  renderTarget_->BeginDraw();
  renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
  renderTarget_->Clear(D2D1::ColorF(0.025f, 0.03f, 0.045f, 1.0f));
  const D2D1_SIZE_F renderSize = renderTarget_->GetSize();
  const float uiScale = HudScale(settings_.scalePercent);
  const D2D1_SIZE_F size = D2D1::SizeF(renderSize.width / uiScale,
                                       renderSize.height / uiScale);
  renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(uiScale, uiScale));

  ID2D1SolidColorBrush* background = nullptr;
  ID2D1SolidColorBrush* border = nullptr;
  ID2D1SolidColorBrush* text = nullptr;
  ID2D1SolidColorBrush* muted = nullptr;
  ID2D1SolidColorBrush* track = nullptr;
  ID2D1SolidColorBrush* ionOuterGlow = nullptr;
  ID2D1SolidColorBrush* ionInnerGlow = nullptr;
  ID2D1SolidColorBrush* ionMuted = nullptr;
  ID2D1SolidColorBrush* warningIonMuted = nullptr;
  ID2D1SolidColorBrush* dormantIonText = nullptr;
  ID2D1SolidColorBrush* ionParticle = nullptr;
  ID2D1SolidColorBrush* ionViolet = nullptr;
  ID2D1SolidColorBrush* ionCore = nullptr;
  ID2D1SolidColorBrush* warningIonGlow = nullptr;
  ID2D1SolidColorBrush* warningIonParticle = nullptr;
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.035f, 0.045f, 0.065f, 1.0f), &background);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.28f, 0.38f, 0.95f), &border);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.96f, 1.0f, 1.0f), &text);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.52f, 0.60f, 0.70f, 1.0f), &muted);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.13f, 0.18f, 1.0f), &track);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.20f, 1.00f, 1.0f), &ionOuterGlow);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.86f, 1.00f, 1.0f), &ionInnerGlow);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.82f, 0.94f, 1.0f), &ionMuted);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.48f, 0.66f, 1.0f), &warningIonMuted);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.64f, 0.68f, 0.72f, 1.0f), &dormantIonText);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.94f, 1.00f, 1.0f), &ionParticle);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.38f, 1.00f, 1.0f), &ionViolet);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.92f, 1.00f, 1.00f, 1.0f), &ionCore);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.78f, 0.02f, 0.34f, 1.0f), &warningIonGlow);
  renderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.10f, 0.34f, 1.0f), &warningIonParticle);

  D2D1_GRADIENT_STOP ionTextStops[] = {
      {0.00f, D2D1::ColorF(0.62f, 0.84f, 1.00f, 1.0f)},
      {0.18f, D2D1::ColorF(0.94f, 1.00f, 1.00f, 1.0f)},
      {0.46f, D2D1::ColorF(0.10f, 0.91f, 1.00f, 1.0f)},
      {0.70f, D2D1::ColorF(0.86f, 1.00f, 1.00f, 1.0f)},
      {1.00f, D2D1::ColorF(0.36f, 0.43f, 1.00f, 1.0f)},
  };
  ID2D1GradientStopCollection* ionTextStopCollection = nullptr;
  ID2D1LinearGradientBrush* ionText = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          ionTextStops, static_cast<UINT32>(std::size(ionTextStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &ionTextStopCollection))) {
    renderTarget_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                             D2D1::Point2F(0.0f, 30.0f)),
        ionTextStopCollection, &ionText);
  }
  D2D1_GRADIENT_STOP warningIonTextStops[] = {
      {0.00f, D2D1::ColorF(1.00f, 0.62f, 0.78f, 1.0f)},
      {0.18f, D2D1::ColorF(1.00f, 0.96f, 0.98f, 1.0f)},
      {0.46f, D2D1::ColorF(1.00f, 0.08f, 0.28f, 1.0f)},
      {0.70f, D2D1::ColorF(1.00f, 0.78f, 0.88f, 1.0f)},
      {1.00f, D2D1::ColorF(0.72f, 0.05f, 0.72f, 1.0f)},
  };
  ID2D1GradientStopCollection* warningIonTextStopCollection = nullptr;
  ID2D1LinearGradientBrush* warningIonText = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          warningIonTextStops, static_cast<UINT32>(std::size(warningIonTextStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
          &warningIonTextStopCollection))) {
    renderTarget_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                             D2D1::Point2F(0.0f, 30.0f)),
        warningIonTextStopCollection, &warningIonText);
  }
  const D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, size.width - 0.5f,
                                                                 size.height - 0.5f), 10, 10);
  const bool stonePanel = energyState_.visual == EnergyVisualState::Stone;
  const bool warningPanel =
      energyState_.visual == EnergyVisualState::WarningRedBlink ||
      energyState_.visual == EnergyVisualState::CriticalRedFastBlink;
  D2D1_GRADIENT_STOP panelStops[] = {
      {0.0f, stonePanel
          ? D2D1::ColorF(0.060f, 0.070f, 0.085f, 1.0f)
          : D2D1::ColorF(0.018f, 0.060f, 0.145f, 1.0f)},
      {0.42f, stonePanel
          ? D2D1::ColorF(0.035f, 0.042f, 0.052f, 1.0f)
          : D2D1::ColorF(0.022f, 0.032f, 0.092f, 1.0f)},
      {0.76f, stonePanel
          ? D2D1::ColorF(0.055f, 0.055f, 0.065f, 1.0f)
          : D2D1::ColorF(0.047f, 0.040f, 0.108f, 1.0f)},
      {1.0f, stonePanel
          ? D2D1::ColorF(0.080f, 0.070f, 0.075f, 1.0f)
          : warningPanel
              ? D2D1::ColorF(0.105f, 0.027f, 0.055f, 1.0f)
              : D2D1::ColorF(0.070f, 0.045f, 0.100f, 1.0f)},
  };
  ID2D1GradientStopCollection* panelStopCollection = nullptr;
  ID2D1LinearGradientBrush* panelGradient = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          panelStops, static_cast<UINT32>(std::size(panelStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &panelStopCollection))) {
    renderTarget_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                             D2D1::Point2F(size.width, size.height)),
        panelStopCollection, &panelGradient);
  }
  ID2D1Brush* panelBrush = panelGradient
      ? static_cast<ID2D1Brush*>(panelGradient)
      : static_cast<ID2D1Brush*>(background);
  renderTarget_->FillRoundedRectangle(panel, panelBrush);
  if (settings_.themeEnabled) {
    DrawTributeLightBackground(size.width, size.height);
    if (settings_.displayMode != IndicatorDisplayMode::ProgressOnly) {
      DrawThemedPanelImage(size.width, size.height);
    }
  }

  D2D1_GRADIENT_STOP borderStops[] = {
      {0.0f, stonePanel
          ? D2D1::ColorF(0.34f, 0.37f, 0.41f, 0.75f)
          : D2D1::ColorF(0.12f, 0.68f, 0.95f, 0.42f)},
      {0.50f, stonePanel
          ? D2D1::ColorF(0.42f, 0.42f, 0.44f, 0.62f)
          : D2D1::ColorF(0.48f, 0.28f, 0.82f, 0.30f)},
      {1.0f, stonePanel
          ? D2D1::ColorF(0.43f, 0.40f, 0.37f, 0.70f)
          : D2D1::ColorF(0.74f, 0.70f, 0.62f, 0.36f)},
  };
  ID2D1GradientStopCollection* borderStopCollection = nullptr;
  ID2D1LinearGradientBrush* borderGradient = nullptr;
  if (settings_.themeEnabled && SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          borderStops, static_cast<UINT32>(std::size(borderStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &borderStopCollection))) {
    renderTarget_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(size.width, size.height)),
        borderStopCollection, &borderGradient);
  }
  ID2D1Brush* panelBorder = borderGradient
      ? static_cast<ID2D1Brush*>(borderGradient)
      : static_cast<ID2D1Brush*>(border);
  renderTarget_->DrawRoundedRectangle(panel, panelBorder,
                                      settings_.themeEnabled ? 0.85f : 1.0f);

  const bool showEnergy = settings_.themeEnabled &&
                          settings_.displayMode != IndicatorDisplayMode::ProgressOnly;
  const bool showProgress = !settings_.themeEnabled ||
                            settings_.displayMode != IndicatorDisplayMode::EnergyOnly;
  const bool warningState =
      energyState_.visual == EnergyVisualState::WarningRedBlink ||
      energyState_.visual == EnergyVisualState::CriticalRedFastBlink;
  const bool embeddedIndicatorReady = chestBitmap_ &&
      (!warningState || (chestWarningOnBitmap_ && chestWarningOffBitmap_)) &&
      (energyState_.visual != EnergyVisualState::Stone || chestStoneBitmap_) &&
      (energyState_.visual != EnergyVisualState::DataUnavailable ||
       chestWarningOffBitmap_);
  if (showEnergy && !embeddedIndicatorReady) {
    DrawEnergyIndicator(12.0f, 10.0f, 68.0f,
                        std::min(92.0f, size.height - 20.0f));
  }
  const float contentLeft = showEnergy ? 94.0f : 16.0f;
  const float rightEdge = size.width - 12.0f;
  const CodexQuotaWindows quotaWindows = QuotaWindows();
  const RateWindow* weeklyWindow = quotaWindows.weekly;
  const RateWindow* fiveHourWindow = quotaWindows.fiveHour;
  const RateWindow* window = PrimaryQuotaWindow();
  const bool dualQuota = ShowsFiveHourQuota();
  const float remaining = window ? static_cast<float>(window->remainingPercent) : 0.0f;

  auto drawText = [&](std::wstring_view value, IDWriteTextFormat* format,
                      const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    if (format && brush) renderTarget_->DrawTextW(value.data(), static_cast<UINT32>(value.size()),
                                                  format, rect, brush,
                                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
  };

  auto drawIonField = [&](const D2D1_RECT_F& rect, UINT32 seed,
                          int count, float strength) {
    if (stonePanel || !ionCore || count <= 0) return;
    ID2D1SolidColorBrush* charge = warningState ? warningIonParticle : ionParticle;
    ID2D1SolidColorBrush* orbit = warningState ? warningIonGlow : ionViolet;
    if (!charge || !orbit) return;
    UINT32 state = seed;
    const float rectWidth = std::max(1.0f, rect.right - rect.left);
    const float rectHeight = std::max(1.0f, rect.bottom - rect.top);
    for (int i = 0; i < count; ++i) {
      state = state * 1664525u + 1013904223u;
      const float xUnit = static_cast<float>((state >> 8) & 0xffffu) / 65535.0f;
      state = state * 1664525u + 1013904223u;
      const float yUnit = static_cast<float>((state >> 8) & 0xffffu) / 65535.0f;
      const float x = rect.left + 1.0f + xUnit * std::max(1.0f, rectWidth - 2.0f);
      const float yBand = (i % 2) == 0
          ? 0.08f + 0.12f * yUnit
          : 0.80f + 0.12f * yUnit;
      const float y = rect.top + 1.0f + yBand * std::max(1.0f, rectHeight - 2.0f);
      const float radius = 0.28f + 0.18f * static_cast<float>((state >> 24) & 0xffu) / 255.0f;

      charge->SetOpacity((0.10f + 0.08f * yUnit) * strength);
      renderTarget_->FillEllipse(
          D2D1::Ellipse(D2D1::Point2F(x, y), radius + 0.82f, radius + 0.82f),
          charge);
      orbit->SetOpacity((0.18f + 0.10f * yUnit) * strength);
      if ((i % 3) == 0) {
        renderTarget_->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(x, y), 2.25f, 0.82f), orbit, 0.42f);
      } else {
        const float direction = (i % 2) == 0 ? 1.0f : -1.0f;
        renderTarget_->DrawLine(D2D1::Point2F(x - 2.0f, y + 0.75f * direction),
                                D2D1::Point2F(x + 0.35f, y - 0.15f * direction),
                                orbit, 0.46f);
      }
      charge->SetOpacity((0.54f + 0.22f * yUnit) * strength);
      renderTarget_->FillEllipse(
          D2D1::Ellipse(D2D1::Point2F(x, y), radius + 0.18f, radius + 0.18f),
          charge);
      ionCore->SetOpacity((0.76f + 0.18f * yUnit) * strength);
      renderTarget_->FillEllipse(
          D2D1::Ellipse(D2D1::Point2F(x - 0.08f, y - 0.08f), radius, radius),
          ionCore);
    }
  };

  auto drawIonText = [&](std::wstring_view value, IDWriteTextFormat* format,
                         const D2D1_RECT_F& rect, float strength,
                         UINT32 particleSeed, int particleCount) {
    if (!format) return;
    if (stonePanel) {
      if (ionOuterGlow) {
        ionOuterGlow->SetOpacity(0.055f * strength);
        drawText(value, format,
                 D2D1::RectF(rect.left + 0.55f, rect.top + 0.55f,
                             rect.right + 0.55f, rect.bottom + 0.55f),
                 ionOuterGlow);
      }
      if (dormantIonText) {
        dormantIonText->SetOpacity(0.90f);
        drawText(value, format, rect, dormantIonText);
      } else {
        drawText(value, format, rect, text);
      }
      return;
    }

    const float energizedStrength = strength;
    ID2D1SolidColorBrush* outerGlow = warningState ? warningIonGlow : ionOuterGlow;
    ID2D1SolidColorBrush* innerGlow = warningState ? warningIonParticle : ionInnerGlow;
    if (outerGlow) {
      outerGlow->SetOpacity(0.050f * energizedStrength);
      constexpr D2D1_POINT_2F offsets[] = {
          {-1.10f, 0.0f}, {1.10f, 0.0f}, {0.0f, -1.10f}, {0.0f, 1.10f},
          {-0.78f, -0.78f}, {0.78f, -0.78f}, {-0.78f, 0.78f}, {0.78f, 0.78f},
      };
      for (const D2D1_POINT_2F offset : offsets) {
        drawText(value, format,
                 D2D1::RectF(rect.left + offset.x, rect.top + offset.y,
                             rect.right + offset.x, rect.bottom + offset.y),
                 outerGlow);
      }
    }
    if (innerGlow) {
      innerGlow->SetOpacity(0.085f * energizedStrength);
      constexpr D2D1_POINT_2F offsets[] = {
          {-0.42f, 0.0f}, {0.42f, 0.0f}, {0.0f, -0.42f}, {0.0f, 0.42f},
      };
      for (const D2D1_POINT_2F offset : offsets) {
        drawText(value, format,
                 D2D1::RectF(rect.left + offset.x, rect.top + offset.y,
                             rect.right + offset.x, rect.bottom + offset.y),
                 innerGlow);
      }
    }

    ID2D1LinearGradientBrush* energyText = warningState ? warningIonText : ionText;
    ID2D1Brush* fill = energyText
        ? static_cast<ID2D1Brush*>(energyText)
        : static_cast<ID2D1Brush*>(text);
    if (energyText) {
      energyText->SetOpacity(0.84f + 0.16f * energizedStrength);
      energyText->SetStartPoint(D2D1::Point2F(rect.left, rect.top));
      energyText->SetEndPoint(D2D1::Point2F(rect.left, rect.bottom));
    }
    drawText(value, format, rect, fill);
    drawIonField(rect, particleSeed, particleCount, strength);
  };

  // "100%" is wider than the usual two-digit value. Reserve its full width
  // before placing the single bar, so its leading 1 is never clipped.
  const float percentWidth = SingleQuotaPercentWidth(remaining);
  const bool ringProgress = showProgress &&
                            settings_.progressDisplayMode == ProgressDisplayMode::Ring;
  float barRight = rightEdge - percentWidth - 6.0f;
  float infoLeft = contentLeft;
  const auto now = std::chrono::system_clock::now();

  auto percentText = [](const RateWindow* quota, bool includeSymbol = true) {
    if (!quota) return std::wstring(L"--");
    wchar_t buffer[16]{};
    swprintf_s(buffer, includeSymbol ? L"%.0f%%" : L"%.0f",
               quota->remainingPercent);
    return std::wstring(buffer);
  };
  auto resetCountdown = [&](const RateWindow* quota) {
    return quota && quota->resetsAt
        ? FormatResetCountdown(*quota->resetsAt, now)
        : std::wstring(L"--");
  };
  auto resetAbsolute = [](const RateWindow* quota) {
    return quota && quota->resetsAt
        ? FormatLocalResetTime(*quota->resetsAt)
        : std::wstring(L"--");
  };
  const std::wstring percent = percentText(window);
  const wchar_t* singleQuotaLabel = weeklyWindow ? L"周额度"
      : fiveHourWindow ? L"5小时额度" : L"当前额度";

  struct QuotaPalette {
    D2D1_COLOR_F deep;
    D2D1_COLOR_F bright;
    D2D1_COLOR_F core;
    D2D1_COLOR_F glow;
    D2D1_COLOR_F track;
    D2D1_COLOR_F edge;
  };
  auto quotaPalette = [&](bool fiveHour, bool warning) {
    if (stonePanel || displayStatus_ != DataStatus::Live) {
      return QuotaPalette{
          D2D1::ColorF(0.24f, 0.26f, 0.29f, 1.0f),
          D2D1::ColorF(0.48f, 0.50f, 0.52f, 1.0f),
          D2D1::ColorF(0.72f, 0.73f, 0.74f, 1.0f),
          D2D1::ColorF(0.45f, 0.46f, 0.48f, 0.35f),
          D2D1::ColorF(0.08f, 0.09f, 0.11f, 0.95f),
          D2D1::ColorF(0.36f, 0.38f, 0.41f, 0.82f)};
    }
    if (warning && fiveHour) {
      return QuotaPalette{
          D2D1::ColorF(0.66f, 0.00f, 0.12f, 1.0f),
          D2D1::ColorF(1.00f, 0.08f, 0.25f, 1.0f),
          D2D1::ColorF(1.00f, 0.82f, 0.88f, 1.0f),
          D2D1::ColorF(1.00f, 0.04f, 0.30f, 0.78f),
          D2D1::ColorF(0.16f, 0.03f, 0.08f, 0.96f),
          D2D1::ColorF(0.78f, 0.16f, 0.30f, 0.90f)};
    }
    if (warning) {
      return QuotaPalette{
          D2D1::ColorF(0.32f, 0.00f, 0.05f, 1.0f),
          D2D1::ColorF(0.72f, 0.04f, 0.15f, 1.0f),
          D2D1::ColorF(1.00f, 0.48f, 0.58f, 1.0f),
          D2D1::ColorF(0.66f, 0.00f, 0.12f, 0.68f),
          D2D1::ColorF(0.12f, 0.025f, 0.055f, 0.96f),
          D2D1::ColorF(0.54f, 0.12f, 0.22f, 0.88f)};
    }
    if (fiveHour) {
      return QuotaPalette{
          D2D1::ColorF(0.00f, 0.48f, 0.64f, 1.0f),
          D2D1::ColorF(0.00f, 0.92f, 1.00f, 1.0f),
          D2D1::ColorF(0.84f, 1.00f, 1.00f, 1.0f),
          D2D1::ColorF(0.00f, 0.90f, 1.00f, 0.80f),
          D2D1::ColorF(0.025f, 0.11f, 0.16f, 0.96f),
          D2D1::ColorF(0.12f, 0.64f, 0.76f, 0.92f)};
    }
    // Weekly quota is intentionally the deeper, calmer layer.
    return QuotaPalette{
        D2D1::ColorF(0.02f, 0.14f, 0.42f, 1.0f),
        D2D1::ColorF(0.14f, 0.46f, 0.86f, 1.0f),
        D2D1::ColorF(0.54f, 0.76f, 1.00f, 1.0f),
        D2D1::ColorF(0.10f, 0.40f, 0.88f, 0.68f),
        D2D1::ColorF(0.035f, 0.055f, 0.14f, 0.96f),
        D2D1::ColorF(0.17f, 0.38f, 0.70f, 0.90f)};
  };

  auto drawRingArc = [&](const D2D1_ELLIPSE& ellipse, float sweepDegrees,
                         ID2D1Brush* brush, float strokeWidth) {
    if (!brush || sweepDegrees <= 0.0f) return;
    if (sweepDegrees >= 359.5f) {
      renderTarget_->DrawEllipse(ellipse, brush, strokeWidth);
      return;
    }
    if (!d2dFactory_) return;
    ID2D1PathGeometry* geometry = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geometry)) ||
        FAILED(geometry->Open(&sink))) {
      Release(sink);
      Release(geometry);
      return;
    }
    const float startRadians = -1.57079632679f;
    const float endRadians = startRadians + sweepDegrees * 0.01745329252f;
    const D2D1_POINT_2F start = D2D1::Point2F(
        ellipse.point.x + ellipse.radiusX * std::cos(startRadians),
        ellipse.point.y + ellipse.radiusY * std::sin(startRadians));
    const D2D1_POINT_2F end = D2D1::Point2F(
        ellipse.point.x + ellipse.radiusX * std::cos(endRadians),
        ellipse.point.y + ellipse.radiusY * std::sin(endRadians));
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point = end;
    arc.size = D2D1::SizeF(ellipse.radiusX, ellipse.radiusY);
    arc.rotationAngle = 0.0f;
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = sweepDegrees >= 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    renderTarget_->DrawGeometry(geometry, brush, strokeWidth);
    const float capRadius = strokeWidth * 0.5f;
    renderTarget_->FillEllipse(D2D1::Ellipse(start, capRadius, capRadius), brush);
    renderTarget_->FillEllipse(D2D1::Ellipse(end, capRadius, capRadius), brush);
    Release(sink);
    Release(geometry);
  };

  auto drawQuotaRing = [&](const D2D1_ELLIPSE& ring, const RateWindow* quota,
                           bool fiveHour, float strokeWidth) {
    const bool available = quota && displayStatus_ == DataStatus::Live;
    const float value = quota ? static_cast<float>(quota->remainingPercent) : 0.0f;
    const bool warning = available && value <= settings_.energy.warningThreshold;
    const QuotaPalette palette = quotaPalette(fiveHour, warning);
    ID2D1SolidColorBrush* ringTrack = nullptr;
    ID2D1SolidColorBrush* ringEdge = nullptr;
    ID2D1SolidColorBrush* ringGlow = nullptr;
    ID2D1SolidColorBrush* ringHead = nullptr;
    ID2D1GradientStopCollection* ringStops = nullptr;
    ID2D1LinearGradientBrush* ringEnergy = nullptr;
    renderTarget_->CreateSolidColorBrush(palette.track, &ringTrack);
    renderTarget_->CreateSolidColorBrush(palette.edge, &ringEdge);
    renderTarget_->CreateSolidColorBrush(palette.glow, &ringGlow);
    renderTarget_->CreateSolidColorBrush(palette.core, &ringHead);
    const D2D1_GRADIENT_STOP stops[] = {
        {0.00f, palette.deep}, {0.52f, palette.bright},
        {0.88f, palette.bright}, {1.00f, palette.core},
    };
    if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
            stops, static_cast<UINT32>(std::size(stops)), D2D1_GAMMA_2_2,
            D2D1_EXTEND_MODE_CLAMP, &ringStops))) {
      renderTarget_->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(
              D2D1::Point2F(ring.point.x - ring.radiusX, ring.point.y),
              D2D1::Point2F(ring.point.x + ring.radiusX, ring.point.y)),
          ringStops, &ringEnergy);
    }

    if (ringTrack) renderTarget_->DrawEllipse(ring, ringTrack, strokeWidth);
    if (!stonePanel && available && ringGlow && settings_.energy.glow) {
      ringGlow->SetOpacity(fiveHour ? 0.23f : 0.14f);
      drawRingArc(ring, value > 0.0f ? value * 3.6f : 0.0f,
                  ringGlow, strokeWidth + (fiveHour ? 5.0f : 4.0f));
    }
    if (ringEnergy && quota && value > 0.0f) {
      ringEnergy->SetOpacity(available ? 1.0f : 0.30f);
      drawRingArc(ring, std::clamp(value, 0.0f, 100.0f) * 3.6f,
                  ringEnergy, strokeWidth);
      if (available && ringHead && value < 99.9f) {
        const float angle = -1.57079632679f + value * 0.06283185307f;
        const float headRadius = std::max(1.5f, strokeWidth * 0.43f);
        renderTarget_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(
                              ring.point.x + ring.radiusX * std::cos(angle),
                              ring.point.y + ring.radiusY * std::sin(angle)),
                          headRadius, headRadius), ringHead);
      }
    }
    if (ringEdge) {
      ringEdge->SetOpacity(0.38f);
      renderTarget_->DrawEllipse(ring, ringEdge, 0.55f);
    }
    Release(ringEnergy);
    Release(ringStops);
    Release(ringHead);
    Release(ringGlow);
    Release(ringEdge);
    Release(ringTrack);
  };

  auto drawQuotaLabel = [&](std::wstring_view value, const D2D1_RECT_F& rect,
                            const RateWindow* quota, bool fiveHour,
                            DWRITE_TEXT_ALIGNMENT alignment,
                            IDWriteTextFormat* requestedFormat = nullptr) {
    IDWriteTextFormat* format = requestedFormat ? requestedFormat : textSmall_;
    if (!format) return;
    const bool warning = quota && displayStatus_ == DataStatus::Live &&
                         quota->remainingPercent <= settings_.energy.warningThreshold;
    const QuotaPalette palette = quotaPalette(fiveHour, warning);
    ID2D1SolidColorBrush* labelBrush = nullptr;
    renderTarget_->CreateSolidColorBrush(
        quota && displayStatus_ == DataStatus::Live ? palette.core
                                                    : D2D1::ColorF(0.48f, 0.52f, 0.58f, 1.0f),
        &labelBrush);
    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    drawText(value, format, rect, labelBrush ? static_cast<ID2D1Brush*>(labelBrush)
                                                : static_cast<ID2D1Brush*>(muted));
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    Release(labelBrush);
  };

  auto drawQuotaBar = [&](const D2D1_RECT_F& barRect, const RateWindow* quota,
                          bool fiveHour) {
    const bool available = quota && displayStatus_ == DataStatus::Live;
    const float value = quota ? static_cast<float>(quota->remainingPercent) : 0.0f;
    const bool warning = available && value <= settings_.energy.warningThreshold;
    const QuotaPalette palette = quotaPalette(fiveHour, warning);
    const float height = barRect.bottom - barRect.top;
    const float outerRadius = std::max(2.0f, height * 0.42f);
    const D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(barRect, outerRadius, outerRadius);
    ID2D1SolidColorBrush* trackBrush = nullptr;
    ID2D1SolidColorBrush* trackEdge = nullptr;
    renderTarget_->CreateSolidColorBrush(palette.track, &trackBrush);
    renderTarget_->CreateSolidColorBrush(palette.edge, &trackEdge);
    renderTarget_->FillRoundedRectangle(bar,
        trackBrush ? static_cast<ID2D1Brush*>(trackBrush)
                   : static_cast<ID2D1Brush*>(track));

    if (quota && value > 0.0f) {
      const float fillRight = barRect.left +
          (barRect.right - barRect.left) * std::clamp(value, 0.0f, 100.0f) / 100.0f;
      const float fillWidth = std::max(1.0f, fillRight - barRect.left);
      const float radius = std::min(outerRadius, fillWidth * 0.5f);
      ID2D1SolidColorBrush* bloom = nullptr;
      ID2D1SolidColorBrush* streak = nullptr;
      ID2D1SolidColorBrush* head = nullptr;
      renderTarget_->CreateSolidColorBrush(palette.glow, &bloom);
      renderTarget_->CreateSolidColorBrush(palette.core, &streak);
      renderTarget_->CreateSolidColorBrush(palette.core, &head);
      if (!stonePanel && bloom && settings_.energy.glow) {
        bloom->SetOpacity(fiveHour ? 0.18f : 0.10f);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(barRect.left - 1.5f, barRect.top - 2.0f,
                        fillRight + 3.0f, barRect.bottom + 2.0f),
            radius + 2.0f, radius + 2.0f), bloom);
      }
      const D2D1_GRADIENT_STOP stops[] = {
          {0.00f, palette.deep}, {0.50f, palette.bright},
          {0.88f, palette.bright}, {1.00f, palette.core},
      };
      ID2D1GradientStopCollection* stopCollection = nullptr;
      ID2D1LinearGradientBrush* energy = nullptr;
      if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
              stops, static_cast<UINT32>(std::size(stops)), D2D1_GAMMA_2_2,
              D2D1_EXTEND_MODE_CLAMP, &stopCollection))) {
        renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(barRect.left, barRect.top),
                D2D1::Point2F(fillRight, barRect.bottom)),
            stopCollection, &energy);
      }
      if (energy) {
        energy->SetOpacity(available ? 1.0f : 0.30f);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(barRect.left, barRect.top, fillRight, barRect.bottom),
            radius, radius), energy);
      }
      renderTarget_->PushAxisAlignedClip(
          D2D1::RectF(barRect.left, barRect.top, fillRight, barRect.bottom),
          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      if (available && streak && fillWidth > 30.0f) {
        streak->SetOpacity(fiveHour ? 0.42f : 0.24f);
        for (int index = 1; index <= 3; ++index) {
          const float x = barRect.left + fillWidth * static_cast<float>(index) / 4.0f;
          renderTarget_->DrawLine(
              D2D1::Point2F(x - 3.5f, barRect.bottom - 1.0f),
              D2D1::Point2F(x + 3.5f, barRect.top + 1.0f), streak, 0.8f);
        }
      }
      renderTarget_->PopAxisAlignedClip();
      if (available && head && fillWidth > 4.0f) {
        renderTarget_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillRight - 1.0f,
                                        (barRect.top + barRect.bottom) * 0.5f),
                          1.7f, std::max(2.0f, height * 0.37f)), head);
      }
      Release(energy);
      Release(stopCollection);
      Release(head);
      Release(streak);
      Release(bloom);
    }
    if (trackEdge) renderTarget_->DrawRoundedRectangle(bar, trackEdge, 0.85f);
    Release(trackEdge);
    Release(trackBrush);
  };

  auto drawQuotaRow = [&](const wchar_t* label, const RateWindow* quota,
                          bool fiveHour, float left, float rowTop) {
    drawQuotaLabel(label, D2D1::RectF(left, rowTop, left + 34.0f, rowTop + 17.0f),
                   quota, fiveHour, DWRITE_TEXT_ALIGNMENT_LEADING);
    drawQuotaLabel(percentText(quota),
                   D2D1::RectF(left + 37.0f, rowTop - 1.0f, rightEdge, rowTop + 18.0f),
                   quota, fiveHour, DWRITE_TEXT_ALIGNMENT_TRAILING, textValue_);
    drawText(L"重置 " + resetCountdown(quota), textSmall_,
             D2D1::RectF(left, rowTop + 17.0f, rightEdge, rowTop + 29.0f), muted);
  };

  if (ringProgress) {
    const float diameter = settings_.sizeMode == HudSizeMode::Compact
        ? (dualQuota ? 56.0f : 52.0f)
        : (settings_.sizeMode == HudSizeMode::Expanded
               ? (dualQuota ? 76.0f : 72.0f)
               : (dualQuota ? 66.0f : 62.0f));
    const float top = settings_.sizeMode == HudSizeMode::Compact ? 7.0f
        : (settings_.sizeMode == HudSizeMode::Expanded ? 17.0f : 11.0f);
    const float ringLeft = CalculateHudRingLeft(contentLeft, showEnergy);
    const D2D1_ELLIPSE outerRing = D2D1::Ellipse(
        D2D1::Point2F(ringLeft + diameter * 0.5f + 1.0f,
                      top + diameter * 0.5f),
        diameter * 0.5f - 3.2f, diameter * 0.5f - 3.2f);
    // Preserve the full-size values in compact dual mode after moving the
    // ring clear of the chest; only tighten the ring-to-details gutter.
    infoLeft = ringLeft + diameter + (dualQuota ? 4.0f : 10.0f);
    drawQuotaRing(outerRing, dualQuota ? weeklyWindow : window, false,
                  dualQuota ? 4.8f : 5.6f);
    if (dualQuota) {
      const float innerRadius = std::max(8.0f, outerRing.radiusX - 8.2f);
      const D2D1_ELLIPSE innerRing = D2D1::Ellipse(
          outerRing.point, innerRadius, innerRadius);
      drawQuotaRing(innerRing, fiveHourWindow, true, 4.0f);

      const std::wstring innerPercent = percentText(fiveHourWindow, false);
      drawQuotaLabel(innerPercent,
                     D2D1::RectF(innerRing.point.x - innerRadius + 2.0f,
                                 innerRing.point.y - 13.0f,
                                 innerRing.point.x + innerRadius - 2.0f,
                                 innerRing.point.y + 4.0f),
                     fiveHourWindow, true, DWRITE_TEXT_ALIGNMENT_CENTER, textMedium_);
      drawQuotaLabel(L"5H",
                     D2D1::RectF(innerRing.point.x - innerRadius + 2.0f,
                                 innerRing.point.y + 1.0f,
                                 innerRing.point.x + innerRadius - 2.0f,
                                 innerRing.point.y + 14.0f),
                     fiveHourWindow, true, DWRITE_TEXT_ALIGNMENT_CENTER);
      const float rowGap = settings_.sizeMode == HudSizeMode::Compact ? 28.0f : 35.0f;
      drawQuotaRow(L"周额度", weeklyWindow, false, infoLeft, top - 1.0f);
      drawQuotaRow(L"5小时", fiveHourWindow, true, infoLeft, top - 1.0f + rowGap);
    } else {
      IDWriteTextFormat* centerFormat = percent.size() >= 4 ? textValue_ : textRing_;
      if (centerFormat) {
        centerFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        centerFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      }
      drawIonText(percent, centerFormat,
                  D2D1::RectF(outerRing.point.x - outerRing.radiusX - 2.0f,
                              outerRing.point.y - outerRing.radiusY + 1.0f,
                              outerRing.point.x + outerRing.radiusX + 2.0f,
                              outerRing.point.y + outerRing.radiusY - 1.0f),
                  1.0f, 0x7a31u + static_cast<UINT32>(std::max(0.0f, remaining)), 0);
      if (centerFormat) {
        centerFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        centerFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
      }
    }
    barRight = rightEdge;
  }

  if (!ringProgress && showProgress) {
    if (dualQuota) {
      const float gap = settings_.sizeMode == HudSizeMode::Compact ? 29.0f : 35.0f;
      auto drawBarRow = [&](const wchar_t* label, const RateWindow* quota, bool fiveHour, float rowTop) {
        drawQuotaLabel(label, D2D1::RectF(contentLeft, rowTop, contentLeft + 32.0f, rowTop + 17.0f),
                       quota, fiveHour, DWRITE_TEXT_ALIGNMENT_LEADING);
        drawText(L"重置 " + resetCountdown(quota), textSmall_,
                 D2D1::RectF(contentLeft + 40.0f, rowTop + 3.0f, rightEdge - 46.0f, rowTop + 16.0f), muted);
        drawQuotaLabel(percentText(quota),
                       D2D1::RectF(rightEdge - 46.0f, rowTop - 1.0f, rightEdge, rowTop + 18.0f),
                       quota, fiveHour, DWRITE_TEXT_ALIGNMENT_TRAILING, textValue_);
        drawQuotaBar(D2D1::RectF(contentLeft, rowTop + 20.0f, rightEdge, rowTop + 27.0f),
                     quota, fiveHour);
      };
      drawBarRow(L"周额度", weeklyWindow, false, 6.0f);
      drawBarRow(L"5小时", fiveHourWindow, true, 6.0f + gap);
      barRight = rightEdge;
    } else {
      drawQuotaBar(D2D1::RectF(contentLeft, 18.0f, barRight, 35.0f),
                   window, false);
    }
  }

  if (!showProgress && dualQuota) {
    const float rowGap = settings_.sizeMode == HudSizeMode::Compact ? 28.0f : 35.0f;
    drawQuotaRow(L"周额度", weeklyWindow, false, contentLeft, 6.0f);
    drawQuotaRow(L"5小时", fiveHourWindow, true, contentLeft, 6.0f + rowGap);
  }
  if (textLarge_) textLarge_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
  if (!ringProgress && !dualQuota) {
    const D2D1_RECT_F percentRect =
        D2D1::RectF(rightEdge - percentWidth, 7.0f, rightEdge, 45.0f);
    drawIonText(percent, textLarge_, percentRect, 1.0f,
                0x7a31u + static_cast<UINT32>(std::max(0.0f, remaining)), 0);
    const float percentIonWidth = percent.size() >= 4 ? 63.0f
        : (percent.size() == 3 ? 56.0f : 43.0f);
    drawIonField(D2D1::RectF(rightEdge - percentIonWidth, 7.0f,
                             rightEdge, 45.0f),
                 0x7a31u + static_cast<UINT32>(std::max(0.0f, remaining)),
                 4, 1.0f);
  }
  if (textLarge_) textLarge_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

  ID2D1SolidColorBrush* secondaryText = stonePanel
      ? dormantIonText
      : (warningState ? warningIonMuted : ionMuted);
  if (secondaryText) secondaryText->SetOpacity(stonePanel ? 0.78f : 0.94f);
  if (!dualQuota) {
    // A single quota gets a complete, centered information group. No empty
    // five-hour row, and no countdown/date overlap in the compact bar layout.
    const float row2 = ringProgress
        ? (settings_.sizeMode == HudSizeMode::Compact ? 23.0f
            : settings_.sizeMode == HudSizeMode::Expanded ? 43.0f : 33.0f)
        : (settings_.sizeMode == HudSizeMode::Compact ? 39.0f : 44.0f);
    drawQuotaLabel(singleQuotaLabel,
        D2D1::RectF(infoLeft, ringProgress ? row2 - 17.0f : 1.0f,
                    ringProgress ? rightEdge : barRight,
                    ringProgress ? row2 : 18.0f),
        window, false, ringProgress ? DWRITE_TEXT_ALIGNMENT_CENTER
                                   : DWRITE_TEXT_ALIGNMENT_LEADING);
    const std::wstring countdown = L"重置  " + resetCountdown(window);
    const std::wstring absolute = resetAbsolute(window);
    const D2D1_RECT_F countdownRect =
        D2D1::RectF(infoLeft, row2, rightEdge, row2 + 16.0f);
    if (textSmall_) {
      textSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    drawIonText(countdown, textSmall_, countdownRect, 0.64f,
                0xc013u + static_cast<UINT32>(countdown.size()), 2);
    const float absoluteRow = row2 + 15.0f;
    drawText(absolute, textSmall_,
             D2D1::RectF(infoLeft, absoluteRow, rightEdge, absoluteRow + 14.0f),
             secondaryText ? static_cast<ID2D1Brush*>(secondaryText)
                           : static_cast<ID2D1Brush*>(muted));
  } else if (settings_.sizeMode == HudSizeMode::Expanded) {
    const std::wstring absolute = L"周 " + resetAbsolute(weeklyWindow) +
                                  L"  ·  5H " + resetAbsolute(fiveHourWindow);
    drawText(absolute, textSmall_, D2D1::RectF(contentLeft, 88.0f, rightEdge, 102.0f), muted);
  }
  if (textSmall_) textSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

  const float row3 = size.height - (settings_.sizeMode == HudSizeMode::Expanded ? 39.0f : 23.0f);
  D2D1_COLOR_F statusColor = D2D1::ColorF(0.98f, 0.58f, 0.18f, 1.0f);
  if (displayStatus_ == DataStatus::Live) {
    if (warningState) {
      statusColor = D2D1::ColorF(1.0f, 0.22f, 0.30f, 1.0f);
    } else if (energyState_.visual == EnergyVisualState::Stone) {
      statusColor = D2D1::ColorF(0.62f, 0.65f, 0.69f, 1.0f);
    } else {
      statusColor = D2D1::ColorF(0.18f, 0.92f, 0.55f, 1.0f);
    }
  }
  ID2D1SolidColorBrush* statusBrush = nullptr;
  renderTarget_->CreateSolidColorBrush(statusColor, &statusBrush);
  ID2D1SolidColorBrush* statusHalo = warningState ? warningIonParticle : ionInnerGlow;
  if (!stonePanel && statusHalo) {
    statusHalo->SetOpacity(0.12f);
    renderTarget_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(contentLeft + 3, row3 + 7), 5.0f, 5.0f),
        statusHalo);
  }
  renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(contentLeft + 3, row3 + 7), 3, 3),
                             statusBrush);
  const D2D1_RECT_F statusRect =
      D2D1::RectF(contentLeft + 11, row3, rightEdge - 55, row3 + 18);
  const std::wstring statusLine = CurrentStatusLine();
  if (!stonePanel && statusHalo) {
    statusHalo->SetOpacity(0.070f);
    constexpr D2D1_POINT_2F statusOffsets[] = {
        {-0.48f, 0.0f}, {0.48f, 0.0f}, {0.0f, -0.48f}, {0.0f, 0.48f},
    };
    for (const D2D1_POINT_2F offset : statusOffsets) {
      drawText(statusLine, textSmall_,
               D2D1::RectF(statusRect.left + offset.x, statusRect.top + offset.y,
                           statusRect.right + offset.x, statusRect.bottom + offset.y),
               statusHalo);
    }
  }
  drawText(statusLine, textSmall_, statusRect, statusBrush);
  const HudControlLayout controls = CalculateHudControlLayout(size.width, size.height);
  ID2D1SolidColorBrush* controlBrush = nullptr;
  renderTarget_->CreateSolidColorBrush(stonePanel
      ? D2D1::ColorF(0.65f, 0.68f, 0.72f)
      : D2D1::ColorF(0.66f, 0.78f, 0.90f), &controlBrush);
  if (controlBrush) {
    const auto drawControl = [&](HudControl control, const HudControlRect& rect) {
      const float centerX = (rect.left + rect.right) * 0.5f;
      const float centerY = (rect.top + rect.bottom) * 0.5f;
      const bool hovered = hoveredControl_ == control;
      controlBrush->SetOpacity(hovered ? 0.16f : 0.035f);
      renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
          D2D1::RectF(centerX - 11.0f, centerY - 10.0f,
                      centerX + 11.0f, centerY + 10.0f), 4, 4), controlBrush);
      controlBrush->SetOpacity(hovered ? 1.0f : 0.72f);
      if (control == HudControl::Minimize) {
        renderTarget_->DrawLine(D2D1::Point2F(centerX - 5, centerY),
                                D2D1::Point2F(centerX + 5, centerY), controlBrush, 1.4f);
      } else {
        // Vector gear: its visible center is the same center used by hit-testing.
        renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY),
                                                4.0f, 4.0f), controlBrush, 1.1f);
        renderTarget_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY),
                                                1.35f, 1.35f), controlBrush, 1.0f);
        for (int tooth = 0; tooth < 8; ++tooth) {
          const float angle = static_cast<float>(tooth) * 0.78539816f;
          const float x = std::cos(angle), y = std::sin(angle);
          renderTarget_->DrawLine(D2D1::Point2F(centerX + x * 4.0f, centerY + y * 4.0f),
                                  D2D1::Point2F(centerX + x * 5.6f, centerY + y * 5.6f),
                                  controlBrush, 1.3f);
        }
      }
    };
    drawControl(HudControl::Settings, controls.settings);
    drawControl(HudControl::Minimize, controls.minimize);
    Release(controlBrush);
  }

  if (settings_.sizeMode == HudSizeMode::Expanded && dualQuota && showProgress) {
    const std::wstring detail = settings_.progressDisplayMode == ProgressDisplayMode::Ring
        ? L"周·深色外圈   5H·亮色内圈"
        : L"周·深色上条   5H·亮色下条";
    if (secondaryText) secondaryText->SetOpacity(stonePanel ? 0.78f : 0.94f);
    drawText(detail, textSmall_, D2D1::RectF(contentLeft, size.height - 25,
                                             rightEdge - 55, size.height - 7),
             secondaryText ? static_cast<ID2D1Brush*>(secondaryText)
                           : static_cast<ID2D1Brush*>(muted));
  }

  Release(statusBrush);
  Release(warningIonText);
  Release(warningIonTextStopCollection);
  Release(ionText);
  Release(ionTextStopCollection);
  Release(warningIonParticle);
  Release(warningIonGlow);
  Release(ionCore);
  Release(ionViolet);
  Release(ionParticle);
  Release(dormantIonText);
  Release(warningIonMuted);
  Release(ionMuted);
  Release(ionInnerGlow);
  Release(ionOuterGlow);
  Release(borderGradient);
  Release(borderStopCollection);
  Release(panelGradient);
  Release(panelStopCollection);
  Release(track);
  Release(muted);
  Release(text);
  Release(border);
  Release(background);
  renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
  const HRESULT result = renderTarget_->EndDraw();
  if (result == D2DERR_RECREATE_TARGET) DiscardGraphicsResources();
  if (!workingSetTrimmed_) {
    workingSetTrimmed_ = true;
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1),
                             static_cast<SIZE_T>(-1));
  }
  EndPaint(hwnd_, &paint);
}

void OverlayWindow::StartCodex() {
  if (demoMode_) return;
  displayStatus_ = DataStatus::Connecting;
  energyState_ = ComputeEnergyState(std::nullopt, settings_.energy);
  const HWND target = hwnd_;
  appServer_.Start(
      [target](RateLimitSnapshot snapshot) {
        auto* message = new RateLimitSnapshot(std::move(snapshot));
        if (!PostMessageW(target, WM_MONITOR_SNAPSHOT, 0, reinterpret_cast<LPARAM>(message))) delete message;
      },
      [target](AppServerError error) {
        auto* message = new AppServerError(std::move(error));
        if (!PostMessageW(target, WM_MONITOR_SERVER_ERROR, 0, reinterpret_cast<LPARAM>(message))) delete message;
      });
  // Start() also sends the first quota request. It needs the same watchdog as
  // later polls, otherwise a silent initial request can remain stuck forever.
  if (appServer_.RequestInFlight()) SetTimer(hwnd_, kRequestTimeoutTimer, 15000, nullptr);
  UpdateTooltips();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::RequestRefresh(bool userInitiated) {
  if (demoMode_) return;
  if (!appServer_.IsRunning()) {
    StartCodex();
    return;
  }
  if (appServer_.RequestRateLimits()) {
    SetTimer(hwnd_, kRequestTimeoutTimer, 15000, nullptr);
    if (userInitiated) InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void OverlayWindow::ApplySnapshot(RateLimitSnapshot snapshot) {
  // An unsolicited sparse notification is not the reply to an in-flight poll.
  if (!appServer_.RequestInFlight()) KillTimer(hwnd_, kRequestTimeoutTimer);
  // Spark-only notifications neither replace main data nor make its cache
  // appear freshly synchronized. The scheduled complete read still runs.
  if (snapshot.sparseUpdate && !HasCodexQuotaUpdate(snapshot)) return;
  const bool fullRead = !snapshot.sparseUpdate;
  const bool firstSnapshot = !snapshot_;
  if (snapshot.sparseUpdate && snapshot_) {
    snapshot = MergeSparseRateLimitSnapshot(*snapshot_, snapshot);
  }
  snapshot_ = std::move(snapshot);
  const RateWindow* limitingWindow = LimitingQuotaWindow();
  displayStatus_ = limitingWindow ? DataStatus::Live : DataStatus::DataUnavailable;
  snapshot_->status = displayStatus_;
  if (limitingWindow) {
    const double current = limitingWindow->remainingPercent;
    NotifyThresholds(previousRemaining_, current);
    previousRemaining_ = current;
  } else {
    // Missing quota data is not zero and must not send exhaustion alerts.
    previousRemaining_ = 101.0;
  }
  energyState_ = ComputeEnergyState(limitingWindow
      ? std::optional<double>(limitingWindow->remainingPercent) : std::nullopt,
      settings_.energy);
  blinkOn_ = true;
  ConfigureBlinkTimer();
  // A busy notification stream must not postpone full reads indefinitely.
  if (!demoMode_ && (fullRead || firstSnapshot)) ScheduleNextRefresh(true);
  UpdateTrayTooltip();
  UpdateTooltips();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::ApplyServerError(AppServerError error) {
  if (!appServer_.RequestInFlight()) KillTimer(hwnd_, kRequestTimeoutTimer);
  switch (error.kind) {
    case AppServerErrorKind::CliMissing:
    case AppServerErrorKind::LaunchFailed: displayStatus_ = DataStatus::CliMissing; break;
    case AppServerErrorKind::NotLoggedIn:
    case AppServerErrorKind::Unauthorized: displayStatus_ = DataStatus::NotLoggedIn; break;
    case AppServerErrorKind::Network: displayStatus_ = DataStatus::NetworkUnavailable; break;
    case AppServerErrorKind::Closed:
    case AppServerErrorKind::Timeout: displayStatus_ = DataStatus::Offline; break;
    default: displayStatus_ = snapshot_ ? DataStatus::Stale : DataStatus::DataUnavailable; break;
  }
  if (snapshot_) {
    snapshot_->errorMessage = WideToUtf8(error.message);
    snapshot_->status = displayStatus_;
  }
  energyState_ = ComputeEnergyState(std::nullopt, settings_.energy);
  ConfigureBlinkTimer();
  ScheduleNextRefresh(false);
  UpdateTrayTooltip();
  UpdateTooltips();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::ConfigureBlinkTimer() {
  KillTimer(hwnd_, kBlinkTimer);
  blinkOn_ = true;
  if (!hidden_ && energyState_.fullCycleMs > 0) {
    SetTimer(hwnd_, kBlinkTimer, static_cast<UINT>(std::max(100, energyState_.fullCycleMs / 2)), nullptr);
  }
}

void OverlayWindow::ScheduleNextRefresh(bool success) {
  if (demoMode_) return;
  KillTimer(hwnd_, kRefreshTimer);
  if (success) consecutiveFailures_ = 0;
  else consecutiveFailures_ = std::min(consecutiveFailures_ + 1, 5);
  int seconds = settings_.refreshSeconds;
  if (!success) seconds = std::min(600, 60 * (1 << std::max(0, consecutiveFailures_ - 1)));
  SetTimer(hwnd_, kRefreshTimer, static_cast<UINT>(seconds * 1000), nullptr);
}

void OverlayWindow::AuditChatGptWindows() {
  if (lifecycle_.HasAnyWindow()) {
    KillTimer(hwnd_, kLifecycleDebounceTimer);
  } else {
    SetTimer(hwnd_, kLifecycleDebounceTimer, 5000, nullptr);
  }
}

void OverlayWindow::FollowChatGptWindow() {
  if (!settings_.followChatGpt) return;
  HWND target = lifecycle_.BestWindow();
  if (!target) return;
  RECT chat{}, hud{};
  GetWindowRect(target, &chat);
  GetWindowRect(hwnd_, &hud);
  HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  const int width = hud.right - hud.left;
  const int height = hud.bottom - hud.top;
  int x = chat.right + 15;
  int y = chat.top;
  if (x + width > info.rcWork.right) x = chat.left - 15 - width;
  if (x < info.rcWork.left) x = chat.right - width - 15;
  if (x < info.rcWork.left || x + width > info.rcWork.right) x = info.rcWork.right - width - 30;
  y = std::clamp(y, static_cast<int>(info.rcWork.top),
                 static_cast<int>(info.rcWork.bottom) - height);
  SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void OverlayWindow::ResizeForMode() {
  const UINT dpi = hwnd_ ? GetDpiForWindow(hwnd_) : GetDpiForSystem();
  const float uiScale = HudScale(settings_.scalePercent);
  const ProgressDisplayMode layoutProgressMode =
      settings_.themeEnabled && settings_.displayMode == IndicatorDisplayMode::EnergyOnly
          ? ProgressDisplayMode::Bar
          : settings_.progressDisplayMode;
  const int logicalWidth = static_cast<int>(std::lround(
      LogicalWidth(settings_.sizeMode, layoutProgressMode) * uiScale));
  const int logicalHeight = static_cast<int>(std::lround(LogicalHeight(settings_.sizeMode) * uiScale));
  const int width = MulDiv(logicalWidth, static_cast<int>(dpi), 96);
  const int height = MulDiv(logicalHeight, static_cast<int>(dpi), 96);
  SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  DiscardGraphicsResources();
  if (tooltip_) UpdateTooltips();
}

void OverlayWindow::PositionInitially() {
  RECT rect{};
  GetWindowRect(hwnd_, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (settings_.x != INT_MIN && settings_.y != INT_MIN) {
    rect = {settings_.x, settings_.y, settings_.x + width, settings_.y + height};
    ClampToWorkArea(&rect);
  } else {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
    rect.left = info.rcWork.right - width - 30;
    rect.top = info.rcWork.top + 80;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
  }
  SetWindowPos(hwnd_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
               rect.left, rect.top, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayWindow::ClampToWorkArea(RECT* rect) const {
  HMONITOR monitor = MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  const int width = rect->right - rect->left;
  const int height = rect->bottom - rect->top;
  rect->left = std::clamp(rect->left, info.rcWork.left, info.rcWork.right - width);
  rect->top = std::clamp(rect->top, info.rcWork.top, info.rcWork.bottom - height);
  rect->right = rect->left + width;
  rect->bottom = rect->top + height;
}

void OverlayWindow::SaveWindowPosition() {
  if (!hwnd_ || demoMode_) return;
  RECT rect{};
  if (GetWindowRect(hwnd_, &rect)) {
    settings_.x = rect.left;
    settings_.y = rect.top;
    SaveSettings(settings_);
  }
}

void OverlayWindow::SetHidden(bool hidden) {
  hidden_ = hidden;
  hoveredControl_ = HudControl::None;
  ConfigureBlinkTimer();
  if (hidden_) {
    KillTimer(hwnd_, kDataAgeTimer);
    if (tooltip_) SendMessageW(tooltip_, TTM_POP, 0, 0);
    ShowWindow(hwnd_, demoMode_ ? SW_MINIMIZE : SW_HIDE);
    return;
  }
  if (demoMode_) ShowWindow(hwnd_, SW_RESTORE);
  RECT rect{};
  GetWindowRect(hwnd_, &rect);
  ClampToWorkArea(&rect);
  if (!demoMode_) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
               rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (!demoMode_) SetTimer(hwnd_, kDataAgeTimer, 30000, nullptr);
  RefreshDataAge();
}

void OverlayWindow::RefreshDataAge() {
  if (!demoMode_ && snapshot_ && displayStatus_ == DataStatus::Live &&
      IsSnapshotStale(*snapshot_, std::chrono::system_clock::now(),
                      std::chrono::seconds(std::max(120, settings_.refreshSeconds * 2)))) {
    displayStatus_ = DataStatus::Stale;
    energyState_ = ComputeEnergyState(std::nullopt, settings_.energy);
    ConfigureBlinkTimer();
  }
  UpdateTrayTooltip();
  UpdateTooltips();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::UpdateTooltips() {
  if (!hwnd_) return;
  if (!tooltip_) {
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&init);
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, instance_, nullptr);
    if (!tooltip_) return;
    SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 340);
    SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, 550);
    for (UINT_PTR id = 1; id <= 3; ++id) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = hwnd_;
      tool.uId = id;
      tool.uFlags = TTF_SUBCLASS;
      tool.lpszText = const_cast<wchar_t*>(L"");
      SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
  }

  quotaTooltip_ = demoMode_ ? L"精修预览 · 以下均为示例数据\n" : L"Codex 额度\n";
  const CodexQuotaWindows windows = QuotaWindows();
  auto appendWindow = [&](const wchar_t* label, const RateWindow* quota) {
    quotaTooltip_ += label;
    if (!quota) { quotaTooltip_ += L"：暂无数据\n"; return; }
    wchar_t percent[32]{};
    swprintf_s(percent, L"：%.0f%% 剩余", quota->remainingPercent);
    quotaTooltip_ += percent;
    if (quota->resetsAt) quotaTooltip_ += L"  |  重置 " + FormatLocalResetTime(*quota->resetsAt);
    quotaTooltip_ += L"\n";
  };
  appendWindow(ShowsFiveHourQuota() ? L"周额度 · 深色外圈 / 上条" : L"周额度", windows.weekly);
  if (ShowsFiveHourQuota()) {
    appendWindow(L"5小时 · 亮色内圈 / 下条", windows.fiveHour);
  } else if (windows.weekly && !windows.fiveHour && displayStatus_ == DataStatus::Live) {
    quotaTooltip_ += L"主额度当前仅返回周周期，已自动使用单圈 / 单条。\n";
  } else if (windows.fiveHour && settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour) {
    appendWindow(L"5小时额度", windows.fiveHour);
  }
  if (displayStatus_ != DataStatus::Live) quotaTooltip_ += L"暂不可用；如有数值，为上次成功读取的记录。\n";
  if (snapshot_ && !demoMode_) {
    quotaTooltip_ += L"最近同步：" + FormatLocalResetTime(snapshot_->lastSuccessAt) + L"\n";
    if (!snapshot_->planType.empty()) quotaTooltip_ += L"套餐：" + Utf8ToWide(snapshot_->planType) + L"\n";
  }
  quotaTooltip_ += L"双击收起 · 右键设置 · 拖动调整位置";
  RECT client{};
  GetClientRect(hwnd_, &client);
  const float scale = GetDpiForWindow(hwnd_) / 96.0f * HudScale(settings_.scalePercent);
  const auto layout = CalculateHudControlLayout(client.right / scale, client.bottom / scale);
  for (UINT_PTR id = 1; id <= 3; ++id) {
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.hwnd = hwnd_;
    tool.uId = id;
    if (id == 1) {
      tool.rect = {0, 0, client.right, static_cast<LONG>(layout.settings.top * scale)};
      tool.lpszText = quotaTooltip_.data();
    } else {
      const auto& rect = id == 2 ? layout.settings : layout.minimize;
      tool.rect = {static_cast<LONG>(rect.left * scale), static_cast<LONG>(rect.top * scale),
                   static_cast<LONG>(rect.right * scale), static_cast<LONG>(rect.bottom * scale)};
      tool.lpszText = const_cast<wchar_t*>(id == 2 ? L"设置 · 额度形式、尺寸与主题"
          : demoMode_ ? L"最小化预览 · 从任务栏恢复"
                      : L"最小化到托盘 · 点击托盘图标恢复");
    }
    SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

CodexQuotaWindows OverlayWindow::QuotaWindows() const {
  return snapshot_ ? SelectCodexQuotaWindows(*snapshot_) : CodexQuotaWindows{};
}

const RateWindow* OverlayWindow::PrimaryQuotaWindow() const {
  if (!snapshot_ || snapshot_->windows.empty()) return nullptr;
  const CodexQuotaWindows windows = QuotaWindows();
  if (windows.weekly) return windows.weekly;
  if (settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyOnly) return nullptr;
  if (windows.fiveHour) return windows.fiveHour;
  return windows.other;
}

bool OverlayWindow::ShowsFiveHourQuota() const {
  if (settings_.quotaDisplayMode != QuotaDisplayMode::WeeklyAndFiveHour) return false;
  const CodexQuotaWindows windows = QuotaWindows();
  return windows.weekly && windows.fiveHour && windows.weekly != windows.fiveHour;
}

const RateWindow* OverlayWindow::LimitingQuotaWindow() const {
  const RateWindow* primary = PrimaryQuotaWindow();
  if (!primary || !ShowsFiveHourQuota()) return primary;
  const CodexQuotaWindows windows = QuotaWindows();
  return windows.fiveHour->remainingPercent < windows.weekly->remainingPercent
      ? windows.fiveHour
      : windows.weekly;
}

std::wstring OverlayWindow::CurrentStatusLine() const {
  if (demoMode_) return L"预览 · 示例数据";
  std::wstring state;
  if (displayStatus_ != DataStatus::Live) {
    switch (displayStatus_) {
      case DataStatus::Connecting: state = L"连接中"; break;
      case DataStatus::Stale: state = L"数据过期"; break;
      case DataStatus::Offline: state = L"离线"; break;
      case DataStatus::CliMissing: state = L"缺少 CLI"; break;
      case DataStatus::NotLoggedIn: state = L"未登录"; break;
      case DataStatus::NetworkUnavailable: state = L"网络异常"; break;
      case DataStatus::DataUnavailable: state = L"暂不可用"; break;
      default: state = L"待同步"; break;
    }
  } else {
    const RateWindow* quota = LimitingQuotaWindow();
    if (!quota) return L"暂无额度数据";
    // The label follows the same state machine as the chest lens, including
    // the user's custom threshold and the inclusive 10% critical boundary.
    switch (energyState_.visual) {
      case EnergyVisualState::NormalBlue: state = L"正常"; break;
      case EnergyVisualState::WarningRedBlink: state = L"警戒"; break;
      case EnergyVisualState::CriticalRedFastBlink: state = L"危急"; break;
      case EnergyVisualState::Stone: state = L"石化"; break;
      default: state = quota->remainingPercent <= 0.0 ? L"额度耗尽" : L"待同步"; break;
    }
  }
  if (appServer_.RequestInFlight()) state += L" · 刷新中";
  else if (snapshot_) {
    state += L" · " + FormatDataAge(snapshot_->lastSuccessAt, std::chrono::system_clock::now());
  }
  return state;
}

void OverlayWindow::NotifyThresholds(double previous, double current) {
  if (demoMode_) return;
  if (previous > 100.0) return;
  if (current > previous + 20.0) notifiedThresholds_.clear();
  for (const int threshold : settings_.notificationThresholds) {
    if (previous > threshold && current <= threshold &&
        std::find(notifiedThresholds_.begin(), notifiedThresholds_.end(), threshold) == notifiedThresholds_.end()) {
      notifiedThresholds_.push_back(threshold);
      NOTIFYICONDATAW data{};
      data.cbSize = sizeof(data);
      data.hWnd = hwnd_;
      data.uID = 1;
      data.uFlags = NIF_INFO;
      wcscpy_s(data.szInfoTitle, L"Codex 额度提醒");
      if (threshold == 0) wcscpy_s(data.szInfo, L"额度已耗尽，能量指示器已进入石化状态。");
      else swprintf_s(data.szInfo, L"当前限制窗口剩余已降至 %.0f%%。", current);
      data.dwInfoFlags = NIIF_WARNING;
      Shell_NotifyIconW(NIM_MODIFY, &data);
    }
  }
}

void OverlayWindow::OpenUsagePage() {
  ShellExecuteW(hwnd_, L"open", L"https://chatgpt.com/codex/settings/usage", nullptr, nullptr, SW_SHOWNORMAL);
}

void OverlayWindow::OpenChatGpt() {
  HWND window = lifecycle_.BestWindow();
  if (!window) window = chatGpt_.mainWindow;
  if (window) ActivateChatGptWindow(window);
}

void OverlayWindow::OpenSettingsMenu() {
  POINT point{};
  GetCursorPos(&point);
  ShowTrayMenu(point);
}

void OverlayWindow::OnTrayCommand(UINT command) {
  switch (command) {
    case kTrayViewRing:
    case kTrayViewBar:
      settings_.progressDisplayMode = command == kTrayViewRing
          ? ProgressDisplayMode::Ring : ProgressDisplayMode::Bar;
      ResizeForMode();
      break;
    case kTrayQuotaBoth:
    case kTrayQuotaWeekly:
      settings_.quotaDisplayMode = command == kTrayQuotaBoth
          ? QuotaDisplayMode::WeeklyAndFiveHour : QuotaDisplayMode::WeeklyOnly;
      break;
    case kTrayDisplayBoth:
    case kTrayDisplayProgress:
    case kTrayDisplayEnergy:
      settings_.displayMode = command == kTrayDisplayBoth ? IndicatorDisplayMode::Both
          : command == kTrayDisplayProgress ? IndicatorDisplayMode::ProgressOnly
                                           : IndicatorDisplayMode::EnergyOnly;
      ResizeForMode();
      break;
    case kTrayLayoutCompact:
    case kTrayLayoutStandard:
    case kTrayLayoutExpanded:
      settings_.sizeMode = command == kTrayLayoutCompact ? HudSizeMode::Compact
          : command == kTrayLayoutStandard ? HudSizeMode::Standard : HudSizeMode::Expanded;
      ResizeForMode();
      break;
    case kTrayBlinkGentle:
    case kTrayBlinkStandard:
    case kTrayBlinkFast:
      settings_.energy.blinkStyle = command == kTrayBlinkGentle ? BlinkStyle::Gentle
          : command == kTrayBlinkStandard ? BlinkStyle::Standard : BlinkStyle::Aggressive;
      break;
    case kTrayPreviewNormal:
    case kTrayPreviewWarning:
    case kTrayPreviewStone:
      if (demoMode_ && snapshot_) {
        RateLimitSnapshot demo = *snapshot_;
        for (auto& window : demo.windows) {
          const bool weekly = ClassifyQuotaWindow(window) == QuotaWindowKind::Weekly;
          window.remainingPercent = command == kTrayPreviewNormal ? (weekly ? 68.0 : 86.0)
              : command == kTrayPreviewWarning ? (weekly ? 34.0 : 22.0) : 0.0;
          window.usedPercent = 100.0 - window.remainingPercent;
        }
        ApplySnapshot(std::move(demo));
      }
      break;
    case kTrayShowHide:
      SetHidden(!hidden_);
      break;
    case kTrayRefresh: RequestRefresh(true); break;
    case kTrayUsage: OpenUsagePage(); break;
    case kTrayOpenChatGpt: OpenChatGpt(); break;
    case kTrayDisplayMode:
      settings_.displayMode = static_cast<IndicatorDisplayMode>((static_cast<int>(settings_.displayMode) + 1) % 3);
      ResizeForMode();
      break;
    case kTrayProgressDisplayMode:
      settings_.progressDisplayMode = settings_.progressDisplayMode == ProgressDisplayMode::Bar
          ? ProgressDisplayMode::Ring
          : ProgressDisplayMode::Bar;
      ResizeForMode();
      break;
    case kTrayQuotaDisplayMode:
      settings_.quotaDisplayMode =
          settings_.quotaDisplayMode == QuotaDisplayMode::WeeklyAndFiveHour
              ? QuotaDisplayMode::WeeklyOnly
              : QuotaDisplayMode::WeeklyAndFiveHour;
      break;
    case kTrayFollow:
      settings_.followChatGpt = !settings_.followChatGpt;
      if (settings_.followChatGpt) FollowChatGptWindow();
      break;
    case kTrayTheme:
      settings_.themeEnabled = !settings_.themeEnabled;
      ResizeForMode();
      break;
    case kTrayGlow: settings_.energy.glow = !settings_.energy.glow; break;
    case kTrayStone: settings_.energy.stoneAtZero = !settings_.energy.stoneAtZero; break;
    case kTrayBlinkStyle:
      settings_.energy.blinkStyle = static_cast<BlinkStyle>((static_cast<int>(settings_.energy.blinkStyle) + 1) % 3);
      break;
    case kTrayThresholdDown:
      settings_.energy.warningThreshold = std::max(10, settings_.energy.warningThreshold - 5);
      break;
    case kTrayThresholdUp:
      settings_.energy.warningThreshold = std::min(80, settings_.energy.warningThreshold + 5);
      break;
    case kTraySizeMode:
      settings_.sizeMode = static_cast<HudSizeMode>((static_cast<int>(settings_.sizeMode) + 1) % 3);
      ResizeForMode();
      break;
    case kTrayScaleDown:
      settings_.scalePercent = std::max(kMinHudScalePercent, settings_.scalePercent - 5);
      ResizeForMode();
      break;
    case kTrayScaleUp:
      settings_.scalePercent = std::min(kMaxHudScalePercent, settings_.scalePercent + 5);
      ResizeForMode();
      break;
    case kTrayScaleReset:
      settings_.scalePercent = kDefaultHudScalePercent;
      ResizeForMode();
      break;
    case kTrayAbout:
      MessageBoxW(hwnd_,
          L"ChatGPT Codex Usage Monitor 1.0.5\n\n"
          L"按主额度自动切换单圈/双圈或单条/双条，也可固定只显示周额度。\n"
          L"周额度使用深色，5 小时额度使用浅色；Spark 独立额度不会混入。\n"
          L"额度来自官方 Codex App Server；仅显示接口返回的百分比，不伪造 token。\n"
          L"胸甲背景使用用户提供并确认有权使用的原图；指示灯与动效由 Direct2D 绘制。",
          L"关于", MB_OK | MB_ICONINFORMATION);
      return;
    case kTrayExit:
      DestroyWindow(hwnd_);
      return;
    default: return;
  }
  if (snapshot_ && (displayStatus_ == DataStatus::Live ||
                    displayStatus_ == DataStatus::DataUnavailable)) {
    const RateWindow* window = LimitingQuotaWindow();
    displayStatus_ = window ? DataStatus::Live : DataStatus::DataUnavailable;
    energyState_ = ComputeEnergyState(window
        ? std::optional<double>(window->remainingPercent) : std::nullopt, settings_.energy);
  }
  ConfigureBlinkTimer();
  if (!demoMode_) SaveSettings(settings_);
  UpdateTrayTooltip();
  UpdateTooltips();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace monitor
