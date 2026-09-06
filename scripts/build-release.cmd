@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0.."
for %%I in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fI"
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "DIST_DIR=%PROJECT_ROOT%\dist"
set "CMAKE_EXE="
set "CTEST_EXE="
set "GENERATOR="

for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
)

if defined VSROOT (
  call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
  set "GENERATOR=Visual Studio 17 2022"
) else if exist "%PROJECT_ROOT%\work\toolchain\msvc\setup_x64.bat" (
  call "%PROJECT_ROOT%\work\toolchain\msvc\setup_x64.bat"
  set "GENERATOR=NMake Makefiles"
  if not defined CMAKE_EXE if exist "%PROJECT_ROOT%\work\toolchain\cmake-4.3.3-windows-x86_64\bin\cmake.exe" set "CMAKE_EXE=%PROJECT_ROOT%\work\toolchain\cmake-4.3.3-windows-x86_64\bin\cmake.exe"
) else (
  echo ERROR: Visual Studio 2022 Build Tools with Desktop C++ workload was not found.
  exit /b 1
)

if not defined CMAKE_EXE (
  echo ERROR: CMake was not found.
  exit /b 1
)
for %%I in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpIctest.exe"

powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\scripts\generate-theme-variants.ps1"
if errorlevel 1 exit /b 1

if "%GENERATOR%"=="Visual Studio 17 2022" (
  "%CMAKE_EXE%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64
) else (
  "%CMAKE_EXE%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b 1

"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -C Release --output-on-failure
if errorlevel 1 exit /b 1

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set "BUILT_EXE=%BUILD_DIR%\Release\ChatGPTCodexUsageMonitor.exe"
if not exist "%BUILT_EXE%" set "BUILT_EXE=%BUILD_DIR%\ChatGPTCodexUsageMonitor.exe"
copy /y "%BUILT_EXE%" "%DIST_DIR%\ChatGPTCodexUsageMonitor.exe" >nul
copy /y "%PROJECT_ROOT%\scripts\install-shortcut.ps1" "%DIST_DIR%\install-shortcut.ps1" >nul
copy /y "%PROJECT_ROOT%\scripts\uninstall-shortcut.ps1" "%DIST_DIR%\uninstall-shortcut.ps1" >nul
copy /y "%PROJECT_ROOT%\README.md" "%DIST_DIR%\README.txt" >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_ROOT%\scripts\fetch-official-codex-cli.ps1" -DestinationDirectory "%DIST_DIR%"
if errorlevel 1 exit /b 1

echo.
echo Release build and tests succeeded.
echo Output: %DIST_DIR%\ChatGPTCodexUsageMonitor.exe
exit /b 0
