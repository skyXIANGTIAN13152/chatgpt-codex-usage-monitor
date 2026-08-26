#pragma once

#include "json.h"
#include <ctime>

namespace monitor {

enum class DataStatus {
  Connecting,
  Live,
  Stale,
  Offline,
  CliMissing,
  NotLoggedIn,
  NetworkUnavailable,
  DataUnavailable,
};

struct RateWindow {
  std::string bucketId;
  std::string bucketName;
  std::string windowName;
  double usedPercent = 0.0;
  double remainingPercent = 100.0;
  int windowDurationMins = 0;
  std::optional<std::chrono::system_clock::time_point> resetsAt;
};

struct CreditsInfo {
  bool present = false;
  bool hasCredits = false;
  bool unlimited = false;
  std::optional<double> balance;
  std::optional<int> resetCreditsAvailable;
};

struct RateLimitSnapshot {
  std::vector<RateWindow> windows;
  CreditsInfo credits;
  std::string planType;
  std::string rateLimitReachedType;
  DataStatus status = DataStatus::DataUnavailable;
  bool sparseUpdate = false;
  std::chrono::system_clock::time_point receivedAt{};
  std::chrono::system_clock::time_point lastSuccessAt{};
  std::string errorMessage;
};

enum class QuotaWindowKind {
  FiveHour,
  Weekly,
  Other,
};

struct CodexQuotaWindows {
  const RateWindow* weekly = nullptr;
  const RateWindow* fiveHour = nullptr;
};

double RemainingPercent(double usedPercent);
QuotaWindowKind ClassifyQuotaWindow(const RateWindow& window);
CodexQuotaWindows SelectCodexQuotaWindows(const RateLimitSnapshot& snapshot);
RateLimitSnapshot MergeSparseRateLimitSnapshot(const RateLimitSnapshot& base,
                                               const RateLimitSnapshot& update);
std::optional<std::chrono::system_clock::time_point> ParseResetTime(const JsonValue& value);
std::optional<RateLimitSnapshot> ParseRateLimitMessage(std::string_view json, std::string* error);
bool IsSnapshotStale(const RateLimitSnapshot& snapshot,
                     std::chrono::system_clock::time_point now,
                     std::chrono::seconds maxAge);
std::wstring FormatLocalResetTime(std::chrono::system_clock::time_point value);
std::wstring FormatResetCountdown(std::chrono::system_clock::time_point value,
                                  std::chrono::system_clock::time_point now);
std::wstring DataStatusText(DataStatus status);

}  // namespace monitor
