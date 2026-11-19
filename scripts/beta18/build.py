#!/usr/bin/env python3
"""P14 launcher build: recompile ufc_launcher_ui + main (bigger fonts) + frontdoor, relink the 4 game exes.
Recipes come from P12 (Reborn win/linux, Original linux) and P13 (Original win). Usage: build.py [variant...] [--control]"""
import json, os, re, shlex, subprocess, sys, hashlib, shutil
from pathlib import Path
P14 = os.environ.get(
    "UFC3_BUILD_TREE",
    "/mnt/c/Users/Monscriser/Documents/Codex/2026-11-09/o-zip-inclui-o-c-digo/work/technique-cheat/build-tree",
)
P12 = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/shirt-variety-p12-27-10/release"
P13O = os.environ.get(
    "UFC3_ORIGINAL_WIN_RECIPES",
    "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/p13-original",
)
WIN_CXX = os.environ.get("UFC3_WINDOWS_CXX")
SRC12, SRC14 = P12 + "/src", P14 + "/src"
REPO_CAF_SOURCE = Path(os.environ.get("UFC3_CAF_SOURCE") or Path(__file__).resolve().parents[2] / "app/common/ufc_caf_fast_build.cpp")
# BETA 1.8: Reborn chunk 229 = BETA 1.5 switch fix + issue #24 (jump table entry 7 ->
# sub_83669280). Built by work/beta18/build-fix24-objects.sh / build-fix24-linux.sh; the
# Windows object is the one validated in-game for #24, the Linux one was rebuilt with the
# archived recipe (its control object matches beta15-fix-switch/linux/fix.o byte for byte).
REBORN_SWITCH_FIX = Path(os.environ.get(
    "UFC3_REBORN_SWITCH_FIX", "/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3/work/beta18"))
