@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Visual Studio Build Tools with C++ and Windows SDK are required.
    exit /b 1
  )
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do call "%%I" >nul
  where cl >nul 2>&1 || exit /b 1
)
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /utf-8 "..\station_hub.cpp" "..\station_widget.cpp" "..\station_discovery.cpp" "..\secure_pairing.cpp" "..\yandex_like.cpp" /Fe:StationHub-next.exe /link /SUBSYSTEM:WINDOWS ole32.lib oleaut32.lib uuid.lib ws2_32.lib iphlpapi.lib advapi32.lib user32.lib gdi32.lib gdiplus.lib winhttp.lib comctl32.lib windowsapp.lib uiautomationcore.lib dwmapi.lib shell32.lib
set result=%errorlevel%
popd
exit /b %result%
