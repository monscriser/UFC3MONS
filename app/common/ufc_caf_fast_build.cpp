// Created fighters (CAF) appear sooner on SELECT FIGHTER and wherever the game builds
// their textures (--ufc_caf_fast_build, default on; false = the original pace).
//
// Measured on the Original v48 build (10/11/2026): the created fighter's 3D preview took
// ~4.4 s against ~0.3 s for a roster fighter, with no file reads, idle CPU and 60 fps.
// The time is the game's own pacing. sub_827F3108 builds each skin/clothing texture in
// seven stages (stage at +1792): 0..5 issue the composite passes, 6 waits until the GPU
// result can be compressed (sub_827FE000 -> +2600) and returns 1 when the texture is done.
// It advances one stage per call and its caller (sub_827F6380) reaches it every other
// frame, so each texture costs ~18 frames and a created fighter has seven of them.
//
// With the option on, stages 0..5 run back to back in the same call; stage 6 keeps its
// original per-call wait, so the GPU result is still ready before compression. The GPU
// executes the passes in order, so the textures come out the same, only in fewer frames.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <rex/filesystem.h>

REXCVAR_DEFINE_BOOL(ufc_caf_fast_build, true, "UFC",
                    "Build created-fighter textures without the one-stage-per-call pacing");
REXCVAR_DEFINE_BOOL(ufc_caf_compress_now, true, "UFC",
                    "Compress a finished created-fighter texture at once instead of on the next frame");
REXCVAR_DEFINE_INT32(ufc_caf_model_updates, 64, "UFC",
                      "Created-fighter model updates per call while it waits for parts or textures (1 = original)");
REXCVAR_DEFINE_INT32(ufc_caf_part_wait_us, 0, "UFC",
                      "Microseconds a created-fighter part lookup may wait per call for a read that is in flight (0 = none)");
REXCVAR_DEFINE_INT32(ufc_caf_frame_budget_us, 2000, "UFC",
                      "Extra created-fighter build work allowed per call, in microseconds; keeps the frame rate");
REXCVAR_DEFINE_BOOL(ufc_caf_fast_build_log, false, "Tests",
                    "Log each created-fighter texture builder call");

// Frame-rate guard: every extra step below (beyond the game's own call) runs only while the
// outermost hooked call on this thread is within ufc_caf_frame_budget_us. On a slower PC the
// build simply spreads over more frames, as in the original, instead of lengthening a frame.
namespace {
thread_local bool tl_budget_active = false;
thread_local std::chrono::steady_clock::time_point tl_budget_end{};
struct BudgetScope {
  bool outer = false;
  BudgetScope() {
    if (!tl_budget_active) {
      tl_budget_active = outer = true;
      tl_budget_end = std::chrono::steady_clock::now() +
                      std::chrono::microseconds(std::max(0, REXCVAR_GET(ufc_caf_frame_budget_us)));
    }
  }
  ~BudgetScope() {
    if (outer) tl_budget_active = false;
  }
};
bool BudgetLeft() { return std::chrono::steady_clock::now() < tl_budget_end; }
// The game's file thread completes a read within about a millisecond of the request, but the
// game only looks again at its next update (2..3 frames later) and requests the next file
// after that. Waiting ~100 us at a time, and only inside the frame budget, lets one call see
// the read it just issued without ever lengthening the frame past the budget.
bool WaitForRead() {
  if (!BudgetLeft()) return false;
  const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(100);
  while (std::chrono::steady_clock::now() < until) std::this_thread::yield();
  return true;
}
}  // namespace


