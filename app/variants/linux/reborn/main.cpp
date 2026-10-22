#include <string_view>
#include <stdexcept>
#include <cstdlib>
#include "ufc_offline_memory.h"
#include "ufc3_init.h"
#include "fps_overlay.h"
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
void UfcAttachOverlayToGuestFrames(rex::ui::ImGuiDrawer*, rex::ui::Presenter*);
void UfcDetachOverlayFromGuestFrames();
// Window::presenter() is protected; read it through a derived-class member pointer.
struct UfcWindowPeek : rex::ui::Window {
  static rex::ui::Presenter* Get(rex::ui::Window* w) { return w ? (w->*(&UfcWindowPeek::presenter))() : nullptr; }
};
static rex::ui::Presenter* UfcWindowPresenter(rex::ui::Window* w) { return UfcWindowPeek::Get(w); }
#include "ufc_launcher_ui.h"
#include "ufc_library.h"
#include <imgui.h>
#include <rex/ui/window.h>
REXCVAR_DEFINE_BOOL(ufc_configurator, false, "UFC", "Open the UFC launcher configurator");
REXCVAR_DEFINE_BOOL(ufc_async_shaders, false, "UFC", "Launcher toggle: compile shaders in the background (experimental)");
#include <rex/ui/keybinds.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(ufc_cp_live_test_controls, false, "Tests", "Enable EXP-028 test-only hotkeys");
#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif
// Keep the existing process timer request; prior UFC measurements found no FPS benefit.
REXCVAR_DEFINE_BOOL(ufc_timer_resolution,true,"Display","Request 1 ms Windows timer resolution");

void InstallUfcCafDiagnostics();
void MountUfcLocalizedNames(rex::Runtime*,const std::filesystem::path&);
class Ufc3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ufc3App>(new Ufc3App(ctx, "UFC3-Reborn-static-v64", PPCImageConfig));
  }
 protected:
  bool SetupPresentation() override {
    const bool ok = rex::ReXApp::SetupPresentation();
    if (ok && fps_overlay_) UfcAttachOverlayToGuestFrames(imgui_drawer(), UfcWindowPresenter(window()));
    return ok;
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    window()->SetTitle("UFC3MONS");
    ufc::ApplyWindowIcon(window()->GetNativeWindowHandle());
    rex::ui::UnregisterBind("bind_settings");
    if (REXCVAR_GET(ufc_configurator)) {
      launcher_=ufc::CreateLauncher(drawer,immediate_drawer(),rex::filesystem::GetExecutableFolder());
      return;
    }
    fps_overlay_ = CreateUfcFpsOverlay(drawer);
    if (REXCVAR_GET(ufc_cp_live_test_controls)) {
      const auto bind = [](const char* key, const char* flag) {
        rex::ui::RegisterBind(std::string("bind_test_") + flag, key, "EXP-028 test only", [flag] {
          const bool value = rex::cvar::GetFlagByName(flag) != "true";
          rex::cvar::SetFlagByName(flag, value ? "true" : "false");
          REXLOG_INFO("[ufc-cp-test] {}={}", flag, value);
        });
      };
      bind("F8", "ufc_cp_fast_registers");
      bind("F9", "ufc_cp_verify");
      bind("F10", "ufc_cp_verify_break");
      bind("F11", "ufc_cp_profile");
    }
    rex::ui::RegisterBind("bind_ufc_fps", "F6", "Mostrar/ocultar FPS", [] {
      const auto visible = rex::cvar::GetFlagByName("ufc_show_fps");
      rex::cvar::SetFlagByName("ufc_show_fps", visible == "true" ? "false" : "true");
    });
  }
  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_ufc_fps");
    for (const auto* name : {"ufc_cp_fast_registers", "ufc_cp_verify", "ufc_cp_verify_break", "ufc_cp_profile"})
      rex::ui::UnregisterBind(std::string("bind_test_") + name);
    ufc::DestroyLauncher(launcher_);
    UfcDetachOverlayFromGuestFrames();
    fps_overlay_.reset();
  }
  std::unique_ptr<rex::ui::ImGuiDialog> fps_overlay_;
  std::shared_ptr<ufc::Launcher> launcher_;
  bool OnWindowCloseRequested() override { return !ufc::LauncherRunning(launcher_.get()); }
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& paths,std::function<void(rex::PathConfig)>) override {
    if(REXCVAR_GET(ufc_configurator)) return std::nullopt;
    return paths;
  }
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    if(!REXCVAR_GET(ufc_configurator))return;
    auto file=rex::filesystem::GetExecutableFolder()/"ui/Roboto-Medium.ttf";
    if(std::filesystem::is_regular_file(file)){atlas->Clear();atlas->AddFontFromFileTTF(ufc::U8(file).c_str(),16.f);}
  }
  void OnConfigureStyle(ImGuiStyle& style,rex::ui::Style&) override {
    if(!REXCVAR_GET(ufc_configurator))return;
    ImGui::StyleColorsDark(&style);style.FramePadding=ImVec2(10,7);style.ItemSpacing=ImVec2(10,10);style.Colors[ImGuiCol_TabHovered]=ImVec4(.62f,.13f,.20f,1);style.Colors[ImGuiCol_Tab]=ImVec4(.16f,.11f,.14f,1);style.Colors[ImGuiCol_Separator]=ImVec4(.25f,.18f,.21f,1);
    style.WindowRounding=6;style.FrameRounding=5;style.GrabRounding=4;
    style.Colors[ImGuiCol_WindowBg]=ImVec4(.075f,.078f,.095f,1);
    style.Colors[ImGuiCol_Button]=ImVec4(.51f,.10f,.16f,1);
    style.Colors[ImGuiCol_ButtonHovered]=ImVec4(.73f,.15f,.23f,1);
    style.Colors[ImGuiCol_ButtonActive]=ImVec4(.88f,.19f,.27f,1);
    style.Colors[ImGuiCol_TabSelected]=ImVec4(.48f,.10f,.16f,1);
    style.Colors[ImGuiCol_CheckMark]=ImVec4(.96f,.29f,.36f,1);
  }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto executable=rex::filesystem::GetExecutableFolder();
    // Cvars are UTF-8; Windows narrow fs::path would misread accented game directories.
    const auto data_root=rex::cvar::GetFlagByName("game_data_root");
    if(!data_root.empty())paths.game_data_root=ufc::Path(data_root);
    if(!rex::cvar::GetFlagByName("ufc_test_input_file").empty()) {
      paths.user_data_root=executable/"user"/"Reborn-static-r9";
      paths.cache_root=executable/"cache";paths.metadata_root=executable/"metadata";
      paths.config_path=executable/"ufc3.toml";paths.update_data_root.clear();return;
    }
    if(REXCVAR_GET(ufc_configurator)) {
      paths.user_data_root.clear();paths.cache_root.clear();paths.metadata_root.clear();
      paths.config_path.clear();paths.update_data_root.clear();
      auto log=std::filesystem::temp_directory_path()/"UFC-Reborn-Configurator.log";
      rex::cvar::SetFlagByName("log_file",ufc::U8(log));return;
    }
    const auto root=ufc::Storage(paths.game_data_root);
    paths.user_data_root=root/"user"/"Reborn-static-r9";
