#pragma once

#include "rate_limits.h"

namespace monitor {

enum class AppServerErrorKind {
  CliMissing,
  LaunchFailed,
  NotLoggedIn,
  Unauthorized,
  RateLimited,
  Network,
  Server,
  Timeout,
  Protocol,
  Closed,
};

struct AppServerError {
  AppServerErrorKind kind = AppServerErrorKind::Protocol;
  std::wstring message;
};

using SnapshotCallback = std::function<void(RateLimitSnapshot)>;
using ServerErrorCallback = std::function<void(AppServerError)>;

class CodexAppServer {
 public:
  CodexAppServer();
  ~CodexAppServer();
  CodexAppServer(const CodexAppServer&) = delete;
  CodexAppServer& operator=(const CodexAppServer&) = delete;

  bool Start(SnapshotCallback snapshotCallback, ServerErrorCallback errorCallback);
  void Stop();
  bool RequestRateLimits();
  bool RequestAccount();
  void MarkRequestTimedOut();
  bool IsRunning() const;
  DWORD ProcessId() const { return process_ ? GetProcessId(process_) : 0; }
  bool RequestInFlight() const { return requestInFlight_.load(); }
  std::wstring ExecutablePath() const { return executablePath_; }

  static std::optional<std::wstring> LocateCodexExecutable();

 private:
  static DWORD WINAPI ReaderThreadEntry(void* context);
  DWORD ReaderLoop();
  bool WriteLine(std::string_view line);
  void HandleLine(std::string_view line);
  void ReportError(AppServerErrorKind kind, std::wstring message);

  HANDLE process_ = nullptr;
  HANDLE processThread_ = nullptr;
  HANDLE stdinWrite_ = nullptr;
  HANDLE stdoutRead_ = nullptr;
  HANDLE stderrRead_ = nullptr;
  HANDLE readerThread_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> requestInFlight_{false};
  std::atomic<uint64_t> nextRequestId_{10};
  std::atomic<uint64_t> pendingRateLimitId_{0};
  std::mutex writeMutex_;
  SnapshotCallback snapshotCallback_;
  ServerErrorCallback errorCallback_;
  std::wstring executablePath_;
};

}  // namespace monitor
