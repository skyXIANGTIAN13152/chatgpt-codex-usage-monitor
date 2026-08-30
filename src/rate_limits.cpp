#include "rate_limits.h"
#include <cctype>
#include <climits>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace monitor {
namespace {

constexpr int kFiveHourMinutes = 5 * 60;
constexpr int kWeeklyMinutes = 7 * 24 * 60;

int BucketPriority(const RateWindow& window) {
  std::string id = window.bucketId;
  std::transform(id.begin(), id.end(), id.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  if (id.empty() || id == "codex") return 0;
  if (id.find("codex") != std::string::npos) return 1;
  return 2;
}

int DurationDistance(const RateWindow& window, int targetMinutes) {
  return std::abs(window.windowDurationMins - targetMinutes);
}

std::string StringField(const JsonValue& value, std::string_view key) {
  const JsonValue* field = value.Find(key);
  return field && field->AsString() ? *field->AsString() : std::string();
}

std::optional<double> NumberField(const JsonValue& value, std::string_view key) {
  const JsonValue* field = value.Find(key);
  if (!field) return std::nullopt;
  if (auto number = field->AsNumber()) return number;
  if (const auto* text = field->AsString()) {
    char* end = nullptr;
    const double number = std::strtod(text->c_str(), &end);
    if (end && *end == '\0' && std::isfinite(number)) return number;
  }
  return std::nullopt;
}

std::optional<bool> BoolField(const JsonValue& value, std::string_view key) {
  const JsonValue* field = value.Find(key);
  if (!field) return std::nullopt;
  if (auto boolean = field->AsBool()) return boolean;
  if (auto number = field->AsNumber()) return *number != 0.0;
  return std::nullopt;
}

std::chrono::system_clock::time_point WindowsTicksToTimePoint(uint64_t ticks) {
  constexpr uint64_t kWindowsToUnixSeconds = 11644473600ULL;
  const uint64_t seconds = ticks / 10000000ULL;
  if (seconds < kWindowsToUnixSeconds) return {};
  return std::chrono::system_clock::from_time_t(
      static_cast<time_t>(seconds - kWindowsToUnixSeconds));
}

std::optional<std::chrono::system_clock::time_point> ParseIso8601(std::string_view text) {
  if (text.size() < 19) return std::nullopt;
  SYSTEMTIME st{};
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf_s(std::string(text.substr(0, 19)).c_str(), "%d-%d-%dT%d:%d:%d",
               &year, &month, &day, &hour, &minute, &second) != 6) {
    return std::nullopt;
  }
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(std::min(second, 59));

  size_t zonePos = 19;
  if (zonePos < text.size() && text[zonePos] == '.') {
    ++zonePos;
    int milliseconds = 0;
    int digits = 0;
    while (zonePos < text.size() && text[zonePos] >= '0' && text[zonePos] <= '9') {
      if (digits < 3) milliseconds = milliseconds * 10 + (text[zonePos] - '0');
      ++digits;
      ++zonePos;
    }
    while (digits < 3) { milliseconds *= 10; ++digits; }
    st.wMilliseconds = static_cast<WORD>(milliseconds);
  }

  int offsetMinutes = 0;
  if (zonePos < text.size() && text[zonePos] != 'Z' && text[zonePos] != 'z') {
    const int sign = text[zonePos] == '-' ? -1 : (text[zonePos] == '+' ? 1 : 0);
    if (!sign || zonePos + 5 >= text.size()) return std::nullopt;
    const int oh = (text[zonePos + 1] - '0') * 10 + (text[zonePos + 2] - '0');
    const size_t minutePos = text[zonePos + 3] == ':' ? zonePos + 4 : zonePos + 3;
    if (minutePos + 1 >= text.size()) return std::nullopt;
    const int om = (text[minutePos] - '0') * 10 + (text[minutePos + 1] - '0');
    offsetMinutes = sign * (oh * 60 + om);
  }

  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft)) return std::nullopt;
  ULARGE_INTEGER ticks{};
  ticks.LowPart = ft.dwLowDateTime;
  ticks.HighPart = ft.dwHighDateTime;
  const int64_t adjusted = static_cast<int64_t>(ticks.QuadPart) -
                           static_cast<int64_t>(offsetMinutes) * 60LL * 10000000LL;
  if (adjusted <= 0) return std::nullopt;
  return WindowsTicksToTimePoint(static_cast<uint64_t>(adjusted));
}

