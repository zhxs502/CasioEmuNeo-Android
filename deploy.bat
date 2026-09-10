@echo off
setlocal
cd /d "%~dp0"

rem ==============================================================
rem  CasioEmuNeo-Android  -  build, install and launch on device
rem  Overridable via environment: ADB (full path to adb.exe)
rem ==============================================================

if not defined ADB (
  for %%D in ("D:\apps\Android\sdk\platform-tools\adb.exe" "%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe" "%ANDROID_SDK_ROOT%\platform-tools\adb.exe") do (
    if exist %%D if not defined ADB set "ADB=%%~D"
  )
)
if not defined ADB (
  where adb >nul 2>nul
  if not errorlevel 1 set "ADB=adb"
)
if not defined ADB (
  echo [ERROR] adb not found. Set ADB to the full path of adb.exe.
  exit /b 1
)

call build_apk.bat
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)
echo [OK] build

"%ADB%" install -r app\build\outputs\apk\debug\app-debug.apk
if errorlevel 1 (
  echo [ERROR] install failed
  exit /b 1
)

"%ADB%" logcat -c
"%ADB%" shell am force-stop com.casioemu.neo
"%ADB%" shell am start -n com.casioemu.neo/.CasioActivity
ping -n 8 127.0.0.1 >nul
"%ADB%" logcat -d | findstr /i "CasioEmuNeo SDL libmain DEBUG"
endlocal
