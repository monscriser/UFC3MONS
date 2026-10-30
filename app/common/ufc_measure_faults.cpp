// MEASURE ONLY (Linux): cost of the GPU memory watches on the Ally.
// - mprotect is interposed from the executable (exported with --export-dynamic-symbol=mprotect),
//   so calls from librexruntime/librexgpu are counted and timed, then done with the raw syscall.
// - A SIGSEGV handler is chained in front of the runtime's handler (installed on the first
//   mprotect call, after the runtime set up its own) to count and time every fault it handles.
// Enabled only with UFC_FAULT_MEASURE=1; otherwise mprotect forwards untouched.
#if defined(__linux__)
#include "ufc3_init.h"
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>
#include <thread>
#include <time.h>

namespace {
// Same as glibc's wrapper; no dlsym, so it is safe however early it is called.
int real(void* address, size_t length, int protection) {
  const long r = syscall(SYS_mprotect, address, length, protection);
  return r == 0 ? 0 : -1;
}
std::atomic<uint64_t> g_calls{0}, g_bytes{0}, g_ns{0}, g_faults{0}, g_fault_ns{0};
std::atomic<int> g_state{0};  // 0 unknown, 1 off, 2 on
std::atomic<uint64_t> g_first{0};
std::atomic<bool> g_chained{false};
struct sigaction g_previous {};

uint64_t Now() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return uint64_t(t.tv_sec) * 1000000000ull + uint64_t(t.tv_nsec);
}

void Chained(int signal, siginfo_t* info, void* context) {
  const uint64_t t0 = Now();
  if (g_previous.sa_flags & SA_SIGINFO) {
    g_previous.sa_sigaction(signal, info, context);
  } else if (g_previous.sa_handler != SIG_DFL && g_previous.sa_handler != SIG_IGN) {
    g_previous.sa_handler(signal);
  } else {
    struct sigaction original {};
    original.sa_handler = SIG_DFL;
    sigaction(signal, &original, nullptr);
    raise(signal);
    return;
  }
  g_faults.fetch_add(1, std::memory_order_relaxed);
  g_fault_ns.fetch_add(Now() - t0, std::memory_order_relaxed);
}

void Reporter() {
  uint64_t c0 = 0, b0 = 0, n0 = 0, f0 = 0, fn0 = 0;
  // Lock timing counters from the measurement librexruntime.so (absent in normal runtimes).
  auto* lock = static_cast<std::atomic<unsigned long long>*>(dlsym(RTLD_DEFAULT, "ufc_lock_stats"));
  unsigned long long l0[16] = {};
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const uint64_t c = g_calls.load(), b = g_bytes.load(), n = g_ns.load(), f = g_faults.load(), fn = g_fault_ns.load();
    REXLOG_INFO("[ufc-fault-measure] per_2s mprotect_calls={} mprotect_kib={} mprotect_ms={:.3f} faults={} fault_ms={:.3f}",
                c - c0, (b - b0) / 1024, double(n - n0) / 1e6, f - f0, double(fn - fn0) / 1e6);
    c0 = c; b0 = b; n0 = n; f0 = f; fn0 = fn;
    if (lock) {
      unsigned long long l[16];
      for (int i = 0; i < 16; ++i) l[i] = lock[i].load();
      REXLOG_INFO("[ufc-lock-measure] per_2s faults={} fault_wait_ms={:.3f} fault_work_ms={:.3f} fault_wait_max_ms={:.3f} "
                  "protect_calls={} protect_wait_ms={:.3f} protect_hold_ms={:.3f} protect_hold_max_ms={:.3f}",
                  l[0] - l0[0], double(l[1] - l0[1]) / 1e6, double(l[2] - l0[2]) / 1e6, double(l[3]) / 1e6,
                  l[4] - l0[4], double(l[5] - l0[5]) / 1e6, double(l[6] - l0[6]) / 1e6, double(l[7]) / 1e6);
      REXLOG_INFO("[ufc-lock-measure] per_2s callbacks={} cb0_ms={:.3f} cb1_ms={:.3f} cb2_ms={:.3f} cb3_ms={:.3f} unprotect_ms={:.3f} slow_checks={} raced={}",
                  l[13] - l0[13], double(l[8] - l0[8]) / 1e6, double(l[9] - l0[9]) / 1e6, double(l[10] - l0[10]) / 1e6,
                  double(l[11] - l0[11]) / 1e6, double(l[12] - l0[12]) / 1e6, l[14] - l0[14], l[15] - l0[15]);
      for (int i = 0; i < 16; ++i) l0[i] = l[i];
      lock[3].store(0); lock[7].store(0);  // maxima are per 2 s
    }
  }
}

