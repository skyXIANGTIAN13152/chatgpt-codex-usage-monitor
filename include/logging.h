#pragma once

#include "common.h"

namespace monitor {

enum class LogLevel { Debug, Warning, Error };

class Logger {
 public:
  static Logger& Instance();
  void Initialize(bool debugEnabled);
  void Write(LogLevel level, std::wstring_view message);
  std::wstring LogDirectory() const;

 private:
  Logger() = default;
  void RotateIfNeeded();
  std::mutex mutex_;
  bool debugEnabled_ = false;
  std::wstring directory_;
  std::wstring path_;
};

void LogDebug(std::wstring_view message);
void LogWarning(std::wstring_view message);
void LogError(std::wstring_view message);

}  // namespace monitor

