// Diagnostic: how long SELECT FIGHTER takes to show each side's 3D preview.
// Log only; nothing in guest memory or the game's control flow changes.
//
// Select-screen layout (checked in the Original v48 generated sources):
//  sub_829A8EE8(select, entry, side)   cursor lands on a fighter (side in r5)
//  sub_8290A770(preview, side, ., id)  requests the 3D preview load (side r4, model id r6)
//  sub_829ABD80(select)                per-frame update (vtable); reads preview = select+4536
//                                      (ufc3_recomp.242.cpp:16928)
//  preview + side*96 + 52              load state of that side; 0 or 6 = idle
//  sub_82793B10                        the game's GPU DXT5 compressor (CAF skin bake)
//
// Lines: [ufc-select-preview] with milliseconds since the cursor landed on that side,
// the select-update tick count since then, every state change and the compressor
// calls in between. Compare a created fighter with a roster fighter.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(ufc_select_preview_diag, true, "Tests",
                    "Log SELECT FIGHTER 3D preview load states and timing (log only)");

namespace {
using Clock = std::chrono::steady_clock;

struct Side {
  Clock::time_point landed{};
  bool timing = false;
  uint32_t ticks = 0;          // select updates since landing
  uint32_t state = 0xFFFFFFFFu;
  uint32_t id = 0;
  uint32_t compress_calls = 0;
  double compress_ms = 0;
  uint32_t opens = 0;
  uint32_t loaded = 0;
  double frame_max_ms = 0, frame_sum_ms = 0;
  uint32_t frames = 0, frames_over_20 = 0;
};
Side g_side[2];
std::atomic<int> g_loading_side{-1};  // side that most recently requested a load
uint32_t g_compress_total = 0;

double MsSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
bool On() { return REXCVAR_GET(ufc_select_preview_diag); }
}  // namespace

extern "C" void __imp__sub_8290A770(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_8290A770) {
  if (On()) {
    const int side = ctx.r4.u32 ? 1 : 0;
    Side& s = g_side[side];
    // The cursor landing (sub_829A8EE8) calls this load request in the same frame; start the
    // timing here (the landing itself is hooked by ufc_reborn_caf_page.cpp on Reborn).
    s.landed = Clock::now();
    s.timing = true;
    s.ticks = 0;
    s.compress_calls = 0;
    s.compress_ms = 0;
    s.opens = 0;
    s.loaded = 0;
    s.frame_max_ms = s.frame_sum_ms = 0;
    s.frames = s.frames_over_20 = 0;
    s.id = ctx.r6.u32;
    g_loading_side.store(side, std::memory_order_relaxed);
    REXLOG_INFO("[ufc-select-preview] side={} request id={} attire={} t={:.1f}ms ticks={}", side,
                s.id, ctx.r8.u32 & 1, s.timing ? MsSince(s.landed) : -1.0, s.ticks);
  }
  __imp__sub_8290A770(ctx, base);
}