void ParseCreditsObject(const JsonValue* credits, CreditsInfo* output) {
  if (!credits || !credits->IsObject()) return;
  output->present = true;
  if (auto value = BoolField(*credits, "hasCredits")) output->hasCredits = *value;
  if (auto value = BoolField(*credits, "unlimited")) output->unlimited = *value;
  if (auto value = NumberField(*credits, "balance")) {
    output->balance = *value;
    output->hasCredits = *value > 0;
  }
}

void ParseWindow(const JsonValue* window, std::string_view bucketId,
                 std::string_view bucketName, std::string_view windowName,
                 RateLimitSnapshot* snapshot) {
  if (!window || !window->IsObject()) return;
  const auto used = NumberField(*window, "usedPercent");
  if (!used) return;
  RateWindow parsed;
  parsed.bucketId = std::string(bucketId);
  parsed.bucketName = std::string(bucketName);
  parsed.windowName = std::string(windowName);
  parsed.usedPercent = std::clamp(*used, 0.0, 100.0);
  parsed.remainingPercent = RemainingPercent(*used);
  if (auto duration = NumberField(*window, "windowDurationMins")) {
    parsed.windowDurationMins = static_cast<int>(std::clamp(*duration, 0.0, 5256000.0));
  }
  if (const JsonValue* reset = window->Find("resetsAt")) parsed.resetsAt = ParseResetTime(*reset);
  snapshot->windows.push_back(std::move(parsed));
}

void ParseBucket(const JsonValue& bucket, std::string_view fallbackId,
                 RateLimitSnapshot* snapshot) {
  if (!bucket.IsObject()) return;
  std::string id = StringField(bucket, "limitId");
  if (id.empty()) id = std::string(fallbackId);
  std::string name = StringField(bucket, "limitName");
  if (name.empty()) name = id;
  ParseWindow(bucket.Find("primary"), id, name, "primary", snapshot);
  ParseWindow(bucket.Find("secondary"), id, name, "secondary", snapshot);
  if (!bucket.Find("primary") && bucket.Find("usedPercent")) {
    ParseWindow(&bucket, id, name, "primary", snapshot);
  }
  if (snapshot->planType.empty()) snapshot->planType = StringField(bucket, "planType");
  if (snapshot->rateLimitReachedType.empty()) {
    snapshot->rateLimitReachedType = StringField(bucket, "rateLimitReachedType");
  }
  ParseCreditsObject(bucket.Find("credits"), &snapshot->credits);
  if (auto value = BoolField(bucket, "unlimited")) {
    snapshot->credits.present = true;
    snapshot->credits.unlimited = *value;
  }
  if (auto value = BoolField(bucket, "hasCredits")) {
    snapshot->credits.present = true;
    snapshot->credits.hasCredits = *value;
  }
  if (auto value = NumberField(bucket, "balance")) {
    snapshot->credits.present = true;
    snapshot->credits.balance = *value;
  }
}

}  // namespace

double RemainingPercent(double usedPercent) {
  if (!std::isfinite(usedPercent)) return 0.0;
  return std::clamp(100.0 - usedPercent, 0.0, 100.0);
}

QuotaWindowKind ClassifyQuotaWindow(const RateWindow& window) {
  // App Server intentionally exposes primary/secondary as generic windows.
  // Duration is the stable discriminator: 300 minutes and 10,080 minutes.
  if (window.windowDurationMins >= 4 * 60 &&
      window.windowDurationMins <= 6 * 60) {
    return QuotaWindowKind::FiveHour;
  }
  if (window.windowDurationMins >= 6 * 24 * 60 &&
      window.windowDurationMins <= 8 * 24 * 60) {
    return QuotaWindowKind::Weekly;
  }
  return QuotaWindowKind::Other;
}

