#include "codex_app_server.h"
#include "json.h"
#include "logging.h"
#include <array>
#include <cwctype>
#include <filesystem>

namespace monitor {
namespace {

std::wstring EnvironmentValue(const wchar_t* name) {
  const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
  if (!needed) return {};
  std::wstring value(needed, L'\0');
  const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
  if (!written) return {};
  value.resize(written);
  return value;
}

bool ExistingFile(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring ExecutableDirectory() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) return {};
  std::filesystem::path executable(std::wstring(path.data(), length));
  return executable.parent_path().wstring();
}

std::wstring ErrorText(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessageW(flags, nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                 reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::wstring text = buffer ? buffer : L"Unknown error";
  if (buffer) LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
    text.pop_back();
  }
  return text;
}

std::optional<double> JsonNumberField(const JsonValue& value, std::string_view key) {
  const JsonValue* item = value.Find(key);
  return item ? item->AsNumber() : std::nullopt;
}

std::string JsonStringField(const JsonValue& value, std::string_view key) {
  const JsonValue* item = value.Find(key);
  return item && item->AsString() ? *item->AsString() : std::string();
}

}  // namespace

CodexAppServer::CodexAppServer() = default;
CodexAppServer::~CodexAppServer() { Stop(); }

std::optional<std::wstring> CodexAppServer::LocateCodexExecutable() {
  const std::wstring configured = EnvironmentValue(L"CODEX_EXECUTABLE");
  if (!configured.empty() && ExistingFile(configured)) return configured;

  const std::wstring bundled = JoinPath(ExecutableDirectory(), L"codex.exe");
  if (ExistingFile(bundled)) return bundled;

  wchar_t found[32768]{};
  if (SearchPathW(nullptr, L"codex.exe", nullptr, static_cast<DWORD>(std::size(found)),
                  found, nullptr) > 0) {
    return std::wstring(found);
  }

  const std::wstring userProfile = EnvironmentValue(L"USERPROFILE");
  const std::wstring localAppData = GetLocalAppDataDirectory();
  const std::vector<std::wstring> candidates = {
      JoinPath(userProfile, L".codex\\bin\\codex.exe"),
      JoinPath(localAppData, L"Programs\\Codex\\codex.exe"),
      JoinPath(localAppData, L"Microsoft\\WinGet\\Links\\codex.exe"),
  };
  for (const auto& candidate : candidates) if (ExistingFile(candidate)) return candidate;
  return std::nullopt;
}

bool CodexAppServer::Start(SnapshotCallback snapshotCallback,
                           ServerErrorCallback errorCallback) {
  Stop();
  snapshotCallback_ = std::move(snapshotCallback);
  errorCallback_ = std::move(errorCallback);
  auto executable = LocateCodexExecutable();
  if (!executable) {
    ReportError(AppServerErrorKind::CliMissing, L"No executable official Codex CLI was found. Install it and sign in to Codex CLI.");
    return false;
  }
  executablePath_ = *executable;

  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE stdinRead = nullptr;
  HANDLE stdoutWrite = nullptr;
  if (!CreatePipe(&stdinRead, &stdinWrite_, &security, 0) ||
      !CreatePipe(&stdoutRead_, &stdoutWrite, &security, 0)) {
    SafeCloseHandle(stdinRead);
    SafeCloseHandle(stdinWrite_);
    SafeCloseHandle(stdoutRead_);
    SafeCloseHandle(stdoutWrite);
    ReportError(AppServerErrorKind::LaunchFailed, L"Unable to create the Codex App Server communication pipes.");
    return false;
  }
  SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = stdinRead;
  startup.hStdOutput = stdoutWrite;
  startup.hStdError = stdoutWrite;
  PROCESS_INFORMATION processInfo{};
  std::wstring command = L"\"" + executablePath_ + L"\" app-server";
  BOOL launched = CreateProcessW(executablePath_.c_str(), command.data(), nullptr, nullptr,
                                 TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                 nullptr, nullptr, &startup, &processInfo);
  DWORD launchError = GetLastError();
  if (!launched && launchError == ERROR_ACCESS_DENIED) {
    std::wstring lowerPath = executablePath_;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), towlower);
    if (lowerPath.find(L"\\windowsapps\\") != std::wstring::npos) {
      std::wstring aliasCommand = L"codex.exe app-server";
      launched = CreateProcessW(nullptr, aliasCommand.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                nullptr, nullptr, &startup, &processInfo);
      launchError = GetLastError();
    }
  }
  SafeCloseHandle(stdinRead);
  SafeCloseHandle(stdoutWrite);
  if (!launched) {
    SafeCloseHandle(stdinWrite_);
    SafeCloseHandle(stdoutRead_);
    ReportError(AppServerErrorKind::LaunchFailed,
                L"Unable to launch Codex CLI: " + ErrorText(launchError));
    return false;
  }
  process_ = processInfo.hProcess;
  processThread_ = processInfo.hThread;
  stopping_.store(false);
  readerThread_ = CreateThread(nullptr, 0, ReaderThreadEntry, this, 0, nullptr);
  if (!readerThread_) {
    Stop();
    ReportError(AppServerErrorKind::LaunchFailed, L"Unable to start the App Server reader thread.");
    return false;
  }

  const std::string initialize =
      "{\"method\":\"initialize\",\"id\":1,\"params\":{\"clientInfo\":{"
      "\"name\":\"chatgpt_codex_usage_monitor\","
      "\"title\":\"ChatGPT Codex Usage Monitor\",\"version\":\"1.0.6\"}}}";
  if (!WriteLine(initialize) || !WriteLine("{\"method\":\"initialized\",\"params\":{}}")) {
    ReportError(AppServerErrorKind::Protocol, L"Unable to send the App Server initialization message.");
    Stop();
    return false;
  }
  RequestAccount();
  RequestRateLimits();
  return true;
}

