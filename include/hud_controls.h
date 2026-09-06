#pragma once

#include <windows.h>

namespace monitor {

enum class HudControl {
  None,
  Settings,
  Minimize,
};

struct HudControlRect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;
};

struct HudControlLayout {
  HudControlRect settings;
  HudControlRect minimize;
};

HudControlLayout CalculateHudControlLayout(float widthDips, float heightDips);
constexpr float kHudArtworkRight = 120.0f;
float CalculateHudRingLeft(float contentLeft, bool showEnergy);
float SingleQuotaPercentWidth(double remainingPercent);
HudControl HitTestHudControl(int xPixels, int yPixels, int widthPixels,
                             int heightPixels, UINT dpi, float uiScale = 1.0f);

// Primary activation of a notification icon is a restore action, not a toggle.
// Returning false makes duplicate WM_LBUTTONUP/NIN_SELECT notifications idempotent.
bool HiddenAfterTrayPrimaryActivation(bool currentlyHidden);

}  // namespace monitor
