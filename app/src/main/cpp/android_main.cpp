// CasioEmuNeo - Android 入口
// 单窗口双模式：计算器（竖屏）<-> 调试（横屏），左上角半透明按钮切换
#include <SDL.h>
#include <SDL_system.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "imgui.h"
#include "Emulator.hpp"
#include "Config/Config.hpp"
#include "Gui/Ui.hpp"
#include "Gui/imgui_impl_sdl2.h"
#include "Gui/imgui_impl_sdlrenderer2.h"
#include "Data/EventCode.hpp"
#include "Logger.hpp"

using namespace casioemu;

// ================= JNI 交互 =================
static std::mutex g_model_mutex;
static std::string g_imported_model;   // Java 导入完成的模型名
static bool g_import_flag = false;

extern "C" JNIEXPORT void JNICALL Java_com_casioemu_neo_CasioActivity_onModelsImported(JNIEnv *env, jobject, jstring path)
{
	const char *s = path ? env->GetStringUTFChars(path, nullptr) : nullptr;
	{
		std::lock_guard<std::mutex> lk(g_model_mutex);
		g_imported_model = s ? s : "";
		g_import_flag = true;
	}
	if (s)
		env->ReleaseStringUTFChars(path, s);
}

static void JniSetOrientation(int portrait)
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	if (!env)
		return;
	jclass cls = env->FindClass("com/casioemu/neo/CasioActivity");
	if (!cls)
		return;
	jmethodID mid = env->GetStaticMethodID(cls, "setOrientation", "(I)V");
	if (mid)
		env->CallStaticVoidMethod(cls, mid, portrait);
	env->DeleteLocalRef(cls);
}

void JniShowModelPicker()
{
	JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	if (!env)
		return;
	jclass cls = env->FindClass("com/casioemu/neo/CasioActivity");
	if (!cls)
		return;
	jmethodID mid = env->GetStaticMethodID(cls, "showModelPicker", "()V");
	if (mid)
		env->CallStaticVoidMethod(cls, mid);
	env->DeleteLocalRef(cls);
}

// ================= 文件/模型工具 =================
static bool FileExists(const std::string &p)
{
	return access(p.c_str(), F_OK) == 0;
}

static std::string ExtractRomPathFromModelLua(const std::string &model_lua_path)
{
	std::ifstream f(model_lua_path);
	std::string line;
	while (std::getline(f, line))
	{
		size_t p = line.find("rom_path");
		if (p == std::string::npos)
			continue;
		size_t q1 = line.find('"', p);
		if (q1 == std::string::npos)
			continue;
		size_t q2 = line.find('"', q1 + 1);
		if (q2 != std::string::npos)
			return line.substr(q1 + 1, q2 - q1 - 1);
	}
	return "rom.bin";
}

// 返回空串 = 有效，否则为错误信息
static std::string ValidateModel(const std::string &dir)
{
	if (!FileExists(dir + "/model.lua"))
		return "缺少 model.lua";
	std::string rom_rel = ExtractRomPathFromModelLua(dir + "/model.lua");
	if (!FileExists(dir + "/" + rom_rel))
		return "缺少 ROM 文件: " + rom_rel;
	return "";
}

static std::vector<std::string> ListModels(const std::string &base)
{
	std::vector<std::string> out;
	DIR *d = opendir(base.c_str());
	if (!d)
		return out;
	struct dirent *e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.')
			continue;
		std::string p = base + "/" + e->d_name;
		struct stat st;
		if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && ValidateModel(p).empty())
			out.push_back(e->d_name);
	}
	closedir(d);
	std::sort(out.begin(), out.end());
	return out;
}

// ================= 全局 UI 状态 =================
std::atomic<bool> g_switch_request(false);
static int g_mode = 0; // 0=计算器(竖屏), 1=调试(横屏)
static bool g_force_frame = false;

static bool InButtonArea(int x, int y)
{
	return x >= 8 && x <= 104 && y >= 8 && y <= 56;
}

