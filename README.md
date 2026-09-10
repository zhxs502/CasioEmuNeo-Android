# CasioEmuNeo-Android

[中文](#中文) | [English](#english)

---

## 中文

[qiufuyu123/CasioEmuNeo](https://github.com/qiufuyu123/CasioEmuNeo) 的 **Android 移植** —— 卡西欧 ClassWizard 系列（fx-991CN X 等）计算器模拟器，支持 ROP 注入与调试。

模拟器核心来自上游，本工程提供 Android 前端、构建系统与打包流程。

许可证：**GPL-3.0**（见 [LICENSE](LICENSE)）。上游版权归 qiufuyu123 及各贡献者所有。

### 特性

- **单窗口双模式**：竖屏为计算器界面，横屏（调试模式）为完整 ImGui 调试器，左上角半透明按钮切换
- **触屏适配**：滚动条加宽至 26px、抓取区 24px，输入框聚焦时自动唤起软键盘
- **模型导入**：右上角「模型」按钮经 SAF（`ACTION_OPEN_DOCUMENT_TREE`）选择含 `model.lua` + ROM 的目录，自动复制到应用内部存储
- **纯 native 渲染**：SDL2（内置编译）+ SDL_Renderer + ImGui，无 Java UI 依赖
- 单指触摸映射为鼠标左键

### 目录结构

```
app/src/main/
├── AndroidManifest.xml
├── java/
│   ├── com/casioemu/neo/CasioActivity.java   # 移植新增：SAF 模型导入、屏幕方向、assets 解压
│   └── org/libsdl/app/                       # SDL2 官方 Java 壳
├── cpp/
│   ├── android_main.cpp                      # 移植新增：Android 入口（双模式 UI、JNI 桥接）
│   ├── CMakeLists.txt                        # 移植新增：native 构建描述
│   ├── emulator/                             # 上游模拟器核心（未改动逻辑）
│   ├── imgui/  lua/  include/                # 第三方（见 THIRD-PARTY.md）
│   └── src/  SDL2/                           # SDL 2.30.0（裁剪为 Android 可构建子集）
└── assets/
    ├── unifont.otf         # CJK 字形字体
    ├── lua-common.lua      # 模型脚本公共库
    └── models/<机型>/       # 模型目录（ROM 需自备）
```

### 不包含 ROM

由于版权原因，本仓库**不含任何卡西欧原厂 ROM**（上游同样如此）。请自行依法备份自有设备取得 `rom.bin`。

模型目录（放到设备内部存储 `files/models/<机型>/`）：

```
model.lua        必填，模型脚本（内含 rom_path）
rom.bin          必填，自备
interface.png    面板图
mem-spans.txt    内存映射
_disas.txt       可选，调试器反汇编视图用，由上游 disas 工具生成
```

三种放入方式：

1. **应用内**：右上角「模型」按钮 → 选择上述目录 → 自动复制到内部存储
2. **adb**：`adb push <目录> /data/data/com.casioemu.neo/files/models/<机型>/`
3. **编进 APK**：放到 `app/src/main/assets/models/<机型>/`（**注意别把 ROM 提交进版本库**）

### 构建

#### 依赖

| 组件 | 版本 | 备注 |
|---|---|---|
| JDK | 17 | |
| Android SDK | compileSdk 34 / build-tools 34.0.0 | |
| Android NDK | **r23 (23.2.8568313)** | 其他版本未验证 |
| CMake | ≥ 3.13 | 需支持 `-S` / `-B` |
| Ninja | 任意 | Windows 下 SDK 自带的即可 |

#### 环境变量

脚本会自动探测常见路径，也可显式指定：

```
ANDROID_SDK_ROOT   Android SDK 根目录
ANDROID_NDK_HOME   NDK r23 根目录
JAVA_HOME          JDK 17
CMAKE_EXE          cmake 可执行文件（默认取 PATH 中的 cmake）
NINJA_EXE          ninja 可执行文件（默认取 PATH 中的 ninja，或 SDK 自带）
GRADLE_EXE         gradle 可执行文件（默认 gradlew.bat，再退回 PATH 中的 gradle）
```

`local.properties` 里的 `sdk.dir` 由本机生成，**不要提交**（已在 `.gitignore` 中）。

#### 一键构建

```bat
build_apk.bat
:: 产物：app\build\outputs\apk\debug\app-debug.apk
```

流程：配置并编译 native（arm64-v8a + armeabi-v7a）→ 拷贝 `.so` 到 `app/libs/<ABI>/` → `gradle assembleDebug`。

增量编译时若已配置过，会跳过 `cmake` 配置步骤；改过 `CMakeLists.txt` 或换 SDK 后请删除 `build/` 重新配置。

#### 部署到设备

```bat
deploy.bat
:: 需先设 ADB 环境变量，或在脚本内改 ADB 路径
```

#### 手动构建 native

```bat
cmake -G Ninja -S app\src\main\cpp -B build\arm64-v8a ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DANDROID_ABI=arm64-v8a ^
  -DANDROID_PLATFORM=android-19 ^
  -DANDROID_STL=c++_static ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake
cmake --build build\arm64-v8a
```

`armeabi-v7a` 同理，改 `-DANDROID_ABI` 与 `-B` 目录。

### 与上游的差异

| | 上游 CasioEmuNeo | 本工程 |
|---|---|---|
| 构建系统 | xmake（桌面） | CMake + Gradle（Android） |
| 入口 | `emulator/casioemu.cpp`（SDL 桌面） | `android_main.cpp`（Android，未编译前者） |
| SDL2 | 系统动态库 | 源码内置静态编译 |
| 模型加载 | 命令行参数 | SAF 目录导入 / assets 预置 |
| 休眠/字体/DPI | 桌面 | 触屏与软键盘适配 |

模拟器核心（`emulator/` 目录）保持与上游一致，便于后续同步。

### 许可

GPL-3.0。第三方组件许可见 [THIRD-PARTY.md](THIRD-PARTY.md)。

---

## English

**Android port** of [qiufuyu123/CasioEmuNeo](https://github.com/qiufuyu123/CasioEmuNeo) — a Casio ClassWizard series (fx-991CN X etc.) calculator emulator with ROP injection and debugger.

The emulator core comes from upstream; this project adds the Android frontend, build system and packaging.

Licensed under **GPL-3.0** (see [LICENSE](LICENSE)). Upstream copyright belongs to qiufuyu123 and contributors.

### Features

- **Dual-mode single window**: portrait = calculator, landscape = full ImGui debugger; toggled by a translucent button
- **Touch tuning**: 26px scrollbars, 24px grab area, soft keyboard on text input focus
- **Model import** via SAF (`ACTION_OPEN_DOCUMENT_TREE`), copied into app private storage
- **Fully native rendering**: bundled SDL2 + SDL_Renderer + ImGui, no Java UI

### No ROM included

For copyright reasons this repository ships **no Casio ROM**. Dump your own device's `rom.bin` legally.

Model directory (`files/models/<model>/` on device):
`model.lua` (required), `rom.bin` (required, bring your own), `interface.png`, `mem-spans.txt`, `_disas.txt` (optional, for the disassembly view).

### Build

Requirements: JDK 17, Android SDK (compileSdk 34), **NDK r23 (23.2.8568313)**, CMake ≥ 3.13, Ninja.

```bat
build_apk.bat    :: -> app\build\outputs\apk\debug\app-debug.apk
deploy.bat       :: install & launch on a connected device
```

Override toolchain locations with `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME`, `JAVA_HOME`, `CMAKE_EXE`, `NINJA_EXE`, `GRADLE_EXE`.

### License

GPL-3.0. See [THIRD-PARTY.md](THIRD-PARTY.md) for bundled third-party components.
