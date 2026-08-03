#include "logging.h"
#include "settings.h"
#include <cstdio>
#include <shlobj.h>

namespace monitor {

Logger& Logger::Instance() {
  static Logger logger;
  return logger;
}

void Logger::Initialize(bool debugEnabled) {
  std::scoped_lock lock(mutex_);
  debugEnabled_ = debugEnabled;
  directory_ = JoinPath(SettingsDirectory(), L"logs");
  SHCreateDirectoryExW(nullptr, directory_.c_str(), nullptr);
  path_ = JoinPath(directory_, L"monitor.log");
  RotateIfNeeded();
}

void Logger::RotateIfNeeded() {
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data)) return;
  ULARGE_INTEGER size{};
  size.LowPart = data.nFileSizeLow;
  size.HighPart = data.nFileSizeHigh;
  if (size.QuadPart < 1024ULL * 1024ULL) return;
  const std::wstring old3 = JoinPath(directory_, L"monitor.3.log");
  const std::wstring old2 = JoinPath(directory_, L"monitor.2.log");
  const std::wstring old1 = JoinPath(directory_, L"monitor.1.log");
  DeleteFileW(old3.c_str());
  MoveFileExW(old2.c_str(), old3.c_str(), MOVEFILE_REPLACE_EXISTING);
  MoveFileExW(old1.c_str(), old2.c_str(), MOVEFILE_REPLACE_EXISTING);
  MoveFileExW(path_.c_str(), old1.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Logger::Write(LogLevel level, std::wstring_view message) {
  if (level == LogLevel::Debug && !debugEnabled_) return;
  std::scoped_lock lock(mutex_);
  if (path_.empty()) return;
  RotateIfNeeded();
  FILE* file = nullptr;
  if (_wfopen_s(&file, path_.c_str(), L"a+, ccs=UTF-8") != 0 || !file) return;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const wchar_t* label = level == LogLevel::Debug ? L"DEBUG" :
                         level == LogLevel::Warning ? L"WARN" : L"ERROR";
  fwprintf(file, L"%04u-%02u-%02u %02u:%02u:%02u [%s] %.*s\n",
           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
           label, static_cast<int>(message.size()), message.data());
  fclose(file);
}

std::wstring Logger::LogDirectory() const { return directory_; }
void LogDebug(std::wstring_view message) { Logger::Instance().Write(LogLevel::Debug, message); }
void LogWarning(std::wstring_view message) { Logger::Instance().Write(LogLevel::Warning, message); }
void LogError(std::wstring_view message) { Logger::Instance().Write(LogLevel::Error, message); }

}  // namespace monitor