void Chain() {
  struct sigaction mine {};
  if (sigaction(SIGSEGV, nullptr, &g_previous) == 0) {
    mine = g_previous;
    mine.sa_flags |= SA_SIGINFO;
    mine.sa_sigaction = Chained;
    sigaction(SIGSEGV, &mine, nullptr);
  }
  REXLOG_INFO("[ufc-fault-measure] fault handler chained, previous_siginfo={}", (g_previous.sa_flags & SA_SIGINFO) != 0);
}

void Start() {
  g_first.store(Now());
  std::thread(Reporter).detach();
}
}  // namespace

extern "C" int mprotect(void* address, size_t length, int protection) {
  int state = g_state.load(std::memory_order_acquire);
  if (state == 0) {
    const char* v = std::getenv("UFC_FAULT_MEASURE");
    int expected = 0;
    const int want = (v && v[0] == '1') ? 2 : 1;
    if (g_state.compare_exchange_strong(expected, want) && want == 2) Start();
    state = g_state.load(std::memory_order_acquire);
  }
  if (state != 2) return real(address, length, protection);
  const uint64_t t0 = Now();
  // The runtime installs its fault handler during startup: chain ours 20 s after the first call.
  const uint64_t first = g_first.load();
  if (first && !g_chained.load(std::memory_order_relaxed) && t0 - first > 20000000000ull &&
      !g_chained.exchange(true))
    Chain();
  const int result = real(address, length, protection);
  g_ns.fetch_add(Now() - t0, std::memory_order_relaxed);
  g_calls.fetch_add(1, std::memory_order_relaxed);
  g_bytes.fetch_add(length, std::memory_order_relaxed);
  return result;
}
#endif

#if defined(_WIN32)
// Windows: only the runtime lock/callback counters (exported by the measurement librexruntime.dll).
#include "ufc3_init.h"
#include <rex/logging.h>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
namespace {
struct UfcLockReporter {
  UfcLockReporter() {
    const char* v = std::getenv("UFC_FAULT_MEASURE");
    if (!v || v[0] != '1') return;
    std::thread([] {
      std::atomic<unsigned long long>* lock = nullptr;
      for (int i = 0; i < 60 && !lock; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (HMODULE m = GetModuleHandleA("librexruntime.dll"))
          lock = reinterpret_cast<std::atomic<unsigned long long>*>(GetProcAddress(m, "ufc_lock_stats"));
      }
      if (!lock) return;
      unsigned long long l0[16] = {};
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        unsigned long long l[16];
        for (int i = 0; i < 16; ++i) l[i] = lock[i].load();
        REXLOG_INFO("[ufc-lock-measure] per_2s faults={} fault_wait_ms={:.3f} fault_work_ms={:.3f} protect_calls={} protect_hold_ms={:.3f} "
                    "callbacks={} cb0_ms={:.3f} cb1_ms={:.3f} cb2_ms={:.3f} cb3_ms={:.3f} unprotect_ms={:.3f} slow_checks={} raced={}",
                    l[0] - l0[0], double(l[1] - l0[1]) / 1e6, double(l[2] - l0[2]) / 1e6, l[4] - l0[4],
                    double(l[6] - l0[6]) / 1e6, l[13] - l0[13], double(l[8] - l0[8]) / 1e6, double(l[9] - l0[9]) / 1e6,
                    double(l[10] - l0[10]) / 1e6, double(l[11] - l0[11]) / 1e6, double(l[12] - l0[12]) / 1e6,
                    l[14] - l0[14], l[15] - l0[15]);
        for (int i = 0; i < 16; ++i) l0[i] = l[i];
      }
    }).detach();
  }
} ufc_lock_reporter;
}  // namespace
#endif
