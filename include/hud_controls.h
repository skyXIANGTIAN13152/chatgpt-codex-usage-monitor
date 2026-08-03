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
HudControl HitTestHudControl(int xPixels, int yPixels, int widthPixels,
                             int heightPixels, UINT dpi);

// Primary activation of a notification icon is a restore action, not a toggle.
// Returning false makes duplicate WM_LBUTTONUP/NIN_SELECT notifications idempotent.
bool HiddenAfterTrayPrimaryActivation(bool currentlyHidden);

}  // namespace monitor
