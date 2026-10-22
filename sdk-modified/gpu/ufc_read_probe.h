#pragma once
// v70 DIAGNOSTIC ONLY (UFC_READ_PROBE=1). After the readback memcpy of a
// resolve, the guest virtual alias pages of that range get PAGE_GUARD added to
// their current protection. The first access to a page raises
// STATUS_GUARD_PAGE_VIOLATION; this handler only counts it and continues, the
// OS has already removed the guard flag and the access is retried with the
// original protection (write watches keep working). The physical view used by
// host GPU code is not guarded, so this measures guest/kernel virtual accesses.
#include "ufc_linux_guard.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
namespace ufc_read_probe {
constexpr uint32_t kEntries = 2048, kRips = 8;
struct Entry {
  std::atomic<uintptr_t> host[3];
  std::atomic<uint32_t> length{0};
  uint32_t address = 0;
  std::atomic<uint64_t> arms{0}, reads{0}, writes{0}, cp_hits{0}, other_hits{0};
  std::atomic<uint64_t> rip[kRips];
  std::atomic<uint64_t> rip_count[kRips];
};
struct State {
  Entry entries[kEntries];
  std::atomic<uint32_t> used{0};
  std::atomic<uint64_t> unmatched{0}, protect_failures{0}, protected_regions{0}, table_full{0};
  std::unordered_map<uint64_t, uint32_t> index;
  uint64_t small_arms = 0, small_entries_quiet = 0;
  uint32_t cp_thread = 0;
  uint64_t frames = 0;
  int enabled = -1;
  void* veh = nullptr;
};
inline State& Get() { static State s; return s; }
inline bool Enabled() {
  auto& s = Get();
  if (s.enabled < 0) { const char* v = std::getenv("UFC_READ_PROBE"); s.enabled = (v && v[0] == '1') ? 1 : 0; }
  return s.enabled == 1;
}
inline bool Handler(const ufc_linux::Fault& fault) {
  if(fault.execute||!ufc_linux_guard::Consume(fault.address))return false;
  auto& s = Get();
  uintptr_t a = uintptr_t(fault.address);
  bool write = fault.write;
  uint32_t n = std::min(s.used.load(std::memory_order_acquire), kEntries);
  for (uint32_t i = 0; i < n; ++i) {
    Entry& e = s.entries[i];
    uint32_t len = e.length.load(std::memory_order_relaxed);
    for (uint32_t v = 0; v < 3; ++v) {
      uintptr_t h = e.host[v].load(std::memory_order_relaxed);
      if (!h || a < h || a >= h + len) continue;
      (write ? e.writes : e.reads).fetch_add(1, std::memory_order_relaxed);
      (ufc_linux::ThreadId() == s.cp_thread ? e.cp_hits : e.other_hits).fetch_add(1, std::memory_order_relaxed);
      uint64_t rip = fault.instruction;
      for (uint32_t r = 0; r < kRips; ++r) {
        uint64_t cur = e.rip[r].load(std::memory_order_relaxed);
        if (cur == 0) { uint64_t z = 0; if (!e.rip[r].compare_exchange_strong(z, rip) && z != rip) continue; e.rip_count[r].fetch_add(1); break; }
        if (cur == rip) { e.rip_count[r].fetch_add(1); break; }
      }
      return true;
    }
  }
  // Guard page that is not ours: leave it to the rest of the chain.
  s.unmatched.fetch_add(1, std::memory_order_relaxed);
  return false;
}
inline void GuardRange(uint8_t* base,uint32_t length){ufc_linux_guard::Arm(base,length,Get().protected_regions,Get().protect_failures);}
// Called on the command processor thread after the guest memory received the readback bytes.
inline void Arm(rex::memory::Memory& memory, uint32_t physical, uint32_t length) {
  if (!Enabled() || !length) return;
  auto& s = Get();
  if (!s.veh) { s.cp_thread = ufc_linux::ThreadId(); s.veh = ufc_linux::AddFaultHandler(Handler);if(!s.veh){s.protect_failures.fetch_add(1);return;} REXGPU_INFO("[ufc-read-probe] installed cp_thread={}", s.cp_thread); }
  uint32_t page = 4096;
  uint32_t first = physical & ~(page - 1);
  uint32_t last = (physical + length + page - 1) & ~(page - 1);
  // Only pages fully inside the written range, so neighbours are not counted.
  uint32_t inner_first = (physical + page - 1) & ~(page - 1);
  uint32_t inner_last = (physical + length) & ~(page - 1);
  (void)first; (void)last;
  if (inner_last <= inner_first) return;
  Entry* e = nullptr;
  uint32_t n = std::min(s.used.load(), kEntries);
  uint64_t key = (uint64_t(physical) << 32) | length;
  auto found = s.index.find(key);
  if (found != s.index.end()) e = &s.entries[found->second];
  if (!e) {
    if (n >= kEntries) { if (!s.table_full.fetch_add(1)) REXGPU_WARN("[ufc-read-probe] table full"); return; }
    s.index.emplace(key, n);
    e = &s.entries[n];
    e->address = physical;
    static const uint32_t bases[3] = {0xA0000000u, 0xC0000000u, 0xE0000000u};
    for (uint32_t v = 0; v < 3; ++v) {
      uint8_t* h = nullptr;
      if (inner_first < 0x20000000u) {
        // The 0xE0000000 alias is offset by 4 KB on the console.
        for (uint32_t delta : {0u, 0x1000u}) {
          if (inner_first < delta) continue;
          uint32_t va = bases[v] + inner_first - delta;
          if (memory.GetPhysicalAddress(va) == inner_first) { h = memory.TranslateVirtual<uint8_t*>(va); break; }
        }
      }
      e->host[v].store(uintptr_t(h));
      if (inner_last - inner_first >= 262144) REXGPU_INFO("[ufc-read-probe] map address={:08X} view={} host={:016X}", inner_first, v, uint64_t(uintptr_t(h)));
    }
    e->length.store(inner_last - inner_first);
    s.used.store(n + 1, std::memory_order_release);
  }
  e->arms.fetch_add(1, std::memory_order_relaxed);
  for (uint32_t v = 0; v < 3; ++v) {
    uintptr_t h = e->host[v].load();
    if (h) GuardRange(reinterpret_cast<uint8_t*>(h), e->length.load());
  }
  // Positive control (UFC_READ_PROBE_SELFTEST=1): one read through the guarded
  // 0xA0000000 alias on the first arm of each window must be counted as a CP hit.
  static int selftest = -1;
  if (selftest < 0) { const char* v = std::getenv("UFC_READ_PROBE_SELFTEST"); selftest = (v && v[0] == '1') ? 1 : 0; }
  if (selftest == 1 && e->length.load() >= 262144 && e->arms.load(std::memory_order_relaxed) == 1 && e->host[0].load()) {
    volatile uint8_t sink = *reinterpret_cast<volatile uint8_t*>(e->host[0].load());
    (void)sink;
  }
}
inline void EndFrame() {
  if (!Enabled()) return;
  auto& s = Get();
  if (++s.frames % 120) return;
  uint32_t n = std::min(s.used.load(), kEntries);
  for (uint32_t i = 0; i < n; ++i) {
    Entry& e = s.entries[i];
    uint64_t arms = e.arms.exchange(0);
    uint64_t reads = e.reads.exchange(0), writes = e.writes.exchange(0);
    uint64_t cp = e.cp_hits.exchange(0), other = e.other_hits.exchange(0);
    if (!arms && !reads && !writes) continue;
    s.small_arms += (e.length.load() < 262144) ? arms : 0;
    if (e.length.load() < 262144 && !reads && !writes) { s.small_entries_quiet++; continue; }
    char rips[512]; int off = 0; rips[0] = 0;
    for (uint32_t r = 0; r < kRips; ++r) {
      uint64_t rip = e.rip[r].exchange(0), c = e.rip_count[r].exchange(0);
      if (!rip) continue;
      const auto description=ufc_linux::DescribeAddress(rip);
      off += std::snprintf(rips+off,sizeof(rips)-off,"%s:%llu ",description.c_str(),(unsigned long long)c);
      if (off >= int(sizeof(rips))) break;
    }
    REXGPU_INFO("[ufc-read-probe] frames=120 address={:08X} guarded_bytes={} arms={} read_pages={} write_pages={} cp_thread_hits={} other_thread_hits={} rips={}",
                e.address, e.length.load(), arms, reads, writes, cp, other, rips);
  }
  REXGPU_INFO("[ufc-read-probe] window small_quiet_entries={} small_arms={} entries={} protected_regions={} protect_failures={} unmatched_guard_hits={} table_full={}", s.small_entries_quiet, s.small_arms, n, s.protected_regions.exchange(0), s.protect_failures.exchange(0), s.unmatched.exchange(0), s.table_full.load());
  s.small_entries_quiet = 0; s.small_arms = 0;
}
}  // namespace ufc_read_probe
