// UFC3MONS R8.4 (Linux measurement): where does the AK::IOThread (Wwise I/O) spin?
// A background thread sends a real-time signal to that one thread ~1000 times per second;
// the handler stores the interrupted instruction pointer plus the first executable return
// addresses found on its stack. Every 10 s the busiest locations are logged as module+offset
// (the game executable has sub_8XXXXXXX symbols, resolved offline with nm). Read-only.
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>
#ifdef __linux__
#include <dirent.h>
#include <dlfcn.h>
#include <fstream>
#include <link.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

namespace {
constexpr int kSlots = 1 << 15, kFrames = 4;
struct Slot { uintptr_t pc[kFrames]; };
Slot slots[kSlots];
std::atomic<uint32_t> next_slot{0};
uintptr_t exe_lo = 0, exe_hi = 0, exe_base = 0;

int FindExe(dl_phdr_info* info, size_t, void*) {
  if (info->dlpi_name && info->dlpi_name[0]) return 0;  // the main program has an empty name
  exe_base = info->dlpi_addr;
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const auto& ph = info->dlpi_phdr[i];
    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
      exe_lo = info->dlpi_addr + ph.p_vaddr;
      exe_hi = exe_lo + ph.p_memsz;
    }
  }
  return 1;
}

void Handler(int, siginfo_t*, void* raw) {
  auto* uc = static_cast<ucontext_t*>(raw);
  const uint32_t index = next_slot.fetch_add(1, std::memory_order_relaxed) & (kSlots - 1);
  Slot& s = slots[index];
  s.pc[0] = uintptr_t(uc->uc_mcontext.gregs[REG_RIP]);
  int n = 1;
  auto* sp = reinterpret_cast<const uintptr_t*>(uc->uc_mcontext.gregs[REG_RSP]);
  for (int i = 0; i < 96 && n < kFrames; ++i) {
    const uintptr_t v = sp[i];
    if (v >= exe_lo && v < exe_hi) s.pc[n++] = v;
  }
  for (; n < kFrames; ++n) s.pc[n] = 0;
}

std::string Describe(uintptr_t pc) {
  char buf[96];
  if (pc >= exe_lo && pc < exe_hi) {
    std::snprintf(buf, sizeof buf, "exe+0x%lx", (unsigned long)(pc - exe_base));
    return buf;
  }
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
    std::string mod = info.dli_fname;
    mod = mod.substr(mod.find_last_of('/') + 1);
    if (info.dli_sname) return mod + ":" + info.dli_sname;
    std::snprintf(buf, sizeof buf, "+0x%lx", (unsigned long)(pc - uintptr_t(info.dli_fbase)));
    return mod + buf;
  }
  std::snprintf(buf, sizeof buf, "?0x%lx", (unsigned long)pc);
  return buf;
}

int FindThread(const char* prefix) {
  int found = 0;
  if (DIR* dir = opendir("/proc/self/task")) {
    while (dirent* e = readdir(dir)) {
      if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
      std::ifstream comm(std::string("/proc/self/task/") + e->d_name + "/comm");
      std::string name;
      std::getline(comm, name);
      if (name.rfind(prefix, 0) == 0) found = std::atoi(e->d_name);
    }
    closedir(dir);
  }
  return found;
}
}  // namespace

void UfcStartHotSampler() {
  static bool started = false;
  if (started) return;
  started = true;
  dl_iterate_phdr(FindExe, nullptr);
  const int sig = SIGRTMIN + 6;
  struct sigaction sa{};
  sa.sa_sigaction = Handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  std::thread([sig] {
    const pid_t pid = getpid();
    int rounds = 0;
    while (rounds < 12) {  // at most 12 windows of 10 s, then the sampler stops
      const int tid = FindThread("AK::IOThread");
      if (!tid) { std::this_thread::sleep_for(std::chrono::seconds(2)); continue; }
      next_slot.store(0);
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (std::chrono::steady_clock::now() < end) {
        syscall(SYS_tgkill, pid, tid, sig);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const uint32_t n = std::min<uint32_t>(next_slot.load(), kSlots);
      std::map<uintptr_t, uint32_t> leaf, caller;
      for (uint32_t i = 0; i < n; ++i) {
        ++leaf[slots[i].pc[0]];
        for (int f = 1; f < kFrames; ++f) if (slots[i].pc[f]) ++caller[slots[i].pc[f]];
      }
      auto top = [](const std::map<uintptr_t, uint32_t>& m, size_t k) {
        std::vector<std::pair<uint32_t, uintptr_t>> v;
        for (auto& [pc, c] : m) v.emplace_back(c, pc);
        std::sort(v.rbegin(), v.rend());
        std::string s;
        for (size_t i = 0; i < v.size() && i < k; ++i) s += " " + Describe(v[i].second) + "=" + std::to_string(v[i].first);
        return s;
      };
      REXLOG_INFO("[ufc-hot] thread=AK::IOThread tid={} samples={} exe_base=0x{:x} pc:{}", tid, n, exe_base, top(leaf, 14));
      REXLOG_INFO("[ufc-hot] thread=AK::IOThread callers:{}", top(caller, 14));
      ++rounds;
    }
  }).detach();
  REXLOG_INFO("[ufc-hot] sampler armed for AK::IOThread (signal {})", sig);
}
#else
void UfcStartHotSampler() {}
#endif