// The builder ends each texture by queueing the game's DXT compressor job (sub_82794378)
// on the render-thread job queue (push sub_8274A0D8; the queue runs once per frame in
// sub_82749490, on the same thread that runs the builder). With ufc_caf_compress_now the
// job runs at once, in the queue's own context, but only when the push comes from the
// builder and from the thread that runs the queue; anything else is queued as before.
namespace {
constexpr uint32_t kCompressJob = 0x82794378u;
std::atomic<std::thread::id> g_queue_thread{};
thread_local bool tl_in_builder = false;
thread_local bool tl_compressed_now = false;
thread_local uint32_t tl_builder_item = 0;       // texture item the builder is working on
thread_local uint32_t tl_compress_item = 0;      // item of a compress job run at once
std::atomic<uint32_t> g_queued_compress_item{0};  // item of the last compress job queued
}  // namespace
extern "C" void __imp__sub_82749490(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82749490) {
  g_queue_thread.store(std::this_thread::get_id(), std::memory_order_relaxed);
  __imp__sub_82749490(ctx, base);
}
extern "C" void __imp__sub_8279C6C8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8279D358(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82794378(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_8274A0D8(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_8274A0D8) {
  if (tl_in_builder && ctx.r4.u32 == kCompressJob) g_queued_compress_item.store(tl_builder_item);
  if (REXCVAR_GET(ufc_caf_compress_now) && tl_in_builder && tl_budget_active && BudgetLeft() &&
      ctx.r4.u32 == kCompressJob &&
      g_queue_thread.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
    const uint32_t queue = ctx.r3.u32, argument = ctx.r5.u32;
    // Same context switch as the queue runner around its jobs.
    ctx.r3.u64 = queue;
    __imp__sub_8279C6C8(ctx, base);
    const uint32_t previous = ctx.r3.u32;
    ctx.r3.u64 = 5;
    __imp__sub_8279D358(ctx, base);
    ctx.r3.u64 = argument;
    tl_compress_item = tl_builder_item;
    __imp__sub_82794378(ctx, base);
    tl_compress_item = 0;
    ctx.r3.u64 = previous;
    __imp__sub_8279D358(ctx, base);
    tl_compressed_now = true;
    if (REXCVAR_GET(ufc_caf_fast_build_log))
      REXLOG_INFO("[ufc-caf-fast-build] compressed now job_arg=0x{:08X}", argument);
    return;
  }
  __imp__sub_8274A0D8(ctx, base);
}

extern "C" void __imp__sub_827F3108(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_827F3108) {
  constexpr uint32_t kStage = 1792, kIndex = 1788, kWaitStage = 6, kMaxSteps = 8;
  const uint32_t self = ctx.r3.u32, mode = ctx.r4.u32;
  const uint32_t first_stage = REX_LOAD_U32(self + kStage);
  // Keep the caller's non-volatile view intact: the original only uses r3/r4 as inputs.
  PPCContext entry = ctx;
  BudgetScope budget;
  const bool outer = !tl_in_builder;
  const uint32_t previous_item = tl_builder_item;
  tl_builder_item = self;
  tl_in_builder = true;
  tl_compressed_now = false;
  __imp__sub_827F3108(ctx, base);
  uint32_t steps = 1;
  if (REXCVAR_GET(ufc_caf_fast_build)) {
    while (steps < kMaxSteps && ctx.r3.u32 == 0 && BudgetLeft()) {
      const uint32_t stage = REX_LOAD_U32(self + kStage);
      // Composite stages (1..5 reached from 0..4) are chained; stage 6 is called again
      // only when its compression just ran at once, to collect the finished texture.
      // The item index must stay at 0 (one item per texture, as in every CAF texture seen).
      const bool composite = stage != 0 && stage < kWaitStage;
      const bool collect = stage == kWaitStage && tl_compressed_now;
      if (!(composite || collect) || REX_LOAD_U32(self + kIndex) != 0) break;
      tl_compressed_now = false;
      ctx = entry;
      ctx.r3.u64 = self;
      ctx.r4.u64 = mode;
      __imp__sub_827F3108(ctx, base);
      ++steps;
    }
  }
  if (outer) tl_in_builder = false;
  tl_builder_item = previous_item;
  if (REXCVAR_GET(ufc_caf_fast_build_log))
    REXLOG_INFO("[ufc-caf-fast-build] self=0x{:08X} type={} stage {}->{} steps={} ret={}", self,
                REX_LOAD_U32(self + 8), first_stage, REX_LOAD_U32(self + kStage), steps, ctx.r3.u32);
}

// Before the textures, the created fighter's model (state machine sub_827E7220, state 5)
// resolves its parts one per call: sub_827F8BF0 handles item +8 of +4 and returns 1 when
// all are done. A part that is not available yet returns 0 without advancing, which is
// kept: the loop below only repeats while the index moves forward.
extern "C" void __imp__sub_827F8BF0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_827F8B28(PPCContext& ctx, uint8_t* base);
REXCVAR_DEFINE_BOOL(ufc_caf_prefetch_parts, true, "UFC",
                    "Request all of a created fighter's parts at once instead of one after the other");
namespace {
// sub_827F8B28(list, index) requests part `index` (its lookup first reuses an existing
// resource, sub_827EE6E8 -> sub_8224A9C8). The game requests the next part only after the
// previous one arrived; requesting the rest up front lets the file thread read them while
// the frames go on. Done once per list object, when it starts at index 0.
thread_local uint32_t tl_prefetched_list = 0;
void PrefetchParts(PPCContext& ctx, uint8_t* base, uint32_t list) {
  const uint32_t count = REX_LOAD_U32(list + 4), index = REX_LOAD_U32(list + 8);
  if (!REXCVAR_GET(ufc_caf_prefetch_parts) || list == tl_prefetched_list || index != 0 || count > 256) return;
  tl_prefetched_list = list;
  PPCContext saved = ctx;
  for (uint32_t i = 1; i < count; ++i) {
    ctx = saved;
    ctx.r3.u64 = list;
    ctx.r4.u64 = i;
    __imp__sub_827F8B28(ctx, base);
  }
  ctx = saved;
  if (REXCVAR_GET(ufc_caf_fast_build_log))
    REXLOG_INFO("[ufc-caf-fast-build] parts prefetched list=0x{:08X} count={}", list, count);
}
}  // namespace
REX_HOOK_RAW(sub_827F8BF0) {
  PrefetchParts(ctx, base, ctx.r3.u32);
  constexpr uint32_t kMaxSteps = 256;
  const uint32_t self = ctx.r3.u32;
  const uint32_t first = REX_LOAD_U32(self + 8);
  PPCContext entry = ctx;
  BudgetScope budget;
  __imp__sub_827F8BF0(ctx, base);
  uint32_t steps = 1;
  if (REXCVAR_GET(ufc_caf_fast_build)) {
    uint32_t index = REX_LOAD_U32(self + 8);
    uint32_t previous = first;
    // Each part is a read that the game's file thread (sub_822D2678) finishes a few
    // milliseconds after the request, which this call issued for the next part. Instead of
    // seeing it only at the next model update, look again within this call (ufc_caf_part_wait_us,
    // default 0: never sleep; the frame budget bounds the extra calls).
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(std::max(0, REXCVAR_GET(ufc_caf_part_wait_us)));
    while (steps < kMaxSteps && ctx.r3.u32 == 0 && index < REX_LOAD_U32(self + 4) && BudgetLeft()) {
      if (index == previous) {
        const bool extra_wait = std::chrono::steady_clock::now() < deadline;
        if (!extra_wait && !WaitForRead()) break;
        if (extra_wait) std::this_thread::sleep_for(std::chrono::microseconds(250));
      }
      ctx = entry;
      ctx.r3.u64 = self;
      __imp__sub_827F8BF0(ctx, base);
      ++steps;
      previous = index;
      index = REX_LOAD_U32(self + 8);
    }
  }
  if (REXCVAR_GET(ufc_caf_fast_build_log))
    REXLOG_INFO("[ufc-caf-fast-build] parts self=0x{:08X} index {}->{} of {} steps={} ret={}", self, first,
                REX_LOAD_U32(self + 8), REX_LOAD_U32(self + 4), steps, ctx.r3.u32);
}

// The created fighter's model state machine (sub_827E7220, state at +260) is updated every
// other frame. In state 5 it waits for its parts (sub_827F8BF0) and in state 8 for its
// textures (driver sub_827F6D00 -> sub_827F6380 -> builder). Both are polls that do nothing
// until the next piece is ready, so while the machine sits in those two states it is
// updated up to ufc_caf_model_updates times per call; it stops as soon as it leaves them.
// States 7 (creates the texture driver) and 10..12 (finish the model) run once or poll,
// so they are chained the same way.
extern "C" void __imp__sub_827E7220(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_827E7220) {
  constexpr uint32_t kState = 260, kParts = 5, kTextures = 8;
  const uint32_t self = ctx.r3.u32;
  const uint32_t before = REX_LOAD_U32(self + kState);
  PPCContext entry = ctx;
  BudgetScope budget;
  __imp__sub_827E7220(ctx, base);
  int calls = 1;
  const int limit = REXCVAR_GET(ufc_caf_fast_build) ? REXCVAR_GET(ufc_caf_model_updates) : 1;
  // In state 8 the texture driver (model+368, item/phase counter +76) may wait for the next
  // item's data, again a read the file thread finishes within milliseconds: when a call made
  // no progress, wait briefly (same bounded budget as the parts) before calling again.
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::microseconds(std::max(0, REXCVAR_GET(ufc_caf_part_wait_us)));
  // Progress marker: texture driver item/phase counter (model+368 -> +76) and the parts
  // index (model+308 -> +8).
  auto driver_counter = [&]() -> uint32_t {
    const uint32_t driver = REX_LOAD_U32(self + 368);
    const uint32_t parts = REX_LOAD_U32(self + 308);
    return (driver ? REX_LOAD_U32(driver + 76) : 0) * 1024 + (parts ? REX_LOAD_U32(parts + 8) : 0);
  };
  uint32_t last_state = REX_LOAD_U32(self + kState), last_counter = driver_counter();
  while (calls < limit && BudgetLeft()) {
    const uint32_t state = REX_LOAD_U32(self + kState);
    // 7 creates the texture driver (one shot), 10..12 finish the model (one shot or a poll).
    const bool repeat = state == kParts || state == 7 || state == kTextures || (state >= 10 && state <= 12);
    if (!repeat) break;
    const uint32_t counter = driver_counter();
    const bool progress = state != last_state || counter != last_counter;
    if (!progress && calls > 1) {
      if (state != kTextures && state != kParts) break;
      const bool extra_wait = std::chrono::steady_clock::now() < deadline;
      if (!extra_wait && !WaitForRead()) break;
      if (extra_wait) std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    last_state = state;
    last_counter = counter;
    ctx = entry;
    ctx.r3.u64 = self;
    __imp__sub_827E7220(ctx, base);
    ++calls;
  }
  const uint32_t after = REX_LOAD_U32(self + kState);
  if (REXCVAR_GET(ufc_caf_fast_build_log) && after != before)
    REXLOG_INFO("[ufc-caf-fast-build] model self=0x{:08X} state {}->{} calls={}", self, before, after, calls);
}

// SELECT FIGHTER preview state 8 waits for the created fighter's data object (+64 of the
// side) with sub_828F6E08: step +8 is 1 while a read is in flight (global object at
// 0x83574A00), 2 while the data is processed, 3 when ready. Same bounded wait: when a call
// returns 0, look again within the budget, continuing as long as the step moves on.
extern "C" void __imp__sub_828F6E08(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_828F6E08) {
  const uint32_t self = ctx.r3.u32;
  PPCContext entry = ctx;
  const uint32_t first = REX_LOAD_U32(self + 8);
  BudgetScope budget;
  __imp__sub_828F6E08(ctx, base);
  int calls = 1;
  // Only for the SELECT FIGHTER preview (caller inside sub_8290D870); the same poll also
  // runs while the save loads at boot, where waiting would only delay start-up.
  const uint32_t lr = uint32_t(entry.lr);
  const bool from_preview = lr >= 0x8290D870u && lr < 0x8290E400u;
  if (REXCVAR_GET(ufc_caf_fast_build) && from_preview && ctx.r3.u32 == 0 && (first == 1 || first == 2)) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(std::max(0, REXCVAR_GET(ufc_caf_part_wait_us)));
    uint32_t last = first;
    while (calls < 64 && ctx.r3.u32 == 0 && BudgetLeft()) {
      const uint32_t step = REX_LOAD_U32(self + 8);
      if (step != 1 && step != 2) break;
      if (step == last && calls > 1) {
        const bool extra_wait = std::chrono::steady_clock::now() < deadline;
        if (!extra_wait && !WaitForRead()) break;
        if (extra_wait) std::this_thread::sleep_for(std::chrono::microseconds(250));
      }
      last = step;
      ctx = entry;
      ctx.r3.u64 = self;
      __imp__sub_828F6E08(ctx, base);
      ++calls;
    }
  }
  if (REXCVAR_GET(ufc_caf_fast_build_log) && (calls > 1 || REX_LOAD_U32(self + 8) != first))
    REXLOG_INFO("[ufc-caf-fast-build] data self=0x{:08X} step {}->{} calls={} ret={}", self, first,
                REX_LOAD_U32(self + 8), calls, ctx.r3.u32);
}

// ---------------------------------------------------------------------------------------
// Created-fighter texture cache (--ufc_caf_texture_cache, default on).
//
// Each finished CAF texture is compressed to DXT5 by the game's GPU compressor, one call of
// sub_82793B10 per mip level inside the job sub_82794378. Under emulation each call waits
// for the GPU memexport result (8..13 ms for the first level), the main remaining cost.
// The job keeps running as in the game (level descriptions, offsets, done flag +480); only
// the per-level GPU compression is replaced by a copy when the texture is known:
//  - level index = r30 of the job (callee-saved, the job's loop counter),
//  - destination address of the level = job frame (r1 at entry) + 236,
//  - level size in bytes = destination description (r6) word 6.
// Key: the texture item's type and layout fields plus every byte of the parts list of each
// of its layers (48-byte elements: image ids, colours, flags), so any change to the fighter's
// look gives a new key. Misses compress normally and store the result (memory + disk).
// --ufc_caf_texture_cache_verify compresses on hits too and compares, for testing.
REXCVAR_DEFINE_BOOL(ufc_caf_texture_cache, true, "UFC",
                    "Reuse finished created-fighter textures (memory and disk) instead of compressing them again");
REXCVAR_DEFINE_BOOL(ufc_caf_texture_cache_verify, false, "Tests",
                    "On a created-fighter texture cache hit, also compress normally and compare");
REXCVAR_DEFINE_STRING(ufc_caf_texture_cache_dir, "", "UFC",
                      "Folder of the created-fighter texture cache (default: cache/caf-textures next to the executable)");
namespace {
constexpr uint32_t kCacheVersion = 1;
std::mutex g_cache_mutex;
uint64_t g_cache_hits = 0, g_cache_misses = 0, g_cache_mismatches = 0;

uint64_t Mix(uint64_t h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}
uint64_t Mix32(uint64_t h, uint32_t v) {
  const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
  return Mix(h, b, 4);
}
bool GuestPointer(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u; }

// Hash of everything the game composes the texture from. False when the layout is not the
// expected one; the texture is then compressed as in the game and not cached.
bool ItemKey(uint8_t* base, uint32_t item, uint64_t& key) {
  // Only the created-fighter texture item class (vtable measured on Original v48 and Reborn
  // r9); anything else is left to the game untouched.
  constexpr uint32_t kItemVtable = 0x820F2EB4u, kLayerResourceVtable = 0x820F3C3Cu;
  if (!GuestPointer(item) || REX_LOAD_U32(item) != kItemVtable) return false;
  const uint32_t type = REX_LOAD_U32(item + 8), count = REX_LOAD_U32(item + 44);
  const uint32_t layers = REX_LOAD_U32(item + 48);
  if (count == 0 || count > 16 || !GuestPointer(layers)) return false;
  uint64_t h = Mix32(1469598103934665603ull, kCacheVersion);
  h = Mix32(h, type);
  h = Mix32(h, count);
  for (uint32_t off = 76; off < 84; off += 4) h = Mix32(h, REX_LOAD_U32(item + off));
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t layer = REX_LOAD_U32(layers + i * 4);
    if (!GuestPointer(layer)) return false;
    const uint32_t res = REX_LOAD_U32(layer + 84);
    if (!GuestPointer(res) || REX_LOAD_U32(res) != kLayerResourceVtable) return false;
    const uint32_t holder = REX_LOAD_U32(res + 4);
    if (!GuestPointer(holder)) return false;
    const uint32_t elements = REX_LOAD_U32(holder + 48), n = REX_LOAD_U32(res + 12);
    if (n > 256 || (n && !GuestPointer(elements))) return false;
    h = Mix32(h, n);
    for (uint32_t e = 0; e < n; ++e)
      for (uint32_t w = 0; w < 48; w += 4) h = Mix32(h, REX_LOAD_U32(elements + e * 48 + w));
  }
  key = h;
  return true;
}
std::filesystem::path CacheDir() {
  const std::string configured = REXCVAR_GET(ufc_caf_texture_cache_dir);
  if (!configured.empty()) return std::filesystem::u8path(configured);
  return rex::filesystem::GetExecutableFolder() / "cache" / "caf-textures";
}
std::filesystem::path CacheFile(uint64_t key) {
  char name[40];
  std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(key));
  return CacheDir() / name;
}
// Two-step trust: a texture level is reused only after two builds produced the same bytes
// (some builds of the same fighter differ slightly, e.g. a layer still loading the first
// time). The first result is a candidate (memory, "<key>.cand" on disk); a later identical
// build confirms it ("<key>.bin"); a different one replaces the candidate.
struct Entry {
  std::vector<uint8_t> data;
  bool confirmed = false;
};
std::unordered_map<uint64_t, Entry> g_entries;

bool ReadFile(const std::filesystem::path& file, uint32_t size, std::vector<uint8_t>& out) {
  std::error_code error;
  const auto file_size = std::filesystem::file_size(file, error);
  if (error || file_size != size) return false;
  std::ifstream in(file, std::ios::binary);
  out.resize(size);
  return bool(in.read(reinterpret_cast<char*>(out.data()), std::streamsize(size)));
}
std::filesystem::path EntryFile(uint64_t key, bool confirmed) {
  auto file = CacheFile(key);
  if (!confirmed) file.replace_extension(".cand");
  return file;
}
// Fills `entry` from memory or disk. False when nothing of this size is known.
bool CacheFind(uint64_t key, uint32_t size, Entry& entry) {
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = g_entries.find(key);
    if (it != g_entries.end()) {
      if (it->second.data.size() != size) return false;
      entry = it->second;
      return true;
    }
  }
  Entry loaded;
  if (ReadFile(EntryFile(key, true), size, loaded.data)) loaded.confirmed = true;
  else if (!ReadFile(EntryFile(key, false), size, loaded.data)) return false;
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_entries[key] = loaded;
  entry = std::move(loaded);
  return true;
}
void WriteEntryFile(uint64_t key, bool confirmed, std::vector<uint8_t> data) {
  std::thread([key, confirmed, data = std::move(data)] {
    std::error_code error;
    std::filesystem::create_directories(CacheDir(), error);
    const auto file = EntryFile(key, confirmed);
    auto temporary = file;
    temporary += ".tmp";
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()))) return;
    }
    std::filesystem::rename(temporary, file, error);
    if (confirmed) std::filesystem::remove(EntryFile(key, false), error);
  }).detach();
}
void CacheStore(uint64_t key, std::vector<uint8_t> data, bool confirmed) {
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    Entry& entry = g_entries[key];
    entry.data = data;
    entry.confirmed = confirmed;
  }
  WriteEntryFile(key, confirmed, std::move(data));
}
}  // namespace

