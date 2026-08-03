cmake_minimum_required(VERSION 3.25)
project(ChatGPTCodexUsageMonitor VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "This project targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_library(monitor_core STATIC
  src/common.cpp
  src/json.cpp
  src/rate_limits.cpp
  src/energy_indicator_state.cpp
  src/hud_controls.cpp
  src/settings.cpp
  src/logging.cpp
  src/chatgpt_locator.cpp
  src/chatgpt_launcher.cpp
  src/chatgpt_lifecycle.cpp
  src/codex_app_server.cpp
)
target_include_directories(monitor_core PUBLIC include)
target_compile_definitions(monitor_core PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
)
target_compile_options(monitor_core PRIVATE
  /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
  $<$<CONFIG:Release>:/O2 /GL /Gy /Gw>
)
target_link_libraries(monitor_core PUBLIC
  ole32 runtimeobject shell32 shlwapi advapi32 user32 dwmapi iphlpapi windowsapp
)

add_executable(ChatGPTCodexUsageMonitor WIN32
  src/main.cpp
  src/overlay_window.cpp
  src/energy_indicator_renderer.cpp
  src/tray_icon.cpp
  resources/app.rc
)
set_property(SOURCE resources/app.rc APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/usage_monitor_icon.ico"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_master_red.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_blue_lens_donor.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_reference.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_warning_on.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_warning_off.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_stone.png"
)
target_link_libraries(ChatGPTCodexUsageMonitor PRIVATE
  monitor_core d2d1 dwrite gdi32 comctl32 windowscodecs
)
target_compile_options(ChatGPTCodexUsageMonitor PRIVATE
  /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus
  $<$<CONFIG:Release>:/O2 /GL /Gy /Gw>
)
target_link_options(ChatGPTCodexUsageMonitor PRIVATE
  $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO>
  /SUBSYSTEM:WINDOWS,6.01
  "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest"
)

add_executable(monitor_tests tests/test_main.cpp)
target_link_libraries(monitor_tests PRIVATE monitor_core psapi)
target_compile_options(monitor_tests PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)

add_executable(fake_codex_app_server tests/fake_codex_app_server.cpp)
target_link_libraries(fake_codex_app_server PRIVATE monitor_core)
target_compile_options(fake_codex_app_server PRIVATE /W4 /permissive- /EHsc /utf-8 /Zc:__cplusplus)

enable_testing()
add_test(NAME monitor_unit_and_lifecycle_tests COMMAND monitor_tests)
add_test(NAME fake_app_server_protocol
  COMMAND fake_codex_app_server --self-test
)
add_test(NAME app_server_integration
  COMMAND monitor_tests --integration $<TARGET_FILE:fake_codex_app_server>
)
add_test(NAME theme_asset_alignment
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify-theme-assets.ps1"
    -MasterRedPath "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_master_red.png"
    -SourcePath "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_reference.png"
    -WarningOnPath "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_warning_on.png"
    -WarningOffPath "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_warning_off.png"
    -StonePath "${CMAKE_CURRENT_SOURCE_DIR}/resources/tiga_chest_stone.png"
)

install(TARGETS ChatGPTCodexUsageMonitor RUNTIME DESTINATION .)
