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
#include <cstdint>

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

namespace { void ProbeScene(uint8_t* base); extern std::atomic<uint32_t> g_probe_obj; }
namespace { void CallProfTick(uint8_t* base); }
REX_HOOK_RAW(sub_8273B540) {
  for (int spin = 0; g_rate_masked.load() && spin < 1000000; ++spin) {}
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
std::atomic<uint32_t> g_step_logs{0};
}  // namespace
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
  __imp__sub_822ABF60(ctx, base);
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
