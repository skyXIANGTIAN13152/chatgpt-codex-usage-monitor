#include "json.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string Scenario(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--scenario") return argv[i + 1];
  }
  char* environment = nullptr;
  size_t length = 0;
  _dupenv_s(&environment, &length, "FAKE_CODEX_SCENARIO");
  std::string value = environment ? environment : "normal";
  std::free(environment);
  return value;
}

void Send(std::string_view line) {
  std::cout << line << '\n' << std::flush;
}

uint64_t RequestId(const monitor::JsonValue& message) {
  const monitor::JsonValue* id = message.Find("id");
  return id && id->AsNumber() ? static_cast<uint64_t>(*id->AsNumber()) : 0;
}

std::string Method(const monitor::JsonValue& message) {
  const monitor::JsonValue* method = message.Find("method");
  return method && method->AsString() ? *method->AsString() : std::string();
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--self-test") return 0;
  const std::string scenario = Scenario(argc, argv);
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto parsed = monitor::ParseJson(line);
    if (!parsed || !parsed.value.IsObject()) continue;
    const std::string method = Method(parsed.value);
    const uint64_t id = RequestId(parsed.value);
    if (method == "initialize") {
      Send("{\"id\":" + std::to_string(id) +
           ",\"result\":{\"userAgent\":\"fake/1.0\",\"platformFamily\":\"windows\",\"platformOs\":\"windows\"}}");
    } else if (method == "account/read") {
      if (scenario == "signed-out") {
        Send("{\"id\":" + std::to_string(id) +
             ",\"result\":{\"account\":null,\"requiresOpenaiAuth\":true}}");
        continue;
      }
      Send("{\"id\":" + std::to_string(id) +
           ",\"result\":{\"account\":{\"type\":\"chatgpt\"},\"requiresOpenaiAuth\":true}}");
    } else if (method == "account/rateLimits/read") {
      if (scenario == "timeout") continue;
      if (scenario == "notification-timeout") {
        Send("{\"method\":\"account/rateLimits/updated\",\"params\":{\"rateLimits\":{"
             "\"limitId\":\"codex\",\"primary\":{\"usedPercent\":78,"
             "\"windowDurationMins\":300,\"resetsAt\":1893456000}}}}");
        continue;
      }
      if (scenario == "close") return 0;
      if (scenario == "401" || scenario == "signed-out") {
        Send("{\"id\":" + std::to_string(id) +
             ",\"error\":{\"code\":401,\"message\":\"unauthorized\"}}");
        continue;
      }
      if (scenario == "429") {
        Send("{\"id\":" + std::to_string(id) +
             ",\"error\":{\"code\":429,\"message\":\"rate limited\"}}");
        continue;
      }
      const int used = scenario == "zero" ? 100 : 45;
      if (scenario == "double") {
        Send("{\"id\":" + std::to_string(id) +
             ",\"result\":{\"rateLimitsByLimitId\":{"
             "\"codex\":{\"limitId\":\"codex\",\"primary\":{\"usedPercent\":45,\"windowDurationMins\":300,\"resetsAt\":1893456000},"
             "\"secondary\":{\"usedPercent\":70,\"windowDurationMins\":10080,\"resetsAt\":1893542400}}}}}");
      } else if (scenario == "pro-weekly") {
        Send("{\"id\":" + std::to_string(id) +
             R"(,"result":{"rateLimitsByLimitId":{)"
             R"("codex":{"planType":"prolite","primary":{"usedPercent":2,"windowDurationMins":10080,"resetsAt":1893542400},"secondary":null},)"
             R"("codex_bengalfox":{"primary":{"usedPercent":0,"windowDurationMins":300},"secondary":{"usedPercent":0,"windowDurationMins":10080}}}}})");
      } else {
        std::string extras;
        if (scenario == "unlimited") extras = ",\"credits\":{\"unlimited\":true}";
        if (scenario == "credits") extras = ",\"credits\":{\"hasCredits\":true,\"balance\":12.5}";
        Send("{\"id\":" + std::to_string(id) +
             ",\"result\":{\"rateLimits\":{\"limitId\":\"codex\",\"planType\":\"plus\","
             "\"primary\":{\"usedPercent\":" + std::to_string(used) +
             ",\"windowDurationMins\":300,\"resetsAt\":1893456000},\"secondary\":null}" + extras + "}}");
      }
      if (scenario == "notification") {
        Send("{\"method\":\"account/rateLimits/updated\",\"params\":{\"rateLimits\":{"
             "\"limitId\":\"codex\",\"primary\":{\"usedPercent\":46,"
             "\"windowDurationMins\":300,\"resetsAt\":1893456000}}}}");
      }
    }
  }
  return 0;
}