static bool InModelButtonArea(int x, int y)
{
	int w = (int)ImGui::GetIO().DisplaySize.x;
	return x >= w - 104 && x <= w - 8 && y >= 8 && y <= 56;
}

// ================= ImGui 初始化 =================
static void InitImGui(SDL_Window *window, SDL_Renderer *renderer)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigDockingWithShift = true;

	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
	ImVector<ImWchar> ranges_vec;
	builder.BuildRanges(&ranges_vec);

	auto font_path = EmuGloConfig.GetUsableFontPath();
	if (io.Fonts->AddFontFromFileTTF(font_path.c_str(), 15.0f, nullptr, ranges_vec.Data) == nullptr)
		io.Fonts->AddFontDefault();
	io.Fonts->Build();

	ImGui::StyleColorsDark();
	// 触屏友好：滚动条和拖拽手柄加宽
	ImGuiStyle &style = ImGui::GetStyle();
	style.ScrollbarSize = 26.0f;
	style.GrabMinSize = 24.0f;
	ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
	ImGui_ImplSDLRenderer2_Init(renderer);
}

// ================= 模型选择引导 =================
// 返回选中的模型目录（绝对路径）
static std::string ChooseModel(const std::string &files, SDL_Renderer *renderer)
{
	std::string models_dir = files + "/models";

	// 如果 config 里已有有效模型，直接用（后续启动不再弹选择）
	std::string configured = EmuGloConfig.GetModulePath();
	if (!configured.empty() && configured.find("models/") != std::string::npos &&
		ValidateModel(configured).empty())
		return configured;

	ImGuiIO &io = ImGui::GetIO();
	bool picked = false;
	std::string result;
	bool request_picker = false;
	bool show_status = false;
	std::string status_text;

	while (!picked)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			ImGui_ImplSDL2_ProcessEvent(&ev);
			if (ev.type == SDL_QUIT)
				exit(0);
		}

		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
		ImGui::Begin("##model_picker", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

		ImGui::Text("CasioEmuNeo - 选择模型");
		ImGui::Separator();
		ImGui::Spacing();

		auto models = ListModels(models_dir);
		if (models.empty())
			ImGui::TextWrapped("尚未发现可用模型。\n请导入模型目录（需包含 model.lua 和 ROM 文件）。");
		else
			ImGui::Text("发现 %d 个模型：", (int)models.size());

		for (auto &m : models)
		{
			std::string label = "  使用  " + m;
			if (ImGui::Button(label.c_str(), ImVec2(io.DisplaySize.x - 40, 0)))
			{
				result = models_dir + "/" + m;
				picked = true;
			}
			ImGui::Spacing();
		}

		ImGui::Separator();
		ImGui::Spacing();
		if (ImGui::Button("  导入模型目录…", ImVec2(io.DisplaySize.x - 40, 0)))
			request_picker = true;
		ImGui::Spacing();
		if (ImGui::Button("  退出", ImVec2(io.DisplaySize.x - 40, 0)))
			exit(0);

		if (show_status)
			ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "%s", status_text.c_str());

		ImGui::End();
		ImGui::Render();

		SDL_SetRenderDrawColor(renderer, 25, 25, 30, 255);
		SDL_RenderClear(renderer);
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
		SDL_RenderPresent(renderer);

		if (request_picker)
		{
			request_picker = false;
			JniShowModelPicker();
		}

		// 检查 Java 导入结果
		{
			std::lock_guard<std::mutex> lk(g_model_mutex);
			if (g_import_flag)
			{
				g_import_flag = false;
				std::string name = g_imported_model;
				if (!name.empty())
				{
					std::string dir = models_dir + "/" + name;
					std::string err = ValidateModel(dir);
					if (err.empty())
						status_text = "导入成功：" + name;
					else
						status_text = "导入的目录无效：" + err;
				}
				else
					status_text = "未选择目录";
				show_status = true;
			}
		}

		SDL_Delay(16);
	}

	// 记住选择
	EmuGloConfig.SetModulePath(result);
	return result;
}

