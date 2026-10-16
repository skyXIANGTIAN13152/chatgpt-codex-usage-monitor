#include "watcher_core.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
int main() {
  using namespace tiga::launch;
  const auto both = parse_settings(R"({"launchWithCodex":true,"launchWithClaude":true,"other":42})");
  require(both && both->launch_with_codex && both->launch_with_claude, "Both explicit settings are accepted");
  const auto strings = parse_settings(R"({"launchWithCodex":"true","launchWithClaude":1})");
  require(strings && !strings->launch_with_codex && !strings->launch_with_claude, "Only real booleans enable monitoring");
  require(!parse_settings("{\"launchWithCodex\":"), "Partial settings do not enable monitoring");
  require(!parse_settings("true"), "Settings must be an object");
  require(parse_settings("\xEF\xBB\xBF{}").has_value(), "UTF8 BOM accepted");

  constexpr auto root = L"C:\\Apps\\Tiga Monitor";
  require(!classify_process(L"c:/apps/TIGA MONITOR/runtime/codex.exe", root), "Bundled Codex must not recursively launch the monitor");
  require(!classify_process(L"C:\\Apps\\Tiga Monitor\\native\\claude\\claude.exe", root), "Bundled Claude is excluded");
  require(!classify_process(L"\\\\?\\C:\\Apps\\Tiga Monitor\\runtime\\codex.exe", root), "Extended Windows paths are excluded");
  require(!classify_process(L"C:\\Apps\\Tiga Monitor\\runtime\\..\\Codex.exe", root), "Parent components cannot bypass install exclusion");
  require(classify_process(L"C:\\Apps\\Tiga Monitor-other\\Codex.exe", root) == Provider::codex, "Directory exclusion uses a boundary, not a text prefix");
  require(classify_process(L"C:\\External\\ChatGPT.exe", root) == Provider::codex, "External ChatGPT recognized");
  require(classify_process(L"D:\\Claude\\CLAUDE.EXE", root) == Provider::claude, "External Claude recognized case-insensitively");
  require(!classify_process(L"", root), "Unknown paths fail closed");
  require(!classify_process(L"C:\\External\\my-codex.exe", root), "Only exact process names recognized");

  RisingEdges edges;
  require(edges.update(*both, {true, false}) == std::vector{Provider::codex}, "Already-running provider launches on login startup");
  require(edges.update(*both, {true, false}).empty(), "Continued presence never repeatedly launches");
  require(edges.update(*both, {true, true}) == std::vector{Provider::claude}, "Second provider has an independent edge");
  require(edges.update(*both, {false, true}).empty(), "Closing provider never starts or closes the monitor");
  require(edges.update(*both, {true, true}) == std::vector{Provider::codex}, "Provider restart launches again");
  require(edges.update({false, false}, {true, true}).empty(), "Disabled switches prevent launches");
  require(edges.update({false, true}, {true, true}) == std::vector{Provider::claude}, "Re-enabled provider can start from existing session");

  RisingEdges simultaneous;
  require(simultaneous.update(*both, {true, true}) == std::vector{Provider::codex, Provider::claude},
          "Simultaneous starts retain both independent edges in deterministic trigger order");
  require(simultaneous.update(*both, {true, true}).empty(),
          "Coalescing the first wake must still consume both provider edges");
  require(simultaneous.update(*both, {true, false}).empty(),
          "Closing one simultaneous provider does not create another wake");
  require(simultaneous.update(*both, {true, true}) == std::vector{Provider::claude},
          "The other provider can independently restart after a coalesced wake");

  require(quote_argument(L"C:\\应用 空间\\monitor.exe") == L"\"C:\\应用 空间\\monitor.exe\"", "Unicode/space paths quoted");
  require(quote_argument(L"C:\\trailing\\") == L"\"C:\\trailing\\\\\"", "Trailing slash quoted correctly");
  std::cout << "Launch watcher settings, exclusion, edge, and argument tests passed\n";
}
