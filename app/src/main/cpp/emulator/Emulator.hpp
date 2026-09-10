#pragma once
#include "Config.hpp"

#include <string>
#include <map>
#include <SDL.h>
#ifndef __ANDROID__
#include <SDL_image.h>
#endif
#include <lua.hpp>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <mutex>
#include "Data/HardwareId.hpp"
#include "Data/ModelInfo.hpp"
#include "Data/SpriteInfo.hpp"

#define MEM_EDIT_BASE_ADDR 0xD000
#define MEM_EDIT_MEM_SIZE 0x2800
#define LABEL_INPUT_BUF 0xd180

namespace casioemu
{
	class Chipset;
	class CPU;
	class MMU;

	/**
	 * A mutex that ensures that a thread cannot get the mutex right after it's released if there are another waiting thread.
	 */
	class FairRecursiveMutex
	{
		std::mutex m;
		std::thread::id holding;
		int recursive_count;
		std::queue<std::condition_variable> waiting;

	public:
		FairRecursiveMutex();
		~FairRecursiveMutex();
		void lock();
		void unlock();
	};

	class Emulator
	{
		
		SDL_Renderer *renderer;
		SDL_Texture *interface_texture;
		unsigned int timer_interval;
		bool running, paused;
		unsigned int last_frame_tick_count;
		std::string model_path;
		bool pause_on_mem_error;

		std::thread *tick_thread;

		SpriteInfo interface_background;
		int width, height;
#ifdef __ANDROID__
		int viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
		// 持久帧纹理：Android 上计算器画面离屏渲染到这里，主循环每帧完整重绘屏幕，避免 EGL back buffer 残留闪烁
		SDL_Texture *frame_tex = nullptr;
		int frame_tex_w = 0, frame_tex_h = 0;
#endif

		/**
		 * A bunch of internally used methods for encapsulation purposes.
		 */
		void LoadModelDefition();
		void TimerCallback();
		void SetupLuaAPI();
		void SetupInternals();
		void RunStartupScript();

	public:
		SDL_Window *window;
#ifdef __ANDROID__
		static SDL_Window *pre_window;
		static SDL_Renderer *pre_renderer;
#endif
		Emulator(std::map<std::string, std::string> &argv_map, bool paused = false);
		~Emulator();

		FairRecursiveMutex access_mx;
		lua_State *lua_state;
		int lua_model_ref, lua_pre_tick_ref, lua_post_tick_ref;
		HardwareId hardware_id;
		std::map<std::string, std::string> &argv_map;

	private:
		/**
		 * The cycle manager structure. This structure is reset every time the
		 * emulator starts emulating CPU cycles and in every timer callback
		 * it's queried for the number of cycles that need to be emulated in the
		 * callback. This ensures that only as many cycles are emulated in a period
		 * of time as many would be in real life.
		 *
		 * Note that it's assumed that the GetDelta function is called once every
		 * timer_interval milliseconds. It's up to the timer to make sure that
		 * there's no drift.
		 */
		struct Cycles
		{
			void Setup(Uint64 cycles_per_second, unsigned int timer_interval);
			void Reset();
			Uint64 GetDelta();
			Uint64 ticks_now, cycles_emulated, cycles_per_second;
			unsigned int timer_interval;
		} cycles;

	public:
		/**
		 * A reference to the emulator chipset. This object holds all CPU, MMU, memory and
		 * peripheral state. The emulator interfaces with the chipset by issuing interrupts
		 * and rendering the screen buffer. It may also read internal state for testing purposes.
		 */
		Chipset &chipset;

		static Emulator *instance;

		bool Running();
		void HandleMemoryError();
		void Shutdown();
		void Tick();
		/**
		 * Called when SDL_WINDOWEVENT_EXPOSED event is received. Does not re-frame.
		 */
		void Repaint();
		void Frame();
		void WindowResize(int width, int height);
#ifdef __ANDROID__
		void RecalcViewport();
		SDL_Texture *GetFrameTexture();
		SDL_Rect GetViewport();
#endif
		void ExecuteCommand(std::string command);
		unsigned int GetCyclesPerSecond();
		bool GetPaused();
		void SetPaused(bool paused);
		void UIEvent(SDL_Event &event);
		SDL_Renderer *GetRenderer();
		SDL_Texture *GetInterfaceTexture();
		ModelInfo GetModelInfo(std::string key);
		std::string GetModelFilePath(std::string relative_path);

        /**
         * We make the UI resizable by default
         * You can also specify a negative config parameter to disable this: resizable=0
         */
        bool IsResizable();

		friend class ModelInfo;
		friend class CPU;
		friend class MMU;
	};
}
