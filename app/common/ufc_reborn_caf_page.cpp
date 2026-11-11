// Reborn: created fighters (CAF) on SELECT FIGHTER (--ufc_reborn_caf_page, default on).
//
// The select screen keeps five pages per side (side object +40; each page has its own cursor
// at +44 + 4*page): 0 = UFC roster, 1 = PRIDE roster, 2 = created fighters, 3 = DLC,
// 4 = random. Reborn's trigger handler (mod code at 0x8367F000) sets the page so that the
// game's page step (sub_829A9620: +1 or -1 with wrap, repeated while the page is empty, then
// the grid, name and panel refresh) never reaches pages 2 and 3: the cycle is
// 0 -> 1 -> 4 -> 0 one way and 0 -> 4 -> 1 -> 0 the other, so created fighters cannot be
// picked in Exhibition.
//
// At the entry of the page step the starting page is adjusted so the step lands on page 2
// when it holds created fighters, always right after the random page:
//   step +1 (r21 bit 2): 0 -> 1 -> 4 -> 2 -> 0;
//   step -1 (r21 bit 8): 0 -> 4 -> 2 -> 1 -> 0.
// (Reborn's grid is rebuilt only when it comes back from the random page, which hides it;
// entering page 2 straight from the UFC or PRIDE page kept that page's portraits.)
// The rest of the step is the game's own code, so the grid lists only the created fighters
// (it wraps inside the page's count) and the name, model and panel refresh as on any page.
// The page each side shows is tracked at the cursor landing (sub_829A8EE8) and after each step.
// Built only into the Reborn executables.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <cstdint>

REXCVAR_DEFINE_BOOL(ufc_reborn_caf_page, true, "UFC",
                    "Reborn: include the created fighters page in the SELECT FIGHTER page cycle");

