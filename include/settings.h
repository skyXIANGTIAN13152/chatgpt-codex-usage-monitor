#pragma once

#include "energy_indicator.h"

namespace monitor {

enum class IndicatorDisplayMode { ProgressOnly, EnergyOnly, Both };
enum class ProgressDisplayMode { Bar, Ring };
enum class HudSizeMode { Compact, Standard, Expanded };

constexpr int kDefaultHudScalePercent = 80;
constexpr int kMinHudScalePercent = 60;
constexpr int kMaxHudScalePercent = 140;

struct Settings {
  int x = INT_MIN;
  int y = INT_MIN;
  bool alwaysOnTop = true;
  bool followChatGpt = false;
  int refreshSeconds = 60;
  bool themeEnabled = true;
  IndicatorDisplayMode displayMode = IndicatorDisplayMode::Both;
  ProgressDisplayMode progressDisplayMode = ProgressDisplayMode::Bar;
  HudSizeMode sizeMode = HudSizeMode::Compact;
  int scalePercent = kDefaultHudScalePercent;
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
