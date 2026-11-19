// Reborn: created fighter (CAF) portraits on SELECT FIGHTER (work in progress, diagnostics).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <cstdint>
#include <cstring>
#include <string>

REXCVAR_DEFINE_BOOL(ufc_caf_thumb_diag, false, "Tests", "Log portrait name formatting and CAF portrait lookups");

extern "C" void __imp__sub_827A0878(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82AA50B8(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_827A0878) {
  const uint32_t fmt = ctx.r5.u32, dst = ctx.r3.u32, lr = uint32_t(ctx.lr), v = ctx.r6.u32, v2 = ctx.r7.u32;
  __imp__sub_827A0878(ctx, base);
  if (!REXCVAR_GET(ufc_caf_thumb_diag) || fmt < 0x82000000u || fmt >= 0x84000000u || dst < 0x40000000u) return;
  const char* f = reinterpret_cast<const char*>(base + fmt);
  if (!std::strstr(f, "fc") && !std::strstr(f, "face") && !std::strstr(f, ".dds")) return;
  static int n = 0;
  if (++n > 600) return;
  REXLOG_INFO("[ufc-caf-thumb] fmt lr=0x{:08X} r1={:08X} fmt='{}' v={} v2={} -> '{}'", lr, ctx.r1.u32, f, v, v2,
              std::string(reinterpret_cast<const char*>(base + dst), strnlen(reinterpret_cast<const char*>(base + dst), 96)));
}

REXCVAR_DEFINE_INT32(ufc_caf_thumb_test_id, 0, "Tests", "Test: CAF ids (1200-1239) get the portrait of this fighter id");
REXCVAR_DEFINE_INT32(ufc_caf_thumb_swap_id, 0, "Tests", "Test: this fighter id gets the CAF 1201 portrait");
REX_HOOK_RAW(sub_82AA50B8) {
  const uint32_t mgr = ctx.r3.u32, r4 = ctx.r4.u32, id = ctx.r5.u32, lr = uint32_t(ctx.lr);
  const int test = REXCVAR_GET(ufc_caf_thumb_test_id);
  if (test > 0 && id >= 1200 && id < 1240) { ctx.r4.u64 = uint32_t(test); ctx.r5.u64 = uint32_t(test); }
  const int swap = REXCVAR_GET(ufc_caf_thumb_swap_id);
  if (swap > 0 && int(id) == swap) { ctx.r4.u64 = 1201; ctx.r5.u64 = 1201; }
  __imp__sub_82AA50B8(ctx, base);
  if (!REXCVAR_GET(ufc_caf_thumb_diag)) return;
  static int n = 0;
  if (++n > 300) return;
  REXLOG_INFO("[ufc-caf-thumb] portrait lr=0x{:08X} mgr={:08X} r4={:08X} id={} -> {:08X}", lr, mgr, r4, id, ctx.r3.u32);
}

// Grid refresh of SELECT FIGHTER (r3 = screen object): log its vtable and the image setter (vtable+0xE8).
extern "C" void __imp__sub_829AADF0(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_829AADF0) {
  if (REXCVAR_GET(ufc_caf_thumb_diag)) {
    static int n = 0;
    const uint32_t obj = ctx.r3.u32, vt = REX_LOAD_U32(obj);
    if (++n <= 6)
      REXLOG_INFO("[ufc-caf-thumb] grid obj={:08X} vt={:08X} set_image={:08X} r4={} r5={}", obj, vt, REX_LOAD_U32(vt + 0xE8),
                  ctx.r4.u32, ctx.r5.u32);
  }
  __imp__sub_829AADF0(ctx, base);
}