// ================= 主入口 =================
extern "C" int SDL_main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	// 1. 内部存储路径 + config
	logger::Info("SDL_main start\n");
	const char *files_c = SDL_AndroidGetInternalStoragePath();
	if (!files_c)
		return 1;
	std::string files = files_c;
	logger::Info("files=%s\n", files.c_str());
	chdir(files.c_str());
	EmuGloConfigPtr = new EmuConfig((files + "/config.ini").c_str());

	// 2. SDL 初始化
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
	{
		logger::Info("SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	logger::Info("SDL_Init ok\n");

	// 3. 主窗口（Android 全屏；竖屏尺寸避免 SDL 请求横屏导致 surface 重建）
	SDL_Window *window = SDL_CreateWindow("CasioEmuNeo",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		720, 1280, SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (!window)
	{
		logger::Info("SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
	{
		logger::Info("SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}
	// 等待窗口尺寸就绪（surface 刚创建时可能为 -1）
	for (int wait_i = 0; wait_i < 200; wait_i++) {
		int ww = 0, wh = 0;
		SDL_GetWindowSize(window, &ww, &wh);
		if (ww > 0 && wh > 0)
			break;
		SDL_Delay(50);
		SDL_PumpEvents();
	}
	{
		int ww = 0, wh = 0;
		SDL_GetWindowSize(window, &ww, &wh);
		logger::Info("window size=%dx%d\n", ww, wh);
	}
	logger::Info("window/renderer ok\n");

	// 4. ImGui（引导界面 + 调试界面共用）
	InitImGui(window, renderer);
	logger::Info("ImGui init done, display=%.0fx%.0f\n", ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);

	// 5. 模型选择（初次启动/无有效模型时）
	logger::Info("ChooseModel begin\n");
	std::string model_dir = ChooseModel(files, renderer);
	logger::Info("ChooseModel picked: %s\n", model_dir.c_str());

	// 6. Emulator 复用窗口
	Emulator::pre_window = window;
	Emulator::pre_renderer = renderer;

	// 主循环外层：导入新模型后自动重启模拟器切换
	bool restart_request = false;
	while (true)
	{
		{
			std::map<std::string, std::string> argv_map;
			argv_map["model"] = model_dir;
			argv_map["script"] = files + "/lua-common.lua";
			argv_map["resizable"] = "1";

			Emulator emulator(argv_map);
			DebugUi ui;
			g_mode = 0;
			g_force_frame = true;
			JniSetOrientation(1); // 计算器模式：竖屏
			logger::Info("main loop begin\n");
			unsigned long loop_count = 0;

			while (emulator.Running())
			{
				// ---- 渲染 ----
			if (g_mode == 0)
			{
				if (g_force_frame)
				{
					emulator.Frame();
					g_force_frame = false;
				}
				// 每帧完整重绘：黑底 + 计算器持久帧 + 左上角按钮。
				// 不能只依赖 CE_FRAME_REQUEST 后重绘再 present——Android EGL swapBuffers
				// 之后 back buffer 内容未定义，空 present 会翻出残留/垃圾导致闪烁。
				SDL_RenderSetScale(renderer, 1.0f, 1.0f);
				SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
				SDL_RenderClear(renderer);
				if (emulator.GetFrameTexture())
				{
					SDL_Rect vp = emulator.GetViewport();
					SDL_RenderCopy(renderer, emulator.GetFrameTexture(), nullptr, &vp);
				}
				ImGui_ImplSDLRenderer2_NewFrame();
				ImGui_ImplSDL2_NewFrame();
				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(8, 8));
				ImGui::SetNextWindowSize(ImVec2(96, 48));
				ImGui::Begin("##switch_btn", nullptr,
					ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.45f));
				ImGui::Button("调试", ImVec2(88, 40));
				ImGui::PopStyleColor();
				ImGui::End();
				// 右上角：模型选择
				ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 104, 8));
				ImGui::SetNextWindowSize(ImVec2(96, 48));
				ImGui::Begin("##model_btn", nullptr,
					ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.45f));
				ImGui::Button("模型", ImVec2(88, 40));
				ImGui::PopStyleColor();
				ImGui::End();
				ImGui::Render();
				ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
				SDL_RenderPresent(renderer);
			}
			else
			{
				ui.PaintUi();  // 调试界面，含"计算器"切换按钮
				ui.PaintSDL(); // 配对 present，完成 need_paint 握手；否则空 present 会翻出旧 buffer（模型选择界面残留）导致闪烁
			}

			// ---- 事件 ----
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				// 切换按钮（触摸/鼠标点击左上角）
				if (event.type == SDL_MOUSEBUTTONDOWN && InButtonArea(event.button.x, event.button.y))
				{
					g_switch_request = true;
					continue;
				}
				if (event.type == SDL_MOUSEBUTTONUP && InButtonArea(event.button.x, event.button.y))
				{
					// 吃掉抬起事件：一次触摸产生 down+up，down 已触发切换；
					// 若 up 再命中（调试界面"计算器"按钮也在同位置），会 0->1->0 瞬间闪回
					continue;
				}
				// 右上角模型按钮（仅计算器模式手动拦截：该模式下事件走 emulator.UIEvent，
				// 不会到 ImGui；调试模式事件全给 ImGui，按钮由 ImGui 处理）
				if (g_mode == 0)
				{
					if (event.type == SDL_MOUSEBUTTONDOWN && InModelButtonArea(event.button.x, event.button.y))
					{
						JniShowModelPicker();
						continue;
					}
					if (event.type == SDL_MOUSEBUTTONUP && InModelButtonArea(event.button.x, event.button.y))
						continue;
				}

				switch (event.type)
				{
				case SDL_QUIT:
					emulator.Shutdown();
					break;

				case SDL_USEREVENT:
					if (event.user.code == CE_FRAME_REQUEST)
					{
						if (g_mode == 0)
						{
							emulator.Frame();
							g_force_frame = false;
						}
					}
					break;

				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_RESIZED)
						emulator.WindowResize(event.window.data1, event.window.data2);
					break;

				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
				case SDL_MOUSEMOTION:
				case SDL_MOUSEWHEEL:
				case SDL_KEYDOWN:
				case SDL_KEYUP:
				case SDL_TEXTINPUT:
					if (g_mode == 0)
						emulator.UIEvent(event);
					else
						ImGui_ImplSDL2_ProcessEvent(&event);
					break;

				default:
					ImGui_ImplSDL2_ProcessEvent(&event);
					break;
				}
			}

			// ---- 模式切换 ----
			if (g_switch_request.exchange(false))
			{
				g_mode = g_mode == 0 ? 1 : 0;
				if (g_mode == 0)
					JniSetOrientation(1); // 竖屏
				else
					JniSetOrientation(0); // 横屏
				g_force_frame = true;
				emulator.WindowResize(0, 0);
			}

			// ---- 导入模型检测（选择器导入完成后自动重启切换） ----
			{
				std::lock_guard<std::mutex> lk(g_model_mutex);
				if (g_import_flag)
				{
					g_import_flag = false;
					std::string name = g_imported_model;
					if (!name.empty())
					{
						std::string dir = files + "/models/" + name;
						if (ValidateModel(dir).empty())
						{
							EmuGloConfig.SetModulePath(dir);
							model_dir = dir;
							restart_request = true;
							emulator.Shutdown();
						}
					}
				}
			}

			// ---- 输入法：ImGui 输入框聚焦时唤起/收起软键盘 ----
			{
				ImGuiIO &io = ImGui::GetIO();
				if (io.WantTextInput && !SDL_IsTextInputActive())
					SDL_StartTextInput();
				else if (!io.WantTextInput && SDL_IsTextInputActive())
					SDL_StopTextInput();
			}

			SDL_Delay(1);
			if ((++loop_count % 300) == 0)
				logger::Info("main loop alive, mode=%d\n", g_mode);
		}
		}
		if (!restart_request)
			break;
		restart_request = false;
		logger::Info("restarting emulator with model: %s\n", model_dir.c_str());
	}

	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
