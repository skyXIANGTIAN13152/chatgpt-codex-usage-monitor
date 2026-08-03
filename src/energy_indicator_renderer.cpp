#include "overlay_window.h"
#include <d2d1.h>

namespace monitor {
namespace {

template <typename T>
void Release(T*& value) {
  if (value) { value->Release(); value = nullptr; }
}

D2D1_COLOR_F CoreColor(EnergyVisualState state, bool lit) {
  if (!lit && (state == EnergyVisualState::WarningRedBlink ||
               state == EnergyVisualState::CriticalRedFastBlink)) {
    return D2D1::ColorF(0.18f, 0.015f, 0.025f, 1.0f);
  }
  switch (state) {
    case EnergyVisualState::NormalBlue: return D2D1::ColorF(0.02f, 0.84f, 1.0f, 1.0f);
    case EnergyVisualState::WarningRedBlink:
    case EnergyVisualState::CriticalRedFastBlink: return D2D1::ColorF(1.0f, 0.04f, 0.16f, 1.0f);
    case EnergyVisualState::Stone: return D2D1::ColorF(0.52f, 0.54f, 0.57f, 1.0f);
    case EnergyVisualState::DataUnavailable: return D2D1::ColorF(0.18f, 0.20f, 0.23f, 1.0f);
  }
  return D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f);
}

ID2D1PathGeometry* CreateCrystalGeometry(ID2D1Factory* factory, float cx,
                                         float top, float halfWidth,
                                         float height) {
  if (!factory) return nullptr;
  ID2D1PathGeometry* geometry = nullptr;
  ID2D1GeometrySink* sink = nullptr;
  if (FAILED(factory->CreatePathGeometry(&geometry)) ||
      FAILED(geometry->Open(&sink))) {
    Release(sink);
    Release(geometry);
    return nullptr;
  }

  sink->BeginFigure(D2D1::Point2F(cx, top), D2D1_FIGURE_BEGIN_FILLED);
  sink->AddBezier(D2D1::BezierSegment(
      D2D1::Point2F(cx - halfWidth * 0.72f, top),
      D2D1::Point2F(cx - halfWidth, top + height * 0.20f),
      D2D1::Point2F(cx - halfWidth, top + height * 0.38f)));
  sink->AddBezier(D2D1::BezierSegment(
      D2D1::Point2F(cx - halfWidth, top + height * 0.61f),
      D2D1::Point2F(cx - halfWidth * 0.44f, top + height * 0.84f),
      D2D1::Point2F(cx, top + height)));
  sink->AddBezier(D2D1::BezierSegment(
      D2D1::Point2F(cx + halfWidth * 0.44f, top + height * 0.84f),
      D2D1::Point2F(cx + halfWidth, top + height * 0.61f),
      D2D1::Point2F(cx + halfWidth, top + height * 0.38f)));
  sink->AddBezier(D2D1::BezierSegment(
      D2D1::Point2F(cx + halfWidth, top + height * 0.20f),
      D2D1::Point2F(cx + halfWidth * 0.72f, top),
      D2D1::Point2F(cx, top)));
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  if (FAILED(sink->Close())) Release(geometry);
  Release(sink);
  return geometry;
}

}  // namespace

