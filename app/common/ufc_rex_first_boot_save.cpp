// First-boot answers are saved (--ufc_rex_first_boot_save, default on; false = the original,
// which asks the grappling-controls and tutorial questions again on every boot).
//
// Guest code (identical in Original v48 and Reborn r9):
//  sub_82975280(self)   first-boot prompt machine, state at self+8 (0..22, jump table), frame of
//                       9392 bytes. Only state 0 saves the system data, and only once ever (when
//                       bit 0x80 of byte sys+200 is clear); that is before any question is shown.
//                       State 17 asks the grappling question only while bit 0x20 of the word at
//                       sys+128 is set (otherwise straight to 22); state 19 clears that bit in
//                       memory; 20/21 are the tutorial question; 22 is the end (+12 = 1, +4 = -1)
//                       and stays 22. Nothing after state 0 asks for a save, so SYSTEM.DAT keeps
//                       bit 0x20 set and the questions come back on the next boot.
//  sub_82933BF0         saves the system data at 0x82933D6C with this sequence (frame 9392):
//                       zero the descriptor at sp+80..sp+203, +80 = sys, +188 = 7;
//                       sub_8234E5F0(sp+208)            params object constructor
//                       sub_82A22CE8(sp+208, sp+80)     params from the descriptor
//                       sub_82EE9128(wrapper, 0, sp+208) request, kind 0 = system data
//                       [sp+208] = 0x82011B1C; sub_82259088(sp+208)   destructor
//                       wrapper = [0x83574A00], sys = [0x83577434]. (State 0 of the prompt
//                       machine uses the same sequence.)
//
// Fix: the word at sys+128 is noted the first time the machine is seen before its end state.
// When the machine then enters state 22 and the word differs, this hook issues that same
// system-data save on a frame of the same size below the caller's stack pointer (the machine
// itself uses 9392 bytes at this depth). The "saving" message (sub_82EE8D30) is not shown.
// Once bit 0x20 is saved clear, state 17 goes straight to 22 and nothing is saved again.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_BOOL(ufc_rex_first_boot_save, true, "UFC",
                    "Save the system data when the first-boot questions (grappling controls, tutorial) are "
                    "answered, so they are not asked again on every boot");

extern "C" void __imp__sub_82975280(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kSaveWrapper = 0x83574A00;
constexpr uint32_t kSystemData = 0x83577434;
constexpr uint32_t kFlagWord = 128;  // sys + 128, bit 0x20 = grappling question still to ask
constexpr uint32_t kParamsVtable = 0x82011B1C;
constexpr uint32_t kStateOffset = 8;
constexpr uint32_t kEndState = 22;
constexpr uint32_t kFrame = 9392;  // the frame sub_82933BF0 / sub_82975280 use for this
constexpr uint32_t kDescriptor = 80, kParams = 208;

bool g_noted = false;
uint32_t g_flags_before = 0;

bool GuestPointer(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u; }

PPCContext Callee(const PPCContext& ctx, uint32_t sp, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0) {
  PPCContext c = ctx;
  c.r1.u64 = sp;
  c.r3.u64 = r3;
  c.r4.u64 = r4;
  c.r5.u64 = r5;
  return c;
}

bool SaveSystemData(PPCContext& ctx, uint8_t* base) {
  const uint32_t wrapper = REX_LOAD_U32(kSaveWrapper);
  const uint32_t sys = REX_LOAD_U32(kSystemData);
  if (!GuestPointer(wrapper) || !GuestPointer(sys)) return false;
  const uint32_t sp = (ctx.r1.u32 - kFrame) & ~0xFu;
  REX_STORE_U32(sp, ctx.r1.u32);  // back chain, as a stwu would leave it
  for (uint32_t off = kDescriptor; off < kParams; off += 4) REX_STORE_U32(sp + off, 0u);
  REX_STORE_U32(sp + kDescriptor, sys);
  REX_STORE_U32(sp + 188, 7u);
  PPCContext c = Callee(ctx, sp, sp + kParams);
  sub_8234E5F0(c, base);
  c = Callee(ctx, sp, sp + kParams, sp + kDescriptor);
  sub_82A22CE8(c, base);
  c = Callee(ctx, sp, wrapper, 0, sp + kParams);
  sub_82EE9128(c, base);
  REX_STORE_U32(sp + kParams, kParamsVtable);
  c = Callee(ctx, sp, sp + kParams);
  sub_82259088(c, base);
  return true;
}
}  // namespace

REX_HOOK_RAW(sub_82975280) {
  const uint32_t self = ctx.r3.u32;
  const uint32_t before = GuestPointer(self) ? REX_LOAD_U32(self + kStateOffset) : kEndState;
  const uint32_t sys = REX_LOAD_U32(kSystemData);
  if (before < kEndState && !g_noted && GuestPointer(sys)) {
    g_flags_before = REX_LOAD_U32(sys + kFlagWord);
    g_noted = true;
  }
  __imp__sub_82975280(ctx, base);
  if (!GuestPointer(self) || before == kEndState || REX_LOAD_U32(self + kStateOffset) != kEndState) return;
  if (!g_noted || !GuestPointer(sys)) return;
  g_noted = false;  // the machine may run again later in the session
  const uint32_t flags = REX_LOAD_U32(sys + kFlagWord);
  if (flags == g_flags_before || !REXCVAR_GET(ufc_rex_first_boot_save)) return;
  if (SaveSystemData(ctx, base))
    REXLOG_INFO("[ufc-rex-first-boot] answers saved (system flags 0x{:08X} -> 0x{:08X})", g_flags_before, flags);
  else
    REXLOG_WARN("[ufc-rex-first-boot] system data save skipped: no save wrapper or system data");
}