REBORN_SWITCH_OBJECTS = {
    "win-reborn": REBORN_SWITCH_FIX / "win/fix24-validated.obj",
    "linux-reborn": REBORN_SWITCH_FIX / "linux/fix24.o",
}
V = {
 "win-reborn":  dict(dir=P12+"/apps/windows/reborn", main=P12+"/apps/windows/reborn/main.command.json", ui=P12+"/apps/windows/reborn/ufc_launcher_ui.command.json", link=P12+"/apps/windows/reborn/link.command.json", cwd="/mnt/e/UFC3-RexGlue-Lab/build/native-r9-sdk", ext=".obj", exe="ufc3-launcher-l1.exe"),
 "win-original":dict(dir=P13O, main=P13O+"/compile.command.json", ui=P12+"/apps/windows/original/ufc_launcher_ui.command.json", link=P13O+"/link-new.command.json", cwd="/mnt/e/UFC3-RexGlue-Lab/build/original-v48", ext=".obj", exe="ufc3-original-v48.exe"),
 "linux-reborn":dict(dir=P12+"/apps/linux/reborn", main=P12+"/apps/linux/reborn/main.command.json", ui=P12+"/apps/linux/reborn/ufc_launcher_ui.command.json", link=P12+"/apps/linux/reborn/link.command.json", cwd="/", ext=".o", exe="ufc3-launcher-l1"),
 "linux-original":dict(dir=P12+"/apps/linux/original", main=P12+"/apps/linux/original/main.command.json", ui=P12+"/apps/linux/original/ufc_launcher_ui.command.json", link=P12+"/apps/linux/original/link.command.json", cwd="/", ext=".o", exe="ufc3-original-v48"),
}
FONT_OLD = "atlas->AddFontFromFileTTF(ufc::U8(file).c_str(),16.f);}"
FONT_NEW = "atlas->AddFontFromFileTTF(ufc::U8(file).c_str(),16.f);atlas->AddFontFromFileTTF(ufc::U8(file).c_str(),21.f);atlas->AddFontFromFileTTF(ufc::U8(file).c_str(),28.f);}"
# P14.7: window close on Windows. ReXApp::OnClosing ends with std::_Exit(0), which in the UCRT still calls
# ExitProcess -> DLL_PROCESS_DETACH -> static destructors in librexruntime/libc++ while title threads were just
# killed -> std::terminate ([ufc-crash] in the 2x repro log). Same steps, but TerminateProcess (no DLL teardown).
CLASS_OLD = "class Ufc3App : public rex::ReXApp {"
CLASS_NEW = """#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall TerminateProcess(void*, unsigned);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess();
REXCVAR_DEFINE_BOOL(ufc_exit_terminate_process, true, "UFC", "Window close: end with TerminateProcess instead of std::_Exit (no DLL teardown)");
#endif
void MountUfcMods(rex::Runtime*);  // P14.9 Mods tab (ufc_mods_mount.cpp)
void UfcUnlinkOwnGuestMemory();  // Ally: ufc_shm_unlink.cpp
REXCVAR_DEFINE_BOOL(ufc_precompile_only, false, "UFC", "Launcher Pre-compile shaders: close right after the stored pipelines were created");
// P14.10: the pre-compile run must not show a black game window. The SDK window is SDL (created hidden,
// shown by Open); SDL3 is already linked for the launcher, declared here because main.cpp does not include it.
struct SDL_Window;
extern "C" SDL_Window** SDL_GetWindows(int* count);
extern "C" bool SDL_HideWindow(SDL_Window* window);
extern "C" bool SDL_SetWindowPosition(SDL_Window* window, int x, int y);
extern "C" bool SDL_SetWindowSize(SDL_Window* window, int w, int h);
extern "C" void SDL_free(void* mem);
#ifdef _WIN32
// The SDK shows and resizes the window again right before creating the pipelines; a watcher thread hides
// any visible window of this process at once until the run ends (ShowWindow is safe from another thread).
// While the UI thread is blocked creating pipelines a hide request waits in its queue, so the window is also made
// fully transparent, without a taskbar button and off-screen: even if the SDK shows it again nothing appears.
static BOOL CALLBACK UfcGhostOwnWindow(HWND window, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid != GetCurrentProcessId()) return TRUE;
  SetWindowLongPtrW(window, GWL_EXSTYLE, (GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
  SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
  SetWindowPos(window, nullptr, -32000, -32000, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(window, SW_HIDE);
  return TRUE;
}
// Measured 29/10: SDL shows the window ~60 ms before the first app hook after Open. A CBT hook installed in
// OnPreSetup (before the window exists) makes every window of the pre-compile run born transparent and off-screen.
// (SDL later resets the extended style, so a taskbar button may still flash for a few ms; the window stays off-screen.)
static LRESULT CALLBACK UfcPrecompileCbt(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HCBT_CREATEWND) {
    HWND window = reinterpret_cast<HWND>(wparam);
    auto* create = reinterpret_cast<CBT_CREATEWNDW*>(lparam)->lpcs;
    if (!create->hwndParent) {
      create->x = -32000; create->y = -32000;
      SetWindowLongPtrW(window, GWL_EXSTYLE, (GetWindowLongPtrW(window, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW);
      SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}
static BOOL CALLBACK UfcHideOwnWindow(HWND window, LPARAM) {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == GetCurrentProcessId() && IsWindowVisible(window)) ShowWindowAsync(window, SW_HIDE);
  return TRUE;
}
#endif
static void UfcPrecompileHideWindow() {
  if (!REXCVAR_GET(ufc_precompile_only)) return;
#ifdef _WIN32
  static bool watcher = false;
  if (!watcher) {
    watcher = true;
    std::thread([] { for (;;) { EnumWindows(UfcHideOwnWindow, 0); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } }).detach();
  }
  EnumWindows(UfcGhostOwnWindow, 0);  // UI thread: applied at once
#endif
  int count = 0;
  if (SDL_Window** windows = SDL_GetWindows(&count)) {
    // Also off-screen and tiny: the SDK may show the window again later (seen once for ~100 ms).
    for (int i = 0; i < count; ++i) { SDL_SetWindowSize(windows[i], 1, 1); SDL_SetWindowPosition(windows[i], -32000, -32000); SDL_HideWindow(windows[i]); }
    SDL_free(windows);
  }
  REXLOG_INFO("[ufc-precompile] game window hidden ({} window(s))", count);
}
// Called from OnPreSetup, before the SDK creates the window.
static void UfcPrecompileEarly() {
  if (!REXCVAR_GET(ufc_precompile_only)) return;
#ifdef _WIN32
  SetWindowsHookExW(WH_CBT, UfcPrecompileCbt, nullptr, GetCurrentThreadId());
#endif
}
class Ufc3App : public rex::ReXApp {"""
CLOSE_OLD = "  bool OnWindowCloseRequested() override { return !ufc::LauncherRunning(launcher_.get()); }"
CLOSE_NEW = CLOSE_OLD + """
#ifdef _WIN32
  void OnClosing(rex::ui::UIEvent& e) override {
    (void)e;
    REXLOG_INFO("Window closing, shutting down...");
    if (runtime() && runtime()->kernel_state()) runtime()->kernel_state()->TerminateTitle();
    const bool hard = REXCVAR_GET(ufc_exit_terminate_process);
    REXLOG_INFO("Title terminated; hard-exiting process ({}).", hard ? "TerminateProcess" : "_Exit");
    rex::FlushLogging();
    if (hard) TerminateProcess(GetCurrentProcess(), 0);
    std::_Exit(0);
  }
#endif
  // P14.8: optional pre-compile run. ReXApp::LaunchModule creates every pipeline of the shader storage
  // (blocking, with this GPU's driver) right before this call; stop here, before the title starts.
  // P14.10: last UI-thread point before the blocking pipeline creation; hide again (the SDK restores the window).
  void OnPreLaunchModule() override { UfcPrecompileHideWindow(); }
  void OnPostLaunchModule(rex::system::XThread* thread) override {
    (void)thread;
    if (!REXCVAR_GET(ufc_precompile_only)) return;
    REXLOG_INFO("[ufc-precompile] done; closing before the game starts.");
    rex::FlushLogging();
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
  }"""