extern "C" void __imp__sub_829ABD80(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_829ABD80) {
  const uint32_t select = ctx.r3.u32;
  __imp__sub_829ABD80(ctx, base);
  if (!On() || !select) return;
  {
    static Clock::time_point last_update{};
    const auto now = Clock::now();
    if (last_update.time_since_epoch().count()) {
      const double gap = std::chrono::duration<double, std::milli>(now - last_update).count();
      for (Side& sd : g_side)
        if (sd.timing) {
          sd.frame_max_ms = std::max(sd.frame_max_ms, gap);
          sd.frame_sum_ms += gap;
          ++sd.frames;
          if (gap > 20.0) ++sd.frames_over_20;
        }
    }
    last_update = now;
  }
  {
    // One-time scan of the loaded image for the magazine screen's package name.
    static bool scanned = false;
    if (!scanned) {
      scanned = true;
      const char* needles[] = {"CAR_magazine", "magazine", "CAR_news_top"};
      for (const char* needle : needles) {
        const size_t len = std::strlen(needle);
        int found = 0;
        bool page_ok = false;
        for (uint32_t a = 0x82000000u; a < 0x83600000u - 64 && found < 8; ++a) {
          if ((a & 0xFFF) == 0 || a == 0x82000000u) {
#ifdef _WIN32
            MEMORY_BASIC_INFORMATION m{};
            const uint8_t* hp = const_cast<const uint8_t*>(&REX_LOAD_U8(a));
            page_ok = VirtualQuery(hp, &m, sizeof(m)) && m.State == MEM_COMMIT &&
                      !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                      hp + 4096 + 64 <= static_cast<const uint8_t*>(m.BaseAddress) + m.RegionSize;
#else
            page_ok = false;
#endif
          }
          if (!page_ok) { a |= 0xFFF; continue; }
          if (REX_LOAD_U8(a) != uint8_t(needle[0])) continue;
          size_t i = 1;
          while (i < len && REX_LOAD_U8(a + uint32_t(i)) == uint8_t(needle[i])) ++i;
          if (i == len) {
            char ctxs[48];
            size_t k = 0;
            for (; k + 1 < sizeof(ctxs); ++k) {
              const uint8_t c = REX_LOAD_U8(a + uint32_t(k));
              if (c < 0x20 || c > 0x7E) break;
              ctxs[k] = char(c);
            }
            ctxs[k] = 0;
            REXLOG_INFO("[ufc-select-preview] string '{}' at 0x{:08X}: {}", needle, a, ctxs);
            ++found;
          }
        }
      }
    }
  }
  const uint32_t preview = REX_LOAD_U32(select + 4536);
  if (!preview) return;
  for (int side = 0; side < 2; ++side) {
    Side& s = g_side[side];
    if (s.timing) ++s.ticks;
    const uint32_t state = REX_LOAD_U32(preview + side * 96 + 52);
    if (state == 4 && s.state == 4 && s.timing) {
      const uint32_t obj = REX_LOAD_U32(preview + side * 96 + 52 + 16);
      const uint32_t loaded = obj ? REX_LOAD_U32(obj + 1004) : 0;
      if (loaded != s.loaded) {
        s.loaded = loaded;
        REXLOG_INFO("[ufc-select-preview] side={} model loaded(+1004)={} t={:.1f}ms ticks={}", side,
                    loaded, MsSince(s.landed), s.ticks);
      }
    }
    if (state == s.state) continue;
    const bool idle = state == 6;  // 13->0 is the unload before a new load, not the end
    REXLOG_INFO("[ufc-select-preview] side={} id={} state {}->{} t={:.1f}ms ticks={} compress={} "
                "compress_ms={:.1f} opens={}{}",
                side, s.id, int32_t(s.state), state, s.timing ? MsSince(s.landed) : -1.0, s.ticks,
                s.compress_calls, s.compress_ms, s.opens, idle && s.timing ? " READY" : "");
    if (state == 4) {
      // The object state 4 polls (sub_8290A298: vtable +16 = done?, +20 = failed?).
      const uint32_t obj = REX_LOAD_U32(preview + side * 96 + 52 + 16);
      const uint32_t vt = obj ? REX_LOAD_U32(obj) : 0;
      REXLOG_INFO("[ufc-select-preview] side={} wait-object obj=0x{:08X} vtable=0x{:08X} done=0x{:08X} "
                  "failed=0x{:08X}",
                  side, obj, vt, vt ? REX_LOAD_U32(vt + 16) : 0, vt ? REX_LOAD_U32(vt + 20) : 0);
    }
    if (state == 4) {
      // The model's texture pipeline (sub_82BD1760 -> sub_82C47158): three parts at
      // obj+1020+{4,928,1196}, each with vtable +12 = update and +16 = done.
      const uint32_t obj = REX_LOAD_U32(preview + side * 96 + 52 + 16);
      if (obj) {
        for (uint32_t off : {4u, 928u, 1196u}) {
          const uint32_t vt = REX_LOAD_U32(obj + 1020 + off);
          REXLOG_INFO("[ufc-select-preview] side={} part +{} vtable=0x{:08X} update=0x{:08X} done=0x{:08X}",
                      side, off, vt, vt ? REX_LOAD_U32(vt + 12) : 0, vt ? REX_LOAD_U32(vt + 16) : 0);
        }
        REXLOG_INFO("[ufc-select-preview] side={} flags loaded(+1004)={} textures(+2236)={}", side,
                    REX_LOAD_U32(obj + 1004), REX_LOAD_U32(obj + 2236));
      }
    }
    if (idle && s.timing && s.frames)
      REXLOG_INFO("[ufc-select-preview] side={} id={} frames={} avg_ms={:.1f} max_ms={:.1f} over20ms={}", side, s.id,
                  s.frames, s.frame_sum_ms / s.frames, s.frame_max_ms, s.frames_over_20);
    s.state = state;
    if (idle) s.timing = false;
  }
}