CodexQuotaWindows SelectCodexQuotaWindows(const RateLimitSnapshot& snapshot) {
  CodexQuotaWindows selected;
  long long bestPairScore = LLONG_MAX;

  // Prefer two matching windows from the same quota bucket, then prefer the
  // standard Codex bucket, and finally the closest durations. This prevents a
  // model-specific limit from being paired with the account-wide weekly limit.
  for (const RateWindow& weekly : snapshot.windows) {
    if (ClassifyQuotaWindow(weekly) != QuotaWindowKind::Weekly) continue;
    for (const RateWindow& fiveHour : snapshot.windows) {
      if (ClassifyQuotaWindow(fiveHour) != QuotaWindowKind::FiveHour) continue;
      const bool sameBucket = weekly.bucketId == fiveHour.bucketId;
      const long long score = (sameBucket ? 0LL : 1000000LL) +
          static_cast<long long>(BucketPriority(weekly) + BucketPriority(fiveHour)) * 10000LL +
          static_cast<long long>(DurationDistance(weekly, kWeeklyMinutes)) * 10LL +
          DurationDistance(fiveHour, kFiveHourMinutes);
      if (score < bestPairScore) {
        bestPairScore = score;
        selected.weekly = &weekly;
        selected.fiveHour = &fiveHour;
      }
    }
  }
  if (selected.weekly && selected.fiveHour) return selected;

  auto chooseSingle = [&](QuotaWindowKind kind, int targetMinutes) {
    const RateWindow* best = nullptr;
    int bestScore = INT_MAX;
    for (const RateWindow& window : snapshot.windows) {
      if (ClassifyQuotaWindow(window) != kind) continue;
      const int score = BucketPriority(window) * 100000 +
                        DurationDistance(window, targetMinutes);
      if (score < bestScore) {
        best = &window;
        bestScore = score;
      }
    }
    return best;
  };
  selected.weekly = chooseSingle(QuotaWindowKind::Weekly, kWeeklyMinutes);
  selected.fiveHour = chooseSingle(QuotaWindowKind::FiveHour, kFiveHourMinutes);
  return selected;
}

RateLimitSnapshot MergeSparseRateLimitSnapshot(const RateLimitSnapshot& base,
                                               const RateLimitSnapshot& update) {
  RateLimitSnapshot merged = base;
  for (const RateWindow& incoming : update.windows) {
    const auto existing = std::find_if(
        merged.windows.begin(), merged.windows.end(), [&](const RateWindow& current) {
          return current.bucketId == incoming.bucketId &&
                 current.windowName == incoming.windowName;
        });
    if (existing != merged.windows.end()) *existing = incoming;
    else merged.windows.push_back(incoming);
  }
  if (update.credits.present) merged.credits = update.credits;
  if (!update.planType.empty()) merged.planType = update.planType;
  if (!update.rateLimitReachedType.empty()) {
    merged.rateLimitReachedType = update.rateLimitReachedType;
  }
  merged.status = update.status;
  merged.sparseUpdate = false;
  merged.receivedAt = update.receivedAt;
  merged.lastSuccessAt = update.lastSuccessAt;
  merged.errorMessage = update.errorMessage;
  return merged;
}

std::optional<std::chrono::system_clock::time_point> ParseResetTime(const JsonValue& value) {
  if (auto numeric = value.AsNumber()) {
    double seconds = *numeric;
    if (seconds > 100000000000.0) seconds /= 1000.0;
    if (seconds < 0 || seconds > 32503680000.0) return std::nullopt;
    return std::chrono::system_clock::from_time_t(static_cast<time_t>(seconds));
  }
  if (const auto* text = value.AsString()) {
    char* end = nullptr;
    const double numeric = std::strtod(text->c_str(), &end);
    if (end && *end == '\0') {
      JsonValue asNumber(numeric);
      return ParseResetTime(asNumber);
    }
    return ParseIso8601(*text);
  }
  return std::nullopt;
}

std::optional<RateLimitSnapshot> ParseRateLimitMessage(std::string_view json,
                                                       std::string* error) {
  const JsonParseResult parsed = ParseJson(json);
  if (!parsed) {
    if (error) *error = parsed.error;
    return std::nullopt;
  }
  if (!parsed.value.IsObject()) {
    if (error) *error = "message root is not an object";
    return std::nullopt;
  }
  if (const JsonValue* rpcError = parsed.value.Find("error")) {
    std::string message = StringField(*rpcError, "message");
    if (message.empty()) message = "app-server returned an error";
    if (error) *error = message;
    return std::nullopt;
  }

  const JsonValue* payload = parsed.value.Find("result");
  if (!payload) payload = parsed.value.Find("params");
  if (!payload) payload = &parsed.value;
  if (!payload->IsObject()) {
    if (error) *error = "rate-limit payload is not an object";
    return std::nullopt;
  }

  RateLimitSnapshot snapshot;
  snapshot.status = DataStatus::Live;
  snapshot.sparseUpdate = StringField(parsed.value, "method") ==
                          "account/rateLimits/updated";
  snapshot.receivedAt = std::chrono::system_clock::now();
  snapshot.lastSuccessAt = snapshot.receivedAt;
  snapshot.planType = StringField(*payload, "planType");
  snapshot.rateLimitReachedType = StringField(*payload, "rateLimitReachedType");

  const JsonValue* multi = payload->Find("rateLimitsByLimitId");
  if (multi && multi->IsObject()) {
    for (const auto& [id, bucket] : *multi->AsObject()) ParseBucket(bucket, id, &snapshot);
  }
  if (snapshot.windows.empty()) {
    const JsonValue* single = payload->Find("rateLimits");
    if (single && single->IsArray()) {
      for (const JsonValue& bucket : *single->AsArray()) ParseBucket(bucket, {}, &snapshot);
    } else if (single) {
      ParseBucket(*single, "codex", &snapshot);
    } else if (payload->Find("primary") || payload->Find("usedPercent")) {
      ParseBucket(*payload, "codex", &snapshot);
    }
  }

  ParseCreditsObject(payload->Find("credits"), &snapshot.credits);
  if (auto value = BoolField(*payload, "unlimited")) {
    snapshot.credits.present = true;
    snapshot.credits.unlimited = *value;
  }
  if (auto value = BoolField(*payload, "hasCredits")) {
    snapshot.credits.present = true;
    snapshot.credits.hasCredits = *value;
  }
  if (auto value = NumberField(*payload, "balance")) {
    snapshot.credits.present = true;
    snapshot.credits.balance = *value;
  }
  if (const JsonValue* resetCredits = payload->Find("rateLimitResetCredits");
      resetCredits && resetCredits->IsObject()) {
    snapshot.credits.present = true;
    if (auto count = NumberField(*resetCredits, "availableCount")) {
      snapshot.credits.resetCreditsAvailable = static_cast<int>(std::max(0.0, *count));
    }
  }

  if (snapshot.windows.empty()) {
    if (error) *error = "official interface returned no quota windows";
    return std::nullopt;
  }
  return snapshot;
}

