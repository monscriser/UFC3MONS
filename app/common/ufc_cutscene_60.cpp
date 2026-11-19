// 60 FPS for the scenes the game drops to 30 FPS (walkouts / ring entrances, intros, cutscenes,
// replays, presentation screens) at their original speed (--ufc_cutscene_60, default on, see STATUS;
// false = the original 30 FPS scenes).
//
// Guest code (identical in Original v48 and Reborn r9, see the edition check in the report):
//  0x833C80B8    frame-rate block (ufc_adaptive_rate.cpp): +8 rate (60/50/30/25), +12 lock (copied
//                from the config by sub_82748E70), +28 fps (float), +32 ms per frame (float),
//                +36 ms per frame (int), +56 step multiplier (1 at 60, 2 at 30), +60 1/multiplier.
//  sub_82748D38  half rate: 60 -> 30 (50 -> 25) with the matching step values (multiplier 2, 33.33 ms).
//                Called from 29 sites (25 functions) when one of those scenes starts, e.g. sub_822B7E10
//                (logged caller 822B7E50).
//  sub_82748DC8  full rate: 30 -> 60 (25 -> 50) with multiplier 1, 16.67 ms; does nothing while +12 lock
//                is set. Called when the scene ends (e.g. sub_822BA8E8, logged caller 822BA980).
//  sub_8273BBE0  once per presented frame (hooked by ufc_adaptive_rate.cpp): when +8 changed it sets the
//                present interval, 2 for 30/25 and 1 otherwise ([ufc-present-parameter] native_interval).
//  sub_8273B540  called by sub_8273BBE0 right before that rate check (and by sub_8273BB08); hooked here.
// sub_82748D38/DC8 themselves cannot be hooked here: the P10 present-parameter trace (params.obj, linked
// in all four builds) already defines them.
//
// The game's own 60 FPS form of the block (what sub_82748DC8 writes) advances half of the 30 FPS step per
// frame, so every reader of the block (multiplier, ms, fps, or a branch on the rate) covers the same game
// time per second at 60 as at 30. With the option on, when a half-rate request has put the block in its
// 30 (25) form, this hook runs the game's own full-rate function on a copy of the context before the
// render loop reads the rate: the present interval stays 1 (60 presented frames per second) and each frame
// advances half the 30 FPS step, i.e. the scene keeps its original speed. Only a block that was halved
// from a seen 60 (50) and has +12 lock clear is touched, which is exactly when sub_82748DC8 would restore
// it. The scene's own full-rate call later finds the block already at 60 and changes nothing.
// Because the rate field stays 60, ufc_adaptive_rate keeps working in these scenes: when a PC cannot render
// 60 there, the step follows the real frame time down to the exact 30 FPS values the game uses for them.
// Limits: the frame in which the game halves still runs with its 30 values from that call until this hook
// (at most one frame, as in the original); code that steps a scene by a fixed per-frame amount without
// reading the block runs faster at 60 (found: the sub_822B7E10 scene sequence, fixed below).
// STATUS (11/11/2026): ON by default, walkout included. The scenes started by sub_822B7E10 (walkout and
// the scenes after it, logged caller 0x822B7E50) step their sequence by a fixed 1 per frame (see
// "Walkout step" below); that step is now scaled by the block, so they run at 60 at the 30 FPS speed.
// --ufc_cutscene_60_walkout=false keeps them at the game's 30 FPS (the step factor is then 1).
// The other measured scenes keep their length at 60 (Reborn, same session flow, 60 vs 30: 82B6C5BC
// 16.6 vs 17.3 s and 11.2 vs 11.3 s; 82222384 3.0 vs 3.0 s).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>

REXCVAR_DEFINE_BOOL(ufc_cutscene_60, true, "UFC",
                    "Run walkouts, intros, cutscenes, replays and presentation screens at 60 FPS at their "
                    "original speed (the game drops them to 30 FPS); false = original 30 FPS");
REXCVAR_DEFINE_BOOL(ufc_cutscene_60_log, false, "Tests",
                    "Log every 30 FPS scene request that ufc_cutscene_60 keeps at 60 ([ufc-cutscene60])");

REXCVAR_DEFINE_BOOL(ufc_cutscene_60_walkout, true, "UFC",
                    "With ufc_cutscene_60: also run the walkout (and the scenes sub_822B7E10 starts) at 60 FPS; "
                    "false = keep them at the game's 30 FPS");
REXCVAR_DEFINE_BOOL(ufc_cutscene_60_walkout_step, true, "Tests",
                    "Scale the walkout scene step by the frame-rate block (false = the game's fixed per-frame step, "
                    "about 1.3-2x too fast at 60 FPS)");

