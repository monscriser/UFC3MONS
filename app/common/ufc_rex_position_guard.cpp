// Match load: the fighters' position graph never reads guest address 0 (--ufc_rex_position_guard,
// default on; false = the original, which crashes loading e.g. Liddell vs Quarry in PRIDE).
//
// Guest code (identical in Original v48 and Reborn r9):
//  sub_82905C48(obj, p)  builds player p's position list: n = [obj + 4 + 4p] entries of 852
//                        bytes at entry(k) = obj + 12 + (p*128 + k)*852 (room for 128 per player,
//                        sub_82902C08 clears them). Entry: +0 position id, +4 4 bytes copied from
//                        the position table, two target lists of 32 slots at +12 and +428
//                        (ids at +0, 5-byte values at +128, 4-byte values at +288; an empty slot
//                        is 0 / 0 / 0xFF), their counts at +844 and +848.
//                        It then calls sub_829030A8 and stores r3 as the new n, then sub_82903898.
//  sub_829030A8(obj, p)  for i = 0 .. n-1 (n grows while it runs), for each target of both
//                        lists: if no entry k != i has that id, entry(n) = {target, 4 bytes of
//                        posTbl entry target}, n++. posTbl = [0x8357498C]: +4 count, +8 table of
//                        8-byte entries; a target < 0 or >= count gives a null entry and the
//                        copy reads guest address 0. There is no check against 128 entries.
//  sub_82903898(obj, p)  for every list target finds its entry and reads its +848 with no
//                        null check (a target with no entry reads guest 0x350).
//
// Fix: before the game's own sub_829030A8 runs, this hook walks the lists in the same order with
// the same duplicate rule and removes from its list (all three arrays, count lowered, freed slot
// cleared as sub_82902C08 does) every target that would be appended but is outside posTbl, or
// would be appended past the 128th entry. A list count outside 0..32 is clamped. Then the
// original runs on the cleaned lists and gives the same result as before for every valid target,
// and sub_82903898 finds an entry for every remaining target. With valid data nothing is changed.
// Each removal is logged (first 64).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdint>
#include <vector>

REXCVAR_DEFINE_BOOL(ufc_rex_position_guard, true, "UFC",
                    "Match load: drop fighter position links that point outside the position table or past "
                    "128 positions instead of reading guest address 0 (PRIDE Liddell vs Quarry crash)");

extern "C" void __imp__sub_829030A8(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kPosTablePtr = 0x8357498C;
constexpr uint32_t kEntrySize = 852;
constexpr int32_t kMaxEntries = 128;
constexpr int32_t kListSlots = 32;
constexpr uint32_t kListItems[2] = {12, 428};
constexpr uint32_t kListCount[2] = {844, 848};
constexpr uint32_t kValues5 = 128, kValues4 = 288;

std::atomic<uint32_t> g_logged{0};

void CopyBytes(uint8_t* base, uint32_t dst, uint32_t src, uint32_t n) {
  for (uint32_t b = 0; b < n; ++b) REX_STORE_U8(dst + b, REX_LOAD_U8(src + b));
}

void FillBytes(uint8_t* base, uint32_t dst, uint8_t v, uint32_t n) {
  for (uint32_t b = 0; b < n; ++b) REX_STORE_U8(dst + b, v);
}

void RemoveTarget(uint8_t* base, uint32_t items, int32_t count, int32_t slot) {
  for (int32_t s = slot; s + 1 < count; ++s) {
    CopyBytes(base, items + 4 * s, items + 4 * (s + 1), 4);
    CopyBytes(base, items + kValues5 + 5 * s, items + kValues5 + 5 * (s + 1), 5);
    CopyBytes(base, items + kValues4 + 4 * s, items + kValues4 + 4 * (s + 1), 4);
  }
  const uint32_t last = static_cast<uint32_t>(count - 1);
  FillBytes(base, items + 4 * last, 0, 4);
  FillBytes(base, items + kValues5 + 5 * last, 0, 5);
  FillBytes(base, items + kValues4 + 4 * last, 0xFF, 4);
}

// Returns the entry count the original will reach, or -1 if the input is not usable.
int32_t CleanTargetLists(uint32_t obj, uint32_t p, uint8_t* base) {
  auto entry = [&](int32_t k) { return obj + 12 + (p * kMaxEntries + static_cast<uint32_t>(k)) * kEntrySize; };
  const int32_t n0 = static_cast<int32_t>(REX_LOAD_U32(obj + 4 + 4 * p));
  if (n0 < 0 || n0 > kMaxEntries) return -1;
  const uint32_t table = REX_LOAD_U32(kPosTablePtr);
  const uint32_t table_entries = table ? REX_LOAD_U32(table + 8) : 0;
  const int32_t table_count = table ? static_cast<int32_t>(REX_LOAD_U32(table + 4)) : 0;
  std::vector<int32_t> ids;
  ids.reserve(kMaxEntries);
  for (int32_t k = 0; k < n0; ++k) ids.push_back(static_cast<int32_t>(REX_LOAD_U32(entry(k))));
  for (int32_t i = 0; i < static_cast<int32_t>(ids.size()); ++i) {
    const uint32_t e = entry(i);
    for (int list = 0; list < 2; ++list) {
      const uint32_t items = e + kListItems[list];
      const uint32_t count_addr = e + kListCount[list];
      int32_t count = static_cast<int32_t>(REX_LOAD_U32(count_addr));
      if (count < 0 || count > kListSlots) {
        if (g_logged.fetch_add(1, std::memory_order_relaxed) < 64)
          REXLOG_WARN("[ufc-rex-positions] player {} position {} list {}: count {} clamped", p, ids[i], list, count);
        count = count < 0 ? 0 : kListSlots;
        REX_STORE_U32(count_addr, static_cast<uint32_t>(count));
      }
      for (int32_t j = 0; j < count; ++j) {
        const int32_t target = static_cast<int32_t>(REX_LOAD_U32(items + 4 * j));
        bool known = false;
        for (int32_t k = 0; k < static_cast<int32_t>(ids.size()) && !known; ++k)
          known = k != i && ids[k] == target;
        if (known) continue;
        const bool outside = table_entries == 0 || table_count == 0 || target < 0 || target >= table_count;
        const bool full = static_cast<int32_t>(ids.size()) >= kMaxEntries;
        if (!outside && !full) {
          ids.push_back(target);
          continue;
        }
        if (g_logged.fetch_add(1, std::memory_order_relaxed) < 64)
          REXLOG_WARN("[ufc-rex-positions] player {} position {} list {}: target {} removed ({})", p, ids[i], list,
                      target, outside ? "outside the position table" : "128 positions already");
        RemoveTarget(base, items, count, j);
        REX_STORE_U32(count_addr, static_cast<uint32_t>(--count));
        --j;
      }
    }
  }
  return static_cast<int32_t>(ids.size());
}
}  // namespace

REX_HOOK_RAW(sub_829030A8) {
  const uint32_t obj = ctx.r3.u32, p = ctx.r4.u32;
  if (!REXCVAR_GET(ufc_rex_position_guard) || p > 1 || obj < 0x40000000u || obj >= 0xFFFF0000u) {
    __imp__sub_829030A8(ctx, base);
    return;
  }
  const int32_t expected = CleanTargetLists(obj, p, base);
  __imp__sub_829030A8(ctx, base);
  if (expected >= 0 && ctx.r3.s32 != expected)
    REXLOG_WARN("[ufc-rex-positions] player {}: {} positions, expected {}", p, ctx.r3.s32, expected);
}
