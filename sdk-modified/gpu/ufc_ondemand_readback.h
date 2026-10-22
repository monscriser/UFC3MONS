#pragma once
// v70 candidate (UFC_ONDEMAND_READBACK=1): exact on-demand resolve readback.
//
// The original readback path copies every resolved range GPU -> host buffer,
// waits for it (FULL) or reads the previous slot (FAST), and memcpys it into
// guest RAM, even when no guest code ever reads those bytes. Here an eligible
// resolve (page aligned, unscaled, at least UFC_ONDEMAND_MIN bytes) is only
// recorded: the guest virtual alias pages of the range become ufc_linux::NoAccess
// and nothing is copied. The GPU shared memory already holds the resolved
// bytes (the texture cache keeps reading them from there).
//
// The first guest access to such a page raises an access violation. This
// handler (registered before the SDK's) waits until the submission containing
// the resolve has been submitted, copies the current shared-memory bytes of
// every pending range on that page with its own command buffer and fence on
// the same queue (ordered after all earlier submissions, and a trailing
// barrier orders all later work after the copy), writes them into guest RAM
// through the unprotected physical view, restores the page protection the SDK
// had set (normally read-only write watch) and retries the access. A write
// therefore reaches the SDK invalidation path with the guest RAM already
// holding the resolved data. The guest never observes older data than FULL.
//
// Coverage/verification (test only):
//   UFC_ONDEMAND_TOUCH_MS=n  helper thread reads one byte of every pending
//                            range every n ms through the 0xA0000000 alias.
//   UFC_ONDEMAND_VERIFY=1    each deferred resolve also records the original
//                            GPU copy into a private reference buffer; every
//                            materialization is compared byte by byte.
//   UFC_ONDEMAND_VERIFY=positive  flips one reference byte before comparing
//                            (must report mismatches).
#include "ufc_linux_native.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/vulkan/util.h>

namespace rex::graphics {
// Defined in command_processor.cpp (v70): called by the command processor
// whenever it waits (idle ring, WAIT_REG_MEM), on the command processor thread.
extern void (*ufc_cp_wait_hook)(CommandProcessor* command_processor);
}  // namespace rex::graphics