# P14.9: mods replace game files before anything else reads them.
POST_OLD = "void OnPostLoadXexImage() override {"
POST_NEW = POST_OLD + "UfcPrecompileHideWindow();MountUfcMods(runtime());UfcUnlinkOwnGuestMemory();"  # Ally: /dev/shm
# P14.10: first app hook after ReXApp::SetupPresentation opened the window.
FIN_OLD = "std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& paths,std::function<void(rex::PathConfig)>) override {"
FIN_NEW = FIN_OLD + "UfcPrecompileHideWindow();"
PRE_OLD = "void OnPreSetup(rex::RuntimeConfig& config) override {"
PRE_NEW = PRE_OLD + ('UfcPrecompileEarly();'
    # BETA1.1: never tear. Mailbox if the driver has it, otherwise FIFO. UFC_PRESENT_MODE=immediate|fifo = A/B tests only.
    '{const char* ufc_pm=std::getenv("UFC_PRESENT_MODE");const std::string_view ufc_pmv=ufc_pm?ufc_pm:"";'
    'if(ufc_pmv!="immediate"){rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate","false");'
    'rex::cvar::SetFlagByName("vulkan_allow_present_mode_fifo_relaxed","false");}'
    'if(ufc_pmv=="fifo")rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox","false");}')

QUIT_PATCHES = (
    ("class Ufc3App : public rex::ReXApp {",
     "std::unique_ptr<rex::ui::ImGuiDialog> CreateUfcQuitPanel(rex::ui::ImGuiDrawer*, rex::ui::Window*);\n"
     "void UfcToggleQuitPanel();\n"
     "class Ufc3App : public rex::ReXApp {"),
    ("    fps_overlay_ = CreateUfcFpsOverlay(drawer);",
     "    fps_overlay_ = CreateUfcFpsOverlay(drawer);\n"
     "    quit_panel_ = CreateUfcQuitPanel(drawer, window());\n"
     "    rex::ui::RegisterBind(\"bind_ufc_quit\", \"F4\", \"Sair do jogo\", [] { UfcToggleQuitPanel(); });"),
    ("    fps_overlay_.reset();",
     "    rex::ui::UnregisterBind(\"bind_ufc_quit\");\n    quit_panel_.reset();\n    fps_overlay_.reset();"),
    ("  std::unique_ptr<rex::ui::ImGuiDialog> fps_overlay_;",
     "  std::unique_ptr<rex::ui::ImGuiDialog> fps_overlay_;\n  std::unique_ptr<rex::ui::ImGuiDialog> quit_panel_;"),
)
REPO_QUIT_SOURCE = Path(__file__).resolve().parents[2] / "app/common/ufc_quit_panel.cpp"
# Ally fixes: these units come from the repository app/common (not from the P14 tree).
REPO_UNITS = {u: Path(__file__).resolve().parents[2] / f"app/common/{u}.cpp" for u in ("ufc_caf_fast_build", "ufc_quit_panel", "ufc_shm_unlink", "ufc_cutscene_60", "ufc_caf_thumb", "ufc_pgo_dump")}

