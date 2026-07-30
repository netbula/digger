@echo off
REM  Digger -- a from-scratch remake of the 1983 arcade game.
REM  Copyright (C) 2026 netbula.  Licensed under the GNU General Public License
REM  version 3 or later.  Comes with ABSOLUTELY NO WARRANTY.  See LICENSE.
REM ---------------------------------------------------------------
REM  Build DIGGER with whatever MSVC toolchain is installed.
REM    build.bat          compile
REM    build.bat run      compile then launch the game
REM    build.bat test     compile then run the headless self test
REM ---------------------------------------------------------------
setlocal enabledelayedexpansion
pushd "%~dp0"

set VCVARS=
for %%E in (Enterprise Professional Community BuildTools) do (
  for %%V in (18 2026 2022 17) do (
    if not defined VCVARS (
      set "C=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
      if exist "!C!" set "VCVARS=!C!"
    )
    if not defined VCVARS (
      set "C=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
      if exist "!C!" set "VCVARS=!C!"
    )
  )
)
if not defined VCVARS (
  echo Could not locate vcvars64.bat. Install Visual Studio with the
  echo "Desktop development with C++" workload, or set VCVARS by hand.
  popd
  exit /b 1
)

REM vcvars is chatty on stderr about vswhere; none of it matters here.
call "%VCVARS%" >nul 2>nul
where cl >nul 2>nul
if errorlevel 1 (
  echo cl.exe is still not on PATH after running vcvars64.bat.
  popd
  exit /b 1
)

if not exist build mkdir build
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /W3 /Fe:build\digger.exe /Fo:build\ digger.cpp ^
   user32.lib gdi32.lib winmm.lib comdlg32.lib ole32.lib ^
   mfplat.lib mfreadwrite.lib mfuuid.lib /link /SUBSYSTEM:WINDOWS
if errorlevel 1 (
  echo BUILD FAILED
  popd
  exit /b 1
)
echo BUILD OK  -^>  build\digger.exe

if /i "%~1"=="run"  start "" build\digger.exe
if /i "%~1"=="test" (
  build\digger.exe -test
  type selftest.txt
)
popd
exit /b 0
