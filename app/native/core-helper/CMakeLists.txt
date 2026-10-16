cmake_minimum_required(VERSION 3.25)
project(TigaMonitorCore LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(legacy_monitor_core STATIC
  upstream/src/json.cpp
  upstream/src/rate_limits.cpp
  upstream/src/energy_indicator_state.cpp)
target_include_directories(legacy_monitor_core PUBLIC upstream/include)
target_compile_definitions(legacy_monitor_core PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(legacy_monitor_core PRIVATE /W4 /permissive- /EHsc /utf-8)
add_executable(tiga-core-helper main.cpp)
set_target_properties(tiga-core-helper PROPERTIES OUTPUT_NAME "monitor-core"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../../runtime")
target_link_libraries(tiga-core-helper PRIVATE legacy_monitor_core)
target_compile_options(tiga-core-helper PRIVATE /W4 /permissive- /EHsc /utf-8)
add_executable(legacy_core_tests tests/legacy_core_tests.cpp)
target_link_libraries(legacy_core_tests PRIVATE legacy_monitor_core)
target_compile_options(legacy_core_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
enable_testing()
add_test(NAME legacy_rate_limits_and_energy COMMAND legacy_core_tests)
