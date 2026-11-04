#!/usr/bin/env python3
"""UFC3MONS: on-demand readback fast page check (BETA 1.2 user report, Original edition on Steam Deck).

Every physical-memory invalidation and every new deferred resolve walked ALL ranges ever seen (the map only
grows: 1,368 -> 1,786 in 3 minutes on the Original), so the cost per frame kept rising (Deck: 44 -> 35 FPS).
Both loops only act on pending ranges that overlap the given pages, and the existing page bitmap already marks
every page with a pending reference. Now they first test the bitmap and skip the walk when no page of the
range is pending - the same result, without the walk.
UFC_ONDEMAND_FASTCHECK=0 restores the full walk (A/B). UFC_ONDEMAND_FASTCHECK_VERIFY=1 also runs the walk when
the bitmap says "none" and counts any range it would have touched ([ufc-ondemand-fastcheck] mismatches=, must
stay 0); UFC_ONDEMAND_FASTCHECK_VERIFY=positive forces the skip even when pages are pending (positive control:
mismatches must appear).
Writes od-win/ufc_ondemand_readback.h and od-linux/ufc_ondemand_readback.h from the headers the builds use."""
import os, sys
W = os.path.dirname(os.path.abspath(__file__))
SRC = {
    "od-win": "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/r4-gpu/ufc_ondemand_readback.h",
    "od-linux": "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/r8-input-lock/gpu84-async/gpu/ufc_ondemand_readback.h",
}
if os.name == "nt":
    SRC = {k: "C:/" + v[len("/mnt/c/"):] for k, v in SRC.items()}

HELPER = r'''
// UFC3MONS fast page check: true when any page of [start, start+length) has a pending reference.
inline int FastCheckMode() {
  static const int mode = [] {
    const char* off = std::getenv("UFC_ONDEMAND_FASTCHECK");
    if (off && off[0] == '0') return 0;
    const char* v = std::getenv("UFC_ONDEMAND_FASTCHECK_VERIFY");
    if (v && std::string(v) == "positive") return 3;
    if (v && v[0] == '1') return 2;
    return 1;
  }();
  return mode;
}
inline std::atomic<uint64_t> fastcheck_skips{0}, fastcheck_checks{0}, fastcheck_mismatches{0};
inline bool AnyPendingPage(uint32_t start, uint32_t length) {
  auto& s = Get();
  if (!length) return false;
  uint64_t first = start / kPage, end = (uint64_t(start) + length + kPage - 1) / kPage;
  const uint64_t limit = 0x20000000u / kPage;
  if (end > limit) end = limit;
  for (uint64_t p = first; p < end; ++p) {
    if (s.page_bits[p >> 6].load(std::memory_order_acquire) & (uint64_t(1) << (p & 63))) return true;
  }
  return false;
}
// Under mutex (verify only): would the full walk touch any pending range overlapping these pages?
inline bool WalkWouldAct(uint32_t start, uint32_t length) {
  auto& s = Get();
  for (auto& kv : s.ranges) {
    const Range& r = kv.second;
    if (r.pending && FirstPage(r) < uint64_t(start) + length && start < EndPage(r)) return true;
  }
  return false;
}
// Decides whether a walk over [start, start+length) can be skipped. Counts verify results.
inline bool FastSkip(uint32_t start, uint32_t length) {
  const int mode = FastCheckMode();
  if (!mode) return false;
  const bool any = AnyPendingPage(start, length);
  if (mode == 3) { fastcheck_checks.fetch_add(1, std::memory_order_relaxed); if (any) { fastcheck_mismatches.fetch_add(1, std::memory_order_relaxed); return true; } }
  if (any) return false;
  fastcheck_skips.fetch_add(1, std::memory_order_relaxed);
  if (mode == 2) {
    auto& s = Get();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    fastcheck_checks.fetch_add(1, std::memory_order_relaxed);
    if (WalkWouldAct(start, length)) fastcheck_mismatches.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}
// Under mutex. Defer's overlap walk only acts on ANOTHER pending range overlapping [phys, phys+len). A page
// can hold such a range only if its reference count is above this range's own reference (1 when this exact
// range is pending). Same answer as the walk, in pages-of-this-range lookups instead of all ranges.
inline bool OtherPendingOverlap(State& s, uint32_t phys, uint32_t len) {
  auto it = s.ranges.find(Key(phys, len));
  const uint32_t own = (it != s.ranges.end() && it->second.pending) ? 1u : 0u;
  Range probe; probe.phys = phys; probe.len = len;
  for (uint32_t a = FirstPage(probe); a < EndPage(probe); a += kPage) {
    auto p = s.pages.find(a / kPage);
    if (p != s.pages.end() && p->second.refs > own) return true;
  }
  return false;
}
inline bool WalkWouldActOther(State& s, uint32_t phys, uint32_t len) {
  for (auto& kv : s.ranges) {
    const Range& o = kv.second;
    if (o.pending && o.ref_submission && !(o.phys == phys && o.len == len) && o.phys < phys + len && phys < o.phys + o.len)
      return true;
  }
  return false;
}
// Under mutex: true when Defer's overlap walk can be skipped.
inline bool FastSkipDefer(State& s, uint32_t phys, uint32_t len) {
  const int mode = FastCheckMode();
  if (!mode) return false;
  const bool other = OtherPendingOverlap(s, phys, len);
  if (mode == 3) { fastcheck_checks.fetch_add(1, std::memory_order_relaxed); if (other) { fastcheck_mismatches.fetch_add(1, std::memory_order_relaxed); return true; } }
  if (other) return false;
  fastcheck_skips.fetch_add(1, std::memory_order_relaxed);
  if (mode == 2) {
    fastcheck_checks.fetch_add(1, std::memory_order_relaxed);
    if (WalkWouldActOther(s, phys, len)) fastcheck_mismatches.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}
'''