// File opens while a preview loads. The four guest wrappers that call NtCreateFile
// (sub_82FF7D68 takes the path in r3; the others are logged the same way, as text when
// r3 points at printable ASCII, otherwise as a number).
namespace {
void LogOpen(const char* fn, PPCContext& ctx, uint8_t* base) {
  if (!On()) return;
  {
    // Menu packages: log which wrapper opens them, whenever (few per screen change).
    const uint32_t a0 = ctx.r3.u32;
    bool readable0 = a0 >= 0x40000000u && a0 < 0xFFFF0000u;
#ifdef _WIN32
    if (readable0) {
      MEMORY_BASIC_INFORMATION m{};
      const uint8_t* host0 = const_cast<const uint8_t*>(&REX_LOAD_U8(a0));
      readable0 = VirtualQuery(host0, &m, sizeof(m)) && m.State == MEM_COMMIT &&
                  !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                  host0 + 96 <= static_cast<const uint8_t*>(m.BaseAddress) + m.RegionSize;
    }
#else
    readable0 = false;
#endif
    if (readable0) {
      char head[96];
      size_t k = 0;
      for (; k + 1 < sizeof(head); ++k) {
        const char c = char(REX_LOAD_U8(a0 + uint32_t(k)));
        if (c < 0x20 || c > 0x7E) break;
        head[k] = c;
      }
      head[k] = 0;
      if (k > 8 && std::strstr(head, ".pac"))
        REXLOG_INFO("[ufc-select-preview] pac-open via={} lr=0x{:08X} path={}", fn, uint32_t(ctx.lr), head);
    }
  }
  const int side = g_loading_side.load(std::memory_order_relaxed);
  if (side < 0 || !g_side[side].timing) return;
  Side& s = g_side[side];
  ++s.opens;
  char path[200];
  size_t n = 0;
  const uint32_t a = ctx.r3.u32;
  bool readable = a >= 0x10000 && a < 0xFFFF0000u;
#ifdef _WIN32
  if (readable) {
    MEMORY_BASIC_INFORMATION m{};
    const uint8_t* host = const_cast<const uint8_t*>(&REX_LOAD_U8(a));
    readable = VirtualQuery(host, &m, sizeof(m)) && m.State == MEM_COMMIT &&
               !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
               host + sizeof(path) <= static_cast<const uint8_t*>(m.BaseAddress) + m.RegionSize;
  }
#endif
  if (readable) {
    for (; n + 1 < sizeof(path); ++n) {
      const char c = char(REX_LOAD_U8(a + uint32_t(n)));
      if (!c) break;
      if (c < 0x20 || c > 0x7E) { n = 0; break; }
      path[n] = c;
    }
  }
  path[n] = 0;
  if (n >= 4)
    REXLOG_INFO("[ufc-select-preview] open side={} n={} t={:.1f}ms ticks={} via={} path={}", side,
                s.opens, MsSince(s.landed), s.ticks, fn, path);
  else
    REXLOG_INFO("[ufc-select-preview] open side={} n={} t={:.1f}ms ticks={} via={} r3=0x{:08X}", side,
                s.opens, MsSince(s.landed), s.ticks, fn, a);
}
}  // namespace
#define UFC_OPEN_HOOK(name)                                           extern "C" void __imp__##name(PPCContext& ctx, uint8_t* base);      REX_HOOK_RAW(name) {                                                  LogOpen(#name, ctx, base);                                          __imp__##name(ctx, base);                                         }
UFC_OPEN_HOOK(sub_82FF7038)
UFC_OPEN_HOOK(sub_82FF7170)
UFC_OPEN_HOOK(sub_82FF7260)
UFC_OPEN_HOOK(sub_82FF7D68)

