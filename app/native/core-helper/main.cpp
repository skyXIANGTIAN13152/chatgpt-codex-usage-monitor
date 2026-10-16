#include "energy_indicator.h"
#include "rate_limits.h"
#include <cmath>
#include <iostream>
#include <sstream>

namespace {
using monitor::JsonValue;

std::string Quote(std::string_view value) {
  return "\"" + monitor::EscapeJsonString(value) + "\"";
}

std::string EnergyJson(const JsonValue* params) {
  std::optional<double> remaining;
  monitor::EnergyIndicatorConfig config;
  if (params) {
    if (const auto* value = params->Find("remainingPercent")) remaining = value->AsNumber();
    if (const auto* value = params->Find("warningThreshold")) {
      if (const auto number = value->AsNumber(); number && std::isfinite(*number)) {
        config.warningThreshold = static_cast<int>(std::clamp(*number, 1.0, 99.0));
      }
    }
    if (const auto* value = params->Find("stoneAtZero")) {
      if (const auto boolean = value->AsBool()) config.stoneAtZero = *boolean;
    }
  }
  const auto energy = monitor::ComputeEnergyState(remaining, config);
  std::string_view visual = "unavailable";
  switch (energy.visual) {
    case monitor::EnergyVisualState::NormalBlue: visual = "normal"; break;
    case monitor::EnergyVisualState::WarningRedBlink: visual = "warning"; break;
    case monitor::EnergyVisualState::CriticalRedFastBlink: visual = "critical"; break;
    case monitor::EnergyVisualState::Stone: visual = "exhausted"; break;
    case monitor::EnergyVisualState::DataUnavailable: break;
  }
  return "{\"visual\":" + Quote(visual) + ",\"fullCycleMs\":" +
      std::to_string(energy.fullCycleMs) + ",\"illuminated\":" +
      (energy.illuminated ? "true" : "false") + "}";
}

std::string WindowJson(const monitor::RateWindow& window) {
  const auto kind = monitor::ClassifyQuotaWindow(window);
  const std::string id = kind == monitor::QuotaWindowKind::FiveHour ? "five_hour" :
      kind == monitor::QuotaWindowKind::Weekly ? "weekly" : "codex_" + window.windowName;
  const std::string label = kind == monitor::QuotaWindowKind::FiveHour ? "5 小时" :
      kind == monitor::QuotaWindowKind::Weekly ? "每周" : "其他额度";
  std::ostringstream out;
  out << "{\"id\":" << Quote(id) << ",\"label\":" << Quote(label)
      << ",\"usedPercent\":" << window.usedPercent
      << ",\"remainingPercent\":" << window.remainingPercent
      << ",\"windowMinutes\":";
  if (window.windowDurationMins > 0) out << window.windowDurationMins;
  else out << "null";
  out << ",\"resetsAt\":";
  if (window.resetsAt) {
    out << std::chrono::duration_cast<std::chrono::seconds>(
        window.resetsAt->time_since_epoch()).count();
  } else out << "null";
  out << '}';
  return out.str();
}

std::string SnapshotJson(const monitor::RateLimitSnapshot& snapshot) {
  const auto selected = monitor::SelectCodexQuotaWindows(snapshot);
  std::string windows = "[";
  bool first = true;
  for (const auto* window : {selected.fiveHour, selected.weekly, selected.other}) {
    if (!window) continue;
    if (!first) windows += ',';
    first = false;
    windows += WindowJson(*window);
  }
  windows += ']';
  return "{\"windows\":" + windows + ",\"plan\":" +
      (snapshot.planType.empty() ? "null" : Quote(snapshot.planType)) + "}";
}
}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  monitor::RateLimitSnapshot cached;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.size() > 1024 * 1024) return 2;
    const auto parsed = monitor::ParseJson(line);
    if (!parsed || !parsed.value.IsObject()) continue;
    const auto* id_value = parsed.value.Find("id");
    const auto id = id_value ? id_value->AsNumber() : std::nullopt;
    const auto* method_value = parsed.value.Find("method");
    const auto* method = method_value ? method_value->AsString() : nullptr;
    if (!id || !method) continue;
    const auto* params = parsed.value.Find("params");
    std::string result;
    if (*method == "computeEnergy") {
      result = EnergyJson(params);
    } else if (*method == "clear") {
      cached = {};
      result = "{}";
    } else if (*method == "parseRateLimits") {
      const auto* message_value = params ? params->Find("message") : nullptr;
      const auto* message = message_value ? message_value->AsString() : nullptr;
      std::string parse_error;
      const auto snapshot = message ? monitor::ParseRateLimitMessage(*message, &parse_error) : std::nullopt;
      if (!snapshot) {
        // Raw server messages and tokens never cross this boundary.
        std::cout << "{\"id\":" << *id << ",\"error\":{\"code\":-32602,\"message\":\"Invalid quota data\"}}\n" << std::flush;
        continue;
      }
      cached = snapshot->sparseUpdate
          ? monitor::MergeSparseRateLimitSnapshot(cached, *snapshot) : *snapshot;
      result = SnapshotJson(cached);
    } else {
      std::cout << "{\"id\":" << *id << ",\"error\":{\"code\":-32601,\"message\":\"Unsupported method\"}}\n" << std::flush;
      continue;
    }
    std::cout << "{\"id\":" << *id << ",\"result\":" << result << "}\n" << std::flush;
  }
  return 0;
}