extern "C" void __imp__sub_8273B540(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_822B7E10(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82748DC8(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kBlock = 0x833C80B8;
std::atomic<uint32_t> g_full_rate{0};  // last full rate seen (60 or 50); 0 = none yet
std::atomic<uint32_t> g_kept{0};       // half-rate blocks put back in the 60 (50) form
// Walkout hold: 0 none, 1 armed (sub_822B7E10 running), 2 active (its halving seen); while 1 or 2
// the per-frame hook leaves a halved block alone, so the walkout keeps the game's own 30 FPS.
std::atomic<int> g_walkout{0};
// Set while sub_822A6B08 runs with the 30 (25) rate shown in +8 (see "Walkout step"); the per-frame
// hook waits for it so the render loop never takes that value for a real halving.
std::atomic<int> g_rate_masked{0};

void KeepFullRate(PPCContext& ctx, uint8_t* base) {
  const uint32_t rate = REX_LOAD_U32(kBlock + 8);
  if (rate == 60 || rate == 50) {
    g_full_rate.store(rate, std::memory_order_relaxed);
    if (g_walkout.load() == 2) g_walkout = 0;
    return;
  }
  if (!REXCVAR_GET(ufc_cutscene_60) || g_rate_masked.load()) return;
  if (g_walkout.load() != 0) { g_walkout = 2; return; }
  const uint32_t full = g_full_rate.load(std::memory_order_relaxed);
  if (full == 0 || rate * 2 != full || REX_LOAD_U32(kBlock + 12) != 0) return;
  PPCContext call = ctx;  // leaf function: only scratch registers change, on the copy
  __imp__sub_82748DC8(call, base);
  const uint32_t n = ++g_kept;
  if (n <= 16 || REXCVAR_GET(ufc_cutscene_60_log))
    REXLOG_INFO("[ufc-cutscene60] kept {} fps (game asked {}) request={}", REX_LOAD_U32(kBlock + 8), rate, n);
}
}  // namespace

// DIAG (compiled only with -DUFC_CS60_DIAG, never in a release): live control of the sub_822890B0 steps.
// --ufc_cs60_diag_ctl=<file>; the file holds one line "<serial> <command> <args>", read every 6 frames:
//   freeze <hex mask>  objects (index = order first seen in the current scaled scene) get step 0
//   smooth <hex mask>  objects get the scaled step every frame (no whole-step handling)
//   whole  <hex mask>  objects get whole steps every other frame
//   set <cvar> <value> any cvar
//   burst <n> <prefix> capture n consecutive presented frames to <prefix>NN.ppm (written after the last)
//   dump <index>       log 0x300 bytes of that object
#ifdef UFC_CS60_DIAG
#include <rex/system/kernel_state.h>
#include <rex/runtime.h>
#include <rex/graphics/graphics_system.h>
#include <rex/ui/presenter.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>
REXCVAR_DEFINE_STRING(ufc_cs60_diag_ctl, "", "Tests", "DIAG control file for the sub_822890B0 steps");
namespace {
std::mutex g_diag_mutex;
std::vector<uint32_t> g_diag_objs;
std::atomic<uint64_t> g_diag_freeze{0}, g_diag_smooth{0}, g_diag_whole{0};
std::atomic<int> g_diag_smooth_bones{-1};  // objects with this +120 count get the smooth step
std::atomic<int> g_diag_trace_idx{-1}, g_diag_trace_left{0};  // log the pose of one object per call
int DiagIndex(uint8_t* base, uint32_t obj) {
  std::lock_guard<std::mutex> lock(g_diag_mutex);
  for (size_t i = 0; i < g_diag_objs.size(); ++i) if (g_diag_objs[i] == obj) return int(i);
  g_diag_objs.push_back(obj);
  const int i = int(g_diag_objs.size() - 1);
  REXLOG_INFO("[ufc-cs60-diag] idx={} obj={:08X} +274={:08X} bones={}", i, obj, REX_LOAD_U32(obj + 0x274),
              int32_t(REX_LOAD_U32(obj + 120)));
  auto words = [&](uint32_t at, uint32_t n) {
    std::string out;
    if (at < 0x40000000u || at >= 0xC0000000u) return std::string(" (bad)");
    for (uint32_t k = 0; k < n; k += 4) { char w[12]; std::snprintf(w, sizeof(w), " %08X", REX_LOAD_U32(at + k)); out += w; }
    return out;
  };
  const uint32_t res = REX_LOAD_U32(obj + 0x44), r24 = REX_LOAD_U32(obj + 0x24), r60 = REX_LOAD_U32(obj + 0x60);
  REXLOG_INFO("[ufc-cs60-diag] res idx={} +44={:08X}:{}", i, res, words(res, 0x40));
  REXLOG_INFO("[ufc-cs60-diag] r24 idx={} +24={:08X}:{} +60={:08X}:{}", i, r24, r24 ? words(r24, 0x20) : std::string(), r60, words(r60, 0x20));
  if (res >= 0x40000000u && res < 0xC0000000u) {
    const uint32_t res0 = REX_LOAD_U32(res);
    REXLOG_INFO("[ufc-cs60-diag] res0 idx={} *res={:08X}:{}", i, res0, words(res0, 0x40));
  }
  return i;
}
void DiagReset() {
  std::lock_guard<std::mutex> lock(g_diag_mutex);
  REXLOG_INFO("[ufc-cs60-diag] scene start (previous scene had {} objects)", g_diag_objs.size());
  g_diag_objs.clear();
}
// 0 = untouched, 1 = freeze, 2 = smooth, 3 = whole
int DiagMode(uint8_t* base, uint32_t obj) {
  const uint64_t f = g_diag_freeze.load(), sm = g_diag_smooth.load(), w = g_diag_whole.load();
  const int i = DiagIndex(base, obj);
  if (int32_t(REX_LOAD_U32(obj + 120)) == g_diag_smooth_bones.load()) return 2;
  if (i >= 64) return 0;
  const uint64_t bit = uint64_t(1) << i;
  return (f & bit) ? 1 : (sm & bit) ? 2 : (w & bit) ? 3 : 0;
}
void DiagTrace(uint8_t* base, uint32_t obj, double step, const char* when) {
  if (g_diag_trace_left.load() <= 0) return;
  int i;
  { std::lock_guard<std::mutex> lock(g_diag_mutex); i = -1;
    for (size_t k = 0; k < g_diag_objs.size(); ++k) if (g_diag_objs[k] == obj) i = int(k); }
  if (i != g_diag_trace_idx.load()) return;
  if (when[0] == 'a') --g_diag_trace_left;
  const uint32_t p60 = REX_LOAD_U32(obj + 0x50);
  std::string out;
  for (uint32_t k = 0; k < 0x50; k += 4) { char w[12]; std::snprintf(w, sizeof(w), " %08X", REX_LOAD_U32(p60 + k)); out += w; }
  std::string o80;
  for (uint32_t k = 0; k < 0x50; k += 4) { char w[12]; std::snprintf(w, sizeof(w), " %08X", REX_LOAD_U32(REX_LOAD_U32(obj + 0x54) + k)); o80 += w; }
  REXLOG_INFO("[ufc-cs60-trace] {} idx={} step={:.4f} t2C={:08X} p60:{} o80:{}", when, i, step, REX_LOAD_U32(obj + 0x2C), out, o80);
}
struct DiagShot { long long us; rex::ui::RawImage image; };
std::vector<DiagShot> g_burst;
int g_burst_left = 0;
std::string g_burst_prefix;
uint64_t g_ctl_serial = 0;
int g_ctl_tick = 0;
void DiagFrame(uint8_t* base) {
  if (g_burst_left > 0) {
    DiagShot shot;
    shot.us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* presenter = REX_KERNEL_STATE()->emulator()->graphics_system()->presenter();
    if (presenter && presenter->CaptureGuestOutput(shot.image)) g_burst.push_back(std::move(shot));
    if (--g_burst_left == 0) {
      for (size_t n = 0; n < g_burst.size(); ++n) {
        const auto& im = g_burst[n].image;
        char name[32]; std::snprintf(name, sizeof(name), "%02zu.ppm", n);
        std::ofstream out(g_burst_prefix + name, std::ios::binary);
        out << "P6\n" << im.width << ' ' << im.height << "\n255\n";
        for (uint32_t y = 0; y < im.height; ++y)
          for (uint32_t x = 0; x < im.width; ++x) out.write(reinterpret_cast<const char*>(im.data.data() + y * im.stride + x * 4), 3);
        REXLOG_INFO("[ufc-cs60-diag] burst {} t={}us", n, g_burst[n].us - g_burst[0].us);
      }
      REXLOG_INFO("[ufc-cs60-diag] burst written {} frames {}", g_burst.size(), g_burst_prefix);
      g_burst.clear();
    }
  }
  if (++g_ctl_tick % 6) return;
  static const std::string file = REXCVAR_GET(ufc_cs60_diag_ctl);
  if (file.empty()) return;
  std::ifstream in(file);
  uint64_t serial = 0; std::string cmd;
  if (!(in >> serial >> cmd) || serial <= g_ctl_serial) return;
  g_ctl_serial = serial;
  if (cmd == "freeze" || cmd == "smooth" || cmd == "whole") {
    std::string hex; in >> hex;
    const uint64_t m = std::strtoull(hex.c_str(), nullptr, 16);
    (cmd == "freeze" ? g_diag_freeze : cmd == "smooth" ? g_diag_smooth : g_diag_whole).store(m);
    REXLOG_INFO("[ufc-cs60-diag] {} {:016X}", cmd, m);
  } else if (cmd == "smoothbones") {
    int n = -1; in >> n; g_diag_smooth_bones = n;
    REXLOG_INFO("[ufc-cs60-diag] smoothbones {}", n);
  } else if (cmd == "trace") {
    int i = -1, n = 0; in >> i >> n; g_diag_trace_idx = i; g_diag_trace_left = n;
    REXLOG_INFO("[ufc-cs60-diag] trace {} {}", i, n);
  } else if (cmd == "set") {
    std::string k, v; in >> k >> v;
    rex::cvar::SetFlagByName(k, v);
    REXLOG_INFO("[ufc-cs60-diag] set {}={}", k, v);
  } else if (cmd == "burst") {
    int n = 0; std::string prefix; in >> n >> prefix;
    if (n > 0 && n <= 90) { g_burst_left = n; g_burst_prefix = prefix; g_burst.clear(); }
    REXLOG_INFO("[ufc-cs60-diag] burst {} {}", n, prefix);
  } else if (cmd == "dump") {
    int i = -1; in >> i;
    uint32_t obj = 0;
    { std::lock_guard<std::mutex> lock(g_diag_mutex); if (i >= 0 && i < int(g_diag_objs.size())) obj = g_diag_objs[i]; }
    if (obj) for (uint32_t off = 0; off < 0x300; off += 0x20) {
      std::ostringstream os;
      for (uint32_t k = 0; k < 0x20; k += 4) { char w[12]; std::snprintf(w, sizeof(w), " %08X", REX_LOAD_U32(obj + off + k)); os << w; }
      REXLOG_INFO("[ufc-cs60-diag] dump {} {:08X}+{:03X}:{}", i, obj, off, os.str());
    }
  }
}
}  // namespace
#define UFC_CS60_DIAG_FRAME(base) DiagFrame(base)
#else
#define UFC_CS60_DIAG_FRAME(base) ((void)0)
#endif

namespace { void ProbeScene(uint8_t* base); extern std::atomic<uint32_t> g_probe_obj; }
namespace { void CallProfTick(uint8_t* base); }
REX_HOOK_RAW(sub_8273B540) {
  for (int spin = 0; g_rate_masked.load() && spin < 1000000; ++spin) {}
  UFC_CS60_DIAG_FRAME(base);
  CallProfTick(base);
  ProbeScene(base);
  KeepFullRate(ctx, base);
  __imp__sub_8273B540(ctx, base);
}

// Walkout (sub_822B7E10 halves the rate when the fighters walk to the cage, logged caller
// 0x822B7E50). With ufc_cutscene_60_walkout=false a halving made here keeps its 30 FPS until the game
// restores the full rate (without the step fix below the walkout ran 46.6 s at 60 against 68-73 s).
namespace { void CallProfArm(); }
namespace { void WatchInstall(uint8_t* base); extern std::atomic<int> g_scene_on; }
REX_HOOK_RAW(sub_822B7E10) {
  CallProfArm();
  WatchInstall(base);
  g_scene_on = 1;
  // Armed before the game's call: the render thread runs the per-frame hook concurrently, so a
  // flag set after the call came too late (the first walkout was restored to 60 in between).
  const bool hold = REXCVAR_GET(ufc_cutscene_60) && !REXCVAR_GET(ufc_cutscene_60_walkout);
  g_probe_obj = ctx.r3.u32;
  if (hold) g_walkout = 1;
  __imp__sub_822B7E10(ctx, base);
  if (!hold) return;
  const uint32_t after = REX_LOAD_U32(kBlock + 8);
  if (after == 30 || after == 25) {
    g_walkout = 2;
    REXLOG_INFO("[ufc-cutscene60] walkout kept at {} fps", after);
  } else {
    int armed = 1;
    g_walkout.compare_exchange_strong(armed, 0);
  }
}

// Walkout step (root cause of the 1.3-1.5x fast walkout at 60 FPS; Original v48 and Reborn r9 identical):
//  sub_822BAB40  per-frame update of a sub_822B7E10 scene (scene object r3, slot r4; reached through the
//                dispatch table, return address 0x822BB078). It calls the scene's sequence object
//                (+720 of the slot) through vtable+12 with f1 = scene+2380 * scene+2376 (speed factors,
//                1.0 in the walkout) and never reads the frame-rate block: a step of 1 per frame, i.e. the
//                sequence time is counted in 30 FPS frames because the game always halves the rate here.
//  sub_822ABF60  that vtable+12 (sequence update): adds f1 to its clocks (+1972, +316), passes it to
//                sub_822AA680 -> sub_822A2FC8 -> each subscene's vtable+36 (sub_8229F6A8: +956, +1076,
//                +2652, animation step) and to sub_82B6A458.
// Measured (watch on the dispatch entries): f1 = 1 per call at 30 FPS (31 calls/s) and at 60 FPS
// (60 calls/s), so at 60 the sequence and its fighter animation ran twice as fast while the music/camera
// timeline kept real time. Fix: inside sub_822BAB40 only, scale the f1 of the sequence update by
// (block +56) / (the 30 FPS multiplier: 2, or 2.4 on a 50 Hz base): exactly 1 at the game's 30 (25) FPS
// block, 0.5 at 60, and the real frame time with ufc_adaptive_rate. Other users of sub_822ABF60 are
// untouched (thread-local flag, cleared for nested calls so the step is scaled once).
//  sub_822A6B08  (called by sub_822A9C00) sets the sequence timers +788 / +796, which sub_822ABF60 counts
//                down by f1 (sub_822A76F0, sub_822A47B0), from the CURRENT rate (+8 * 5.0, * 7.0, * 2, +7xx *
//                rate): those already followed the real rate, so with the scaled step they lasted twice as
//                long at 60 (measured: first walkout 92.9 s, last scene 38-39 s against 72.7 s and 32 s).
//                Other timers it sets are in fixed 30 FPS frames (count * 30). While a scaled scene runs,
//                this function sees the rate the scene was designed for (+8 = 30, or 25) for the duration
//                of the call, so every timer is in 30 FPS frames like the step.
extern "C" void __imp__sub_822A6B08(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_822A6B08) {
  const uint32_t full = g_full_rate.load(std::memory_order_relaxed);
  const uint32_t rate = REX_LOAD_U32(kBlock + 8);
  if (!g_scene_on.load() || full == 0 || rate != full || !REXCVAR_GET(ufc_cutscene_60) ||
      !REXCVAR_GET(ufc_cutscene_60_walkout) || !REXCVAR_GET(ufc_cutscene_60_walkout_step)) {
    __imp__sub_822A6B08(ctx, base);
    return;
  }
  g_rate_masked = 1;
  REX_STORE_U32(kBlock + 8, rate / 2);
  __imp__sub_822A6B08(ctx, base);
  if (REX_LOAD_U32(kBlock + 8) == rate / 2) REX_STORE_U32(kBlock + 8, rate);
  g_rate_masked = 0;
}
extern "C" void __imp__sub_822BAB40(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_822ABF60(PPCContext& ctx, uint8_t* base);
namespace {
thread_local bool t_scene_update = false;
// Step factor of the sequence update running on this thread (1 = not scaled). Read by the
// sub_822890B0 hook below.
thread_local float t_step_factor = 1.0f;
std::atomic<uint32_t> g_step_logs{0};
}  // namespace

// BETA 1.8 fix (Ally, 18/11/2026): the walkout lighting turned into noise (spotlights red/yellow
// blotches, fighter posterized) with the walkout step above. Isolated on the ROG Ally with
// ufc_cs60_scale (factor 2.0 = give one function the unscaled step back while everything else keeps
// the half step), one function at a time: only sub_822890B0 (reached ~5 times per frame from the
// sequence update, return address 0x82289FA4) breaks with a fractional step. With it back on the full
// step the walkout stays clean for the whole scene (captures up to 25 s) and keeps its 30 FPS speed;
// with every other step consumer back on the full step and this one halved it degrades within ~10 s.
// So it gets the step the game designed it for, while the sequence and the fighter animation keep the
// scaled one. --ufc_cutscene_60_effect_step=false restores the BETA 1.7/1.8 behaviour (for tests).
REXCVAR_DEFINE_BOOL(ufc_cutscene_60_effect_step, true, "UFC",
                    "Walkout at 60 FPS: give sub_822890B0 the game's full step (fixes the walkout lighting noise)");
// Speed: giving it the full step on every 60 FPS frame made its effect run up to twice as fast (the user
// saw the walkout accelerated). BETA 1.8 Ally (18/11): the first fix kept a per-object accumulator in a
// 16-slot table; objects sharing a slot reset each other and never reached a whole step, so Bruce Buffer
// and the fighters froze ("statues"), and the others updated at an irregular rate (a trail on the ring
// girl). Now the whole-step decision is made once per frame (in the sequence update, sub_822ABF60) and
// shared by every object, and only the callee that needs it gets whole steps:
//  sub_822890B0 passes the step to sub_82EFCAC0 (first call) and to sub_8223B1E0 (second call).
//  ufc_cs60_effect_whole: bit 0 = sub_82EFCAC0 gets whole steps, bit 1 = sub_8223B1E0 gets whole steps;
//  the callees without the bit get the scaled (smooth) step. 3 = both whole (no fractional step at all).
// Measured on the Ally (18/11, Reborn, Career walkout + Buffer intro; ~65 objects, all vtable 0x8200E208):
//  objects with +0x274 set (one per group, pointing at a sibling's data) animate the characters
//  (freezing them froze the fighters); objects without it break the lighting with a fractional step.
//  ufc_cs60_effect_select=2 (default): whole steps only for the objects without +0x274, smooth scaled
//  steps for the characters -> walkout clean (0 noisy frames of 15), characters move every frame.
//  select=1 (the opposite) and smooth for all: noisy. 0 + effect_whole=3: clean but characters at whole steps.
REXCVAR_DEFINE_INT32(ufc_cs60_effect_whole, 3, "Tests",
                     "Walkout at 60 FPS: which sub_822890B0 callees get whole steps (bit0 82EFCAC0, bit1 8223B1E0)");
extern "C" void __imp__sub_822890B0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82EFCAC0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8223B1E0(PPCContext& ctx, uint8_t* base);
namespace {
thread_local double t_whole_acc = 0;      // scaled steps accumulated by the sequence updates
thread_local bool t_whole_now = false;    // this frame completes a whole 30 FPS step
thread_local int t_in_effect = 0;         // inside sub_822890B0 with a scaled step
thread_local double t_eff_scaled = 0, t_eff_full = 0;
void EffectStep(PPCContext& ctx, int bit) {
  if (!t_in_effect) return;
  ctx.f1.f64 = (REXCVAR_GET(ufc_cs60_effect_whole) & bit) ? (t_whole_now ? t_eff_full : 0.0) : t_eff_scaled;
}
}  // namespace
REXCVAR_DEFINE_STRING(ufc_cs60_whole_vt, "", "Tests",
                      "Comma list of hex object vtables (word 0) whose sub_822890B0 step is whole; others smooth");
// BETA 1.8 (PC, 18/11 evening): with select=2 the player saw a trail that doubles the characters during the
// presentations and Bruce Buffer's mouth out of sync with his voice. Logged on the PC (Reborn, exhibition
// presentation, 64 objects): every object with +0x274 points 0xC0 bytes before ANOTHER object of the scene
// (AFE22B50 -> AFE16840 = AFE16900 - 0xC0, AFF13030 -> AFF02740 = AFF02800 - 0xC0, ...), i.e. a character is a
// base object (no +0x274) plus one or more layers (with +0x274) that read the base's data. select=2 stepped
// the base at 30 Hz and its layers at 60 Hz, so one character was animated at two rates (the trail; the mouth
// is a face layer). select=5 (default): layers AND the bases they point at get the smooth step every frame;
// whole steps only for objects without +0x274 that no layer points at (the stand-alone effect objects, which
// break the lighting with a fractional step).
// BETA 1.8 (PC, 18-19/11 night, player's Career save + Exhibition, consecutive-frame captures): select=5 still
// left the trail. The stand-alone objects include the camera of every shot (one 1-11 track object per shot,
// quaternion + position at *(obj+0x50)); at whole steps the camera moved on every other frame (background
// shift 0,2,0,2 px), so the characters smeared against the 30 Hz background. Making every object smooth fixed the
// camera but brought back the Career spotlight noise; bisecting the objects of the spotlight intro (smooth masks
// over the objects in the order seen) showed it comes from one object only, that intro's camera, and it is its
// fractional TIME that breaks the lighting (a cut-aware smooth step, select=6, still showed it). select=7 (default):
// the stand-alone objects keep the game's whole steps and their pose is interpolated (see "select=7" below).
// Measured: Exhibition presentations/arena intro 60 Hz on all 30 bursts, Career spotlight intro clean and 60 Hz.
REXCVAR_DEFINE_INT32(ufc_cs60_effect_select, 7, "UFC",
                     "sub_822890B0 steps in 60 FPS scenes: 7 = characters smooth, stand-alone objects (scene cameras, lights) on "
                     "the game's whole steps with their pose interpolated to 60 Hz; 5 = BETA 1.8 PC test (cameras at 30 Hz); "
                     "6 = all smooth with cut detection (Career spotlight noise); 0/1/2 = older tests");
REXCVAR_DEFINE_BOOL(ufc_cs60_effect_log, false, "Tests", "Log each object seen by sub_822890B0 in a scaled scene");
namespace {
uint32_t g_whole_vt[16]; int g_nwhole_vt = -1;
bool WholeVt(uint32_t vt) {
  if (g_nwhole_vt < 0) {
    g_nwhole_vt = 0;
    const std::string l = REXCVAR_GET(ufc_cs60_whole_vt);
    size_t p = 0;
    while (p < l.size() && g_nwhole_vt < 16) {
      size_t e = l.find(',', p); if (e == std::string::npos) e = l.size();
      g_whole_vt[g_nwhole_vt++] = uint32_t(std::strtoul(l.substr(p, e - p).c_str(), nullptr, 16));
      p = e + 1;
    }
  }
  for (int i = 0; i < g_nwhole_vt; ++i) if (g_whole_vt[i] == vt) return true;
  return false;
}
uint32_t g_seen[64]; std::atomic<int> g_nseen{0};
// select=5: bases that a layer (+0x274 set) points at; cleared when a scaled scene starts.
std::mutex g_base_mutex;
std::unordered_set<uint32_t> g_layer_bases;
std::atomic<bool> g_scaled_scene{false};
constexpr uint32_t kLayerBaseOffset = 0xC0;  // +0x274 of a layer = its base object - 0xC0 (all pairs logged)
bool StandaloneEffect(uint8_t* base, uint32_t obj) {
  const uint32_t layer_of = REX_LOAD_U32(obj + 0x274);
  std::lock_guard<std::mutex> lock(g_base_mutex);
  if (layer_of) { g_layer_bases.insert(layer_of + kLayerBaseOffset); return false; }
  return g_layer_bases.count(obj) == 0;
}
// select=6 (BETA 1.8, PC 18/11 night): every object gets the scaled (smooth) step, so the scene cameras move
// at 60 Hz (the trail on the presentations was the camera of each shot at 30 Hz with select=5: measured on
// consecutive frames, the background moved on every other frame). The Career spotlight noise that made the
// stand-alone objects need whole steps was isolated on the PC with the player's Career save: it is only the
// camera of the spotlight intro (one track, 451 keys, quaternion + position at *(obj+0x50)). Its track holds
// the camera cuts as consecutive keys; with a half step the camera was sampled between the two sides of a cut
// and flew through the set for 2-3 frames (traced: -340 -> -224 -> -2 -> 102, against 1-19 units per frame
// inside a shot), which broke the spotlight lighting. So for the stand-alone objects a cut is detected (a jump
// of more than kCutDistance units or kCutCos rotation in one call) and the object is advanced by one more frame
// step right away, landing on the far side of the cut; that extra step is then taken from its next steps, so
// its time stays exactly the scene time.
constexpr float kCutDistance = 40.0f;
constexpr float kCutCos = 0.995f;
struct CamPose { float q[4]; float p[3]; };
bool ReadCamPose(uint8_t* base, uint32_t obj, CamPose& out) {
  const uint32_t at = REX_LOAD_U32(obj + 0x50);
  if (int32_t(REX_LOAD_U32(obj + 120)) < 1 || at < 0x40000000u || at >= 0xC0000000u) return false;
  for (int k = 0; k < 4; ++k) { const uint32_t b = REX_LOAD_U32(at + 4 * k); std::memcpy(&out.q[k], &b, 4); }
  for (int k = 0; k < 3; ++k) { const uint32_t b = REX_LOAD_U32(at + 16 + 4 * k); std::memcpy(&out.p[k], &b, 4); }
  for (float v : out.q) if (!std::isfinite(v)) return false;
  for (float v : out.p) if (!std::isfinite(v)) return false;
  return true;
}
bool IsCameraCut(const CamPose& a, const CamPose& b) {
  const float dx = a.p[0] - b.p[0], dy = a.p[1] - b.p[1], dz = a.p[2] - b.p[2];
  const float dot = std::fabs(a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3]);
  return dx * dx + dy * dy + dz * dz > kCutDistance * kCutDistance || dot < kCutCos;
}
// select=7: stand-alone objects keep the game's whole steps (their time stays on whole 30 FPS frames, which
// the Career spotlight intro needs), and their pose is shown at 60 Hz by interpolation: on the frame their time
// advances, the pose written by the game (time k+1) is replaced by the midpoint between the previous pose (k)
// and it; on the next frame (step 0) the game writes the exact pose k+1 again. Half a frame (8 ms) of delay,
// no interpolation across a camera cut. Pose layout per bone (80 bytes at *(obj+0x50)): quaternion, position.
constexpr int kMaxLerpBones = 16;
struct BonePose { float v[7]; };
int ReadBonePoses(uint8_t* base, uint32_t obj, BonePose* out) {
  const uint32_t at = REX_LOAD_U32(obj + 0x50);
  const int n = int32_t(REX_LOAD_U32(obj + 120));
  if (n < 1 || n > kMaxLerpBones || at < 0x40000000u || at >= 0xC0000000u) return 0;
  for (int b = 0; b < n; ++b)
    for (int k = 0; k < 7; ++k) {
      const uint32_t w = REX_LOAD_U32(at + 80 * b + 4 * k);
      std::memcpy(&out[b].v[k], &w, 4);
      if (!std::isfinite(out[b].v[k])) return 0;
    }
  return n;
}
void WriteBoneLerp(uint8_t* base, uint32_t obj, const BonePose* a, const BonePose* c, int n) {
  const uint32_t at = REX_LOAD_U32(obj + 0x50);
  for (int b = 0; b < n; ++b) {
    float q[4], len = 0.0f;
    const float sgn = (a[b].v[0] * c[b].v[0] + a[b].v[1] * c[b].v[1] + a[b].v[2] * c[b].v[2] + a[b].v[3] * c[b].v[3]) < 0 ? -1.0f : 1.0f;
    for (int k = 0; k < 4; ++k) { q[k] = 0.5f * (a[b].v[k] + sgn * c[b].v[k]); len += q[k] * q[k]; }
    if (len < 1e-12f) continue;
    len = 1.0f / std::sqrt(len);
    for (int k = 0; k < 4; ++k) { const float f = q[k] * len; uint32_t w; std::memcpy(&w, &f, 4); REX_STORE_U32(at + 80 * b + 4 * k, w); }
    for (int k = 4; k < 7; ++k) { const float f = 0.5f * (a[b].v[k] + c[b].v[k]); uint32_t w; std::memcpy(&w, &f, 4); REX_STORE_U32(at + 80 * b + 4 * k, w); }
  }
}
bool BoneCut(const BonePose& a, const BonePose& c) {
  CamPose x{}, y{};
  for (int k = 0; k < 4; ++k) { x.q[k] = a.v[k]; y.q[k] = c.v[k]; }
  for (int k = 0; k < 3; ++k) { x.p[k] = a.v[4 + k]; y.p[k] = c.v[4 + k]; }
  return IsCameraCut(x, y);
}
std::unordered_map<uint32_t, double> g_step_debt;  // select=6: extra step already given at a cut (g_base_mutex)
std::atomic<uint32_t> g_cut_logs{0};
}  // namespace
REX_HOOK_RAW(sub_822890B0) {
  const float factor = t_step_factor;
  if (!(factor > 0.0f && factor < 1.0f && REXCVAR_GET(ufc_cutscene_60_effect_step))) {
    __imp__sub_822890B0(ctx, base);
    return;
  }
  const uint32_t obj = ctx.r3.u32, vt = REX_LOAD_U32(obj);
  if (REXCVAR_GET(ufc_cs60_effect_log)) {
    bool seen = false;
    for (int i = 0; i < g_nseen.load() && i < 64; ++i) if (g_seen[i] == obj) seen = true;
    if (!seen && g_nseen.load() < 64) {
      g_seen[g_nseen++] = obj;
      REXLOG_INFO("[ufc-cs60-obj] obj={:08X} vt={:08X} +274={:08X} +280={:08X} +284={:08X} +2E0={:08X} +2E4={:08X} +4={:08X} +8={:08X}"
                  " bones(+120)={} pose(+740)={:08X} +652={} +640={} +628={:08X} +736={:08X} +656={}",
                  obj, vt, REX_LOAD_U32(obj + 0x274), REX_LOAD_U32(obj + 0x280), REX_LOAD_U32(obj + 0x284),
                  REX_LOAD_U32(obj + 0x2E0), REX_LOAD_U32(obj + 0x2E4), REX_LOAD_U32(obj + 4), REX_LOAD_U32(obj + 8),
                  int32_t(REX_LOAD_U32(obj + 120)), REX_LOAD_U32(obj + 740), int32_t(REX_LOAD_U32(obj + 652)),
                  int32_t(REX_LOAD_U32(obj + 640)), REX_LOAD_U32(obj + 628), REX_LOAD_U32(obj + 736),
                  int32_t(REX_LOAD_U32(obj + 656)));
    }
  }
#ifdef UFC_CS60_DIAG
  if (const int mode = DiagMode(base, obj)) {
    if (mode == 1) ctx.f1.f64 = 0.0;
    else if (mode == 3) ctx.f1.f64 = t_whole_now ? double(float(ctx.f1.f64 / factor)) : 0.0;
    const double st = ctx.f1.f64;
    __imp__sub_822890B0(ctx, base);
    DiagTrace(base, obj, st, "after");
    return;
  }
  struct TraceAfter { uint8_t* b; uint32_t o; PPCContext& c; double st; ~TraceAfter() { DiagTrace(b, o, st, "after"); } };
  TraceAfter trace_after{base, obj, ctx, 0.0};
#endif
  const int sel = REXCVAR_GET(ufc_cs60_effect_select);
  if (sel == 3 || sel == 4) {  // test: freeze one group to see what it animates
    const bool keyed = REX_LOAD_U32(obj + 0x274) != 0;
    if (keyed == (sel == 3)) ctx.f1.f64 = 0.0;
    __imp__sub_822890B0(ctx, base);
    return;
  }
  if (sel == 7) {
    if (!StandaloneEffect(base, obj)) { __imp__sub_822890B0(ctx, base); return; }  // characters: smooth
    if (!t_whole_now) { ctx.f1.f64 = 0.0; __imp__sub_822890B0(ctx, base); return; }  // the game rewrites pose k+1
    ctx.f1.f64 = double(float(ctx.f1.f64 / factor));
    BonePose before[kMaxLerpBones], after[kMaxLerpBones];
    const int n = ReadBonePoses(base, obj, before);
    __imp__sub_822890B0(ctx, base);
    if (n && factor <= 0.75f && ReadBonePoses(base, obj, after) == n && !BoneCut(before[0], after[0]))
      WriteBoneLerp(base, obj, before, after, n);
    return;
  }
  if (sel == 6) {
    if (!StandaloneEffect(base, obj)) { __imp__sub_822890B0(ctx, base); return; }  // characters: smooth
    const double frame_step = ctx.f1.f64;
    double step = frame_step;
    {
      std::lock_guard<std::mutex> lock(g_base_mutex);
      const auto it = g_step_debt.find(obj);
      if (it != g_step_debt.end()) {
        const double pay = std::min(step, it->second);
        step -= pay; it->second -= pay;
        if (it->second <= 1e-9) g_step_debt.erase(it);
      }
    }
    CamPose before{}, after{};
    const bool had = step > 0.0 && ReadCamPose(base, obj, before);
    const PPCContext args = ctx;
    ctx.f1.f64 = step;
    __imp__sub_822890B0(ctx, base);
    if (had && ReadCamPose(base, obj, after) && IsCameraCut(before, after)) {
      // Between the two sides of a cut: one more frame step at a time (at most 3, the in-between samples
      // traced) until the camera stops jumping, i.e. it is on the far side.
      double extra = 0.0;
      CamPose last = after, next{};
      for (int n = 0; n < 3; ++n) {
        PPCContext again = args;
        again.f1.f64 = frame_step;
        __imp__sub_822890B0(again, base);
        extra += frame_step;
        if (!ReadCamPose(base, obj, next) || !IsCameraCut(last, next)) break;
        last = next;
      }
      { std::lock_guard<std::mutex> lock(g_base_mutex); g_step_debt[obj] += extra; }
      if (g_cut_logs.fetch_add(1) < 64 || REXCVAR_GET(ufc_cutscene_60_log))
        REXLOG_INFO("[ufc-cutscene60] cut obj={:08X} ({:.1f},{:.1f},{:.1f}) -> ({:.1f},{:.1f},{:.1f}) advanced {:.3f}",
                    obj, before.p[0], before.p[1], before.p[2], next.p[0], next.p[1], next.p[2], extra);
    }
    return;
  }
  if (sel == 5) {
    if (StandaloneEffect(base, obj)) ctx.f1.f64 = t_whole_now ? double(float(ctx.f1.f64 / factor)) : 0.0;
    __imp__sub_822890B0(ctx, base);
    return;
  }
  if (sel == 1 || sel == 2) {
    const bool keyed = REX_LOAD_U32(obj + 0x274) != 0;
    if (keyed == (sel == 1)) ctx.f1.f64 = t_whole_now ? double(float(ctx.f1.f64 / factor)) : 0.0;
    __imp__sub_822890B0(ctx, base);
    return;
  }
  if (!REXCVAR_GET(ufc_cs60_whole_vt).empty()) {
    if (WholeVt(vt)) ctx.f1.f64 = t_whole_now ? double(float(ctx.f1.f64 / factor)) : 0.0;
    __imp__sub_822890B0(ctx, base);
    return;
  }
  const int prev = t_in_effect;
  const double ps = t_eff_scaled, pf = t_eff_full;
  t_eff_scaled = ctx.f1.f64;
  t_eff_full = double(float(ctx.f1.f64 / factor));
  t_in_effect = 1;
  __imp__sub_822890B0(ctx, base);
  t_in_effect = prev; t_eff_scaled = ps; t_eff_full = pf;
}
REX_HOOK_RAW(sub_82EFCAC0) {
  const int prev = t_in_effect;
  EffectStep(ctx, 1);
  t_in_effect = 0;  // nested calls see their own arguments
  __imp__sub_82EFCAC0(ctx, base);
  t_in_effect = prev;
}
REX_HOOK_RAW(sub_8223B1E0) {
  const int prev = t_in_effect;
  EffectStep(ctx, 2);
  t_in_effect = 0;
  __imp__sub_8223B1E0(ctx, base);
  t_in_effect = prev;
}
REX_HOOK_RAW(sub_822BAB40) {
  const bool prev = t_scene_update;
  t_scene_update = true;
  __imp__sub_822BAB40(ctx, base);
  t_scene_update = prev;
}
REX_HOOK_RAW(sub_822ABF60) {
  if (!t_scene_update || !REXCVAR_GET(ufc_cutscene_60_walkout_step)) {
    __imp__sub_822ABF60(ctx, base);
    return;
  }
  const uint32_t bits = REX_LOAD_U32(kBlock + 56);
  float mult;
  std::memcpy(&mult, &bits, 4);
  const float half = g_full_rate.load(std::memory_order_relaxed) == 50 ? 2.4f : 2.0f;
  const float factor = (mult > 0.0f && mult <= half) ? mult / half : 1.0f;
  const double in = ctx.f1.f64;
  if (factor != 1.0f) ctx.f1.f64 = double(float(in) * factor);
  if (factor != 1.0f && g_step_logs.fetch_add(1) < 4)
    REXLOG_INFO("[ufc-cutscene60] walkout step {} -> {} (block multiplier {})", in, ctx.f1.f64, mult);
  t_scene_update = false;  // nested sequence updates get the already scaled step
  const bool scaled = factor > 0.0f && factor < 1.0f;
  if (scaled != g_scaled_scene.exchange(scaled) && scaled) {  // a scaled scene starts: new objects
    std::lock_guard<std::mutex> lock(g_base_mutex);
    g_layer_bases.clear();
    g_step_debt.clear();
#ifdef UFC_CS60_DIAG
    DiagReset();
#endif
  }
  if (factor > 0.0f && factor < 1.0f) {
    t_whole_acc += factor;
    t_whole_now = t_whole_acc + 1e-6 >= 1.0;
    if (t_whole_now) t_whole_acc -= 1.0;
  } else {
    t_whole_acc = 0;
    t_whole_now = true;
  }
  const float previous_factor = t_step_factor;
  t_step_factor = factor;
  __imp__sub_822ABF60(ctx, base);
  t_step_factor = previous_factor;
  t_scene_update = true;
}

// DIAG (ufc_cutscene_60_probe): once per second during a sub_822B7E10 scene, log the words of the
// scene object that changed in the last second (to find what the walkout advances per frame).
#include <chrono>
#include <vector>
REXCVAR_DEFINE_BOOL(ufc_cutscene_60_probe, false, "Tests", "Log the walkout scene object words that change each second");
namespace {
std::atomic<uint32_t> g_probe_obj{0};
void ProbeScene(uint8_t* base) {
  if (!REXCVAR_GET(ufc_cutscene_60_probe)) return;
  const uint32_t obj = g_probe_obj.load();
  if (obj < 0x40000000u) return;
  static std::vector<uint32_t> prev;
  static uint32_t prev_obj = 0;
  static auto next = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (now < next) return;
  next = now + std::chrono::seconds(1);
  constexpr uint32_t kWords = 1536;  // 6 KB
  std::vector<uint32_t> cur(kWords);
  for (uint32_t i = 0; i < kWords; ++i) cur[i] = REX_LOAD_U32(obj + 4 * i);
  if (prev_obj == obj && prev.size() == kWords) {
    std::string line;
    int n = 0;
    for (uint32_t i = 0; i < kWords && n < 60; ++i)
      if (cur[i] != prev[i]) {
        float a, b; std::memcpy(&a, &prev[i], 4); std::memcpy(&b, &cur[i], 4);
        char t[96]; std::snprintf(t, sizeof t, " +%u:%d>%d|%.3f>%.3f", i * 4, int32_t(prev[i]), int32_t(cur[i]), a, b);
        line += t; ++n;
      }
    REXLOG_INFO("[ufc-cs60-probe] rate={} obj=0x{:08X}{}", REX_LOAD_U32(kBlock + 8), obj, line);
  }
  prev = std::move(cur);
  prev_obj = obj;
}
}  // namespace

// DIAG (ufc_cs60_callprof): count every indirect (ctr) guest call during a window of the first
// sub_822B7E10 scene, by swapping the dispatch-table entries for one counting thunk (all indirect
// calls in the generated code are REX_CALL_INDIRECT_FUNC(ctx.ctr.u32), so ctr is the target).
#include <algorithm>
#include <rex/ppc/func.h>
#include <cstdlib>
REXCVAR_DEFINE_BOOL(ufc_cs60_callprof, false, "Tests", "Count indirect guest calls in a walkout window");
REXCVAR_DEFINE_INT32(ufc_cs60_callprof_delay, 4, "Tests", "Seconds after the walkout start before counting");
REXCVAR_DEFINE_INT32(ufc_cs60_callprof_secs, 10, "Tests", "Seconds counted");
REXCVAR_DEFINE_INT32(ufc_cs60_callprof_scene, 1, "Tests", "Which sub_822B7E10 call (1 = walkout) to profile");
namespace {
constexpr uint32_t kSlots = uint32_t((REX_CODE_SIZE + REX_THUNK_RESERVE_SIZE) / 4);
PPCFunc** g_orig = nullptr;
std::atomic<uint32_t>* g_counts = nullptr;
float* g_lastf1 = nullptr;
uint32_t* g_lastlr = nullptr;
std::atomic<int> g_cp_state{0};  // 0 idle, 1 armed, 2 counting, 3 done
std::atomic<int> g_cp_scene{0};
std::chrono::steady_clock::time_point g_cp_t0;
uint32_t g_cp_frames = 0;
PPCFunc** Table(uint8_t* base) {
  return reinterpret_cast<PPCFunc**>(base + REX_IMAGE_BASE + REX_IMAGE_SIZE);
}
void CountThunk(PPCContext& ctx, uint8_t* base) {
  const uint32_t t = ctx.ctr.u32;
  const uint32_t i = (t - uint32_t(REX_CODE_BASE)) >> 2;
  if (i < kSlots && g_orig[i]) {
    g_counts[i].fetch_add(1, std::memory_order_relaxed);
    g_lastf1[i] = float(ctx.f1.f64);
    g_lastlr[i] = uint32_t(ctx.lr);
    return g_orig[i](ctx, base);
  }
  rex::runtime::ResolveIndirectFunction(t)(ctx, base);
}
void CallProfArm() {
  if (!REXCVAR_GET(ufc_cs60_callprof)) return;
  const int n = ++g_cp_scene;
  if (n != REXCVAR_GET(ufc_cs60_callprof_scene)) return;
  int idle = 0;
  if (g_cp_state.compare_exchange_strong(idle, 1)) {
    g_cp_t0 = std::chrono::steady_clock::now();
    REXLOG_INFO("[ufc-cs60-prof] armed scene={}", n);
  }
}
void CallProfTick(uint8_t* base) {
  const int st = g_cp_state.load();
  if (st != 1 && st != 2) return;
  const auto now = std::chrono::steady_clock::now();
  const double el = std::chrono::duration<double>(now - g_cp_t0).count();
  const int delay = REXCVAR_GET(ufc_cs60_callprof_delay), secs = REXCVAR_GET(ufc_cs60_callprof_secs);
  PPCFunc** table = Table(base);
  if (st == 1) {
    if (el < delay) return;
    if (!g_orig) {
      g_orig = new PPCFunc*[kSlots]();
      g_counts = new std::atomic<uint32_t>[kSlots]();
      g_lastf1 = new float[kSlots]();
      g_lastlr = new uint32_t[kSlots]();
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < kSlots; ++i) {
      PPCFunc* f = table[i];
      if (f && f != &CountThunk) { g_orig[i] = f; ++n; }
    }
    for (uint32_t i = 0; i < kSlots; ++i)
      if (g_orig[i]) table[i] = &CountThunk;
    g_cp_frames = 0;
    g_cp_state = 2;
    REXLOG_INFO("[ufc-cs60-prof] counting {} slots rate={}", n, REX_LOAD_U32(kBlock + 8));
    return;
  }
  ++g_cp_frames;
  if (el < delay + secs) return;
  for (uint32_t i = 0; i < kSlots; ++i)
    if (g_orig[i]) table[i] = g_orig[i];
  g_cp_state = 3;
  std::vector<std::pair<uint32_t, uint32_t>> v;
  for (uint32_t i = 0; i < kSlots; ++i) {
    const uint32_t c = g_counts[i].load();
    if (c >= 10) v.emplace_back(c, uint32_t(REX_CODE_BASE) + i * 4);
  }
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
  REXLOG_INFO("[ufc-cs60-prof] done frames={} secs={} rate={} funcs={}", g_cp_frames, secs,
              REX_LOAD_U32(kBlock + 8), v.size());
  for (size_t k = 0; k < v.size(); k += 8) {
    std::string line;
    for (size_t j = k; j < v.size() && j < k + 8; ++j) {
      const uint32_t i = (v[j].second - uint32_t(REX_CODE_BASE)) >> 2;
      char t[64]; std::snprintf(t, sizeof t, " %08X:%u:%g:%08X", v[j].second, v[j].first, g_lastf1[i], g_lastlr[i]); line += t;
    }
    REXLOG_INFO("[ufc-cs60-prof]{}", line);
  }
}
}  // namespace

// DIAG/TEST (ufc_cs60_watch / ufc_cs60_scale): wrap chosen indirect-call targets through the dispatch
// table. watch: log once per second per target the call count, r3, f1, f2 and the return address.
// scale: multiply f1 by ufc_cs60_scale_factor while a sub_822B7E10 scene runs at a 60 block.
REXCVAR_DEFINE_STRING(ufc_cs60_watch, "", "Tests", "Comma list of hex guest functions to log (indirect calls)");
REXCVAR_DEFINE_STRING(ufc_cs60_scale, "", "Tests", "Comma list of hex guest functions whose f1 is scaled in sub_822B7E10 scenes");
REXCVAR_DEFINE_DOUBLE(ufc_cs60_scale_factor, 0.5, "Tests", "Factor for ufc_cs60_scale");
extern "C" void __imp__sub_822BA8E8(PPCContext& ctx, uint8_t* base);
namespace {
std::atomic<int> g_scene_on{0};
struct Watch { uint32_t addr; PPCFunc* orig; bool scale; std::atomic<uint32_t> n{0}; std::chrono::steady_clock::time_point next{}; };
Watch g_watch[16];
int g_nwatch = 0;
bool g_watch_done = false;
void WatchThunk(PPCContext& ctx, uint8_t* base) {
  const uint32_t t = ctx.ctr.u32;
  for (int k = 0; k < g_nwatch; ++k) {
    Watch& w = g_watch[k];
    if (w.addr != t) continue;
    const uint32_t n = ++w.n;
    const auto now = std::chrono::steady_clock::now();
    const bool scaled = w.scale && g_scene_on.load() && REX_LOAD_U32(kBlock + 8) == 60;
    if (now >= w.next) {
      w.next = now + std::chrono::seconds(1);
      REXLOG_INFO("[ufc-cs60-watch] {:08X} n={} r3={:08X} r4={:08X} f1={} f2={} lr={:08X} rate={} scene={} scaled={}",
                  t, n, ctx.r3.u32, ctx.r4.u32, ctx.f1.f64, ctx.f2.f64, uint32_t(ctx.lr),
                  REX_LOAD_U32(kBlock + 8), g_scene_on.load(), scaled);
    }
    if (scaled) ctx.f1.f64 = double(float(ctx.f1.f64 * REXCVAR_GET(ufc_cs60_scale_factor)));
    return w.orig(ctx, base);
  }
  rex::runtime::ResolveIndirectFunction(t)(ctx, base);
}
void AddList(uint8_t* base, const std::string& list, bool scale) {
  size_t p = 0;
  while (p < list.size() && g_nwatch < 16) {
    size_t e = list.find(',', p);
    if (e == std::string::npos) e = list.size();
    const uint32_t a = uint32_t(std::strtoul(list.substr(p, e - p).c_str(), nullptr, 16));
    p = e + 1;
    const uint32_t i = (a - uint32_t(REX_CODE_BASE)) >> 2;
    if (a < REX_CODE_BASE || i >= kSlots) continue;
    PPCFunc** table = Table(base);
    if (!table[i]) { REXLOG_INFO("[ufc-cs60-watch] {:08X} has no dispatch entry", a); continue; }
    Watch& w = g_watch[g_nwatch];
    w.addr = a; w.orig = table[i]; w.scale = scale;
    ++g_nwatch;
    table[i] = &WatchThunk;
    REXLOG_INFO("[ufc-cs60-watch] wrapped {:08X} scale={}", a, scale);
  }
}
void WatchInstall(uint8_t* base) {
  if (g_watch_done) return;
  g_watch_done = true;
  AddList(base, REXCVAR_GET(ufc_cs60_watch), false);
  AddList(base, REXCVAR_GET(ufc_cs60_scale), true);
}
}  // namespace
// sub_822BA8E8 ends a sub_822B7E10 scene (restores the rate, logged caller 0x822BA980): g_scene_on (set
// by the sub_822B7E10 hook) gates the sub_822A6B08 rate view of the walkout step fix and the watch tool.
REX_HOOK_RAW(sub_822BA8E8) {
  g_scene_on = 0;
  __imp__sub_822BA8E8(ctx, base);
}