namespace ufc_ondemand {
using CP = rex::graphics::vulkan::VulkanCommandProcessor;
constexpr uint32_t kPage = 4096;

struct Range {
  uint32_t phys = 0, len = 0;
  uint64_t submission = 0;  // submission that contains the resolve
  bool pending = false;
  uint64_t defers = 0, compares = 0, materializations = 0;
  // Verification reference (original GPU copy of the same generation).
  VkBuffer ref = VK_NULL_HANDLE;
  VkDeviceMemory ref_memory = VK_NULL_HANDLE;
  void* ref_mapping = nullptr;
  bool ref_coherent = true;
  uint64_t ref_submission = 0;  // equals `submission` when the reference is this generation
};
struct PageState {
  uint32_t orig[3] = {0, 0, 0};
  uint8_t* host[3] = {nullptr, nullptr, nullptr};
  uint32_t refs = 0;
};
struct Counters {
  uint64_t deferred = 0, deferred_bytes = 0, first_defers = 0, faults = 0, fault_writes = 0,
           materialized = 0, materialized_bytes = 0, submit_waits = 0, cp_faults = 0,
           cp_unsubmitted = 0, timeouts = 0, failures = 0, protect_failures = 0, protection_lost = 0,
           comparisons = 0, compared_bytes = 0, mismatches = 0, compare_skipped_newer = 0, touches = 0,
           ineligible = 0, chased = 0, overlap_superseded = 0, positive_comparisons = 0, positive_detected = 0,
           invalidations = 0, invalidation_materialized = 0, invalidation_unsubmitted = 0,
           ineligible_scaled = 0, ineligible_small = 0, ineligible_other = 0, unaligned_defers = 0;
};
struct State {
  int enabled = -1;        // installed by UFC_ONDEMAND_READBACK=1
  std::atomic<int> mode{1};  // 0 off (original path), 1 on, 2 on+verify, 3 on+positive control
  int verify = 0;  // derived from mode: 0 off, 1 verify, 2 positive control
  std::string control_path;
  uint32_t min_bytes = 262144;
  uint32_t touch_ms = 0;
  CP* cp = nullptr;
  rex::memory::Memory* memory = nullptr;
  uint32_t cp_thread = 0;
  void* veh = nullptr;
  std::recursive_mutex mutex;
  std::unordered_map<uint64_t, Range> ranges;
  std::unordered_map<uint32_t, PageState> pages;  // by physical page index
  std::atomic<uint64_t> submitted{0};  // highest submission handed to the queue
  std::atomic<uint32_t> waiters{0};
  std::atomic<uint32_t> pending_count{0};
  // One bit per physical 4 KB page with pending references (lock-free check).
  std::atomic<uint64_t> page_bits[0x20000000u / kPage / 64];
  // Materialization resources (any thread, under mutex).
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  void* staging_mapping = nullptr;
  bool staging_coherent = true;
  uint32_t staging_size = 0;
  VkBuffer shared_buffer = VK_NULL_HANDLE;
  Counters c;
  uint64_t frames = 0;
  std::thread touch_thread;
  std::atomic<bool> stop{false};
};
inline State& Get() { static State s; return s; }

inline bool Enabled() {
  auto& s = Get();
  if (s.enabled < 0) {
    const char* v = std::getenv("UFC_ONDEMAND_READBACK");
    s.enabled = (v && v[0] == '1') ? 1 : 0;
    if (const char* m = std::getenv("UFC_ONDEMAND_MIN")) s.min_bytes = std::max<uint32_t>(1, uint32_t(std::strtoul(m, nullptr, 10)));
    if (const char* t = std::getenv("UFC_ONDEMAND_TOUCH_MS")) s.touch_ms = uint32_t(std::strtoul(t, nullptr, 10));
    if (const char* f = std::getenv("UFC_ONDEMAND_VERIFY")) s.mode = std::string(f) == "positive" ? 3 : (f[0] == '1' ? 2 : 1);
    if (const char* c = std::getenv("UFC_ONDEMAND_CONTROL")) s.control_path = c;
    s.verify = s.mode >= 2 ? s.mode - 1 : 0;
    if (s.enabled) REXGPU_INFO("[ufc-ondemand] installed min_bytes={} touch_ms={} mode={} control={}", s.min_bytes, s.touch_ms, s.mode.load(), s.control_path);
  }
  return s.enabled == 1;
}
// Installed and not switched off by the control file.
inline bool Active() { return Enabled() && Get().mode.load(std::memory_order_relaxed) != 0; }

inline uint64_t Key(uint32_t phys, uint32_t len) { return (uint64_t(phys) << 32) | len; }

// Host pointers of the three guest virtual aliases of a physical page.
inline void Aliases(rex::memory::Memory& memory, uint32_t phys_page_address, uint8_t* out[3]) {
  static const uint32_t bases[3] = {0xA0000000u, 0xC0000000u, 0xE0000000u};
  for (uint32_t v = 0; v < 3; ++v) {
    out[v] = nullptr;
    for (uint32_t delta : {0u, 0x1000u}) {
      if (phys_page_address < delta) continue;
      uint32_t va = bases[v] + phys_page_address - delta;
      if (memory.GetPhysicalAddress(va) == phys_page_address) { out[v] = memory.TranslateVirtual<uint8_t*>(va); break; }
    }
  }
}

// Under mutex. Adds one pending reference to every page of the range.
inline uint32_t FirstPage(const Range& r) { return r.phys & ~(kPage - 1); }
inline uint32_t EndPage(const Range& r) { return (r.phys + r.len + kPage - 1) & ~(kPage - 1); }
inline void ProtectRange(State& s, const Range& r) {
  for (uint32_t a = FirstPage(r); a < EndPage(r); a += kPage) {
    PageState& p = s.pages[a / kPage];
    if (p.refs++ != 0) continue;
    s.page_bits[(a / kPage) >> 6].fetch_or(uint64_t(1) << ((a / kPage) & 63), std::memory_order_release);
    Aliases(*s.memory, a, p.host);
    for (uint32_t v = 0; v < 3; ++v) {
      p.orig[v] = 0;
      if (!p.host[v]) continue;
      ufc_linux::Region mbi;
      if (!ufc_linux::Query(p.host[v], &mbi, sizeof(mbi)) || mbi.State != ufc_linux::Committed ||
          (mbi.Protect & (ufc_linux::NoAccess | ufc_linux::Guard)) || !mbi.Protect) {
        p.host[v] = nullptr;  // not an accessible page: real access violations stay with the SDK
        continue;
      }
      uint32_t old = 0;
      if (ufc_linux::Protect(p.host[v], kPage, ufc_linux::NoAccess, &old)) p.orig[v] = old;
      else { ++s.c.protect_failures; p.host[v] = nullptr; }
    }
  }
}
// Under mutex. Drops one pending reference; restores the page when none remain.
inline void UnprotectRange(State& s, const Range& r) {
  for (uint32_t a = FirstPage(r); a < EndPage(r); a += kPage) {
    auto it = s.pages.find(a / kPage);
    if (it == s.pages.end() || it->second.refs == 0) continue;
    PageState& p = it->second;
    if (--p.refs != 0) continue;
    for (uint32_t v = 0; v < 3; ++v) {
      if (!p.host[v]) continue;
      uint32_t old = 0;
      if (!ufc_linux::Protect(p.host[v], kPage, p.orig[v], &old)) ++s.c.protect_failures;
      else if (old != ufc_linux::NoAccess) {
        if (++s.c.protection_lost <= 4) REXGPU_WARN("[ufc-ondemand] protection lost address={:08X} view={} old={:X}", a, v, old);
      }
    }
    s.page_bits[(a / kPage) >> 6].fetch_and(~(uint64_t(1) << ((a / kPage) & 63)), std::memory_order_release);
  }
}

inline bool EnsureMaterializeResources(State& s, uint32_t size) {
  const auto* vd = s.cp->GetVulkanDevice();
  const auto& dfn = vd->functions();
  VkDevice device = vd->device();
  if (!s.pool) {
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = vd->queue_family_graphics_compute();
    if (dfn.vkCreateCommandPool(device, &pool_info, nullptr, &s.pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo alloc = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = s.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (dfn.vkAllocateCommandBuffers(device, &alloc, &s.cb) != VK_SUCCESS) return false;
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (dfn.vkCreateFence(device, &fence_info, nullptr, &s.fence) != VK_SUCCESS) return false;
  }
  if (size > s.staging_size) {
    if (s.staging) {
      dfn.vkUnmapMemory(device, s.staging_memory);
      dfn.vkDestroyBuffer(device, s.staging, nullptr);
      dfn.vkFreeMemory(device, s.staging_memory, nullptr);
      s.staging = VK_NULL_HANDLE; s.staging_memory = VK_NULL_HANDLE; s.staging_mapping = nullptr; s.staging_size = 0;
    }
    uint32_t alloc_size = (size + 0xFFFFF) & ~0xFFFFFu;
    uint32_t type = UINT32_MAX;
    if (!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(vd, alloc_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            rex::ui::vulkan::util::MemoryPurpose::kReadback, s.staging, s.staging_memory, &type)) return false;
    if (dfn.vkMapMemory(device, s.staging_memory, 0, VK_WHOLE_SIZE, 0, &s.staging_mapping) != VK_SUCCESS) return false;
    s.staging_coherent = (vd->memory_types().host_coherent & (uint32_t(1) << type)) != 0;
    s.staging_size = alloc_size;
  }
  return true;
}

// Under mutex, on any thread, after the range's submission was submitted.
// Copies the current shared-memory bytes of the range into guest RAM.
inline bool CopyToGuest(State& s, Range& r) {
  const auto* vd = s.cp->GetVulkanDevice();
  const auto& dfn = vd->functions();
  VkDevice device = vd->device();
  if (!EnsureMaterializeResources(s, r.len)) { ++s.c.failures; return false; }
  dfn.vkResetCommandPool(device, s.pool, 0);
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  dfn.vkBeginCommandBuffer(s.cb, &begin);
  // Everything submitted earlier (the resolve) -> this copy.
  VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  dfn.vkCmdPipelineBarrier(s.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
  VkBufferCopy region = {};
  region.srcOffset = r.phys;
  region.dstOffset = 0;
  region.size = r.len;
  dfn.vkCmdCopyBuffer(s.cb, s.shared_buffer, s.staging, 1, &region);
  VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  dfn.vkCmdPipelineBarrier(s.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
  // This copy (a read of shared memory) -> all later submitted work (WAR).
  dfn.vkCmdPipelineBarrier(s.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
  dfn.vkEndCommandBuffer(s.cb);
  dfn.vkResetFences(device, 1, &s.fence);
  VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &s.cb;
  VkResult result;
  {
    auto queue = vd->AcquireQueue(vd->queue_family_graphics_compute(), 0);
    result = dfn.vkQueueSubmit(queue.queue(), 1, &submit, s.fence);
  }
  if (result != VK_SUCCESS || dfn.vkWaitForFences(device, 1, &s.fence, VK_TRUE, 5000000000ull) != VK_SUCCESS) { ++s.c.failures; return false; }
  if (!s.staging_coherent) {
    VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = s.staging_memory; range.offset = 0; range.size = VK_WHOLE_SIZE;
    dfn.vkInvalidateMappedMemoryRanges(device, 1, &range);
  }
  if (s.verify && r.ref_mapping && r.ref_submission != r.submission) {
    ++s.c.compare_skipped_newer;  // no reference of this generation (verification switched on later)
  } else if (s.verify && r.ref_mapping) {
    if (!r.ref_coherent) {
      VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
      range.memory = r.ref_memory; range.offset = 0; range.size = VK_WHOLE_SIZE;
      dfn.vkInvalidateMappedMemoryRanges(device, 1, &range);
    }
    // The reference was recorded in an earlier submission than this copy, so
    // its bytes are complete once this fence signals.
    uint8_t* ref = static_cast<uint8_t*>(r.ref_mapping);
    if (s.verify == 2) ref[r.len / 2] ^= 0x5A;
    bool differs = std::memcmp(ref, s.staging_mapping, r.len) != 0;
    if (s.verify == 2) {
      // Positive control: counted apart, never mixed with real comparisons.
      ++s.c.positive_comparisons;
      if (differs) ++s.c.positive_detected;
    } else {
      ++s.c.comparisons;
      ++r.compares;
      s.c.compared_bytes += r.len;
    }
    if (differs && s.verify == 1) {
      ++s.c.mismatches;
      if (s.c.mismatches <= 8) REXGPU_WARN("[ufc-ondemand] verify mismatch address={:08X} length={} positive={}", r.phys, r.len, s.verify == 2);
    }
    if (s.verify == 2) ref[r.len / 2] ^= 0x5A;
  }
  std::memcpy(s.memory->TranslatePhysical(r.phys), s.staging_mapping, r.len);
  ++s.c.materialized;
  ++r.materializations;
  s.c.materialized_bytes += r.len;
  return true;
}

// Under mutex. Materializes one range and drops its page references.
inline void Materialize(State& s, Range& r) {
  if (!r.pending) return;
  CopyToGuest(s, r);
  r.pending = false;
  s.pending_count.fetch_sub(1);
  UnprotectRange(s, r);
}

inline Range* FindPendingAt(State& s, uint32_t phys) {
  for (auto& kv : s.ranges) {
    Range& r = kv.second;
    if (r.pending && phys >= FirstPage(r) && phys < EndPage(r)) return &r;
  }
  return nullptr;
}

// Waits (without the state mutex) until submission `id` reached the queue.
inline bool WaitSubmitted(State& s, uint64_t id) {
  if (s.submitted.load(std::memory_order_acquire) >= id) return true;
  ++s.c.submit_waits;
  s.waiters.fetch_add(1);
  auto start = std::chrono::steady_clock::now();
  bool ok = true;
  while (s.submitted.load(std::memory_order_acquire) < id) {
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2)) { ok = false; break; }
    std::this_thread::yield();
  }
  s.waiters.fetch_sub(1);
  return ok;
}

inline bool Handler(const ufc_linux::Fault& fault) {
  if(fault.execute)return false;
  auto& s = Get();
  if (!s.pending_count.load(std::memory_order_acquire)) return false;
  uint8_t* host = static_cast<uint8_t*>(fault.address);
  uint8_t* vbase = s.memory->virtual_membase();
  if (host < vbase + 0xA0000000ull || host >= vbase + 0x100001000ull) return false;
  uint32_t va = s.memory->HostToGuestVirtual(host);
  uint32_t phys = s.memory->GetPhysicalAddress(va);
  if (phys == UINT32_MAX || phys >= 0x20000000u) return false;
  if (!(s.page_bits[(phys / kPage) >> 6].load(std::memory_order_acquire) & (uint64_t(1) << ((phys / kPage) & 63)))) {
    // Not (or no longer) ours. If another thread just restored the page, retry.
    return false;
  }
  bool on_cp = ufc_linux::ThreadId() == s.cp_thread;
  std::unique_lock<std::recursive_mutex> lock(s.mutex);
  Range* r = FindPendingAt(s, phys);
  if (!r) {
    // Another thread materialized this page between the bit check and the lock.
    lock.unlock();
    ufc_linux::Region mbi;
    if (ufc_linux::Query(host, &mbi, sizeof(mbi)) && !(mbi.Protect & ufc_linux::NoAccess)) return true;
    return false;
  }
  ++s.c.faults;
  if (fault.write) ++s.c.fault_writes;
  if (on_cp) ++s.c.cp_faults;
  // Materialize every pending range covering this page, oldest data first is
  // irrelevant: each copy reads the current shared memory.
  uint32_t chases = 0;
  for (;;) {
    r = FindPendingAt(s, phys & ~(kPage - 1));
    if (!r) {
      // Also ranges that start inside this page.
      for (auto& kv : s.ranges) {
        Range& q = kv.second;
        if (q.pending && FirstPage(q) < (phys & ~(kPage - 1)) + kPage && EndPage(q) > (phys & ~(kPage - 1))) { r = &q; break; }
      }
    }
    if (!r) break;
    uint64_t need = r->submission;
    if (s.submitted.load(std::memory_order_acquire) < need && chases < 8) {
      if (on_cp) {
        // The command processor itself cannot submit from here. Never seen in
        // the probe; counted so acceptance can require zero.
        ++s.c.cp_unsubmitted;
        r->pending = false;
        s.pending_count.fetch_sub(1);
        UnprotectRange(s, *r);
        continue;
      }
      lock.unlock();
      bool ok = WaitSubmitted(s, need);
      lock.lock();
      if (!ok) ++s.c.timeouts;
      if (++chases > 1) ++s.c.chased;
      continue;  // a newer resolve may have been deferred while unlocked
    }
    if (s.submitted.load(std::memory_order_acquire) < need) ++s.c.timeouts;
    Materialize(s, *r);
  }
  return true;
}

// Called by the SDK (global critical region held) before it unprotects or
// overwrites guest physical pages: guest protection change, release/decommit,
// file reads, and write-fault invalidation. Every pending range touching the
// pages gets its exact bytes first, so the SDK never exposes stale guest RAM.
inline std::pair<uint32_t, uint32_t> OnPhysicalInvalidation(void* context, uint32_t start, uint32_t length, bool exact_range) {
  (void)context;
  auto& s = Get();
  if (!s.pending_count.load(std::memory_order_acquire) || !length) return {0, UINT32_MAX};
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  for (auto& kv : s.ranges) {
    Range& r = kv.second;
    if (!r.pending || FirstPage(r) >= start + length || start >= EndPage(r)) continue;
    ++s.c.invalidations;
    if (s.submitted.load(std::memory_order_acquire) < r.submission) {
      // Cannot wait here (the command processor may need the global lock).
      if (++s.c.invalidation_unsubmitted <= 16) {
        const auto where=ufc_linux::Backtrace();
        REXGPU_WARN("[ufc-ondemand] invalidation before submit range={:08X}+{} trigger={:08X}+{} exact={} thread={} cp={} stack={}",
                    r.phys, r.len, start, length, exact_range, ufc_linux::ThreadId(), ufc_linux::ThreadId() == s.cp_thread, where);
      }
      r.pending = false;
      s.pending_count.fetch_sub(1);
      UnprotectRange(s, r);
      continue;
    }
    ++s.c.invalidation_materialized;
    Materialize(s, r);
  }
  return {0, UINT32_MAX};
}

inline void TouchLoop() {
  auto& s = Get();
  while (!s.stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(s.touch_ms));
    if (s.mode.load() < 2) continue;
    std::vector<uint8_t*> targets;
    {
      std::lock_guard<std::recursive_mutex> lock(s.mutex);
      for (auto& kv : s.ranges) {
        if (!kv.second.pending) continue;
        uint8_t* a[3];
        Aliases(*s.memory, kv.second.phys, a);
        if (a[0]) targets.push_back(a[0]);
      }
    }
    for (uint8_t* t : targets) {
      volatile uint8_t sink = *reinterpret_cast<volatile uint8_t*>(t);
      (void)sink;
      std::lock_guard<std::recursive_mutex> lock(s.mutex);
      ++s.c.touches;
    }
  }
}

// Command processor thread at a point where all queue work was awaited:
// copies every pending range into guest RAM (used when switching off).
inline void MaterializeAll() {
  auto& s = Get();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  for (auto& kv : s.ranges) if (kv.second.pending) Materialize(s, kv.second);
}
inline bool NeedsDrain() { return Enabled() && !Active() && Get().pending_count.load() != 0; }

// Command processor thread: called after vkQueueSubmit of `id` succeeded.
inline void Submitted(uint64_t id) {
  auto& s = Get();
  uint64_t cur = s.submitted.load(std::memory_order_relaxed);
  while (cur < id && !s.submitted.compare_exchange_weak(cur, id, std::memory_order_release)) {}
}

// Command processor thread, inside the resolve readback path after Resolve().
// Returns true when the range is handled on demand (skip the original copy).
// `record_reference` records the original GPU copy (verification only) into
// the given buffer.
template <typename RecordCopy>
inline bool Defer(CP& cp, rex::memory::Memory& memory, VkBuffer shared_buffer, uint32_t phys, uint32_t len,
                  bool scaled, RecordCopy record_reference) {
  if (!Active()) return false;
  auto& s = Get();
  if (scaled || len < s.min_bytes || !len || phys + len > 0x20000000u) {
    ++s.c.ineligible;
    if (scaled) ++s.c.ineligible_scaled; else if (len < s.min_bytes) ++s.c.ineligible_small; else ++s.c.ineligible_other;
    return false;
  }
  if ((phys | len) & (kPage - 1)) ++s.c.unaligned_defers;
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  if (!s.veh) {
    s.cp = &cp;
    s.memory = &memory;
    s.cp_thread = ufc_linux::ThreadId();
    s.shared_buffer = shared_buffer;
    s.submitted.store(cp.GetCompletedSubmission());
    s.veh = ufc_linux::AddFaultHandler(Handler);
    if(!s.veh){++s.c.failures;return false;}
    memory.RegisterPhysicalMemoryInvalidationCallback(OnPhysicalInvalidation, nullptr);
    if (s.touch_ms) { s.touch_thread = std::thread(TouchLoop); s.touch_thread.detach(); }
    REXGPU_INFO("[ufc-ondemand] handler installed cp_thread={}", s.cp_thread);
  }
  // A newer resolve overlapping another pending range changes that range's
  // current bytes too: its old reference no longer describes this generation.
  for (auto& kv : s.ranges) {
    Range& o = kv.second;
    if (o.pending && o.ref_submission && !(o.phys == phys && o.len == len) &&
        o.phys < phys + len && phys < o.phys + o.len) {
      o.ref_submission = 0;
      ++s.c.overlap_superseded;
    }
  }
  Range& r = s.ranges[Key(phys, len)];
  if (!r.len) { r.phys = phys; r.len = len; ++s.c.first_defers; }
  r.submission = cp.GetCurrentSubmission();
  ++r.defers;
  ++s.c.deferred;
  s.c.deferred_bytes += len;
  if (s.verify) {
    const auto* vd = cp.GetVulkanDevice();
    const auto& dfn = vd->functions();
    if (!r.ref) {
      uint32_t type = UINT32_MAX;
      if (rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(vd, len, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
              rex::ui::vulkan::util::MemoryPurpose::kReadback, r.ref, r.ref_memory, &type) &&
          dfn.vkMapMemory(vd->device(), r.ref_memory, 0, VK_WHOLE_SIZE, 0, &r.ref_mapping) == VK_SUCCESS) {
        r.ref_coherent = (vd->memory_types().host_coherent & (uint32_t(1) << type)) != 0;
      } else {
        r.ref_mapping = nullptr;
      }
    }
    if (r.ref_mapping) { record_reference(r.ref); r.ref_submission = r.submission; }
  } else {
    r.ref_submission = 0;
  }
  if (!r.pending) {
    r.pending = true;
    s.pending_count.fetch_add(1);
    ProtectRange(s, r);
  }
  return true;
}

// Command processor thread: an eager (original path) readback is about to
// write guest RAM. Nothing to do: a pending range materializes the current
// shared memory later, which already contains these bytes.

// Command processor thread, from the wait hook: make sure a guest thread that
// faulted on a deferred range does not wait for a submission that will only
// end after that same guest thread makes progress.
template <typename EndFn>
inline void OnCpWait(CP& cp, EndFn end_submission_if_safe) {
  auto& s = Get();
  if (!s.waiters.load(std::memory_order_acquire)) return;
  if (cp.submission_open()) end_submission_if_safe();
}

inline void EndFrame() {
  if (!Enabled()) return;
  auto& s = Get();
  ++s.frames;
  if (!s.control_path.empty() && !(s.frames % 30)) {
    if (FILE* f = std::fopen(s.control_path.c_str(), "rb")) {
      char text[32] = {};
      std::fread(text, 1, sizeof(text) - 1, f);
      std::fclose(f);
      std::string v(text);
      while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
      int m = v == "off" ? 0 : v == "on" ? 1 : v == "verify" ? 2 : v == "positive" ? 3 : -1;
      if (m >= 0 && m != s.mode.load()) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        s.mode = m;
        s.verify = m >= 2 ? m - 1 : 0;
        REXGPU_INFO("[ufc-ondemand] mode={} ({})", m, v);
      }
    }
  }
  if (s.frames % 120) return;
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  auto& c = s.c;
  REXGPU_INFO("[ufc-ondemand] cumulative mode={} deferred={} deferred_bytes={} first_defers={} ranges={} pending={} faults={} fault_writes={} materialized={} materialized_bytes={} submit_waits={} chased={} cp_faults={} cp_unsubmitted={} timeouts={} failures={} protect_failures={} protection_lost={} comparisons={} compared_bytes={} mismatches={} compare_skipped_newer={} overlap_superseded={} positive_comparisons={} positive_detected={} invalidations={} invalidation_materialized={} invalidation_unsubmitted={} touches={} ineligible={} ineligible_scaled={} ineligible_small={} ineligible_other={} unaligned_defers={}",
              s.mode.load(), c.deferred, c.deferred_bytes, c.first_defers, s.ranges.size(), s.pending_count.load(), c.faults, c.fault_writes, c.materialized, c.materialized_bytes, c.submit_waits, c.chased, c.cp_faults, c.cp_unsubmitted, c.timeouts, c.failures, c.protect_failures, c.protection_lost, c.comparisons, c.compared_bytes, c.mismatches, c.compare_skipped_newer, c.overlap_superseded, c.positive_comparisons, c.positive_detected, c.invalidations, c.invalidation_materialized, c.invalidation_unsubmitted, c.touches, c.ineligible, c.ineligible_scaled, c.ineligible_small, c.ineligible_other, c.unaligned_defers);
  if (!(s.frames % 1200))
    for (auto& kv : s.ranges)
      REXGPU_INFO("[ufc-ondemand] range address={:08X} length={} defers={} materializations={} compares={} pending={}",
                  kv.second.phys, kv.second.len, kv.second.defers, kv.second.materializations, kv.second.compares, kv.second.pending);
}
}  // namespace ufc_ondemand