extern "C" void __imp__sub_829A9620(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_829A8EE8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82EF5488(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82A77658(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kSideObj[2] = {3448, 3452};
constexpr uint32_t kPage = 40;
constexpr uint32_t kUfc = 0, kPride = 1, kCaf = 2, kDlc = 3, kRandom = 4;
constexpr uint32_t kUnknown = 0xFFFFFFFFu;
uint32_t g_page[2] = {kUnknown, kUnknown};

bool GuestPointer(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u; }
// Calls a guest function by address (virtual methods), like a bctrl in recompiled code.
void CallGuest(PPCContext& ctx, uint8_t* base, uint32_t target) { REX_CALL_INDIRECT_FUNC(target); }

// Fighters on the created fighters page (sub_82EF5488(list, page), as the step itself asks).
uint32_t CafCount(PPCContext& ctx, uint8_t* base) {
  const uint32_t holder = ctx.r18.u32;
  if (!GuestPointer(holder) || !GuestPointer(REX_LOAD_U32(holder))) return 0;
  PPCContext call = ctx;
  call.r3.u64 = REX_LOAD_U32(holder);
  call.r4.u64 = kCaf;
  __imp__sub_82EF5488(call, base);
  return call.r3.u32;
}
}  // namespace

// Page step. Live registers at this entry (sub_829A9480 and Reborn's handler branch here):
// r30 = side object, r22 = side (0/1), r21 = step mask (bit 8 = -1, else bit 2 = +1),
// r18 = list holder, r11 = page to step from (copied to r24, compared after the step to
// decide whether to refresh).
REX_HOOK_RAW(sub_829A9620) {
  const uint32_t side_obj = ctx.r30.u32;
  const uint32_t side = ctx.r22.u32 ? 1 : 0;
  const uint32_t mask = ctx.r21.u32;
  const uint32_t last = g_page[side];
  if (REXCVAR_GET(ufc_reborn_caf_page) && GuestPointer(side_obj) && (mask & (2u | 8u)) && last != kUnknown) {
    const bool up = (mask & 8u) == 0;
    uint32_t start = kUnknown;
    if (up && last == kRandom && CafCount(ctx, base) > 0) start = kPride;   // 4 -> 2
    else if (up && last == kCaf) start = kRandom;                           // 2 -> 0
    else if (!up && last == kRandom && CafCount(ctx, base) > 0) start = kDlc;  // 4 -> 2
    else if (!up && last == kCaf) start = kCaf;                                // 2 -> 1
    if (start != kUnknown) {
      REX_STORE_U32(side_obj + kPage, start);
      ctx.r11.u64 = last;  // old page for the refresh test (r24)
    }
  }
  __imp__sub_829A9620(ctx, base);
  if (GuestPointer(side_obj)) {
    const uint32_t page = REX_LOAD_U32(side_obj + kPage);
    if (page != last) REXLOG_INFO("[ufc-reborn-caf] side={} page {} -> {}", side, int32_t(last), page);
    g_page[side] = page;
  }
}

// Cursor landing (select, entry, side): records the page each side shows, also when the
// screen opens again.
REX_HOOK_RAW(sub_829A8EE8) {
  const uint32_t select = ctx.r3.u32;
  const uint32_t side = ctx.r5.u32 ? 1 : 0;
  __imp__sub_829A8EE8(ctx, base);
  if (GuestPointer(select)) {
    const uint32_t side_obj = REX_LOAD_U32(select + kSideObj[side]);
    if (GuestPointer(side_obj)) g_page[side] = REX_LOAD_U32(side_obj + kPage);
  }
}

// Confirm on the created fighters page. Reborn's confirm (sub_829A985C) leaves a created fighter
// unselected (+16 stays -1, no sound, nothing happens). When the press reached the confirm
// with the same gates the game checks (side not on the random choice +8 == -1, pressed
// buttons r14 matching the confirm mask *(r17+12) & *r16, no block flag at r1+96), do what the
// game does for a normal pick (0x829A9BD4..0x829A9D58): +16 = model id, +12 = record +404,
// the confirm sound (select virtual +152 with r4 = 1) and +24 = 1.
extern "C" void __imp__sub_829A985C(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_829A985C) {
  const uint32_t so = ctx.r30.u32, select = ctx.r31.u32;
  const bool candidate = REXCVAR_GET(ufc_reborn_caf_page) && GuestPointer(so) && GuestPointer(select) &&
                         GuestPointer(ctx.r16.u32) && GuestPointer(ctx.r17.u32) && GuestPointer(ctx.r18.u32) &&
                         REX_LOAD_U32(so + kPage) == kCaf && REX_LOAD_U32(so + 8) == kUnknown &&
                         REX_LOAD_U32(so + 16) == kUnknown && ctx.r14.u32 != 0 &&
                         (REX_LOAD_U32(ctx.r17.u32 + 12) & REX_LOAD_U32(ctx.r16.u32)) != 0 &&
                         REX_LOAD_U32(ctx.r1.u32 + 96) == 0;
  const PPCContext entry = ctx;
  __imp__sub_829A985C(ctx, base);
  if (!candidate || REX_LOAD_U32(so + 16) != kUnknown || REX_LOAD_U32(so + 24) != 0) return;
  PPCContext c = entry;
  c.r3.u64 = REX_LOAD_U32(entry.r18.u32);
  __imp__sub_82A77658(c, base);
  if (!GuestPointer(c.r3.u32)) return;
  const uint32_t rec = c.r3.u32 + REX_LOAD_U32(so + 20) * 416;
  const uint32_t id = REX_LOAD_U32(rec + 396);
  if (id < 1200 || id >= 1240) return;
  REX_STORE_U32(so + 16, id);
  REX_STORE_U32(so + 12, REX_LOAD_U32(rec + 404));
  const uint32_t method = REX_LOAD_U32(REX_LOAD_U32(select) + 152);
  if (method >= 0x82000000u && method < 0x84000000u) {
    PPCContext v = entry;
    v.r3.u64 = select;
    v.r4.u64 = 1;
    CallGuest(v, base, method);
  }
  REX_STORE_U32(so + 24, 1);
  REXLOG_INFO("[ufc-reborn-caf] side={} created fighter {} confirmed", entry.r22.u32 ? 1 : 0, id);
}
