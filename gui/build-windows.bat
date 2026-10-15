@echo off
rem Builds dwc-patcher-gui.exe with Visual Studio (2022 or newer) and Ninja.
rem Run it from anywhere; the result is gui\build\dwc-patcher-gui.exe.
setlocal
set "HERE=%~dp0"

set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere.exe was not found: install Visual Studio 2022 or newer.
  exit /b 1
)
rem "call" keeps cmd from stripping the quotes around the path.
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio with the "Desktop development with C++" workload was not found.
  exit /b 1
)

rem vcvars64.bat looks for vswhere.exe on the PATH.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cmake -S "%HERE%." -B "%HERE%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%HERE%build" || exit /b 1
echo.
echo Built %HERE%build\dwc-patcher-gui.exe
