@echo off
setlocal
set "TIGA_CMAKE=cmake"
set "TIGA_CTEST=ctest"
rem Normally run from an MSVC x64 developer command prompt with CMake on PATH.
rem Optional legacy bundle: msvc\setup_x64.bat and cmake-*-windows-x86_64\bin.
if defined TIGA_TOOLCHAIN_ROOT (
  if not exist "%TIGA_TOOLCHAIN_ROOT%\msvc\setup_x64.bat" (
    echo TIGA_TOOLCHAIN_ROOT must contain msvc\setup_x64.bat. 1>&2
    exit /b 1
  )
  call "%TIGA_TOOLCHAIN_ROOT%\msvc\setup_x64.bat"
  if errorlevel 1 exit /b 1
  for /d %%D in ("%TIGA_TOOLCHAIN_ROOT%\cmake-*-windows-x86_64") do (
    if exist "%%~fD\bin\cmake.exe" set "TIGA_CMAKE=%%~fD\bin\cmake.exe"
    if exist "%%~fD\bin\ctest.exe" set "TIGA_CTEST=%%~fD\bin\ctest.exe"
  )
)
where cl >nul 2>&1
if errorlevel 1 (
  echo Open an MSVC x64 developer command prompt before running this script. 1>&2
  exit /b 1
)
if defined VSCMD_ARG_TGT_ARCH if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo An MSVC x64 target environment is required. 1>&2
  exit /b 1
)
where nmake >nul 2>&1
if errorlevel 1 (
  echo NMake was not found in the MSVC developer environment. 1>&2
  exit /b 1
)
"%TIGA_CMAKE%" --version >nul 2>&1
if errorlevel 1 (
  echo CMake 3.25 or newer must be available on PATH or in TIGA_TOOLCHAIN_ROOT. 1>&2
  exit /b 1
)
"%TIGA_CTEST%" --version >nul 2>&1
if errorlevel 1 (
  echo CTest must be available with the CMake installation. 1>&2
  exit /b 1
)
"%TIGA_CMAKE%" -S "%~dp0." -B "%~dp0build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
"%TIGA_CMAKE%" --build "%~dp0build"
if errorlevel 1 exit /b 1
"%TIGA_CTEST%" --test-dir "%~dp0build" --output-on-failure
exit /b %errorlevel%
