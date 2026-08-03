#include "hud_controls.h"
#include <algorithm>

namespace monitor {
namespace {

bool Contains(const HudControlRect& rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

}  // namespace

HudControlLayout CalculateHudControlLayout(float widthDips, float heightDips) {
  const float rightEdge = widthDips - 12.0f;
  const float top = heightDips - 36.0f;
  const float bottom = heightDips - 2.0f;
  const float split = rightEdge - 27.0f;
  return {
      {rightEdge - 54.0f, top, split, bottom},
      {split, top, widthDips, bottom},
  };
}

HudControl HitTestHudControl(int xPixels, int yPixels, int widthPixels,
                             int heightPixels, UINT dpi, float uiScale) {
  const float dpiScale = static_cast<float>(std::max(dpi, 96u)) / 96.0f;
  uiScale = std::clamp(uiScale, 0.5f, 2.0f);
  const float scale = dpiScale * uiScale;
  const float x = static_cast<float>(xPixels) / scale;
  const float y = static_cast<float>(yPixels) / scale;
  const auto layout = CalculateHudControlLayout(
      static_cast<float>(widthPixels) / scale,
      static_cast<float>(heightPixels) / scale);
  if (Contains(layout.minimize, x, y)) return HudControl::Minimize;
  if (Contains(layout.settings, x, y)) return HudControl::Settings;
  return HudControl::None;
}

bool HiddenAfterTrayPrimaryActivation(bool) { return false; }

}  // namespace monitor
