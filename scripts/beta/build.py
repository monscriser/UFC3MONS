#!/usr/bin/env python3
"""P14 launcher build: recompile ufc_launcher_ui + main (bigger fonts) + frontdoor, relink the 4 game exes.
Recipes come from P12 (Reborn win/linux, Original linux) and P13 (Original win). Usage: build.py [variant...] [--control]"""
import json, os, re, subprocess, sys, hashlib, shutil
P14 = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-p14-28-10"
P12 = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/shirt-variety-p12-27-10/release"
P13O = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/p13-original"
SRC12, SRC14 = P12 + "/src", P14 + "/src"
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
POST_NEW = POST_OLD + "UfcPrecompileHideWindow();MountUfcMods(runtime());"
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

def run(args, cwd, log):
    with open(log, "w") as f:
        r = subprocess.run(args, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        print(open(log).read()[-4000:]); sys.exit(f"failed: {log}")

def sha(p): return hashlib.sha256(open(p, "rb").read()).hexdigest()

def swap(args, old, new):
    out = [new if a == old else a for a in args]
    if out == args: sys.exit(f"not found in recipe: {old}")
    return out

def build(name, control):
    v = V[name]; out = f"{P14}/build/{name}" + ("-control" if control else "") + ("-medicao" if MEASURE else ""); os.makedirs(out, exist_ok=True)
    link = json.load(open(v["link"]))
    old_ui = next(a for a in link if a.endswith("ufc_launcher_ui" + v["ext"]))
    old_main = next(a for a in link if a.endswith("/main" + v["ext"]))
    exe_index = link.index("-o") + 1
    if not control:
        # launcher UI from the P14 sources
        ui = [a.replace(SRC12, SRC14) for a in json.load(open(v["ui"]))]
        ui[ui.index("-o") + 1] = f"{out}/ufc_launcher_ui{v['ext']}"
        print(f"[{name}] compile ui"); run(ui, v["cwd"], f"{out}/ui.log")
        # main with the extra font sizes (headers stay on P12 sources)
        main = json.load(open(v["main"])); src = next(a for a in main if a.endswith("/main.cpp"))
        text = open(src, encoding="utf-8").read()
        if text.count(FONT_OLD) != 1: sys.exit(f"font line not found once in {src}")
        for old in (CLASS_OLD, CLOSE_OLD, POST_OLD, FIN_OLD, PRE_OLD):
            if text.count(old) != 1: sys.exit(f"anchor not found once in {src}: {old[:40]}")
        text = text.replace(FONT_OLD, FONT_NEW).replace(CLASS_OLD, CLASS_NEW).replace(CLOSE_OLD, CLOSE_NEW).replace(POST_OLD, POST_NEW).replace(FIN_OLD, FIN_NEW).replace(PRE_OLD, PRE_NEW)
        # BETA1.0: Hydro direct resolve (source-image compute, no EDRAM dump) on by default.
        # UFC_DIRECT_RESOLVE=0 = old path (A/B); UFC_DRES_VERIFY=1 = word-by-word proof; UFC_DRES_BREAK=1 = positive control.
        dres = ('{const char* e=std::getenv("UFC_DIRECT_RESOLVE");rex::cvar::SetFlagByName("ht_direct_resolve",(e&&std::string_view(e)=="0")?"false":"true");'
                'if(const char* v=std::getenv("UFC_DRES_VERIFY");v&&std::string_view(v)=="1")rex::cvar::SetFlagByName("ht_direct_resolve_verify","true");'
                'if(const char* b=std::getenv("UFC_DRES_BREAK");b&&std::string_view(b)=="1")rex::cvar::SetFlagByName("ht_direct_resolve_verify_break","true");}')
        text, n1 = re.subn(r'if\(!rex::cvar::SetFlagByName\("ht_direct_resolve","false"\) \|\|\s*rex::cvar::GetFlagByName\("ht_direct_resolve"\)!="false"\)\s*throw std::runtime_error\("Cannot disable the rejected direct-resolve path"\);', lambda m: dres, text)
        text, n2 = re.subn(r'rex::cvar::SetFlagByName\("ht_direct_resolve","false"\);', lambda m: dres, text)
        if n1 != 1: sys.exit(f"direct-resolve block not found once in {src}")
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
        units = ("ufc_training_rewards", "ufc_crash_report", "ufc_adaptive_rate", "ufc_mods_mount") + (("ufc_measure_faults",) if MEASURE else ())
        if MEASURE and name.startswith("linux"): link.insert(link.index("-o"), "-Wl,--export-dynamic-symbol=mprotect")
        for unit in units:
            tr = swap(json.load(open(v["main"])), src, f"{SRC14}/{unit}.cpp")
            tr[tr.index("-o") + 1] = f"{out}/{unit}{v['ext']}"
            print(f"[{name}] compile {unit}"); run(tr, v["cwd"], f"{out}/{unit}.log")
            link.insert(link.index("-o"), f"{out}/{unit}{v['ext']}")
    exe_index = link.index("-o") + 1
    link[exe_index] = f"{out}/{v['exe']}"
    print(f"[{name}] link"); run(link, v["cwd"], f"{out}/link.log")
    print(f"[{name}] {sha(out + '/' + v['exe'])} {out}/{v['exe']}")

def frontdoor():
    out = f"{P14}/build/frontdoor"; os.makedirs(out, exist_ok=True)
    for plat, exe in (("windows", "UFC3.exe"), ("linux", "UFC3")):
        a = [x.replace(SRC12, SRC14) for x in json.load(open(f"{P12}/{plat}/frontdoor.command.json"))]
        a[a.index("-o") + 1] = f"{out}/{exe}"
        cwd = "/mnt/e/UFC3-RexGlue-Lab/build/native-r9-sdk" if plat == "windows" else "/"
        print(f"[frontdoor {plat}]"); run(a, cwd, f"{out}/{plat}.log"); print(sha(f"{out}/{exe}"), exe)

args = [a for a in sys.argv[1:] if not a.startswith("--")]; control = "--control" in sys.argv
MEASURE = "--medicao" in sys.argv  # Linux measurement build: memory-watch fault/mprotect counters
for n in args or list(V):
    if n == "frontdoor": frontdoor()
    else: build(n, control)
