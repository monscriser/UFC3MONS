// Proof + timing for the PROCMAP_QUERY Query (deck-stalls-05-11). Linux only.
// Usage: test_procmap exact | timing | workload <old|new> [frames]
#include "ufc_linux_native.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <sys/mman.h>
#include <vector>
using namespace ufc_linux;
using clk = std::chrono::steady_clock;
static double Ms(clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); }
static int CountMaps() { FILE* f = std::fopen("/proc/self/maps", "r"); int n = 0, c; while ((c = std::fgetc(f)) != EOF) n += c == '\n'; std::fclose(f); return n; }

// Three views of one shared memory object, like the guest physical aliases (A0/C0/E0).
struct Views { size_t size; uint8_t* v[3]; };
static Views MakeViews(size_t size) {
  int fd = memfd_create("ufc-test", 0); if (ftruncate(fd, size)) std::perror("ftruncate");
  Views w{size, {}};
  for (int i = 0; i < 3; ++i) w.v[i] = (uint8_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd); return w;
}
static int Fragment(Views& w, std::mt19937& rng, int count) {
  const int prots[] = {PROT_NONE, PROT_READ, PROT_READ | PROT_WRITE};
  size_t pages = w.size / 4096;
  for (int i = 0; i < count; ++i) {
    size_t p = rng() % pages, n = 1 + rng() % 3; if (p + n > pages) n = pages - p;
    mprotect(w.v[rng() % 3] + p * 4096, n * 4096, prots[rng() % 3]);
  }
  return CountMaps();
}

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "exact";
  std::mt19937 rng(12345);
  if (mode == "exact") {
    Views w = MakeViews(64u << 20);
    // anonymous exec region + a hole, to cover r-x and unmapped answers
    uint8_t* ex = (uint8_t*)mmap(nullptr, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mprotect(ex, 1 << 19, PROT_READ | PROT_EXEC); munmap(ex + (3 << 18), 1 << 18);
    long compared = 0, mismatches = 0, positive = 0, notmapped = 0, fastcalls = 0;
    for (int round = 0; round < 10; ++round) {
      int vmas = Fragment(w, rng, 2000);
      for (int i = 0; i < 50000; ++i) {
        void* a;
        switch (rng() % 5) {
          case 0: case 1: case 2: a = w.v[rng() % 3] + (rng() % w.size); break;
          case 3: a = ex + (rng() % (1 << 20)); break;
          default: a = (void*)(uintptr_t(0x100000000000ull) + (uint64_t(rng()) << 12)); break;  // usually unmapped
        }
        Region f{}, p{};
        int rf = QueryFast(a, &f); bool rp = QueryParse(a, &p);
        if (rf < 0) { std::printf("PROCMAP_QUERY unavailable on this kernel\n"); return 2; }
        ++fastcalls; ++compared; if (!rp) ++notmapped;
        bool same = (rp == (rf == 1)) && (!rp || (f.BaseAddress == p.BaseAddress && f.RegionSize == p.RegionSize && f.State == p.State && f.Protect == p.Protect));
        if (!same) { if (++mismatches <= 5) std::printf("MISMATCH %p fast=%d %p+%zx prot=%u parse=%d %p+%zx prot=%u\n", a, rf, f.BaseAddress, f.RegionSize, f.Protect, rp, p.BaseAddress, p.RegionSize, p.Protect); }
        // positive control: the same comparison with the fast answer's Write bit flipped
        Region g = f; g.Protect ^= Write;
        bool same_pos = (rp == (rf == 1)) && (!rp || (g.BaseAddress == p.BaseAddress && g.RegionSize == p.RegionSize && g.Protect == p.Protect));
        if (!same_pos) ++positive;
      }
      std::printf("round %d: maps=%d compared=%ld mismatches=%ld\n", round, vmas, compared, mismatches);
    }
    std::printf("RESULT exact: compared=%ld not_mapped=%ld mismatches=%ld positive_control_detected=%ld\n", compared, notmapped, mismatches, positive);
    // Query() wrapper itself, with verification on, must agree too
    setenv("UFC_PROCMAP_VERIFY", "1", 1);
    return mismatches == 0 && positive > 0 ? 0 : 1;
  }
  if (mode == "timing") {
    Views w = MakeViews(256u << 20);
    for (int target : {1000, 5000, 20000}) {
      int vmas = CountMaps();
      while (vmas < target) vmas = Fragment(w, rng, 2000);
      const int N = 2000; Region r;
      auto t0 = clk::now(); for (int i = 0; i < N; ++i) QueryParse(w.v[i % 3] + (rng() % w.size), &r); double parse = Ms(t0) * 1000 / N;
      t0 = clk::now(); for (int i = 0; i < N; ++i) QueryFast(w.v[i % 3] + (rng() % w.size), &r); double fast = Ms(t0) * 1000 / N;
      std::printf("maps=%d  parse=%.1f us/query  procmap=%.2f us/query  (x%.0f)\n", vmas, parse, fast, parse / fast);
    }
    return 0;
  }
  if (mode == "workload") {
    // Deck scene: ~8,300 deferrals per 60 frames (~140 per frame), ~325 KB each (~80 pages), 3 views.
    // Same steps as ufc_ondemand ProtectRange/UnprotectRange: per page and view, Query + Protect(NoAccess),
    // later Protect(original). Ranges stay protected for 2 frames (rolling), like pending readbacks.
    const bool old_path = argc > 2 && std::string(argv[2]) == "old";
    if (old_path) setenv("UFC_PROCMAP_QUERY", "0", 1);
    const int frames = argc > 3 ? std::atoi(argv[3]) : 20;
    Views w = MakeViews(512u << 20);
    Fragment(w, rng, 3000);  // background fragmentation (game watches)
    const size_t pages = w.size / 4096;
    struct Pending { size_t page, n; Protection orig[3][80]; };
    std::vector<std::vector<Pending>> ring(2);
    double total = 0, worst = 0;
    for (int fr = 0; fr < frames; ++fr) {
      auto t0 = clk::now();
      auto& slot = ring[fr % 2];
      for (auto& pr : slot) for (size_t k = 0; k < pr.n; ++k) for (int v = 0; v < 3; ++v) {
        Protection old; Protect(w.v[v] + (pr.page + k) * 4096, 4096, pr.orig[v][k], &old);
      }
      slot.clear();
      for (int r = 0; r < 140; ++r) {
        Pending pr{(rng() % (pages - 80)), 80, {}};
        for (size_t k = 0; k < pr.n; ++k) for (int v = 0; v < 3; ++v) {
          Region mbi; uint8_t* h = w.v[v] + (pr.page + k) * 4096; pr.orig[v][k] = 0;
          if (!Query(h, &mbi, sizeof(mbi)) || (mbi.Protect & NoAccess)) continue;
          Protection old = 0; if (Protect(h, 4096, NoAccess, &old)) pr.orig[v][k] = old;
        }
        slot.push_back(pr);
      }
      double ms = Ms(t0); total += ms; worst = std::max(worst, ms);
      if (fr % 5 == 0) std::printf("frame %d: %.1f ms (maps=%d)\n", fr, ms, CountMaps());
    }
    auto& st = GetProcmapStats();
    std::printf("RESULT workload %s: avg %.1f ms/frame, worst %.1f ms, fast_queries=%llu parse_queries=%llu\n", old_path ? "old" : "new",
                total / frames, worst, (unsigned long long)st.fast.load(), (unsigned long long)st.parse.load());
    return 0;
  }
  return 1;
}
