// SELECT FIGHTER: a created fighter's 3D preview no longer stays frozen after flicking through
// the created fighters (--ufc_rex_caf_preview_hold, default on; false = the original timing).
//
// Guest code (identical in Original v48 and Reborn r9):
//  sub_829ABD80(select)   select frame update (vtable). Near its end it calls the preview
//                         manager's update ([select+4536], vtable +4 = sub_8290D870) and then,
//                         always (also without a manager), sub_829A6F58(select). Hooked here:
//                         sub_829A6F58, i.e. right after the preview update of the same frame.
//  sub_8290A770 / sub_8290A0A0  the cursor landing's preview request. Slot of a side:
//                         slot = manager + side*96 + 52. In state 0 the request writes
//                         +12 = the fighter's entry (pending), +24 = 10 (countdown), +32 = id.
//  sub_8290D870 state 0   if +36 > 0 it only counts +36 down; otherwise, with a pending entry,
//                         +24 counts down one per frame and at 0 the build starts (state 1,
//                         +8 = entry, +12 = 0). When [manager+260] is null or its +136 word is 0
//                         (sub_828F72C0) the countdown is cleared and the build starts at once.
//  sub_8290AB60           the only path for ids 1200..1239 (created fighters): the model and its
//                         skin/clothing textures are composed on the CPU/GPU and a running build
//                         cannot be cancelled; a new landing is queued behind it (state 11) and
//                         the new fighter shows only its still portrait until the old build ends.
//
// Fix: while a side's slot is in state 0 with a created fighter pending, this hook keeps the
// countdown at 1 or more until the cursor has rested ufc_rex_caf_preview_rest_ms on that fighter,
// so flicking through the page never starts a build that would then block the next preview.
// A resting cursor starts its build 1-2 frames after the rest time. Roster fighters are not
// touched. A new landing is recognised by a new entry or id, or by the game raising the
// countdown again (it writes 10 on each request). Nothing else in the slot is written.
// Limit: in the countdown-clearing case above (no object at +260, or its +136 is 0) the game
// starts the build in the same update and this hold has no effect (to be checked in game).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <cstdint>

REXCVAR_DEFINE_BOOL(ufc_rex_caf_preview_hold, true, "UFC",
                    "SELECT FIGHTER: start a created fighter's 3D preview build only after the cursor rests "
                    "on it (the build cannot be cancelled, flicking through left the next preview frozen)");
REXCVAR_DEFINE_INT32(ufc_rex_caf_preview_rest_ms, 1500, "UFC",
                     "With ufc_rex_caf_preview_hold: milliseconds the cursor must rest on a created fighter "
                     "before its 3D preview build starts");

extern "C" void __imp__sub_829A6F58(PPCContext& ctx, uint8_t* base);

namespace {
using Clock = std::chrono::steady_clock;

constexpr uint32_t kManager = 4536;  // select + 4536 -> preview manager
constexpr uint32_t kSlot = 52, kSlotStride = 96;
constexpr uint32_t kState = 0, kPending = 12, kCountdown = 24, kId = 32;

struct Rest {
  bool active = false;
  uint32_t manager = 0, pending = 0, id = 0;
  int32_t left = 0;  // countdown as this hook left it
  Clock::time_point since{};
  bool holding = false;
};
Rest g_rest[2];
std::atomic<uint32_t> g_logged{0};

bool GuestPointer(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u; }
bool CreatedFighter(uint32_t id) { return id >= 1200 && id < 1240; }

void HoldCreatedFighterBuild(uint32_t select, uint8_t* base) {
  if (!GuestPointer(select)) return;
  const uint32_t manager = REX_LOAD_U32(select + kManager);
  if (!GuestPointer(manager)) {
    g_rest[0].active = g_rest[1].active = false;
    return;
  }
  const int32_t rest_ms = REXCVAR_GET(ufc_rex_caf_preview_rest_ms);
  const auto now = Clock::now();
  for (uint32_t side = 0; side < 2; ++side) {
    Rest& r = g_rest[side];
    const uint32_t slot = manager + side * kSlotStride + kSlot;
    const uint32_t state = REX_LOAD_U32(slot + kState);
    const uint32_t pending = REX_LOAD_U32(slot + kPending);
    const uint32_t id = REX_LOAD_U32(slot + kId);
    const int32_t countdown = static_cast<int32_t>(REX_LOAD_U32(slot + kCountdown));
    if (state != 0 || pending == 0 || !CreatedFighter(id) || rest_ms <= 0) {
      if (r.holding && state == 1)
        REXLOG_INFO("[ufc-rex-caf-hold] side={} id={} build started", side, r.id);
      r.active = r.holding = false;
      continue;
    }
    const bool landed = !r.active || r.manager != manager || r.pending != pending || r.id != id ||
                        countdown > r.left;
    if (landed) {
      r.active = true;
      r.manager = manager;
      r.pending = pending;
      r.id = id;
      r.since = now;
    }
    const auto rested = std::chrono::duration_cast<std::chrono::milliseconds>(now - r.since).count();
    if (rested < rest_ms) {
      if (countdown < 1) REX_STORE_U32(slot + kCountdown, 1u);
      r.left = countdown < 1 ? 1 : countdown;
      if (!r.holding && g_logged.fetch_add(1, std::memory_order_relaxed) < 32)
        REXLOG_INFO("[ufc-rex-caf-hold] side={} id={} build held until the cursor rests {} ms", side, id,
                    rest_ms);
      r.holding = true;
    } else {
      r.left = countdown;
    }
  }
}
}  // namespace

// Called once per select frame by sub_829ABD80, right after the preview manager update.
REX_HOOK_RAW(sub_829A6F58) {
  if (REXCVAR_GET(ufc_rex_caf_preview_hold)) HoldCreatedFighterBuild(ctx.r3.u32, base);
  __imp__sub_829A6F58(ctx, base);
}