// The three parts of the model's texture pipeline (obj+1020+{4,928,1196}); their
// "done?" methods. Each logs when its answer changes while a preview loads.
namespace {
void PartDone(int part, uint32_t self, uint32_t result) {
  static uint32_t last[3] = {2, 2, 2};
  static uint32_t last_self[3] = {};
  const int side = g_loading_side.load(std::memory_order_relaxed);
  if (side < 0 || !g_side[side].timing) { last[part] = 2; return; }
  if (result == last[part] && self == last_self[part]) return;
  last[part] = result; last_self[part] = self;
  REXLOG_INFO("[ufc-select-preview] part{} self=0x{:08X} done={} t={:.1f}ms ticks={}", part, self, result,
              MsSince(g_side[side].landed), g_side[side].ticks);
}
}  // namespace
#define UFC_PART_HOOK(name, idx)                                     \
  extern "C" void __imp__##name(PPCContext& ctx, uint8_t* base);     \
  REX_HOOK_RAW(name) {                                               \
    const uint32_t self = ctx.r3.u32;                                \
    __imp__##name(ctx, base);                                        \
    if (On()) PartDone(idx, self, ctx.r3.u32 ? 1u : 0u);             \
  }
UFC_PART_HOOK(sub_82C45790, 0)
UFC_PART_HOOK(sub_82962008, 1)
UFC_PART_HOOK(sub_82C46DD8, 2)



// Texture-builder driver (sub_827F6380): logs its control fields when they change while a
// preview loads (+76 item/phase counter, +72 state, +68, +92, +60 item list).
extern "C" void __imp__sub_827F6380(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_827F6380) {
  const uint32_t self = ctx.r3.u32;
  {
    const int sd = g_loading_side.load(std::memory_order_relaxed);
    static uint32_t first_landing_ticks = 0xFFFFFFFFu;
    if (On() && sd >= 0 && g_side[sd].timing && g_side[sd].ticks < first_landing_ticks + 0 &&
        g_side[sd].ticks <= 400) {
      static Clock::time_point logged_for{};
      if (logged_for != g_side[sd].landed) {
        logged_for = g_side[sd].landed;
        const uint32_t f1 = REX_LOAD_U32(ctx.r1.u32);
        const uint32_t f2 = f1 ? REX_LOAD_U32(f1) : 0;
        const uint32_t f3 = f2 ? REX_LOAD_U32(f2) : 0;
        REXLOG_INFO("[ufc-select-preview] driver first call ticks={} lr=0x{:08X} up1=0x{:08X} up2=0x{:08X} up3=0x{:08X}",
                    g_side[sd].ticks, uint32_t(ctx.lr), f1 ? REX_LOAD_U32(f1 - 8) : 0, f2 ? REX_LOAD_U32(f2 - 8) : 0,
                    f3 ? REX_LOAD_U32(f3 - 8) : 0);
      }
    }
  }
  __imp__sub_827F6380(ctx, base);
  if (!On()) return;
  const int side = g_loading_side.load(std::memory_order_relaxed);
  if (side < 0 || !g_side[side].timing) return;
  static uint32_t last[5] = {};
  const uint32_t now[5] = {REX_LOAD_U32(self + 76), REX_LOAD_U32(self + 72), REX_LOAD_U32(self + 68),
                           REX_LOAD_U32(self + 92), REX_LOAD_U32(self + 64)};
  bool changed = false;
  for (int i = 0; i < 5; ++i) changed |= now[i] != last[i];
  if (!changed) return;
  for (int i = 0; i < 5; ++i) last[i] = now[i];
  REXLOG_INFO("[ufc-select-preview] driver self=0x{:08X} +76={} +72={} +68=0x{:08X} +92={} +64={} ticks={}", self,
              now[0], now[1], now[2], now[3], now[4], g_side[side].ticks);
  {
    const uint32_t list = REX_LOAD_U32(self + 60);
    const uint32_t item = list ? REX_LOAD_U32(list + (now[0] / 2) * 4) : 0;
    const uint32_t vt = item ? REX_LOAD_U32(item) : 0;
    REXLOG_INFO("[ufc-select-preview] driver item=0x{:08X} vtable=0x{:08X} +4=0x{:08X} +12=0x{:08X}", item, vt,
                vt ? REX_LOAD_U32(vt + 4) : 0, vt ? REX_LOAD_U32(vt + 12) : 0);
    if (item) {
      const uint32_t layers = REX_LOAD_U32(item + 48), count = REX_LOAD_U32(item + 44);
      for (uint32_t i = 0; i < count && i < 4 && layers; ++i) {
        const uint32_t layer = REX_LOAD_U32(layers + i * 4);
        const uint32_t obj = layer ? REX_LOAD_U32(layer + 84) : 0;
        const uint32_t lvt = obj ? REX_LOAD_U32(obj) : 0;
        REXLOG_INFO("[ufc-select-preview] driver layer {}/{} layer=0x{:08X} obj=0x{:08X} vtable=0x{:08X} ready=0x{:08X}",
                    i, count, layer, obj, lvt, lvt ? REX_LOAD_U32(lvt + 8) : 0);
      }
    }
  }
}


