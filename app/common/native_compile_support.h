#pragma once
#include "ufc3_pch.h"

#if defined(UFC_EXTERNAL_LOCK_CHECK)
// Keep the RAII lock scope in a host translation unit compiled without async
// EH. Clang's Windows-GNU async EH crashes on this destructor cleanup in a
// lambda. The original lock acquisition, atomic load and unlock are retained.
u64 ufc_external_lock_check(const std::atomic<i32>& count);
#undef REX_CHECK_GLOBAL_LOCK
#define REX_CHECK_GLOBAL_LOCK() ufc_external_lock_check(ppc_global_lock_count_())
#endif

#if defined(UFC_EXTERNAL_JMPBUF)
// EXP-021: the PCH keeps guest jmp_bufs in an unordered_map<u32, jmp_buf>;
// with async EH Clang crashes on that map's destructor. Keep the map (16-byte
// aligned slots) in a host TU without async EH. setjmp itself still runs in
// the guest function's own frame, as it must.
#include <type_traits>
std::remove_extent_t<jmp_buf>* ufc_jmp_buf_for(u32 guest_buf_addr);
[[noreturn]] void ufc_ppc_longjmp(u32 guest_buf_addr, int val);
#undef ppc_setjmp
#define ppc_setjmp(guest_buf_addr) (setjmp(ufc_jmp_buf_for(guest_buf_addr)))
#define ppc_longjmp ufc_ppc_longjmp
#endif
