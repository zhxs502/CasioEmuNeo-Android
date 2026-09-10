cmake_minimum_required(VERSION 3.10)
project(casioemu_android C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CPP ${CMAKE_CURRENT_SOURCE_DIR})

# ---------- 定位 Android NDK ----------
# NDK toolchain 文件会自行设置 ANDROID_NDK；此处兜底支持环境变量指定。
if(NOT ANDROID_NDK)
  if(DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
  elseif(DEFINED ENV{ANDROID_NDK_ROOT})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}")
  endif()
endif()
if(NOT EXISTS "${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c")
  message(FATAL_ERROR
    "Android NDK not found. Set the ANDROID_NDK_HOME environment variable, or pass "
    "-DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake")
endif()

include_directories(
    ${CPP}/include
    ${CPP}/src
    ${CPP}/lua
    ${CPP}/imgui
    ${CPP}/emulator
    ${CPP}
    ${ANDROID_NDK}/sources/android/cpufeatures
)

# ---------- SDL2 ----------
file(GLOB SDL_C
    ${CPP}/src/*.c
    ${CPP}/src/atomic/*.c
    ${CPP}/src/audio/*.c
    ${CPP}/src/audio/android/*.c
    ${CPP}/src/audio/dummy/*.c
    ${CPP}/src/audio/aaudio/*.c
    ${CPP}/src/audio/openslES/*.c
    ${CPP}/src/core/android/*.c
    ${CPP}/src/cpuinfo/*.c
    ${CPP}/src/dynapi/*.c
    ${CPP}/src/events/*.c
    ${CPP}/src/file/*.c
    ${CPP}/src/haptic/*.c
    ${CPP}/src/haptic/android/*.c
    ${CPP}/src/hidapi/*.c
    ${CPP}/src/joystick/*.c
    ${CPP}/src/joystick/android/*.c
    ${CPP}/src/joystick/hidapi/*.c
    ${CPP}/src/joystick/virtual/*.c
    ${CPP}/src/loadso/dlopen/*.c
    ${CPP}/src/locale/*.c
    ${CPP}/src/locale/android/*.c
    ${CPP}/src/misc/*.c
    ${CPP}/src/misc/android/*.c
    ${CPP}/src/power/*.c
    ${CPP}/src/power/android/*.c
    ${CPP}/src/filesystem/android/*.c
    ${CPP}/src/sensor/*.c
    ${CPP}/src/sensor/android/*.c
    ${CPP}/src/render/*.c
    ${CPP}/src/render/opengles/*.c
    ${CPP}/src/render/opengles2/*.c
    ${CPP}/src/render/software/*.c
    ${CPP}/src/stdlib/*.c
    ${CPP}/src/thread/*.c
    ${CPP}/src/thread/pthread/*.c
    ${CPP}/src/timer/*.c
    ${CPP}/src/timer/unix/*.c
    ${CPP}/src/video/*.c
    ${CPP}/src/video/android/*.c
    ${CPP}/src/video/yuv2rgb/*.c
    ${CPP}/src/main/android/*.c
)
file(GLOB SDL_CPP
    ${CPP}/src/hidapi/android/*.cpp
)
set(SDL_NDK_CPUFEATURES
    ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c
)

# ---------- Lua 5.3 ----------
file(GLOB LUA_C ${CPP}/lua/*.c)
list(REMOVE_ITEM LUA_C
    ${CPP}/lua/lua.c
    ${CPP}/lua/luac.c
)

# ---------- ImGui ----------
set(IMGUI_C
    ${CPP}/imgui/imgui.cpp
    ${CPP}/imgui/imgui_draw.cpp
    ${CPP}/imgui/imgui_tables.cpp
    ${CPP}/imgui/imgui_widgets.cpp
    ${CPP}/emulator/Gui/imgui_impl_sdl2.cpp
    ${CPP}/emulator/Gui/imgui_impl_sdlrenderer2.cpp
)

# ---------- Emulator（排除 PC 入口 casioemu.cpp，用 android_main.cpp） ----------
file(GLOB EMU_C
    ${CPP}/emulator/*.cpp
    ${CPP}/emulator/Chipset/*.cpp
    ${CPP}/emulator/Config/*.cpp
    ${CPP}/emulator/Data/*.cpp
    ${CPP}/emulator/Gui/*.cpp
    ${CPP}/emulator/Peripheral/*.cpp
)
list(REMOVE_ITEM EMU_C ${CPP}/emulator/casioemu.cpp)

# ---------- stb_image 实现 ----------
set(STB_C ${CPP}/stb_image_wrap.c)

# ---------- 可执行/库 ----------
add_library(main SHARED
    ${SDL_C}
    ${SDL_CPP}
    ${SDL_NDK_CPUFEATURES}
    ${LUA_C}
    ${IMGUI_C}
    ${EMU_C}
    ${STB_C}
    ${CPP}/android_main.cpp
)

target_compile_options(main PRIVATE
    -DGL_GLEXT_PROTOTYPES
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-c++11-narrowing
    -Wno-error=format-security
)

target_link_libraries(main
    log
    android
    GLESv1_CM
    GLESv2
    OpenSLES
    EGL
    dl
)