# BETA 1.8 PGO: extra flags for every compile (UFC_EXTRA_CXXFLAGS) and link (UFC_EXTRA_LDFLAGS) this script runs.
EXTRA_CXX = shlex.split(os.environ.get("UFC_EXTRA_CXXFLAGS", "")) if "shlex" in globals() else []
EXTRA_LD = shlex.split(os.environ.get("UFC_EXTRA_LDFLAGS", "")) if "shlex" in globals() else []

def run(args, cwd, log):
    if args and args[0].endswith("clang++") and (EXTRA_CXX or EXTRA_LD):
        args = list(args) + (EXTRA_CXX if "-c" in args else EXTRA_LD)
    with open(log, "w") as f:
        r = subprocess.run(args, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        print(open(log).read()[-4000:]); sys.exit(f"failed: {log}")

def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()

def swap(args, old, new):
    out = [new if a == old else a for a in args]
    if out == args: sys.exit(f"not found in recipe: {old}")
    return out

def load_recipe(path):
    with open(path, encoding="utf-8") as recipe:
        args = json.load(recipe)
    if WIN_CXX and args[0].endswith("/x86_64-w64-mingw32-clang++"):
        args[0] = WIN_CXX
    original = "/mnt/c/Users/Monscriser/Documents/Codex/"
    archive = os.environ.get("UFC3_CODEX_ARCHIVE")
    if not archive:
        return args
    archive = archive.rstrip("/") + "/"
    for i, arg in enumerate(args):
        prefix = "-I" if arg.startswith("-I") else ""
        source = arg[len(prefix):]
        if source.startswith(original) and not Path(source).exists():
            recovered = archive + source[len(original):]
            if Path(recovered).exists():
                args[i] = prefix + recovered
    return args

def build(name, control):
    v = V[name]; out = f"{P14}/build/{name}" + os.environ.get("UFC_OUT_SUFFIX", "") + ("-control" if control else "") + ("-medicao" if MEASURE else "") + ("-selectdiag" if os.environ.get("UFC_SELECT_PREVIEW_DIAG") == "1" else ""); os.makedirs(out, exist_ok=True)
    link = load_recipe(v["link"])
    old_ui = next(a for a in link if a.endswith("ufc_launcher_ui" + v["ext"]))
    old_main = next(a for a in link if a.endswith("/main" + v["ext"]))
    exe_index = link.index("-o") + 1
    if not control:
        # launcher UI from the P14 sources
        ui = [a.replace(SRC12, SRC14) for a in load_recipe(v["ui"])]
        ui[ui.index("-o") + 1] = f"{out}/ufc_launcher_ui{v['ext']}"
        print(f"[{name}] compile ui"); run(ui, v["cwd"], f"{out}/ui.log")
        # main with the extra font sizes (headers stay on P12 sources)
        main = load_recipe(v["main"]); src = next(a for a in main if a.endswith("/main.cpp"))
        text = open(src, encoding="utf-8").read()
        if text.count(FONT_OLD) != 1: sys.exit(f"font line not found once in {src}")
        for old in (CLASS_OLD, CLOSE_OLD, POST_OLD, FIN_OLD, PRE_OLD):
            if text.count(old) != 1: sys.exit(f"anchor not found once in {src}: {old[:40]}")
        text = text.replace(FONT_OLD, FONT_NEW).replace(CLASS_OLD, CLASS_NEW).replace(CLOSE_OLD, CLOSE_NEW).replace(POST_OLD, POST_NEW).replace(FIN_OLD, FIN_NEW).replace(PRE_OLD, PRE_NEW)
        # BETA 1.8: in-game quit panel (app/common/ufc_quit_panel.cpp): F4, or Back + Start held 1 s.
        for old, new in QUIT_PATCHES:
            if text.count(old) != 1: sys.exit(f"quit panel anchor not found once in {src}: {old[:40]}")
            text = text.replace(old, new)
        # BETA1.0: Hydro direct resolve (source-image compute, no EDRAM dump) on by default.
        # UFC_DIRECT_RESOLVE=0 = old path (A/B); UFC_DRES_VERIFY=1 = word-by-word proof; UFC_DRES_BREAK=1 = positive control.
        dres = ('{const char* e=std::getenv("UFC_DIRECT_RESOLVE");rex::cvar::SetFlagByName("ht_direct_resolve",(e&&std::string_view(e)=="0")?"false":"true");'
                'if(const char* v=std::getenv("UFC_DRES_VERIFY");v&&std::string_view(v)=="1")rex::cvar::SetFlagByName("ht_direct_resolve_verify","true");'
                'if(const char* b=std::getenv("UFC_DRES_BREAK");b&&std::string_view(b)=="1")rex::cvar::SetFlagByName("ht_direct_resolve_verify_break","true");}')
        text, n1 = re.subn(r'if\(!rex::cvar::SetFlagByName\("ht_direct_resolve","false"\) \|\|\s*rex::cvar::GetFlagByName\("ht_direct_resolve"\)!="false"\)\s*throw std::runtime_error\("Cannot disable the rejected direct-resolve path"\);', lambda m: dres, text)
        text, n2 = re.subn(r'rex::cvar::SetFlagByName\("ht_direct_resolve","false"\);', lambda m: dres, text)
        if n1 != 1: sys.exit(f"direct-resolve block not found once in {src}")
        # U1 (BETA 1.2+): the Original forced readback_resolve=none, so the CPU never saw the GPU-baked CAF
        # skin/clothing textures: the created fighter was saved with stale bytes (black silhouette, noisy shirt
        # and beanie in the fight presentation). Same as Reborn: full readback, made cheap by the exact on-demand
        # readback (copies only what the guest really reads). UFC_ORIGINAL_READBACK=none = old behaviour (A/B).
        RB_OLD = 'if(!REXCVAR_GET(ufc_configurator)) rex::cvar::SetFlagByName("readback_resolve","none");'
        RB_NEW = ('if(!REXCVAR_GET(ufc_configurator)){const char* rb=std::getenv("UFC_ORIGINAL_READBACK");'
                  'const bool old_rb=rb&&std::string_view(rb)=="none";'
                  'rex::cvar::SetFlagByName("readback_resolve",old_rb?"none":"full");'
                  # BETA 1.2 fix: at 2x/3x the scaled resolves cannot be deferred and the eager copy waited on the
                  # GPU (59 -> 20-29 FPS at 3x). Read back only native-size resolves, as BETA 1.0 read none.
                  # UFC_ORIGINAL_SKIP_SCALED=0 = read every scaled resolve (A/B).
                  '{const char* sk=std::getenv("UFC_ORIGINAL_SKIP_SCALED");'
                  'rex::cvar::SetFlagByName("ufc_readback_skip_scaled",(!old_rb&&!(sk&&std::string_view(sk)=="0"))?"true":"false");}'
                  'if(!old_rb&&!std::getenv("UFC_ONDEMAND_READBACK")){'
                  '\n#ifdef _WIN32\n_putenv_s("UFC_ONDEMAND_READBACK","1");_putenv_s("UFC_ONDEMAND_MIN","1");'
                  '\n#else\nsetenv("UFC_ONDEMAND_READBACK","1",1);setenv("UFC_ONDEMAND_MIN","1",1);\n#endif\n}'
                  'REXLOG_INFO("[ufc-u1] original readback_resolve={} skip_scaled={} ondemand={}",rex::cvar::GetFlagByName("readback_resolve"),rex::cvar::GetFlagByName("ufc_readback_skip_scaled"),std::getenv("UFC_ONDEMAND_READBACK")?std::getenv("UFC_ONDEMAND_READBACK"):"0");}')
        if "original" in name:
            if text.count(RB_OLD) != 1: sys.exit(f"Original readback line not found once in {src}")
            text = text.replace(RB_OLD, RB_NEW)
        open(f"{out}/main.cpp", "w", encoding="utf-8").write(text)
        main = swap(main, src, f"{out}/main.cpp"); main[main.index("-o") + 1] = f"{out}/main{v['ext']}"
        # P14.5: main.cpp must see the same ufc_library.h (Settings layout) as the launcher UI, or the
        # inline Settings code from the older header leaves new fields uninitialized. P14 headers first,
        # the P12 copy stays as fallback.
        i12 = main.index("-I" + SRC12) if "-I" + SRC12 in main else next(i for i, a in enumerate(main) if a.startswith("-I"))
        main.insert(i12, "-I" + SRC14)
        print(f"[{name}] compile main"); run(main, v["cwd"], f"{out}/main.log")
        link = swap(swap(link, old_ui, f"{out}/ufc_launcher_ui{v['ext']}"), old_main, f"{out}/main{v['ext']}")
        # P14.4: Cheats tab hook (automatic training reward); P14.5: native memcpy/memset.
        # Both compiled with the main recipe and added to the link.
        # ufc_native_crt (native memcpy/memset) was measured on 28/10: no CPU/FPS gain -> left out.
        units = ("ufc_training_rewards", "ufc_crash_report", "ufc_adaptive_rate", "ufc_mods_mount", "ufc_caf_fast_build", "ufc_cutscene_60", "ufc_rex_caf_preview_hold", "ufc_rex_announcer_lines", "ufc_rex_first_boot_save", "ufc_rex_position_guard", "ufc_rex_steady_skin_light", "ufc_quit_panel", "ufc_shm_unlink") + (("ufc_reborn_caf_page", "ufc_caf_thumb") if "reborn" in name else ()) + (("ufc_select_preview_diag",) if os.environ.get("UFC_SELECT_PREVIEW_DIAG") == "1" else ()) + (("ufc_measure_faults",) if MEASURE else ()) + (("ufc_pgo_dump",) if os.environ.get("UFC_PGO_GEN") == "1" else ())
        if MEASURE and name.startswith("linux"): link.insert(link.index("-o"), "-Wl,--export-dynamic-symbol=mprotect")
        for unit in units:
            unit_source = str(REPO_UNITS[unit]) if unit in REPO_UNITS and REPO_UNITS[unit].is_file() else f"{SRC14}/{unit}.cpp"
            tr = swap(load_recipe(v["main"]), src, unit_source)
            tr[tr.index("-o") + 1] = f"{out}/{unit}{v['ext']}"
            print(f"[{name}] compile {unit}"); run(tr, v["cwd"], f"{out}/{unit}.log")
            link.insert(link.index("-o"), f"{out}/{unit}{v['ext']}")
        # Reborn: ufc_language (NtCreateFile wrapper) from the P14 sources, for the career
        # magazine readback window. The Original keeps its own language unit (readback is on there).
        if "reborn" in name:
            lang = load_recipe(v["dir"] + "/ufc_language.command.json")
            lsrc = next(a for a in lang if a.endswith("/ufc_language.cpp"))
            lang = swap(lang, lsrc, f"{SRC14}/ufc_language.cpp")
            lang[lang.index("-o") + 1] = f"{out}/ufc_language{v['ext']}"
            first_inc = next(i for i, a in enumerate(lang) if a.startswith("-I"))
            lang.insert(first_inc, "-I" + SRC14)
            print(f"[{name}] compile ufc_language"); run(lang, v["cwd"], f"{out}/ufc_language.log")
            link = swap(link, v["dir"] + "/ufc_language" + v["ext"], f"{out}/ufc_language{v['ext']}")
        else:
            # BETA 1.8: the Original language unit is rebuilt from the P14 sources too (same source as its P12 object),
            # so ufc_localization.h fixes reach it (Portuguese tale of the tape: "PEDRA" -> "PESO").
            odir = P12 + "/apps/" + ("windows" if name.startswith("win") else "linux") + "/original"
            lang = load_recipe(odir + "/ufc_language.command.json")
            lsrc = next(a for a in lang if a.endswith("/ufc_language_original.cpp"))
            lang = swap(lang, lsrc, f"{SRC14}/ufc_language_original.cpp")
            lang[lang.index("-o") + 1] = f"{out}/ufc_language{v['ext']}"
            first_inc = next(i for i, a in enumerate(lang) if a.startswith("-I"))
            lang.insert(first_inc, "-I" + SRC14)
            print(f"[{name}] compile ufc_language (original)"); run(lang, v["cwd"], f"{out}/ufc_language.log")
            link = swap(link, odir + "/ufc_language" + v["ext"], f"{out}/ufc_language{v['ext']}")
    # Diagnostic Windows Reborn build: test controller with trigger support (LT/RT pages).
    if name == "win-reborn" and os.environ.get("UFC_SELECT_PREVIEW_DIAG") == "1":
        p6 = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-ui-p6/apps/windows/reborn"
        ti = load_recipe(p6 + "/input.command.json")
        tsrc = next(a for a in ti if a.endswith("/ufc_test_input.cpp"))
        ti = swap(ti, tsrc, f"{SRC14}/testinput-reborn/ufc_test_input.cpp")
        ti[ti.index("-o") + 1] = f"{out}/ufc_test_input{v['ext']}"
        print(f"[{name}] compile ufc_test_input (triggers)"); run(ti, v["cwd"], f"{out}/ufc_test_input.log")
        link = swap(link, p6 + "/ufc_test_input.obj", f"{out}/ufc_test_input{v['ext']}")
    # A13 test (off by default): UFC_A13_OBJS=<dir> swaps every recompiled chunk for that directory's objects
    # (register options stage A, with the P6 hand edits of chunk 76 reapplied), win-reborn only.
    # BETA 1.8 PGO: UFC_RECOMP_OBJS_<VARIANT>=<dir> does the same for any variant (objects from pgo_recomp.py,
    # which keeps the base names, so the chunk 229 swap below still finds its object).
    a13 = os.environ.get("UFC_A13_OBJS") if name == "win-reborn" else None
    a13 = os.environ.get("UFC_RECOMP_OBJS_" + name.replace("-", "_").upper()) or a13
    if a13:
        olds = [a for a in link if re.search(r"ufc3_(recomp\.\d+|init|register)(\.cpp)?\.(obj|o)$", a)]
        news = sorted(os.path.join(a13, f) for f in os.listdir(a13) if f.endswith(v["ext"]))
        if len(news) != len(olds): sys.exit(f"[{name}] {a13}: {len(news)} objects for {len(olds)} recompiled chunks")
        at = link.index(olds[0])
        link = [a for a in link if a not in olds]
        link[at:at] = news
        if not os.environ.get("UFC_OUT_SUFFIX"):
            out = out + "-a13"; os.makedirs(out, exist_ok=True)
        print(f"[{name}] recompiled chunks: {len(olds)} objects -> {len(news)} from {a13}")
    # BETA 1.5 fix: UFC_SWAP_OBJ_<VARIANT>="old=new" swaps one object (Reborn recomp chunk 229, switch fix).
    sw = os.environ.get("UFC_SWAP_OBJ_" + name.replace("-", "_").upper())
    if sw:
        old, new = sw.split("=", 1)
        assert old in link, old
        link[link.index(old)] = new
        print(f"[{name}] swap {old} -> {new}")
    elif name in REBORN_SWITCH_OBJECTS:
        # The inherited P12 recipes list the original chunk, which predates BETA 1.5.
        # Require its corrected object so a normal Reborn build cannot regress the fix.
        suffix = "ufc3_recomp.229.cpp.obj" if name == "win-reborn" else "ufc3_recomp.229.o"
        matches = [i for i, arg in enumerate(link) if Path(arg).name == suffix]
        fixed = REBORN_SWITCH_OBJECTS[name]
        if len(matches) != 1 or not fixed.is_file():
            sys.exit(f"[{name}] Missing BETA 1.8 switch replacement (#24) for {suffix}: {fixed}")
        print(f"[{name}] swap {link[matches[0]]} -> {fixed}")
        link[matches[0]] = str(fixed)
    exe_index = link.index("-o") + 1
    link[exe_index] = f"{out}/{v['exe']}"
    print(f"[{name}] link"); run(link, v["cwd"], f"{out}/link.log")
    print(f"[{name}] {sha(out + '/' + v['exe'])} {out}/{v['exe']}")

def frontdoor(platform=None):
    out = f"{P14}/build/frontdoor"; os.makedirs(out, exist_ok=True)
    for plat, exe in (("windows", "UFC3.exe"), ("linux", "UFC3")):
        if platform and plat != platform:
            continue
        a = [x.replace(SRC12, SRC14) for x in load_recipe(f"{P12}/{plat}/frontdoor.command.json")]
        a[a.index("-o") + 1] = f"{out}/{exe}"
        cwd = "/mnt/e/UFC3-RexGlue-Lab/build/native-r9-sdk" if plat == "windows" else "/"
        print(f"[frontdoor {plat}]"); run(a, cwd, f"{out}/{plat}.log"); print(sha(f"{out}/{exe}"), exe)

args = [a for a in sys.argv[1:] if not a.startswith("--")]; control = "--control" in sys.argv
MEASURE = "--medicao" in sys.argv  # Linux measurement build: memory-watch fault/mprotect counters
for n in args or list(V):
    if n == "frontdoor": frontdoor()
    elif n in ("frontdoor-linux", "frontdoor-windows"): frontdoor(n.split("-", 1)[1])
    else: build(n, control)
