#include <atomic>
#include <rex/types.h>
#include <rex/thread/mutex.h>

u64 ufc_external_lock_check(const std::atomic<i32>& count) {
  auto lock = rex::thread::global_critical_region::AcquireDirect();
  return count.load() ? 0 : 0x8000;
}
