// EXP-021: guest setjmp/longjmp buffers, compiled WITHOUT async EH (see
// native_compile_support.h). Same semantics as the PCH helpers: one native
// jmp_buf per guest jmp_buf address and thread; unknown address -> abort.
#include <csetjmp>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <rex/types.h>

namespace {
struct alignas(16) UfcJmpSlot {
  jmp_buf buf;
};
std::unordered_map<u32, std::unique_ptr<UfcJmpSlot>>& ufc_jmp_map() {
  static thread_local std::unordered_map<u32, std::unique_ptr<UfcJmpSlot>> map;
  return map;
}
}  // namespace

std::remove_extent_t<jmp_buf>* ufc_jmp_buf_for(u32 guest_buf_addr) {
  auto& slot = ufc_jmp_map()[guest_buf_addr];
  if (!slot) slot = std::make_unique<UfcJmpSlot>();
  return slot->buf;
}

[[noreturn]] void ufc_ppc_longjmp(u32 guest_buf_addr, int val) {
  auto& map = ufc_jmp_map();
  auto it = map.find(guest_buf_addr);
  if (it != map.end() && it->second) {
    longjmp(it->second->buf, val);
  }
  std::abort();
}
