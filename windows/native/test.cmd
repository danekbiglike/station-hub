@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 (
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do call "%%I" >nul
  where cl >nul 2>&1 || exit /b 1
)
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /std:c++20 /EHsc /W4 /utf-8 "..\..\..\tests\test_volume_intent.cpp" /Fe:test-volume.exe || exit /b 1
test-volume.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 "..\..\..\tests\test_widget_layout.cpp" /Fe:test-layout.exe || exit /b 1
test-layout.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /DNOMINMAX /utf-8 "..\..\..\tests\test_widget_art.cpp" /Fe:test-art.exe /link gdiplus.lib gdi32.lib || exit /b 1
test-art.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 "..\..\..\tests\test_mdns.cpp" /Fe:test-mdns.exe || exit /b 1
test-mdns.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /utf-8 "..\..\..\tests\discovery_probe.cpp" "..\station_discovery.cpp" /Fe:discovery-probe.exe /link iphlpapi.lib ws2_32.lib || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /utf-8 "..\..\..\tests\fake_adb.cpp" /Fe:fake-adb.exe || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /utf-8 "..\..\..\tests\test_secure_pairing.cpp" "..\secure_pairing.cpp" /Fe:test-pairing.exe /link advapi32.lib ws2_32.lib || exit /b 1
test-pairing.exe || exit /b 1
set result=0
popd
exit /b %result%