// Texture level description used by the compressor job (sub_82794378 calls
// sub_823C0098(texture, level, desc) for the source and the destination of each level).
// Logged only for calls from the job: texture header fetch words and the returned desc.
extern "C" void __imp__sub_823C0098(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_823C0098) {
  const uint32_t lr = uint32_t(ctx.lr), tex = ctx.r3.u32, level = ctx.r4.u32, desc = ctx.r5.u32;
  __imp__sub_823C0098(ctx, base);
  if (!On() || lr < 0x82794378u || lr >= 0x82794700u) return;
  static uint32_t logged = 0;
  if (logged >= 120) return;
  ++logged;
  char words[16 * 9 + 1];
  size_t n = 0;
  for (uint32_t i = 0; i < 16; ++i) n += size_t(std::snprintf(words + n, sizeof(words) - n, " %08X", REX_LOAD_U32(desc + i * 4)));
  char fetch[6 * 9 + 1];
  size_t m = 0;
  for (uint32_t i = 0; i < 6; ++i) m += size_t(std::snprintf(fetch + m, sizeof(fetch) - m, " %08X", REX_LOAD_U32(tex + 28 + i * 4)));
  REXLOG_INFO("[ufc-select-preview] leveldesc lr=0x{:08X} tex=0x{:08X} level={} fetch28:{} desc:{}", lr, tex, level, fetch, words);
}

// Roster list builder (sub_82EF6038, list object in r3): created fighters enter the list
// only when the request flags (+8) have bit 0x4, the CAF table (0x83577444) is loaded, and
// each slot passes the game's filters; their count ends at +32 (+36). Logged per build.
extern "C" void __imp__sub_82EF6038(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82EF6038) {
  const uint32_t list = ctx.r3.u32;
  const uint32_t flags = (list >= 0x40000000u && list < 0xFFFF0000u) ? REX_LOAD_U32(list + 8) : 0;
  const uint32_t table = REX_LOAD_U32(0x83577444u);
  __imp__sub_82EF6038(ctx, base);
  if ((list >= 0x40000000u && list < 0xFFFF0000u))
    REXLOG_INFO("[ufc-reborn-caf] list=0x{:08X} flags=0x{:08X} caf_table=0x{:08X} counts ufc={} pride={} p2={}+{} caf={} total={}",
                list, flags, table, REX_LOAD_U32(list + 16), REX_LOAD_U32(list + 20), REX_LOAD_U32(list + 24),
                REX_LOAD_U32(list + 28), REX_LOAD_U32(list + 32), REX_LOAD_U32(list + 12));
}

// Page count used by the page-change routine (sub_829A9620 calls sub_82EF5488(list, page)
// and skips pages whose count is 0). Logged for calls from that routine.
extern "C" void __imp__sub_82EF5488(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82EF5488) {
  const uint32_t lr = uint32_t(ctx.lr), list = ctx.r3.u32, page = ctx.r4.u32;
  __imp__sub_82EF5488(ctx, base);
  if (lr >= 0x829A9620u && lr < 0x829AA300u)
    REXLOG_INFO("[ufc-reborn-caf] page-count lr=0x{:08X} list=0x{:08X} page={} count={} (+16={} +20={} +32={} flags=0x{:X})",
                lr, list, page, ctx.r3.u32, REX_LOAD_U32(list + 16), REX_LOAD_U32(list + 20), REX_LOAD_U32(list + 32),
                REX_LOAD_U32(list + 8));
}

