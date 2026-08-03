#pragma once

#include "energy_indicator.h"

namespace monitor {

enum class IndicatorDisplayMode { ProgressOnly, EnergyOnly, Both };
enum class HudSizeMode { Compact, Standard, Expanded };

struct Settings {
  int x = INT_MIN;
  int y = INT_MIN;
  bool alwaysOnTop = true;
  bool followChatGpt = false;
  int refreshSeconds = 60;
  bool themeEnabled = true;
  IndicatorDisplayMode displayMode = IndicatorDisplayMode::Both;
  HudSizeMode sizeMode = HudSizeMode::Compact;
  EnergyIndicatorConfig energy;
  int selectedWindow = 0;
  bool debugLogging = false;
  std::vector<int> notificationThresholds{20, 10, 5, 0};
};

std::wstring SettingsDirectory();
std::wstring SettingsFilePath();
Settings LoadSettings();
bool SaveSettings(const Settings& settings);

}  // namespace monitor
