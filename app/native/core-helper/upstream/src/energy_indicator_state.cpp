#include "energy_indicator.h"
#include <cmath>

namespace monitor {

EnergyIndicatorState ComputeEnergyState(std::optional<double> remainingPercent,
                                        const EnergyIndicatorConfig& config) {
  if (!remainingPercent || !std::isfinite(*remainingPercent)) {
    return {EnergyVisualState::DataUnavailable, 0, false};
  }
  const double remaining = std::clamp(*remainingPercent, 0.0, 100.0);
  if (remaining <= 0.0) {
    return {config.stoneAtZero ? EnergyVisualState::Stone
                               : EnergyVisualState::DataUnavailable,
            0, false};
  }
  if (remaining > static_cast<double>(config.warningThreshold)) {
    return {EnergyVisualState::NormalBlue, 0, true};
  }

  int period = 1200;
  if (remaining <= 5.0) period = 250;
  else if (remaining <= 10.0) period = 400;
  else if (remaining <= 20.0) period = 600;
  else if (remaining <= 30.0) period = 900;

  double styleScale = 1.0;
  if (config.blinkStyle == BlinkStyle::Gentle) styleScale = 1.25;
  if (config.blinkStyle == BlinkStyle::Aggressive) styleScale = 0.75;
  period = std::clamp(static_cast<int>(std::lround(period * styleScale)), 200, 1600);
  const EnergyVisualState visual = remaining <= 10.0
      ? EnergyVisualState::CriticalRedFastBlink
      : EnergyVisualState::WarningRedBlink;
  return {visual, period, true};
}

}  // namespace monitor