bool IsSnapshotStale(const RateLimitSnapshot& snapshot,
                     std::chrono::system_clock::time_point now,
                     std::chrono::seconds maxAge) {
  if (snapshot.lastSuccessAt.time_since_epoch().count() == 0) return true;
  return now - snapshot.lastSuccessAt > maxAge;
}

std::wstring FormatLocalResetTime(std::chrono::system_clock::time_point value) {
  const time_t raw = std::chrono::system_clock::to_time_t(value);
  tm utcTm{};
  if (gmtime_s(&utcTm, &raw) != 0) return L"--";
  SYSTEMTIME utc{};
  utc.wYear = static_cast<WORD>(utcTm.tm_year + 1900);
  utc.wMonth = static_cast<WORD>(utcTm.tm_mon + 1);
  utc.wDay = static_cast<WORD>(utcTm.tm_mday);
  utc.wHour = static_cast<WORD>(utcTm.tm_hour);
  utc.wMinute = static_cast<WORD>(utcTm.tm_min);
  utc.wSecond = static_cast<WORD>(utcTm.tm_sec);
  SYSTEMTIME local{};
  if (!SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) return L"--";
  wchar_t buffer[40]{};
  swprintf_s(buffer, L"%02u/%02u %02u:%02u", local.wMonth, local.wDay,
             local.wHour, local.wMinute);
  return buffer;
}

std::wstring FormatResetCountdown(std::chrono::system_clock::time_point value,
                                  std::chrono::system_clock::time_point now) {
  auto total = std::chrono::duration_cast<std::chrono::minutes>(value - now).count();
  if (total <= 0) return L"Resetting soon";
  const auto days = total / (24 * 60);
  total %= 24 * 60;
  const auto hours = total / 60;
  const auto minutes = total % 60;
  wchar_t buffer[48]{};
  if (days > 0) swprintf_s(buffer, L"%lldd %lldh", static_cast<long long>(days),
                            static_cast<long long>(hours));
  else if (hours > 0) swprintf_s(buffer, L"%lldh %lldm", static_cast<long long>(hours),
                                 static_cast<long long>(minutes));
  else swprintf_s(buffer, L"%lld min", static_cast<long long>(minutes));
  return buffer;
}

std::wstring DataStatusText(DataStatus status) {
  switch (status) {
    case DataStatus::Connecting: return L"CONNECTING TO CODEX";
    case DataStatus::Live: return L"LIVE";
    case DataStatus::Stale: return L"STALE · DATA OUTDATED";
    case DataStatus::Offline: return L"OFFLINE";
    case DataStatus::CliMissing: return L"CODEX CLI NOT INSTALLED";
    case DataStatus::NotLoggedIn: return L"CODEX NOT SIGNED IN";
    case DataStatus::NetworkUnavailable: return L"NETWORK UNAVAILABLE";
    case DataStatus::DataUnavailable: return L"DATA UNAVAILABLE";
  }
  return L"DATA UNAVAILABLE";
}

}  // namespace monitor
