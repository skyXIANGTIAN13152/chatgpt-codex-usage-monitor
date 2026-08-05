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

namespace monitor {
namespace {

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kBlinkTimer = 2;
constexpr UINT_PTR kRequestTimeoutTimer = 3;
constexpr UINT_PTR kLifecycleDebounceTimer = 4;
constexpr UINT_PTR kFollowCoalesceTimer = 5;

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

int LogicalWidth(HudSizeMode mode) {
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

std::wstring WindowLabel(const RateWindow& window) {
  if (!window.bucketName.empty() && window.bucketName != "codex") return Utf8ToWide(window.bucketName);
  if (window.windowDurationMins >= 6 * 24 * 60) return L"周额度";
  if (window.windowDurationMins >= 24 * 60) return L"多日额度";
  if (window.windowDurationMins >= 60) return L"小时额度";
  return L"主要额度";
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
  windowClass.lpszClassName = kWindowClass;
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

  DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_LAYERED;
  if (!demoMode_) exStyle |= WS_EX_TOOLWINDOW;
  if (settings_.alwaysOnTop) exStyle |= WS_EX_TOPMOST;
  hwnd_ = CreateWindowExW(exStyle, kWindowClass, kProductName, WS_POPUP,
                          0, 0, 350, 115, nullptr, nullptr, instance_, this);
  if (!hwnd_) return false;
  SetLayeredWindowAttributes(hwnd_, 0, 242, LWA_ALPHA);
  const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  ResizeForMode();
  PositionInitially();
  AddTrayIcon();
  NotifyIpInterfaceChange(AF_UNSPEC, InterfaceChanged, hwnd_, FALSE, &networkNotification_);

  if (demoMode_) {
    RateLimitSnapshot demo;
    demo.status = DataStatus::Live;
    demo.receivedAt = demo.lastSuccessAt = std::chrono::system_clock::now();
    demo.planType = "Demo";
    RateWindow primary;
    primary.bucketId = "codex";
    primary.bucketName = "codex";
    primary.windowName = "primary";
    double demoRemaining = 55.0;
    wchar_t demoValue[32]{};
    if (GetEnvironmentVariableW(L"MONITOR_DEMO_REMAINING", demoValue,
                                static_cast<DWORD>(std::size(demoValue))) > 0) {
      wchar_t* end = nullptr;
      const double parsed = wcstod(demoValue, &end);
      if (end && *end == L'\0' && std::isfinite(parsed)) {
        demoRemaining = std::clamp(parsed, 0.0, 100.0);
      }
    }
    primary.usedPercent = 100.0 - demoRemaining;
    primary.remainingPercent = demoRemaining;
    primary.windowDurationMins = 10080;
    primary.resetsAt = std::chrono::system_clock::now() + std::chrono::hours(158);
    demo.windows.push_back(primary);
    RateWindow secondary = primary;
    secondary.windowName = "secondary";
    secondary.usedPercent = 72;
    secondary.remainingPercent = 28;
    secondary.windowDurationMins = 300;
    demo.windows.push_back(secondary);
    demo.credits.present = true;
    demo.credits.balance = 12.5;
    ApplySnapshot(std::move(demo));
  } else {
    lifecycle_.Start(hwnd_, chatGpt_);
    AuditChatGptWindows();
    StartCodex();
  }
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
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
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
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
        hidden_ = true;
        ShowWindow(hwnd_, SW_HIDE);
      } else if (control == HudControl::Settings) {
        OpenSettingsMenu();
      } else {
        RequestRefresh(true);
      }
      return 0;
    }
    case WM_LBUTTONDBLCLK:
      hidden_ = true;
      ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_RBUTTONUP: {
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
      hidden_ = false;
      ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
      SetWindowPos(hwnd_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                   0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      return 0;
    case WM_MONITOR_TRAY:
      if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == NIN_SELECT ||
          LOWORD(lparam) == NIN_KEYSELECT) {
        hidden_ = HiddenAfterTrayPrimaryActivation(hidden_);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd_, settings_.alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(hwnd_, nullptr, FALSE);
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
    case WM_DESTROY:
      SaveWindowPosition();
      RemoveTrayIcon();
      if (networkNotification_) {
        CancelMibChangeNotify2(networkNotification_);
        networkNotification_ = nullptr;
      }
      lifecycle_.Stop();
      appServer_.Stop();
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

  if (fadeBrush && fadeLayer) {
    D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::RectF(
        0.5f, 0.5f, std::min(width - 0.5f, 120.0f), height - 0.5f);
    parameters.opacityBrush = fadeBrush;
    renderTarget_->PushLayer(parameters, fadeLayer);
  }

  ID2D1Bitmap* artwork = chestBitmap_;
  float artworkOpacity = 0.76f;
  if (energyState_.visual == EnergyVisualState::WarningRedBlink ||
      energyState_.visual == EnergyVisualState::CriticalRedFastBlink) {
    if (blinkOn_ && chestWarningOnBitmap_) {
      artwork = chestWarningOnBitmap_;
      artworkOpacity = 0.88f;
    } else if (chestWarningOffBitmap_) {
      artwork = chestWarningOffBitmap_;
      artworkOpacity = 0.78f;
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

  if (fadeBrush && fadeLayer) renderTarget_->PopLayer();
  Release(fadeLayer);
  Release(fadeBrush);
  Release(fadeCollection);
}

void OverlayWindow::DrawTributeLightBackground(float width, float height) {
  if (!renderTarget_ || !d2dFactory_) return;

  const bool stone = energyState_.visual == EnergyVisualState::Stone;
  const bool warning = energyState_.visual == EnergyVisualState::WarningRedBlink ||
                       energyState_.visual == EnergyVisualState::CriticalRedFastBlink;
  const float lightStrength = stone ? 0.24f : (warning ? 0.88f : 1.0f);
  const float scaleX = width / 300.0f;
  const float scaleY = height / 90.0f;
  const D2D1_POINT_2F focus = D2D1::Point2F(46.0f * scaleX, 35.0f * scaleY);

  // A restrained dawn glow: darkness at the top gives way to warm human light
  // rising from below, echoing the finale without using another bitmap.
  D2D1_GRADIENT_STOP dawnStops[] = {
      {0.00f, D2D1::ColorF(1.00f, 0.82f, 0.34f, 0.25f * lightStrength)},
      {0.28f, D2D1::ColorF(0.55f, 0.38f, 0.92f, 0.115f * lightStrength)},
      {0.66f, D2D1::ColorF(0.08f, 0.42f, 0.88f, 0.040f * lightStrength)},
      {1.00f, D2D1::ColorF(0.02f, 0.03f, 0.08f, 0.00f)},
  };
  ID2D1GradientStopCollection* dawnCollection = nullptr;
  ID2D1RadialGradientBrush* dawn = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          dawnStops, static_cast<UINT32>(std::size(dawnStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &dawnCollection))) {
    renderTarget_->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(width * 0.70f, height + 3.0f),
            D2D1::Point2F(0.0f, 0.0f), width * 0.64f, height * 1.10f),
        dawnCollection, &dawn);
  }
  if (dawn) {
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f,
                                     height - 0.5f), 10.0f, 10.0f), dawn);
  }

  D2D1_GRADIENT_STOP hopeStops[] = {
      {0.00f, warning
          ? D2D1::ColorF(0.92f, 0.24f, 0.50f, 0.12f * lightStrength)
          : D2D1::ColorF(0.22f, 0.68f, 1.00f, 0.12f * lightStrength)},
      {0.42f, D2D1::ColorF(0.46f, 0.24f, 0.86f, 0.055f * lightStrength)},
      {1.00f, D2D1::ColorF(0.04f, 0.03f, 0.10f, 0.00f)},
  };
  ID2D1GradientStopCollection* hopeCollection = nullptr;
  ID2D1RadialGradientBrush* hope = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          hopeStops, static_cast<UINT32>(std::size(hopeStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &hopeCollection))) {
    renderTarget_->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(width * 0.82f, height * 0.08f),
            D2D1::Point2F(0.0f, 0.0f), width * 0.44f, height * 0.72f),
        hopeCollection, &hope);
  }
  if (hope) {
    renderTarget_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f,
                                     height - 0.5f), 10.0f, 10.0f), hope);
  }

  ID2D1SolidColorBrush* humanLight = nullptr;
  ID2D1SolidColorBrush* coolLight = nullptr;
  renderTarget_->CreateSolidColorBrush(
      D2D1::ColorF(1.00f, 0.84f, 0.39f, 1.0f), &humanLight);
  renderTarget_->CreateSolidColorBrush(
      D2D1::ColorF(warning ? 1.00f : 0.33f,
                   warning ? 0.38f : 0.86f,
                   warning ? 0.45f : 1.00f, 1.0f), &coolLight);

  struct RayEnd { float x; float y; float alpha; float stroke; bool warm; };
  constexpr RayEnd rays[] = {
      {111.0f, -3.0f, 0.19f, 0.72f, true},
      {161.0f,  1.0f, 0.11f, 0.56f, false},
      {207.0f, 92.0f, 0.14f, 0.70f, true},
      {259.0f, 93.0f, 0.09f, 0.58f, false},
      {304.0f, 52.0f, 0.08f, 0.52f, true},
  };
  for (const RayEnd& ray : rays) {
    ID2D1SolidColorBrush* brush = ray.warm ? humanLight : coolLight;
    if (!brush) continue;
    brush->SetOpacity(ray.alpha * lightStrength);
    renderTarget_->DrawLine(
        focus, D2D1::Point2F(ray.x * scaleX, ray.y * scaleY), brush,
        ray.stroke * std::max(0.85f, std::min(scaleX, scaleY)));
  }

  // Individual points represent many small lights. Their placement is static,
  // so the panel remains calm and consumes no animation timer.
  struct LightPoint { float x; float y; float radius; float alpha; bool warm; };
  constexpr LightPoint points[] = {
      {98.0f,  8.0f, 0.80f, 0.52f, true},
      {116.0f, 77.0f, 1.10f, 0.70f, true},
      {139.0f, 11.0f, 0.68f, 0.42f, false},
      {161.0f, 82.0f, 0.82f, 0.54f, true},
      {184.0f,  6.0f, 1.05f, 0.68f, true},
      {207.0f, 74.0f, 0.70f, 0.45f, false},
      {215.0f,  7.0f, 1.12f, 0.78f, true},
      {253.0f, 79.0f, 0.86f, 0.58f, true},
      {276.0f, 47.0f, 0.62f, 0.43f, false},
      {291.0f, 15.0f, 0.92f, 0.65f, true},
  };
  for (const LightPoint& point : points) {
    ID2D1SolidColorBrush* brush = point.warm ? humanLight : coolLight;
    if (!brush) continue;
    const float radius = point.radius * std::max(0.9f, std::min(scaleX, scaleY));
    if (point.radius >= 0.9f) {
      brush->SetOpacity(point.alpha * 0.16f * lightStrength);
      renderTarget_->FillEllipse(
          D2D1::Ellipse(D2D1::Point2F(point.x * scaleX, point.y * scaleY),
                        radius * 3.1f, radius * 3.1f), brush);
    }
    brush->SetOpacity(point.alpha * lightStrength);
    renderTarget_->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(point.x * scaleX, point.y * scaleY),
                      radius, radius), brush);
  }

  // Two original light trails follow the lower edge: gold for gathered human
  // light and blue-silver for the giant's returning energy.
  ID2D1PathGeometry* goldTrail = nullptr;
  ID2D1GeometrySink* goldSink = nullptr;
  if (humanLight && SUCCEEDED(d2dFactory_->CreatePathGeometry(&goldTrail)) &&
      SUCCEEDED(goldTrail->Open(&goldSink))) {
    goldSink->BeginFigure(D2D1::Point2F(68.0f * scaleX, height + 1.0f),
                          D2D1_FIGURE_BEGIN_HOLLOW);
    goldSink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(128.0f * scaleX, height - 1.0f),
        D2D1::Point2F(164.0f * scaleX, height - 18.0f * scaleY),
        D2D1::Point2F(width + 5.0f, height - 15.0f * scaleY)));
    goldSink->EndFigure(D2D1_FIGURE_END_OPEN);
    goldSink->Close();
    humanLight->SetOpacity(0.24f * lightStrength);
    renderTarget_->DrawGeometry(goldTrail, humanLight,
                                1.05f * std::max(0.9f, std::min(scaleX, scaleY)));
  }
  Release(goldSink);
  Release(goldTrail);

  ID2D1PathGeometry* blueTrail = nullptr;
  ID2D1GeometrySink* blueSink = nullptr;
  if (coolLight && SUCCEEDED(d2dFactory_->CreatePathGeometry(&blueTrail)) &&
      SUCCEEDED(blueTrail->Open(&blueSink))) {
    blueSink->BeginFigure(D2D1::Point2F(81.0f * scaleX, height + 2.0f),
                          D2D1_FIGURE_BEGIN_HOLLOW);
    blueSink->AddBezier(D2D1::BezierSegment(
        D2D1::Point2F(146.0f * scaleX, height - 2.0f),
        D2D1::Point2F(194.0f * scaleX, height - 13.0f * scaleY),
        D2D1::Point2F(width + 6.0f, height - 9.0f * scaleY)));
    blueSink->EndFigure(D2D1_FIGURE_END_OPEN);
    blueSink->Close();
    coolLight->SetOpacity(0.17f * lightStrength);
    renderTarget_->DrawGeometry(blueTrail, coolLight,
                                0.82f * std::max(0.9f, std::min(scaleX, scaleY)));
  }
  Release(blueSink);
  Release(blueTrail);

  Release(coolLight);
  Release(humanLight);
  Release(hope);
  Release(hopeCollection);
  Release(dawn);
  Release(dawnCollection);
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
          : D2D1::ColorF(0.105f, 0.042f, 0.195f, 1.0f)},
      {1.0f, stonePanel
          ? D2D1::ColorF(0.080f, 0.070f, 0.075f, 1.0f)
          : warningPanel
              ? D2D1::ColorF(0.235f, 0.024f, 0.064f, 1.0f)
              : D2D1::ColorF(0.185f, 0.065f, 0.135f, 1.0f)},
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
    DrawThemedPanelImage(size.width, size.height);
  }

  D2D1_GRADIENT_STOP borderStops[] = {
      {0.0f, stonePanel
          ? D2D1::ColorF(0.34f, 0.37f, 0.41f, 0.75f)
          : D2D1::ColorF(0.12f, 0.68f, 0.95f, 0.85f)},
      {0.50f, stonePanel
          ? D2D1::ColorF(0.42f, 0.42f, 0.44f, 0.62f)
          : D2D1::ColorF(0.48f, 0.28f, 0.82f, 0.72f)},
      {1.0f, stonePanel
          ? D2D1::ColorF(0.43f, 0.40f, 0.37f, 0.70f)
          : D2D1::ColorF(1.00f, 0.70f, 0.25f, 0.88f)},
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
                                      settings_.themeEnabled ? 1.15f : 1.0f);

  const bool showEnergy = settings_.themeEnabled &&
                          settings_.displayMode != IndicatorDisplayMode::ProgressOnly;
  const bool showProgress = settings_.displayMode != IndicatorDisplayMode::EnergyOnly;
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
  const float contentLeft = showEnergy ? 88.0f : 16.0f;
  const float rightEdge = size.width - 12.0f;
  const RateWindow* window = SelectedWindow();
  const bool dataAvailable = window && displayStatus_ == DataStatus::Live;
  const float remaining = window ? static_cast<float>(window->remainingPercent) : 0.0f;

  auto drawText = [&](std::wstring_view value, IDWriteTextFormat* format,
                      const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    if (format && brush) renderTarget_->DrawTextW(value.data(), static_cast<UINT32>(value.size()),
                                                  format, rect, brush,
                                                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
  };

  const float ionPulse = warningState && !blinkOn_ ? 0.42f : 1.0f;

  auto drawIonField = [&](const D2D1_RECT_F& rect, UINT32 seed,
                          int count, float strength) {
    if (stonePanel || !ionCore || count <= 0) return;
    ID2D1SolidColorBrush* charge = warningState ? warningIonParticle : ionParticle;
    ID2D1SolidColorBrush* orbit = warningState ? warningIonGlow : ionViolet;
    if (!charge || !orbit) return;
    strength *= ionPulse;
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

    const float energizedStrength = strength * ionPulse;
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

  const float percentWidth = 64.0f;
  const float barRight = rightEdge - percentWidth - 6.0f;
  if (showProgress) {
    const D2D1_RECT_F barRect = D2D1::RectF(contentLeft, 18.0f, barRight, 35.0f);
    const D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(barRect, 6.0f, 6.0f);
    ID2D1SolidColorBrush* trackEdge = nullptr;
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.28f, 0.44f, 0.8f),
                                          &trackEdge);
    renderTarget_->FillRoundedRectangle(bar, track);

    if (dataAvailable && remaining > 0.0f) {
      const float fillRight = contentLeft + (barRight - contentLeft) * remaining / 100.0f;
      const float fillWidth = std::max(1.0f, fillRight - contentLeft);
      const float radius = std::min(6.0f, fillWidth * 0.5f);
      const bool warning = remaining <= settings_.energy.warningThreshold;
      const float pulse = warning && !blinkOn_ ? 0.34f : 1.0f;
      const D2D1_COLOR_F deep = warning
          ? D2D1::ColorF(0.52f, 0.01f, 0.08f, pulse)
          : D2D1::ColorF(0.00f, 0.34f, 0.76f, pulse);
      const D2D1_COLOR_F bright = warning
          ? D2D1::ColorF(1.00f, 0.08f, 0.18f, pulse)
          : D2D1::ColorF(0.00f, 0.88f, 1.00f, pulse);
      const D2D1_COLOR_F core = warning
          ? D2D1::ColorF(1.00f, 0.72f, 0.76f, pulse)
          : D2D1::ColorF(0.72f, 1.00f, 1.00f, pulse);

      ID2D1SolidColorBrush* bloom = nullptr;
      ID2D1SolidColorBrush* streak = nullptr;
      ID2D1SolidColorBrush* head = nullptr;
      const float bloomAlpha = (settings_.energy.glow ? 0.18f : 0.07f) * pulse;
      renderTarget_->CreateSolidColorBrush(
          warning ? D2D1::ColorF(1.0f, 0.04f, 0.15f, bloomAlpha)
                  : D2D1::ColorF(0.0f, 0.78f, 1.0f, bloomAlpha), &bloom);
      renderTarget_->CreateSolidColorBrush(
          warning ? D2D1::ColorF(1.0f, 0.72f, 0.76f, 0.38f * pulse)
                  : D2D1::ColorF(0.72f, 1.0f, 1.0f, 0.38f * pulse), &streak);
      renderTarget_->CreateSolidColorBrush(core, &head);

      if (bloom) {
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(contentLeft - 2.0f, 15.5f, fillRight + 4.0f, 37.5f),
            radius + 3.0f, radius + 3.0f), bloom);
      }

      D2D1_GRADIENT_STOP stops[] = {
          {0.00f, deep},
          {0.48f, bright},
          {0.86f, bright},
          {1.00f, core},
      };
      ID2D1GradientStopCollection* stopCollection = nullptr;
      ID2D1LinearGradientBrush* energy = nullptr;
      if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
              stops, static_cast<UINT32>(std::size(stops)),
              D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stopCollection))) {
        renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(contentLeft, 26.5f), D2D1::Point2F(fillRight, 26.5f)),
            stopCollection, &energy);
      }
      if (energy) {
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(contentLeft, 18.0f, fillRight, 35.0f), radius, radius), energy);
      }

      renderTarget_->PushAxisAlignedClip(
          D2D1::RectF(contentLeft, 18.0f, fillRight, 35.0f),
          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      if (streak && fillWidth > 32.0f) {
        for (int i = 1; i <= 3; ++i) {
          const float x = contentLeft + fillWidth * static_cast<float>(i) / 4.0f;
          renderTarget_->DrawLine(D2D1::Point2F(x - 5.0f, 34.0f),
                                  D2D1::Point2F(x + 5.0f, 19.0f), streak, 1.0f);
        }
      }
      renderTarget_->PopAxisAlignedClip();

      if (head && fillWidth > 4.0f) {
        renderTarget_->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillRight - 1.5f, 26.5f), 2.2f, 7.0f), head);
      }
      ID2D1SolidColorBrush* highlight = nullptr;
      renderTarget_->CreateSolidColorBrush(
          D2D1::ColorF(0.88f, 1.0f, 1.0f, 0.48f * pulse), &highlight);
      if (highlight) {
        renderTarget_->DrawLine(D2D1::Point2F(contentLeft + 5.0f, 20.0f),
                                D2D1::Point2F(std::max(contentLeft + 5.0f, fillRight - 4.0f),
                                             20.0f), highlight, 1.0f);
      }

      Release(highlight);
      Release(energy);
      Release(stopCollection);
      Release(head);
      Release(streak);
      Release(bloom);
    }
    if (trackEdge) renderTarget_->DrawRoundedRectangle(bar, trackEdge, 1.0f);
    Release(trackEdge);
  }

  if (textLarge_) textLarge_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
  std::wstring percent = L"--";
  if (window) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%.0f%%", window->remainingPercent);
    percent = buffer;
  }
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
  if (textLarge_) textLarge_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

  const float row2 = settings_.sizeMode == HudSizeMode::Compact ? 45.0f : 49.0f;
  std::wstring countdown = L"RESET  --";
  std::wstring absolute = L"--";
  if (window && window->resetsAt) {
    countdown = L"RESET  " + FormatResetCountdown(*window->resetsAt,
                                                    std::chrono::system_clock::now());
    absolute = FormatLocalResetTime(*window->resetsAt);
  }
  const D2D1_RECT_F countdownRect =
      D2D1::RectF(contentLeft, row2, barRight, row2 + 18);
  drawIonText(countdown, textSmall_, countdownRect, 0.64f,
              0xc013u + static_cast<UINT32>(countdown.size()), 2);
  if (textSmall_) textSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
  ID2D1SolidColorBrush* secondaryText = stonePanel
      ? dormantIonText
      : (warningState ? warningIonMuted : ionMuted);
  if (secondaryText) secondaryText->SetOpacity(stonePanel ? 0.78f : 0.94f);
  drawText(absolute, textSmall_, D2D1::RectF(barRight - 105, row2, rightEdge, row2 + 18),
           secondaryText ? static_cast<ID2D1Brush*>(secondaryText)
                         : static_cast<ID2D1Brush*>(muted));
  if (textSmall_) textSmall_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

  const float row3 = size.height - (settings_.sizeMode == HudSizeMode::Expanded ? 47.0f : 26.0f);
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
    statusHalo->SetOpacity(0.12f * ionPulse);
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
    statusHalo->SetOpacity(0.070f * ionPulse);
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
  drawIonText(L"⚙", textMedium_,
              D2D1::RectF(controls.settings.left + 2.0f, size.height - 31.0f,
                          controls.settings.right - 1.0f, size.height - 5.0f),
              0.54f, 0x6e21u, 0);
  drawIonText(L"—", textMedium_,
              D2D1::RectF(controls.minimize.left + 2.0f, size.height - 31.0f,
                          rightEdge, size.height - 5.0f),
              0.54f, 0x9d42u, 0);

  if (settings_.sizeMode == HudSizeMode::Expanded && snapshot_ && snapshot_->windows.size() > 1) {
    const RateWindow& secondary = snapshot_->windows[1];
    wchar_t detail[160]{};
    swprintf_s(detail, L"次要  %.0f%%  ·  %s", secondary.remainingPercent,
               WindowLabel(secondary).c_str());
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
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::RequestRefresh(bool) {
  if (!appServer_.IsRunning()) {
    StartCodex();
    return;
  }
  if (appServer_.RequestRateLimits()) {
    SetTimer(hwnd_, kRequestTimeoutTimer, 15000, nullptr);
  }
}

void OverlayWindow::ApplySnapshot(RateLimitSnapshot snapshot) {
  KillTimer(hwnd_, kRequestTimeoutTimer);
  displayStatus_ = DataStatus::Live;
  const double current = snapshot.windows.empty() ? 0.0 : snapshot.windows.front().remainingPercent;
  NotifyThresholds(previousRemaining_, current);
  previousRemaining_ = current;
  snapshot_ = std::move(snapshot);
  energyState_ = ComputeEnergyState(SelectedWindow() ?
      std::optional<double>(SelectedWindow()->remainingPercent) : std::nullopt, settings_.energy);
  blinkOn_ = true;
  ConfigureBlinkTimer();
  ScheduleNextRefresh(true);
  UpdateTrayTooltip();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::ApplyServerError(AppServerError error) {
  KillTimer(hwnd_, kRequestTimeoutTimer);
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
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::ConfigureBlinkTimer() {
  KillTimer(hwnd_, kBlinkTimer);
  blinkOn_ = true;
  if (energyState_.fullCycleMs > 0) {
    SetTimer(hwnd_, kBlinkTimer, static_cast<UINT>(std::max(100, energyState_.fullCycleMs / 2)), nullptr);
  }
}

void OverlayWindow::ScheduleNextRefresh(bool success) {
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
  const int logicalWidth = static_cast<int>(std::lround(LogicalWidth(settings_.sizeMode) * uiScale));
  const int logicalHeight = static_cast<int>(std::lround(LogicalHeight(settings_.sizeMode) * uiScale));
  const int width = MulDiv(logicalWidth, static_cast<int>(dpi), 96);
  const int height = MulDiv(logicalHeight, static_cast<int>(dpi), 96);
  SetWindowPos(hwnd_, nullptr, 0, 0, width, height,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  DiscardGraphicsResources();
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
  if (!hwnd_) return;
  RECT rect{};
  if (GetWindowRect(hwnd_, &rect)) {
    settings_.x = rect.left;
    settings_.y = rect.top;
    SaveSettings(settings_);
  }
}

const RateWindow* OverlayWindow::SelectedWindow() const {
  if (!snapshot_ || snapshot_->windows.empty()) return nullptr;
  const size_t index = std::min(static_cast<size_t>(std::max(0, settings_.selectedWindow)),
                                snapshot_->windows.size() - 1);
  return &snapshot_->windows[index];
}

std::wstring OverlayWindow::CurrentStatusLine() const {
  if (displayStatus_ != DataStatus::Live) {
    std::wstring value = DataStatusText(displayStatus_);
    if (settings_.sizeMode != HudSizeMode::Compact && snapshot_ &&
        snapshot_->lastSuccessAt.time_since_epoch().count() != 0) {
      value += L" · 更新 " + FormatLocalResetTime(snapshot_->lastSuccessAt);
    }
    return value;
  }
  const RateWindow* window = SelectedWindow();
  if (!window) return L"DATA UNAVAILABLE";
  std::wstring state;
  if (settings_.sizeMode == HudSizeMode::Compact) {
    if (window->remainingPercent <= 0) state = L"STONE";
    else if (window->remainingPercent < 10) state = L"CRITICAL";
    else if (window->remainingPercent <= 30) state = L"WARNING";
    else state = L"LIVE";
    state += L" · " + WindowLabel(*window);
    if (snapshot_ && !snapshot_->planType.empty()) {
      state += L" · " + Utf8ToWide(snapshot_->planType);
    }
    return state;
  }
  if (window->remainingPercent <= 0) state = L"STONE · 额度耗尽";
  else if (window->remainingPercent < 10) state = L"CRITICAL · 严重";
  else if (window->remainingPercent <= 30) state = L"WARNING · 额度不足";
  else state = L"LIVE · READY";
  state += L"  " + WindowLabel(*window);
  if (snapshot_ && !snapshot_->planType.empty()) state += L" · " + Utf8ToWide(snapshot_->planType);
  if (snapshot_ && snapshot_->credits.balance) {
    wchar_t credits[48]{};
    swprintf_s(credits, L" · C %.1f", *snapshot_->credits.balance);
    state += credits;
  }
  return state;
}

void OverlayWindow::NotifyThresholds(double previous, double current) {
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
      else swprintf_s(data.szInfo, L"主要额度剩余已降至 %.0f%%。", current);
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
    case kTrayShowHide:
      hidden_ = !hidden_;
      ShowWindow(hwnd_, hidden_ ? SW_HIDE : SW_SHOWNOACTIVATE);
      break;
    case kTrayRefresh: RequestRefresh(true); break;
    case kTrayUsage: OpenUsagePage(); break;
    case kTrayOpenChatGpt: OpenChatGpt(); break;
    case kTrayDisplayMode:
      settings_.displayMode = static_cast<IndicatorDisplayMode>((static_cast<int>(settings_.displayMode) + 1) % 3);
      break;
    case kTrayNextWindow:
      if (snapshot_ && !snapshot_->windows.empty()) {
        settings_.selectedWindow = (settings_.selectedWindow + 1) %
                                   static_cast<int>(snapshot_->windows.size());
      }
      break;
    case kTrayFollow:
      settings_.followChatGpt = !settings_.followChatGpt;
      if (settings_.followChatGpt) FollowChatGptWindow();
      break;
    case kTrayTheme: settings_.themeEnabled = !settings_.themeEnabled; break;
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
          L"ChatGPT Codex Usage Monitor 1.0.2\n\n"
          L"额度来自官方 Codex App Server；仅显示接口返回的百分比，不伪造 token。\n"
          L"胸甲背景使用用户提供并确认有权使用的原图；指示灯与动效由 Direct2D 绘制。",
          L"关于", MB_OK | MB_ICONINFORMATION);
      return;
    case kTrayExit:
      DestroyWindow(hwnd_);
      return;
    default: return;
  }
  if (const RateWindow* window = SelectedWindow()) {
    energyState_ = ComputeEnergyState(window->remainingPercent, settings_.energy);
    ConfigureBlinkTimer();
  }
  SaveSettings(settings_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace monitor