void OverlayWindow::DrawEnergyIndicator(float left, float top, float width, float height) {
  if (!renderTarget_ || !d2dFactory_) return;

  // The supplied chest artwork already contains the metal housing. Draw only
  // the live crystal over it, at the same narrow vertical proportions, so the
  // indicator reads as part of the armor instead of a second badge on top.
  const float cx = left + width * 0.5f;
  const float crystalTop = top + 8.0f;
  const float crystalHeight = std::min(23.0f, std::max(18.0f, height * 0.34f));
  const float halfWidth = std::min(7.2f, std::max(5.8f, width * 0.105f));
  const float glowCy = crystalTop + crystalHeight * 0.41f;
  const bool lit = blinkOn_ && energyState_.illuminated;
  ID2D1PathGeometry* crystal = CreateCrystalGeometry(
      d2dFactory_, cx, crystalTop, halfWidth, crystalHeight);
  if (!crystal) return;

  const D2D1_COLOR_F core = CoreColor(energyState_.visual, lit);

  // In the normal state the authentic blue lamp in the supplied artwork is
  // already the exact shape and material we want. Add only a restrained halo;
  // do not cover it with a second vector imitation.
  if (energyState_.visual == EnergyVisualState::NormalBlue) {
    if (settings_.energy.glow && lit) {
      for (int i = 2; i >= 1; --i) {
        D2D1_COLOR_F glowColor = core;
        glowColor.a = i == 1 ? 0.055f : 0.025f;
        ID2D1SolidColorBrush* glow = nullptr;
        renderTarget_->CreateSolidColorBrush(glowColor, &glow);
        if (glow) {
          renderTarget_->FillEllipse(
              D2D1::Ellipse(
                  D2D1::Point2F(cx, glowCy),
                  halfWidth + static_cast<float>(i) * 2.0f,
                  crystalHeight * 0.43f + static_cast<float>(i) * 2.2f),
              glow);
        }
        Release(glow);
      }
    }
    Release(crystal);
    return;
  }

  ID2D1SolidColorBrush* shadow = nullptr;
  ID2D1SolidColorBrush* rim = nullptr;
  renderTarget_->CreateSolidColorBrush(
      D2D1::ColorF(0.015f, 0.025f, 0.040f, 0.92f), &shadow);
  renderTarget_->CreateSolidColorBrush(
      energyState_.visual == EnergyVisualState::Stone
          ? D2D1::ColorF(0.72f, 0.74f, 0.77f, 0.58f)
          : D2D1::ColorF(1.00f, 0.58f, 0.62f, lit ? 0.62f : 0.34f),
      &rim);

  if (settings_.energy.glow && lit &&
      energyState_.visual != EnergyVisualState::Stone &&
      energyState_.visual != EnergyVisualState::DataUnavailable) {
    for (int i = 3; i >= 1; --i) {
      D2D1_COLOR_F glowColor = core;
      glowColor.a = 0.035f * static_cast<float>(4 - i);
      ID2D1SolidColorBrush* glow = nullptr;
      renderTarget_->CreateSolidColorBrush(glowColor, &glow);
      if (glow) {
        renderTarget_->FillEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(cx, glowCy),
                halfWidth + static_cast<float>(i) * 2.1f,
                crystalHeight * 0.43f + static_cast<float>(i) * 2.3f),
            glow);
      }
      Release(glow);
    }
  }

  const D2D1_GRADIENT_STOP coreStops[] = {
      {0.00f, D2D1::ColorF(1.0f, 1.0f, 1.0f,
                            lit && energyState_.visual != EnergyVisualState::Stone
                                ? 0.96f
                                : 0.28f)},
      {0.28f, core},
      {0.76f, D2D1::ColorF(core.r * 0.62f, core.g * 0.62f,
                            core.b * 0.62f, 1.0f)},
      {1.00f, D2D1::ColorF(core.r * 0.16f, core.g * 0.16f,
                            core.b * 0.16f, 1.0f)},
  };
  ID2D1GradientStopCollection* coreCollection = nullptr;
  ID2D1RadialGradientBrush* coreBrush = nullptr;
  if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(
          coreStops, static_cast<UINT32>(std::size(coreStops)),
          D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &coreCollection))) {
    renderTarget_->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(cx, glowCy),
            D2D1::Point2F(-halfWidth * 0.24f, -crystalHeight * 0.10f),
            halfWidth * 1.05f, crystalHeight * 0.55f),
        coreCollection, &coreBrush);
  }

  if (shadow && energyState_.visual == EnergyVisualState::DataUnavailable) {
    renderTarget_->DrawGeometry(crystal, shadow, 1.1f);
  }
  if (coreBrush) renderTarget_->FillGeometry(crystal, coreBrush);
  if (rim) renderTarget_->DrawGeometry(crystal, rim, 0.55f);

  if (energyState_.visual == EnergyVisualState::Stone) {
    ID2D1SolidColorBrush* crack = nullptr;
    renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(0.17f, 0.18f, 0.20f, 0.90f), &crack);
    if (crack) {
      renderTarget_->DrawLine(
          D2D1::Point2F(cx - 1.0f, crystalTop + 3.0f),
          D2D1::Point2F(cx + 1.2f, crystalTop + 9.0f), crack, 0.8f);
      renderTarget_->DrawLine(
          D2D1::Point2F(cx + 1.2f, crystalTop + 9.0f),
          D2D1::Point2F(cx - 2.0f, crystalTop + 14.0f), crack, 0.8f);
      renderTarget_->DrawLine(
          D2D1::Point2F(cx - 2.0f, crystalTop + 14.0f),
          D2D1::Point2F(cx + 0.2f, crystalTop + crystalHeight - 3.0f),
          crack, 0.8f);
      renderTarget_->DrawLine(
          D2D1::Point2F(cx + 1.2f, crystalTop + 9.0f),
          D2D1::Point2F(cx + 3.6f, crystalTop + 12.0f), crack, 0.7f);
    }
    Release(crack);
  }

  Release(coreBrush);
  Release(coreCollection);
  Release(rim);
  Release(shadow);
  Release(crystal);
}

}  // namespace monitor
