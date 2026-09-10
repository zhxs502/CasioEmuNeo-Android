@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem ==============================================================
rem  CasioEmuNeo-Android  -  build native libs + APK
rem
rem  Usage:
rem    build_apk.bat            -> debug APK
rem    build_apk.bat release    -> release APK (uses keystore.properties
rem                                if present, otherwise debug signing)
rem
rem  Overridable via environment:
rem    JAVA_HOME / ANDROID_SDK_ROOT / ANDROID_NDK_HOME
rem    CMAKE_EXE / NINJA_EXE / GRADLE_EXE / STRIP_EXE
rem  Requirements: JDK 17, Android SDK (compileSdk 34),
rem                Android NDK r23 (23.2.8568313), CMake >= 3.13, Ninja
rem ==============================================================

set "ABIS=arm64-v8a armeabi-v7a"
set "PLATFORM=android-19"
set "STL=c++_static"
set "BUILDTYPE=Release"

set "GRADLE_TASK=assembleDebug"
set "APK_HINT=debug"
if /i "%~1"=="release" (
  set "GRADLE_TASK=assembleRelease"
  set "APK_HINT=release"
)

rem ---------- 1. toolchain discovery ----------
if not defined JAVA_HOME (
  for %%D in ("D:\apps\jdk-17" "C:\Program Files\Java\jdk-17" "C:\Program Files\Eclipse Adoptium\jdk-17*") do (
    if exist %%D if not defined JAVA_HOME set "JAVA_HOME=%%~D"
  )
)
if not defined ANDROID_SDK_ROOT (
  for %%D in ("D:\apps\Android\sdk" "%LOCALAPPDATA%\Android\Sdk" "%USERPROFILE%\AppData\Local\Android\Sdk") do (
    if exist "%%~D\platform-tools" if not defined ANDROID_SDK_ROOT set "ANDROID_SDK_ROOT=%%~D"
  )
)
if not defined ANDROID_NDK_HOME (
  for %%D in ("D:\apps\ndk-23.2.8568313" "%LOCALAPPDATA%\Android\Sdk\ndk\23.2.8568313") do (
    if exist "%%~D\build\cmake\android.toolchain.cmake" if not defined ANDROID_NDK_HOME set "ANDROID_NDK_HOME=%%~D"
  )
)
if not defined CMAKE_EXE set "CMAKE_EXE=cmake"
if not defined NINJA_EXE (
  where ninja >nul 2>nul
  if not errorlevel 1 set "NINJA_EXE=ninja"
)
if not defined NINJA_EXE if defined ANDROID_SDK_ROOT (
  for /d %%V in ("%ANDROID_SDK_ROOT%\cmake\*") do (
    if exist "%%~V\bin\ninja.exe" if not defined NINJA_EXE set "NINJA_EXE=%%~V\bin\ninja.exe"
  )
)
if not defined STRIP_EXE (
  for %%V in ("%ANDROID_NDK_HOME%\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe") do (
    if exist %%V set "STRIP_EXE=%%~V"
  )
)

rem ---------- 2. sanity checks ----------
if not defined ANDROID_NDK_HOME (
  echo [ERROR] Android NDK not found. Set ANDROID_NDK_HOME to NDK r23 ^(23.2.8568313^).
  exit /b 1
)
if not exist "%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake" (
  echo [ERROR] Invalid NDK: %ANDROID_NDK_HOME%
  exit /b 1
)
if not defined ANDROID_SDK_ROOT (
  echo [ERROR] Android SDK not found. Set ANDROID_SDK_ROOT.
  exit /b 1
)
if not defined NINJA_EXE (
  echo [ERROR] ninja not found. Set NINJA_EXE or put ninja on PATH.
  exit /b 1
)

echo [env] JAVA_HOME         = %JAVA_HOME%
echo [env] ANDROID_SDK_ROOT  = %ANDROID_SDK_ROOT%
echo [env] ANDROID_NDK_HOME  = %ANDROID_NDK_HOME%
echo [env] CMAKE_EXE         = %CMAKE_EXE%
echo [env] NINJA_EXE         = %NINJA_EXE%
echo [env] STRIP_EXE         = %STRIP_EXE%
echo [env] gradle task       = %GRADLE_TASK%
echo.

rem ---------- 3. local.properties (sdk.dir, machine-local) ----------
set "SDK_ESC=%ANDROID_SDK_ROOT:\=\\%"
> "local.properties" echo sdk.dir=%SDK_ESC%

rem ---------- 4. native: configure (if needed) + build, per ABI ----------
for %%A in (%ABIS%) do (
  echo === [native] %%A ===
  if not exist "build\%%A\CMakeCache.txt" (
    echo --- configure build\%%A ---
    "%CMAKE_EXE%" -G Ninja -S app\src\main\cpp -B build\%%A ^
      -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" ^
      -DCMAKE_BUILD_TYPE=%BUILDTYPE% ^
      -DANDROID_ABI=%%A ^
      -DANDROID_PLATFORM=%PLATFORM% ^
      -DANDROID_STL=%STL% ^
      -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake"
    if errorlevel 1 (
      echo [ERROR] cmake configure failed for %%A
      exit /b 1
    )
  ) else (
    echo --- reusing existing build\%%A ^(delete it to reconfigure^) ---
  )
  "%CMAKE_EXE%" --build build\%%A -- -j 8
  if errorlevel 1 (
    echo [ERROR] native build failed for %%A
    exit /b 1
  )
  if not exist "build\%%A\libmain.so" (
    echo [ERROR] libmain.so not produced for %%A
    exit /b 1
  )
  if not exist "app\libs\%%A" mkdir "app\libs\%%A"
  copy /y "build\%%A\libmain.so" "app\libs\%%A\libmain.so" >nul
  rem Strip symbols in the packaged copy (~18MB -> ~5MB). The unstripped
  rem library stays in build\%%A\ for crash symbolication.
  if defined STRIP_EXE (
    "%STRIP_EXE%" --strip-unneeded "app\libs\%%A\libmain.so"
    if errorlevel 1 echo [WARN] strip failed for %%A, packaging unstripped library
  )
)

rem ---------- 5. gradle ----------
echo.
echo === [apk] gradle %GRADLE_TASK% ===
if exist gradlew.bat (
  call gradlew.bat %GRADLE_TASK% --no-daemon
) else if defined GRADLE_EXE (
  call "%GRADLE_EXE%" %GRADLE_TASK% --no-daemon
) else (
  where gradle >nul 2>nul
  if errorlevel 1 (
    echo [ERROR] gradle not found. Add a gradle wrapper or set GRADLE_EXE.
    exit /b 1
  )
  call gradle %GRADLE_TASK% --no-daemon
)
if errorlevel 1 (
  echo [ERROR] gradle build failed
  exit /b 1
)

echo.
if /i "%APK_HINT%"=="release" (
  echo [OK] APK: app\build\outputs\apk\release\app-release.apk
) else (
  echo [OK] APK: app\build\outputs\apk\debug\app-debug.apk
)
endlocal
