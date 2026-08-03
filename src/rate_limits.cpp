#include "rate_limits.h"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace monitor {
namespace {

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
  if (total <= 0) return L"即将重置";
  const auto days = total / (24 * 60);
  total %= 24 * 60;
  const auto hours = total / 60;
  const auto minutes = total % 60;
  wchar_t buffer[48]{};
  if (days > 0) swprintf_s(buffer, L"%lld天%lld时", static_cast<long long>(days),
                            static_cast<long long>(hours));
  else if (hours > 0) swprintf_s(buffer, L"%lld时%lld分", static_cast<long long>(hours),
                                 static_cast<long long>(minutes));
  else swprintf_s(buffer, L"%lld分钟", static_cast<long long>(minutes));
  return buffer;
}

std::wstring DataStatusText(DataStatus status) {
  switch (status) {
    case DataStatus::Connecting: return L"正在连接 CODEX";
    case DataStatus::Live: return L"LIVE";
    case DataStatus::Stale: return L"STALE · 数据过期";
    case DataStatus::Offline: return L"OFFLINE";
    case DataStatus::CliMissing: return L"CODEX CLI 未安装";
    case DataStatus::NotLoggedIn: return L"CODEX 尚未登录";
    case DataStatus::NetworkUnavailable: return L"网络不可用";
    case DataStatus::DataUnavailable: return L"DATA UNAVAILABLE";
  }
  return L"DATA UNAVAILABLE";
}

}  // namespace monitor
