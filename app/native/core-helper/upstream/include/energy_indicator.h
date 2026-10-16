#pragma once

#include "common.h"

namespace monitor {

enum class EnergyVisualState {
  NormalBlue,
  WarningRedBlink,
  CriticalRedFastBlink,
  Stone,
  DataUnavailable,
};

enum class BlinkStyle { Gentle, Standard, Aggressive };

struct EnergyIndicatorConfig {
  int warningThreshold = 50;
  BlinkStyle blinkStyle = BlinkStyle::Standard;
  bool stoneAtZero = true;
  bool glow = true;
  bool breathing = false;
};

struct EnergyIndicatorState {
  EnergyVisualState visual = EnergyVisualState::DataUnavailable;
  int fullCycleMs = 0;
  bool illuminated = false;
};

EnergyIndicatorState ComputeEnergyState(std::optional<double> remainingPercent,
                                        const EnergyIndicatorConfig& config);

}  // namespace monitor