EDITS = [
    # Helper after UnprotectRange (FirstPage/EndPage and Get are defined before).
    ("// Under mutex. Drops one pending reference; restores the page when none remain.\n",
     "%HELPER%// Under mutex. Drops one pending reference; restores the page when none remain.\n"),
    ("  if (!s.pending_count.load(std::memory_order_acquire) || !length) return {0, UINT32_MAX};\n",
     "  if (!s.pending_count.load(std::memory_order_acquire) || !length) return {0, UINT32_MAX};\n"
     "  if (FastSkip(start, length)) return {0, UINT32_MAX};  // UFC3MONS: no pending page here\n"),
    ("  // A newer resolve overlapping another pending range changes that range's\n",
     "  // UFC3MONS fast page check: the overlap walk below only acts when another pending range shares a page.\n"
     "  if (!FastSkipDefer(s, phys, len))\n"
     "  // A newer resolve overlapping another pending range changes that range's\n"),
]


def main():
    for out, src in SRC.items():
        s = open(src, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in s else "\n"
        s = s.replace("\r\n", "\n")
        if "<string>" not in s:
            s = s.replace("#pragma once\n", "#pragma once\n#include <string>\n#include <cstdlib>\n", 1)
        for old, new in EDITS:
            if s.count(old) != 1:
                sys.exit(f"{out}: anchor found {s.count(old)} times: {old[:70]}")
            s = s.replace(old, new.replace("%HELPER%", HELPER.lstrip("\n")))
        # Counters in the existing 10 s summary line (once per EndFrame summary).
        anchor = '  REXGPU_INFO("[ufc-ondemand] cumulative'
        if s.count(anchor) != 1:
            sys.exit(f"{out}: summary anchor")
        s = s.replace(anchor, '  REXGPU_INFO("[ufc-ondemand-fastcheck] mode={} skips={} checks={} mismatches={}", FastCheckMode(), '
                      'fastcheck_skips.load(), fastcheck_checks.load(), fastcheck_mismatches.load());\n' + anchor, 1)
        os.makedirs(os.path.join(W, out), exist_ok=True)
        open(os.path.join(W, out, "ufc_ondemand_readback.h"), "w", encoding="utf-8", newline="").write(s.replace("\n", nl))
        print("wrote", out)


main()
