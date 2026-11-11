// Fighter skin keeps the same brightness on every camera (--ufc_rex_steady_skin_light, default
// off; false = the original, which darkens/warms the skin on steep top-down cameras).
//
// Guest code (identical in Original v48 and Reborn r9):
//  sub_82EEB500()        returns in f1 a top-camera weight from the active camera's angle (+512):
//                        w = clamp(clamp((k1 - angle) * k2, 0, 1) * k3, 0, 1); 0 on the side
//                        (stand-up) cameras, 1 looking down (clinch, takedown and ground cameras);
//                        0 when there is no camera. It only reads; nothing else is written.
//  Callers               sub_8273D210 (scales it by the light set's +992 and lerps the fighter
//                        light colours with it), sub_82752B18 (twice), sub_8275E7F0 and
//                        sub_82C303D8 / 82C30A88 / 82C312F0 / 82C315B8 / 82C31B50, which pass it
//                        in f1 to sub_8273D0E0: that lerps the skin shadow parameters between the
//                        side preset ([0x833C8108] +232..) and the top-down preset (+212..) by w.
//
// Fix: the weight is returned as 0, so every camera uses the side-camera skin lighting. This
// changes the look the game intends on top-down cameras (less warm, more rim light), so it is
// off by default.
#include "ufc3_init.h"
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(ufc_rex_steady_skin_light, false, "UFC",
                    "Keep the fighters' side-camera skin lighting on clinch/ground (top-down) cameras instead "
                    "of the game's darker, warmer top-down blend; changes the original look");

extern "C" void __imp__sub_82EEB500(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_82EEB500) {
  __imp__sub_82EEB500(ctx, base);
  if (REXCVAR_GET(ufc_rex_steady_skin_light)) ctx.f1.f64 = 0.0;
}
