// UFC3MONS R8.3 (Linux measurement build): per-thread CPU time from /proc/self/task, logged
// every 2 s as the busiest threads with their names. Read-only; starts only when called.
#include <rex/logging.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>
#ifdef __linux__
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {
struct Sample { std::string name; unsigned long long ticks = 0; };

std::map<int, Sample> Read() {
  std::map<int, Sample> out;
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return out;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    const int tid = std::atoi(e->d_name);
    const std::string base = std::string("/proc/self/task/") + e->d_name;
    std::ifstream stat(base + "/stat");
    std::string line;
    if (!std::getline(stat, line)) continue;
    // Fields after the parenthesised comm: state(3) ... utime(14) stime(15).
    const auto close = line.rfind(')');
    if (close == std::string::npos) continue;
    std::istringstream rest(line.substr(close + 2));
    std::string field;
    unsigned long long utime = 0, stime = 0;
    for (int index = 3; rest >> field; ++index) {
      if (index == 14) utime = std::stoull(field);
      if (index == 15) { stime = std::stoull(field); break; }
    }
    Sample s;
    std::ifstream comm(base + "/comm");
    std::getline(comm, s.name);
    s.ticks = utime + stime;
    out[tid] = s;
  }
  closedir(dir);
  return out;
}
}  // namespace

void UfcStartThreadCpuSampler() {
  static bool started = false;
  if (started) return;
  started = true;
  std::thread([] {
    const double tick_ms = 1000.0 / double(sysconf(_SC_CLK_TCK));
    auto previous = Read();
    auto last = std::chrono::steady_clock::now();
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(2));
      auto current = Read();
      const auto now = std::chrono::steady_clock::now();
      const double seconds = std::chrono::duration<double>(now - last).count();
      std::vector<std::pair<double, std::string>> rows;
      double total = 0;
      for (const auto& [tid, s] : current) {
        auto it = previous.find(tid);
        if (it == previous.end()) continue;
        const double ms = double(s.ticks - it->second.ticks) * tick_ms / seconds;
        total += ms;
        if (ms > 0) rows.emplace_back(ms, s.name + "/" + std::to_string(tid));
      }
      std::sort(rows.rbegin(), rows.rend());
      std::string text;
      for (size_t i = 0; i < rows.size() && i < 8; ++i)
        text += " " + rows[i].second + "=" + std::to_string(int(rows[i].first));
      REXLOG_INFO("[ufc-thread-cpu] total_ms_per_s={} top:{}", int(total), text);
      previous = std::move(current);
      last = now;
    }
  }).detach();
  REXLOG_INFO("[ufc-thread-cpu] sampler started (ms of CPU per second per thread; 1000 = one full core)");
}
#else
void UfcStartThreadCpuSampler() {}
#endif
