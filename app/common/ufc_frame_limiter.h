#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
namespace ufc {
inline int NormalizeFrameCap(int cap) {
  return cap == 60 || cap == 120 || cap == 240 ? cap : 0;
}
inline int NextFrameCap(int cap) {
  switch (NormalizeFrameCap(cap)) {
    case 60: return 120;
    case 120: return 240;
    case 240: return 0;
    default: return 60;
  }
}
// Pure schedule shared by the real waiter and deterministic regression tests.
class FrameSchedule {
 public:
  int64_t Target(int64_t now, int cap) {
    cap = NormalizeFrameCap(cap);
    if (!cap) { previous_ = 0; next_ = 0; period_ = 0; return now; }
    period_ = 1000000000LL / cap;
    if (cap != previous_ || now - next_ >= period_) next_ = now;
    previous_ = cap;
    return next_;
  }
  void Complete(int64_t actual) {
    if (!period_) return;
    // Small wake overshoots do not move the absolute cadence. A missed whole
    // period rebases it instead of releasing a burst of catch-up submissions.
    next_ = actual - next_ >= period_ ? actual + period_ : next_ + period_;
  }
 private:
  int previous_ = -1;
  int64_t next_ = 0;
  int64_t period_ = 0;
};
class FrameLimiter {
  using Clock = std::chrono::steady_clock;
 public:
  explicit FrameLimiter(bool force_fallback = false) {
#ifdef _WIN32
    if (!force_fallback) {
      timer_ = CreateWaitableTimerExW(nullptr, nullptr, 2, TIMER_MODIFY_STATE | SYNCHRONIZE);
      if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
#endif
  }
  FrameLimiter(const FrameLimiter&) = delete;
  FrameLimiter& operator=(const FrameLimiter&) = delete;
  ~FrameLimiter() {
#ifdef _WIN32
    if (timer_) CloseHandle(timer_);
#endif
  }
  void Pace(int cap) {
    const int64_t target_ns = schedule_.Target(Now(), cap);
    if (!NormalizeFrameCap(cap)) return;
    const Clock::time_point target{std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(target_ns))};
    for (int64_t remaining = target_ns - Now(); remaining > 0; remaining = target_ns - Now()) {
#ifdef _WIN32
      LARGE_INTEGER due;
      due.QuadPart = -std::max<int64_t>(1, (remaining + 99) / 100);
      if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE) &&
          WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0) continue;
#endif
      std::this_thread::sleep_until(target);
    }
    schedule_.Complete(Now());
  }
 private:
  static int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
  }
  FrameSchedule schedule_;
#ifdef _WIN32
  HANDLE timer_ = nullptr;
#endif
};
}
