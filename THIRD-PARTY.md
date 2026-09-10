# 第三方组件与许可

本工程是 [qiufuyu123/CasioEmuNeo](https://github.com/qiufuyu123/CasioEmuNeo)（GPL-3.0）的
Android 移植，整体以 **GPL-3.0** 发布，见根目录 `LICENSE`。

`app/src/main/cpp/` 下随源码分发的第三方组件：

| 组件 | 位置 | 许可 | 说明 |
|---|---|---|---|
| CasioEmuNeo 模拟器核心 | `emulator/` | GPL-3.0 | 上游源码副本，未改动逻辑 |
| SDL 2.30.0 | `src/`、`include/`、`SDL2/` | zlib | 裁剪为 Android 可构建子集 |
| Dear ImGui 1.90.5 | `imgui/` | MIT | 含 `imgui_impl_sdl2` / `imgui_impl_sdlrenderer2` |
| Lua 5.3.0 | `lua/` | MIT | 已排除 `lua.c` / `luac.c` |
| stb_image | `stb_image.h`、`stb_image_wrap.c` | Public Domain / MIT | |

随 APK 分发的资源文件：

| 文件 | 许可 | 说明 |
|---|---|---|
| `assets/unifont.otf` | GPL-2.0+ 附字体嵌入例外（GNU Unifont） | 中日韩字形字体，可随程序分发 |

## 明确不包含

- **卡西欧原厂 ROM**（`rom.bin`）：版权归卡西欧所有，本仓库不含任何 ROM，请自行依法备份自有设备。
- **ROM 反汇编导出**（`_disas.txt`）：ROM 的派生数据，同样不提供，需自行用上游 `disas` 工具生成。
- `third_party/` 下的第三方完整源码包与上游 Windows 预编译产物：体积过大且不参与本工程构建，未纳入版本控制。
