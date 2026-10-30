#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <array>
#include <atomic>

// P6 Career/WFA/Pride HUD fix.
//
// Reborn's sub_83659000 redraws the native stamina bars (S_bar1_left/right,
// 19-element scenes) under its 44-element broadcast clock used by
// exh_ppv/exh_ufn. It discards every non-fill element unconditionally and the
// 3 fill slices whenever that 44-element clock is absent. WFA (WFA_clock.pac,
// 25 elements) and Pride keep the original assets, so the native bar and name
// are never submitted. With the fix ON, when the active clock is a known clock
// object whose scene is readable and is NOT the 44-element Reborn clock, the
// element goes to the native draw sub_8325ED20 unchanged. Every other case
// (including PPV/UFN and anything unknown) runs Reborn's original code.
REXCVAR_DEFINE_BOOL(ufc_career_hud_fix,true,"UFC","Draw native stamina bars/names when the fight uses an original (non-Reborn 44-element) clock, e.g. WFA/Pride in Career");
REXCVAR_DEFINE_BOOL(ufc_career_hud_diag,false,"Tests","Bounded log of Career HUD fix decisions");

namespace {
uint32_t U32(const uint8_t* base,uint32_t at){return(uint32_t(base[at])<<24)|(uint32_t(base[at+1])<<16)|(uint32_t(base[at+2])<<8)|base[at+3];}
bool Ptr(uint32_t p){const uint32_t n=p>>28;return n>=9&&n<=11&&(p&3)==0;}
// Scene data and the element array may live outside the object heap range.
bool Any(uint32_t p){return p>=0x40000000u&&p<0xfff00000u&&(p&3)==0;}

// 0 = not a native stamina bar element (use original), 1 = bar with Reborn
// clock (use original), 2 = bar with original clock (native draw), 3 = bar
// with unknown clock state (use original).
int Classify(const uint8_t* base,uint32_t widget,uint32_t element,uint32_t& clock_out,uint32_t& count_out,uint32_t& side_out){
 if(!Any(widget)||!Any(element))return 0;
 if(U32(base,widget)!=0x8213AA10u)return 0;
 const uint32_t hud=U32(base,widget+16);
 if(!Ptr(hud)||U32(base,hud)!=0x820106A0u||U32(base,hud+288)!=0x8213A9A0u||U32(base,hud+656)!=0x8213AA90u)return 0;
 uint32_t bar=0;
 for(uint32_t side=0;side<2;++side){
  const uint32_t candidate=U32(base,hud+(side==0?1332u:1328u));
  if(Ptr(candidate)&&U32(base,candidate)==0x8214D9B8u&&U32(base,candidate+28)==widget){bar=candidate;side_out=side;break;}
 }
 if(!bar)return 0;
 const uint32_t holder=U32(base,bar+36);
 if(!Any(holder)||U32(base,holder)!=0x8213A584u)return 0;
 const uint32_t layer=U32(base,holder+8);
 if(!Any(layer))return 0;
 const uint32_t scene=U32(base,layer);
 if(!Any(scene)||U32(base,element)!=scene||U32(base,scene+20)!=19)return 0;
 const uint32_t clock=U32(base,hud+1236);clock_out=clock;
 if(!Ptr(clock))return 3;
 const uint32_t vt=U32(base,clock);
 if(vt!=0x8214DF40u&&vt!=0x8214DE18u)return 3;
 const uint32_t clock_holder=U32(base,clock+36);
 if(!Any(clock_holder)||U32(base,clock_holder)!=0x8213A584u)return 3;
 const uint32_t clock_layer=U32(base,clock_holder+8);
 if(!Any(clock_layer))return 3;
 const uint32_t clock_scene=U32(base,clock_layer);
 if(!Any(clock_scene))return 3;
 const uint32_t count=U32(base,clock_scene+20);count_out=count;
 if(count==44)return 1;
 if(count==0||count>512)return 3;
 return 2;
}

void Log(int decision,uint32_t widget,uint32_t clock,uint32_t count,uint32_t side,bool enabled){
 uint64_t key=1469598103934665603ull;
 for(uint32_t v:{uint32_t(decision),widget,clock,count,side,uint32_t(enabled)})key=(key^v)*1099511628211ull;
 if(!key)key=1;
 static std::array<std::atomic<uint64_t>,32> seen{};
 bool inserted=false;
 for(auto& slot:seen){uint64_t v=slot.load(std::memory_order_relaxed);if(v==key)return;if(!v){if(slot.compare_exchange_strong(v,key)){inserted=true;break;}if(v==key)return;}}
 if(!inserted)return; // A full table stops logging, rather than flooding the sink.
 REXLOG_INFO("[ufc-career-hud] enabled={} decision={} widget={:08X} side={} clock={:08X} clock_elements={}",enabled,decision==2?"native":decision==1?"reborn-clock":"unknown",widget,side,clock,count);
}
}

// Strong override of the generated weak alias. Only reachable as the tail of
// sub_83658800 (Reborn's sub_8325ED20 interception).
extern "C" void sub_83659000(PPCContext& ctx,uint8_t* base){
 const bool enabled=REXCVAR_GET(ufc_career_hud_fix);
 const bool diag=REXCVAR_GET(ufc_career_hud_diag);
 if(enabled||diag){
  uint32_t clock=0,count=0,side=0;
  const int decision=Classify(base,ctx.r3.u32,ctx.r4.u32,clock,count,side);
  if(diag&&decision!=0)Log(decision,ctx.r3.u32,clock,count,side,enabled);
  if(enabled&&decision==2){sub_8325ED20(ctx,base);return;}
 }
 __imp__sub_83659000(ctx,base);
}
