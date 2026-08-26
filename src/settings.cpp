#include "settings.h"
#include "json.h"
#include <fstream>
#include <shlobj.h>
#include <sstream>

namespace monitor {
namespace {

std::optional<int> IntField(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.Find(key);
  if (!value) return std::nullopt;
  if (auto number = value->AsNumber()) return static_cast<int>(*number);
  return std::nullopt;
}

std::optional<bool> BoolField(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.Find(key);
  return value ? value->AsBool() : std::nullopt;
}

template <typename T>
T ClampEnum(int value, T minimum, T maximum, T fallback) {
  if (value < static_cast<int>(minimum) || value > static_cast<int>(maximum)) return fallback;
  return static_cast<T>(value);
}

}  // namespace

std::wstring SettingsDirectory() {
  return JoinPath(GetLocalAppDataDirectory(), kProductName);
}

std::wstring SettingsFilePath() { return JoinPath(SettingsDirectory(), L"settings.json"); }

Settings LoadSettings() {
  Settings settings;
  std::ifstream stream(SettingsFilePath(), std::ios::binary);
  if (!stream) return settings;
  std::ostringstream contents;
  contents << stream.rdbuf();
  const JsonParseResult parsed = ParseJson(contents.str());
  if (!parsed || !parsed.value.IsObject()) return settings;

  if (auto value = IntField(parsed.value, "x")) settings.x = *value;
  if (auto value = IntField(parsed.value, "y")) settings.y = *value;
  if (auto value = BoolField(parsed.value, "alwaysOnTop")) settings.alwaysOnTop = *value;
  if (auto value = BoolField(parsed.value, "followChatGpt")) settings.followChatGpt = *value;
  if (auto value = IntField(parsed.value, "refreshSeconds")) {
    settings.refreshSeconds = std::clamp(*value, 30, 600);
  }
  if (auto value = BoolField(parsed.value, "themeEnabled")) settings.themeEnabled = *value;
  if (auto value = IntField(parsed.value, "displayMode")) {
    settings.displayMode = ClampEnum(*value, IndicatorDisplayMode::ProgressOnly,
                                     IndicatorDisplayMode::Both, IndicatorDisplayMode::Both);
  }
  if (auto value = IntField(parsed.value, "progressDisplayMode")) {
    settings.progressDisplayMode = ClampEnum(*value, ProgressDisplayMode::Bar,
                                             ProgressDisplayMode::Ring,
                                             ProgressDisplayMode::Bar);
  }
  if (auto value = IntField(parsed.value, "quotaDisplayMode")) {
    settings.quotaDisplayMode = ClampEnum(*value, QuotaDisplayMode::WeeklyAndFiveHour,
                                          QuotaDisplayMode::WeeklyOnly,
                                          QuotaDisplayMode::WeeklyAndFiveHour);
  }
  if (auto value = IntField(parsed.value, "sizeMode")) {
    settings.sizeMode = ClampEnum(*value, HudSizeMode::Compact,
                                  HudSizeMode::Expanded, HudSizeMode::Standard);
  }
  if (auto value = IntField(parsed.value, "scalePercent")) {
    settings.scalePercent = std::clamp(*value, kMinHudScalePercent,
                                       kMaxHudScalePercent);
  }
  if (auto value = IntField(parsed.value, "warningThreshold")) {
    settings.energy.warningThreshold = std::clamp(*value, 10, 80);
  }
  if (auto value = IntField(parsed.value, "blinkStyle")) {
    settings.energy.blinkStyle = ClampEnum(*value, BlinkStyle::Gentle,
                                           BlinkStyle::Aggressive, BlinkStyle::Standard);
  }
  if (auto value = BoolField(parsed.value, "stoneAtZero")) settings.energy.stoneAtZero = *value;
  if (auto value = BoolField(parsed.value, "glow")) settings.energy.glow = *value;
  if (auto value = BoolField(parsed.value, "breathing")) settings.energy.breathing = *value;
  if (auto value = BoolField(parsed.value, "debugLogging")) settings.debugLogging = *value;
  const int configVersion = IntField(parsed.value, "configVersion").value_or(1);
  if (configVersion < 2) settings.sizeMode = HudSizeMode::Compact;
  return settings;
}

bool SaveSettings(const Settings& settings) {
  SHCreateDirectoryExW(nullptr, SettingsDirectory().c_str(), nullptr);
  const std::wstring path = SettingsFilePath();
  const std::wstring temporary = path + L".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream << "{\n"
         << "  \"configVersion\": 4,\n"
         << "  \"x\": " << settings.x << ",\n"
         << "  \"y\": " << settings.y << ",\n"
         << "  \"alwaysOnTop\": " << (settings.alwaysOnTop ? "true" : "false") << ",\n"
         << "  \"followChatGpt\": " << (settings.followChatGpt ? "true" : "false") << ",\n"
         << "  \"refreshSeconds\": " << settings.refreshSeconds << ",\n"
         << "  \"themeEnabled\": " << (settings.themeEnabled ? "true" : "false") << ",\n"
         << "  \"displayMode\": " << static_cast<int>(settings.displayMode) << ",\n"
         << "  \"progressDisplayMode\": " << static_cast<int>(settings.progressDisplayMode) << ",\n"
         << "  \"quotaDisplayMode\": " << static_cast<int>(settings.quotaDisplayMode) << ",\n"
         << "  \"sizeMode\": " << static_cast<int>(settings.sizeMode) << ",\n"
         << "  \"scalePercent\": " << settings.scalePercent << ",\n"
         << "  \"warningThreshold\": " << settings.energy.warningThreshold << ",\n"
         << "  \"blinkStyle\": " << static_cast<int>(settings.energy.blinkStyle) << ",\n"
         << "  \"stoneAtZero\": " << (settings.energy.stoneAtZero ? "true" : "false") << ",\n"
         << "  \"glow\": " << (settings.energy.glow ? "true" : "false") << ",\n"
         << "  \"breathing\": " << (settings.energy.breathing ? "true" : "false") << ",\n"
         << "  \"debugLogging\": " << (settings.debugLogging ? "true" : "false") << "\n"
         << "}\n";
  stream.close();
  if (!stream) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (!MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace monitor