void CodexAppServer::Stop() {
  stopping_.store(true);
  SafeCloseHandle(stdinWrite_);
  if (process_) {
    DWORD wait = WaitForSingleObject(process_, 1500);
    if (wait == WAIT_TIMEOUT) {
      TerminateProcess(process_, 0);
      WaitForSingleObject(process_, 1000);
    }
  }
  SafeCloseHandle(stdoutRead_);
  if (readerThread_) {
    WaitForSingleObject(readerThread_, 1500);
    SafeCloseHandle(readerThread_);
  }
  SafeCloseHandle(processThread_);
  SafeCloseHandle(process_);
  requestInFlight_.store(false);
  pendingRateLimitId_.store(0);
}

bool CodexAppServer::RequestRateLimits() {
  if (!IsRunning()) return false;
  bool expected = false;
  if (!requestInFlight_.compare_exchange_strong(expected, true)) return false;
  const uint64_t id = nextRequestId_.fetch_add(1);
  pendingRateLimitId_.store(id);
  const std::string line = "{\"method\":\"account/rateLimits/read\",\"id\":" +
                           std::to_string(id) + "}";
  if (!WriteLine(line)) {
    requestInFlight_.store(false);
    pendingRateLimitId_.store(0);
    return false;
  }
  return true;
}

bool CodexAppServer::RequestAccount() {
  if (!IsRunning()) return false;
  const uint64_t id = nextRequestId_.fetch_add(1);
  return WriteLine("{\"method\":\"account/read\",\"id\":" + std::to_string(id) +
                   ",\"params\":{\"refreshToken\":false}}");
}

void CodexAppServer::MarkRequestTimedOut() {
  requestInFlight_.store(false);
  pendingRateLimitId_.store(0);
}

bool CodexAppServer::IsRunning() const {
  if (!process_) return false;
  DWORD code = 0;
  return GetExitCodeProcess(process_, &code) && code == STILL_ACTIVE;
}

bool CodexAppServer::WriteLine(std::string_view line) {
  std::scoped_lock lock(writeMutex_);
  if (!stdinWrite_) return false;
  std::string framed(line);
  framed.push_back('\n');
  size_t offset = 0;
  while (offset < framed.size()) {
    DWORD written = 0;
    if (!WriteFile(stdinWrite_, framed.data() + offset,
                   static_cast<DWORD>(framed.size() - offset), &written, nullptr) || !written) {
      return false;
    }
    offset += written;
  }
  return true;
}

DWORD WINAPI CodexAppServer::ReaderThreadEntry(void* context) {
  return static_cast<CodexAppServer*>(context)->ReaderLoop();
}