#ifdef _WIN32
    paths.cache_root=root/"cache"/"windows-vulkan";
#else
    paths.cache_root=root/"cache"/"linux-vulkan";
#endif
    paths.metadata_root=root/"metadata";paths.update_data_root.clear();
    paths.config_path=root/"config"/"ufc3.toml";
    std::filesystem::create_directories(root/"config");
  }
  void OnPostLoadXexImage() override {ApplyUfcOfflineMemory(runtime()->memory());MountUfcLocalizedNames(runtime(),std::filesystem::u8path(rex::cvar::GetFlagByName("game_data_root")));}
  // EXP-023: effective options of the Reborn Xenia reference
  // (xenia-offline.config.toml + 5451087D.config.toml) that change what is drawn
  // or executed. Without gpu_allow_invalid_fetch_constants the SDK skips textures.
  // Most of these cvars live in the GPU plugin DLL, which the SDK only loads in
  // SetupPresentation right after OnPreSetup; setting them earlier did nothing
  // (v25 log still had the "invalid" texture warnings). So load the plugin here
  // (the SDK skips its own load when config.graphics is set) and then apply.
  static void ApplyRebornOptions() {
    rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
    rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true");
    rex::cvar::SetFlagByName("non_seamless_cube_map", "true");
    rex::cvar::SetFlagByName("present_safe_area_x", "100");
    rex::cvar::SetFlagByName("present_safe_area_y", "100");
    rex::cvar::SetFlagByName("clear_memory_page_state", "false");
    rex::cvar::SetFlagByName("ignore_thread_priorities", "false");
  }
  bool SetupEnvironment() override {
    if (!rex::ReXApp::SetupEnvironment()) return false;
    ApplyRebornOptions();
#ifdef _WIN32
    if (REXCVAR_GET(ufc_timer_resolution)) {
      const MMRESULT result = timeBeginPeriod(1);
      REXLOG_INFO("[ufc-timer] timeBeginPeriod(1) result={}", unsigned(result));
    } else {
      REXLOG_INFO("[ufc-timer] disabled (ufc_timer_resolution=false)");
    }
#endif
    return true;
  }
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics && !config.gpu_plugin.empty())
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
    InstallUfcCafDiagnostics();
    // Item 12: the launcher toggle decides; off (default) keeps every draw.
    // Baseline backend may omit a one-shot texture-bake draw while compiling.
    // Keep synchronous draws until a preserving backend is validated for UFC.
    rex::cvar::SetFlagByName("async_shader_compilation","false");
    REXLOG_INFO("[ufc-fidelity] async_requested={} async_effective=false reason=baseline_can_omit_texture_bake_draw",REXCVAR_GET(ufc_async_shaders));
    ApplyRebornOptions();
    // Linux build: the Linux GPU plugin (v57) has no ht_direct_resolve path, so the
    // cvar is not registered and direct resolve is off by construction. Only
    // enforce the value when the cvar exists (Windows behaviour unchanged).
    if(rex::cvar::GetFlagInfo("ht_direct_resolve")) {
      if(!rex::cvar::SetFlagByName("ht_direct_resolve","false") ||
         rex::cvar::GetFlagByName("ht_direct_resolve")!="false")
        throw std::runtime_error("Cannot disable the rejected direct-resolve path");
    } else {
      REXLOG_INFO("[ufc-linux] ht_direct_resolve not present in this GPU plugin: direct resolve off");
    }
    // R3: the SDK's double-buffered memexport readback copies the PREVIOUS draw's
    // result into guest memory (zeros the first time). UFC compresses generated
    // fighter skins with memexport and the CPU copies them right after, so the
    // fast path produced black or other fighters' textures. Xenia always does the
    // full synchronous readback; match it. UFC_MEMEXPORT_FAST=1 = old behaviour (A/B).
    {
      const char* fast=std::getenv("UFC_MEMEXPORT_FAST");
      const char* want=(fast&&std::string_view(fast)=="1")?"true":"false";
      if(!rex::cvar::SetFlagByName("readback_memexport","true") ||
         !rex::cvar::SetFlagByName("readback_memexport_fast",want) ||
         rex::cvar::GetFlagByName("readback_memexport_fast")!=want)
        throw std::runtime_error("Cannot set the memexport readback mode");
    }
    REXLOG_INFO("[ufc-offline-render] direct_resolve={} memexport={} memexport_fast={} async={}",
        rex::cvar::GetFlagByName("ht_direct_resolve"),
        rex::cvar::GetFlagByName("readback_memexport"),
        rex::cvar::GetFlagByName("readback_memexport_fast"),
        rex::cvar::GetFlagByName("async_shader_compilation"));
    // R3: Xenia reference and the Reborn CAF save code require resolve readback OFF.
    if(!REXCVAR_GET(ufc_configurator)) rex::cvar::SetFlagByName("readback_resolve","none");
    // Explicit isolated diagnostic control, applied after production defaults.
    if(const char* mode=std::getenv("UFC_DIAG_READBACK")) {
      if(std::string_view(mode)=="full" || std::string_view(mode)=="fast" || std::string_view(mode)=="some" || std::string_view(mode)=="none")
        rex::cvar::SetFlagByName("readback_resolve",mode);
    }
    rex::cvar::SetFlagByName("ht_float_constant_runs","false");
    // R8.1: bounded backoff in the guest GPU-fence spin (sub_82374768), on by default.
    rex::cvar::SetFlagByName("ufc_spin_backoff","true");
    // R9: command-thread CPU cuts already in the UFC plugin (Hydro 9.0.0 / 8.8 ports);
    // validated on the ROG Ally (R8.3-R8.5). UFC_R9_CP_CUTS=false restores R8.1.
    {
      const char* v=std::getenv("UFC_R9_CP_CUTS");
      const char* on=(v&&std::string_view(v)=="false")?"false":"true";
      rex::cvar::SetFlagByName("ufc_cp_fast_registers",on);
      rex::cvar::SetFlagByName("ht_float_constant_runs",on);
      REXLOG_INFO("[ufc-cp-cpu] fast_registers={} float_constant_runs={}",rex::cvar::GetFlagByName("ufc_cp_fast_registers"),rex::cvar::GetFlagByName("ht_float_constant_runs"));
    }
    rex::cvar::SetFlagByName("ht_direct_resolve","false");
    for(const char* flag : {"ufc_spin_backoff","ht_float_constant_runs"}) {
      const std::string env_name=std::string("UFC_V60_")+flag;
      if(const char* value=std::getenv(env_name.c_str())) {
        if(std::string_view(value)=="true"||std::string_view(value)=="false")rex::cvar::SetFlagByName(flag,value);
      }
      REXLOG_INFO("[ufc-v60-effective] {}={}",flag,rex::cvar::GetFlagByName(flag));
    }
    if(const char* verify=std::getenv("UFC_V60_VERIFY")) {
      if(std::string_view(verify)=="1") {
        rex::cvar::SetFlagByName("ht_direct_resolve_verify","true");
        rex::cvar::SetFlagByName("ht_float_constant_runs","true");
        rex::cvar::SetFlagByName("ht_float_constant_runs_verify","true");
      }
    }
    REXLOG_INFO("[ufc-effective] readback_resolve={} vsync={} fps_limit={} wait_diag={}",rex::cvar::GetFlagByName("readback_resolve"),rex::cvar::GetFlagByName("vsync"),rex::cvar::GetFlagByName("ufc_fps_limit"),std::getenv("UFC_WAIT_DIAG")?std::getenv("UFC_WAIT_DIAG"):"0");

  }
};
REX_DEFINE_APP(ufc3, Ufc3App::Create)