// Who reads the roster table (sub_82A77658): per-caller counts, logged once per second.
#include <mutex>
#include <unordered_map>
extern "C" void __imp__sub_827A3620(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_827A3620) {
  static std::mutex m;
  static std::unordered_map<uint32_t, uint32_t> counts;
  static Clock::time_point next = Clock::now();
  const uint32_t lr = uint32_t(ctx.lr);
  __imp__sub_827A3620(ctx, base);
  if (lr < 0x829A9620u || lr >= 0x829AA268u) return;
  REXLOG_INFO("[ufc-roster-callers] now lr=0x{:08X} r3=0x{:08X} +8={} +4=0x{:X}", lr, ctx.r3.u32, REX_LOAD_U32(ctx.r3.u32 + 8), REX_LOAD_U32(ctx.r3.u32 + 4));
  return;
  std::lock_guard<std::mutex> lock(m);
  ++counts[lr];
  if (Clock::now() >= next) {
    next = Clock::now() + std::chrono::seconds(1);
    std::string line;
    for (auto& [k, v] : counts) { char b[32]; std::snprintf(b, sizeof b, " %08X:%u", k, v); line += b; }
    counts.clear();
    REXLOG_INFO("[ufc-roster-callers]{}", line);
  }
}

// CAF portrait lookup (sub_82AA50B8 -> sub_82AA4F10): table +372 count, +380 ids, +388 status
// (2 = loaded), +384 textures, +392 default portrait.
extern "C" void __imp__sub_82AA50B8(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82AA50B8) {
  const uint32_t mgr = ctx.r3.u32, id = ctx.r5.u32, lr = uint32_t(ctx.lr);
  __imp__sub_82AA50B8(ctx, base);
  if (id < 1200 || id >= 1240 || mgr < 0x40000000u) return;
  static Clock::time_point next = Clock::now();
  if (Clock::now() < next) return;
  next = Clock::now() + std::chrono::seconds(2);
  const uint32_t n = REX_LOAD_U32(mgr + 372), ids = REX_LOAD_U32(mgr + 380), st = REX_LOAD_U32(mgr + 388);
  std::string l;
  for (uint32_t i = 0; i < n && i < 48 && ids >= 0x40000000u && st >= 0x40000000u; ++i)
    l += " " + std::to_string(REX_LOAD_U32(ids + 4 * i)) + ":" + std::to_string(REX_LOAD_U32(st + 4 * i));
  REXLOG_INFO("[ufc-caf-portrait] lr=0x{:08X} id={} result=0x{:08X} default=0x{:08X} n={}{}", lr, id, ctx.r3.u32,
              REX_LOAD_U32(mgr + 392), n, l);
}


// One-time search of guest memory for the Reborn face pack names (grid portrait source).
#include <windows.h>
namespace {
void FindFaceStrings(uint8_t* base) {
  static bool done = false;
  if (done) return;
  done = true;
  const char* needles[] = {"fighterFace", "facepack", "CAF_face_icon", "CAF_head_icon", "face_%", "face%"};
  for (uint32_t a = 0x82000000u; a < 0x84000000u; a += 0x10000) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(base + a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
    const uint8_t* p = base + a;
    for (uint32_t i = 0; i + 16 < 0x10000; ++i)
      for (const char* n : needles)
        if (p[i] == uint8_t(n[0]) && std::memcmp(p + i, n, std::strlen(n)) == 0)
          REXLOG_INFO("[ufc-facestr] {:08X} {}", a + i, std::string(reinterpret_cast<const char*>(p + i), strnlen(reinterpret_cast<const char*>(p + i), 64)));
  }
}
}
extern "C" void __imp__sub_829A1358(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_829A1358) { FindFaceStrings(base); __imp__sub_829A1358(ctx, base); }

// Face portrait names formatted by the game (format string address in r5, value in r6).
extern "C" void __imp__sub_827A0878(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_827A0878) {
  const uint32_t fmt = ctx.r5.u32, dst = ctx.r3.u32, lr = uint32_t(ctx.lr), v = ctx.r6.u32;
  __imp__sub_827A0878(ctx, base);
  if (fmt < 0x82000000u || fmt >= 0x82200000u) return;
  const char* f = reinterpret_cast<const char*>(base + fmt);
  if (!std::strstr(f, "fc") && !std::strstr(f, "face")) return;
  static int n = 0;
  if (++n > 400) return;
  REXLOG_INFO("[ufc-facefmt] lr=0x{:08X} fmt={} v={} -> {}", lr, f, v,
              std::string(reinterpret_cast<const char*>(base + dst), strnlen(reinterpret_cast<const char*>(base + dst), 80)));
}
