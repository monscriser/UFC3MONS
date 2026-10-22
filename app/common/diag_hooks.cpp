// EXP-024 (diagnostico, so leitura): manual-v20 caiu em sub_8224C250 lendo guest 0x4E800020
// ("blr"), recebido em r4 de sub_8224C9A8, chamado por sub_82C47558 com
//   lwz r11,4(r31); lwz r10,0(r11); lwz r4,832(r10); bl 0x8224c9a8
// Este hook registra r31 (this do chamador, preservado na entrada), a cadeia de ponteiros e o
// endereco de retorno, e depois chama a funcao ORIGINAL sem alterar nada.
#include "ufc3_init.h"
#include <rex/hook.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>
#include <atomic>
#include <windows.h>

extern "C" void __imp__sub_8224C9A8(PPCContext& ctx, uint8_t* base);

namespace {
bool ufc_readable(const uint8_t* base, uint32_t addr) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(base + addr, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
  return true;
}
uint32_t ufc_rd(const uint8_t* base, uint32_t addr) {
  if (addr < 0x10000 || !ufc_readable(base, addr) || !ufc_readable(base, addr + 3)) return 0xDEADDEAD;
  uint32_t v;
  memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}
std::atomic<int> ufc_diag_count{0};
}  // namespace

REX_HOOK_RAW(sub_8224C9A8) {
  const uint32_t r4 = ctx.r4.u32, self = ctx.r31.u32;
  const uint32_t p1 = ufc_rd(base, self + 4), p2 = ufc_rd(base, p1), p3 = ufc_rd(base, p2 + 832);
  const uint32_t at_r4 = ufc_rd(base, r4);
  const bool bad = r4 == 0x4E800020 || at_r4 == 0xDEADDEAD;
  const int n = ufc_diag_count.fetch_add(1);
  if (bad || n < 40) {
    REXLOG_WARN(
        "[ufc-diag] 8224C9A8 n={} bad={} lr={:08X} r3={:08X} r4={:08X} r5={} r6={:08X} "
        "r31={:08X} [r31+4]={:08X} [[r31+4]]={:08X} [..+832]={:08X} [r4]={:08X} r10={:08X} r11={:08X}",
        n, bad, uint32_t(ctx.lr), ctx.r3.u32, r4, ctx.r5.s32, ctx.r6.u32, self, p1, p2, p3, at_r4,
        ctx.r10.u32, ctx.r11.u32);
    if (bad) rex::FlushLogging();
  }
  __imp__sub_8224C9A8(ctx, base);
}

// EXP-024 v2: entrada de sub_82C47558 (slot +8 da vtable 8213A3FC). Registra o objeto (r3),
// o this do chamador (r31 preservado), quem chamou (lr) e 16 palavras de r3-16 a r3+44.
extern "C" void __imp__sub_82C47558(PPCContext& ctx, uint8_t* base);
namespace { std::atomic<int> ufc_diag_count2{0}; }
REX_HOOK_RAW(sub_82C47558) {
  const uint32_t r3 = ctx.r3.u32;
  const int n = ufc_diag_count2.fetch_add(1);
  const bool odd = ufc_rd(base, r3) != 0x8213A3FC;
  if (odd || n < 60) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = ufc_rd(base, r3 - 16 + 4 * i);
    REXLOG_WARN(
        "[ufc-diag2] 82C47558 n={} odd={} lr={:08X} r3={:08X} r31={:08X} r4={:08X} r5={:08X} "
        "mem[r3-16..]={:08X} {:08X} {:08X} {:08X} | {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
        n, odd, uint32_t(ctx.lr), r3, ctx.r31.u32, ctx.r4.u32, ctx.r5.u32, m[0], m[1], m[2], m[3], m[4],
        m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    if (odd) rex::FlushLogging();
  }
  __imp__sub_82C47558(ctx, base);
}