DWORD CodexAppServer::ReaderLoop() {
  std::string pending;
  std::array<char, 8192> buffer{};
  while (!stopping_.load() && stdoutRead_) {
    DWORD read = 0;
    if (!ReadFile(stdoutRead_, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || !read) break;
    pending.append(buffer.data(), read);
    while (true) {
      const size_t newline = pending.find('\n');
      if (newline == std::string::npos) break;
      std::string line = pending.substr(0, newline);
      pending.erase(0, newline + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) HandleLine(line);
    }
    if (pending.size() > 4 * 1024 * 1024) {
      pending.clear();
      ReportError(AppServerErrorKind::Protocol, L"An App Server message exceeded the safe size limit.");
    }
  }
  if (!stopping_.load()) {
    requestInFlight_.store(false);
    ReportError(AppServerErrorKind::Closed, L"The Codex App Server connection has closed.");
  }
  return 0;
}

void CodexAppServer::HandleLine(std::string_view line) {
  if (line.empty() || line.front() != '{') {
    LogDebug(L"Codex App Server emitted a non-protocol diagnostic line.");
    return;
  }
  const JsonParseResult envelope = ParseJson(line);
  if (!envelope || !envelope.value.IsObject()) {
    LogWarning(L"Ignored malformed JSONL from Codex App Server.");
    return;
  }
  const std::string method = JsonStringField(envelope.value, "method");
  uint64_t id = 0;
  if (auto numeric = JsonNumberField(envelope.value, "id")) id = static_cast<uint64_t>(*numeric);
  const bool isPendingRateResponse = id && id == pendingRateLimitId_.load();

  if (const JsonValue* rpcError = envelope.value.Find("error")) {
    if (isPendingRateResponse) {
      requestInFlight_.store(false);
      pendingRateLimitId_.store(0);
    }
    const std::string message = JsonStringField(*rpcError, "message");
    const int code = JsonNumberField(*rpcError, "code").has_value()
                         ? static_cast<int>(*JsonNumberField(*rpcError, "code")) : 0;
    const std::wstring wide = Utf8ToWide(message.empty() ? "App Server error" : message);
    const std::wstring lower = [&]() {
      std::wstring value = wide;
      std::transform(value.begin(), value.end(), value.begin(), towlower);
      return value;
    }();
    if (code == 401 || code == 403 || lower.find(L"unauthorized") != std::wstring::npos) {
      ReportError(AppServerErrorKind::Unauthorized, L"Codex is not signed in, or the login has expired.");
    } else if (code == 429 || lower.find(L"rate") != std::wstring::npos) {
      ReportError(AppServerErrorKind::RateLimited, L"The quota interface is temporarily rate limited (429).");
    } else if (lower.find(L"network") != std::wstring::npos ||
               lower.find(L"offline") != std::wstring::npos ||
               lower.find(L"connect") != std::wstring::npos ||
               lower.find(L"dns") != std::wstring::npos) {
      ReportError(AppServerErrorKind::Network, L"The network is unavailable; the Codex quota cannot be updated.");
    } else {
      ReportError(AppServerErrorKind::Server, L"The quota interface returned an error: " + wide);
    }
    return;
  }

  if (method == "account/updated") {
    const JsonValue* params = envelope.value.Find("params");
    if (params) {
      const JsonValue* auth = params->Find("authMode");
      if (auth && auth->IsNull()) ReportError(AppServerErrorKind::NotLoggedIn, L"Codex is not signed in.");
    }
    return;
  }

  if (method == "account/rateLimits/updated" || isPendingRateResponse) {
    if (isPendingRateResponse) {
      requestInFlight_.store(false);
      pendingRateLimitId_.store(0);
    }
    std::string parseError;
    auto snapshot = ParseRateLimitMessage(line, &parseError);
    if (snapshot) {
      if (snapshotCallback_) snapshotCallback_(std::move(*snapshot));
    } else {
      ReportError(AppServerErrorKind::Protocol,
                  L"The official interface currently provides no displayable quota data: " + Utf8ToWide(parseError));
    }
    return;
  }

  const JsonValue* result = envelope.value.Find("result");
  if (result && result->IsObject() && result->Find("requiresOpenaiAuth")) {
    const JsonValue* account = result->Find("account");
    const auto needsAuth = result->Find("requiresOpenaiAuth")->AsBool();
    if (account && account->IsNull() && needsAuth && *needsAuth) {
      ReportError(AppServerErrorKind::NotLoggedIn, L"Codex is not signed in.");
    }
  }
}

void CodexAppServer::ReportError(AppServerErrorKind kind, std::wstring message) {
  if (kind != AppServerErrorKind::CliMissing) LogWarning(message);
  if (errorCallback_) errorCallback_({kind, std::move(message)});
}

}  // namespace monitor