extern "C" void __imp__sub_82793B10(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82793B10) {
  const uint32_t item = tl_compress_item ? tl_compress_item : g_queued_compress_item.load();
  const uint32_t level = ctx.r30.u32, frame = ctx.r1.u32, dst_desc = ctx.r6.u32;
  const uint32_t address = GuestPointer(frame) ? REX_LOAD_U32(frame + 236) : 0;
  const uint32_t size = GuestPointer(dst_desc) ? REX_LOAD_U32(dst_desc + 24) : 0;
  uint64_t item_key = 0;
  const bool from_job = uint32_t(ctx.lr) >= 0x82794378u && uint32_t(ctx.lr) < 0x82794700u;
  const bool usable = REXCVAR_GET(ufc_caf_texture_cache) && from_job && level < 16 &&
                      GuestPointer(address) && size >= 16 && size <= (8u << 20) &&
                      ItemKey(base, item, item_key);
  if (!usable) {
    __imp__sub_82793B10(ctx, base);
    return;
  }
  const uint64_t key = Mix32(Mix32(item_key, level), size);
  uint8_t* host = const_cast<uint8_t*>(&REX_LOAD_U8(address));
  Entry entry;
  const bool known = CacheFind(key, size, entry);
  const bool verify = REXCVAR_GET(ufc_caf_texture_cache_verify);
  if (known && entry.confirmed && !verify) {
    std::memcpy(host, entry.data.data(), size);
    uint64_t hits;
    {
      std::lock_guard<std::mutex> lock(g_cache_mutex);
      hits = ++g_cache_hits;
    }
    if (REXCVAR_GET(ufc_caf_fast_build_log))
      REXLOG_INFO("[ufc-caf-cache] hit key={:016x} level={} size={} hits={}", key, level, size, hits);
    return;
  }
  __imp__sub_82793B10(ctx, base);
  std::vector<uint8_t> result(host, host + size);
  size_t differing = 0;
  if (known)
    for (uint32_t i = 0; i < size; ++i) differing += result[i] != entry.data[i];
  const char* outcome;
  if (!known) {
    CacheStore(key, std::move(result), false);
    outcome = "candidate";
  } else if (differing == 0) {
    if (!entry.confirmed) CacheStore(key, std::move(result), true);
    outcome = entry.confirmed ? "verified" : "confirmed";
  } else {
    // A confirmed entry that no longer matches is demoted; the new bytes become the candidate.
    CacheStore(key, std::move(result), false);
    outcome = entry.confirmed ? "MISMATCH-demoted" : "replaced";
  }
  uint64_t misses;
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    misses = ++g_cache_misses;
    if (differing) ++g_cache_mismatches;
  }
  if (REXCVAR_GET(ufc_caf_fast_build_log) || verify)
    REXLOG_INFO("[ufc-caf-cache] {} key={:016x} item=0x{:08X} level={} size={} differing_bytes={} builds={} mismatches={}",
                outcome, key, item, level, size, differing, misses, g_cache_mismatches);
}
