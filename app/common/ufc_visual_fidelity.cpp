#include "ufc3_init.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstring>
REXCVAR_DEFINE_BOOL(ufc_skip_live_content_notice,true,"UFC","Continue locally without the redundant offline consumable-content notice");
REXCVAR_DEFINE_BOOL(ufc_shadow_diagnostics,false,"Tests","Read-only bounded shadow-pass coverage (no render-state changes)");
extern "C" void __imp__sub_829628C0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82933EB8(PPCContext&,uint8_t*);
namespace {
uint32_t W(uint8_t* b,uint32_t a){uint32_t v;std::memcpy(&v,b+a,4);return __builtin_bswap32(v);}
bool Sample(std::atomic<uint64_t>& counter){const uint64_t n=++counter;return n<=4||((n&(n-1))==0);}
}
REX_HOOK_RAW(sub_829628C0){
 const auto self=ctx.r3.u32;
 // Match the original Yes branch AND its update boundary. State37 is case36
 // (8296361C), a completion step, not the busy-UI state39. Executing it here
 // would expose done flags44/48 before the caller observes the Yes fields.
 if(REXCVAR_GET(ufc_skip_live_content_notice)&&W(base,self)==4){
  REX_STORE_U32(self+28,1);REX_STORE_U32(self+32,0);
  REX_STORE_U32(self+40,1);REX_STORE_U32(self+52,0);
  REX_STORE_U32(self+56,1);REX_STORE_U32(self,37);
  const auto child=W(base,self+12);
  // Original common epilogue829636D4 still advances the local child once.
  // Preserve its caller frame and LR, leaving state37 for the NEXT update.
  const auto saved_lr=ctx.lr;
  const auto saved_sp=ctx.r1.u32;
  {
   rex::StackFrame frame(ctx,112);
   REX_STORE_U32(ctx.r1.u32,saved_sp);
   ctx.r3.u64=child;
   if(child){ctx.lr=0x829636E4;__imp__sub_82933EB8(ctx,base);}
  }
  ctx.lr=saved_lr;
  REXLOG_INFO("[ufc-ui] omitted offline content notice13022; original Yes fields; deferred state37; done44={} done48={}",W(base,self+44),W(base,self+48));
  return;
 }
 __imp__sub_829628C0(ctx,base);
}
extern "C" void __imp__sub_827502D0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274F968(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274FCF0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82D0A5A0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274F690(PPCContext&,uint8_t*);
namespace { void Precise(const char*,PPCContext&,uint8_t*,uint32_t,uint32_t,uint32_t=0); extern std::atomic<uint64_t> precise_reset; }
REX_HOOK_RAW(sub_827502D0){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){
  auto s=ctx.r3.u32;const auto g=W(base,0x833c0000u+0x10000u-29736u);
  REXLOG_INFO("[ufc-shadow] generate n={} self={:08X} enabled={} casters={} target-global={:08X} target-handle={:08X} zpass={:08X}",count.load(),s,W(base,s+16),W(base,s+996),g,g?W(base,g+84):0,W(base,s+28));
 }
 const auto trace_s=ctx.r3.u32,trace_lr=uint32_t(ctx.lr); Precise("generate-enter",ctx,base,trace_s,trace_lr);
 __imp__sub_827502D0(ctx,base);
 Precise("generate-leave",ctx,base,trace_s,trace_lr);
}
REX_HOOK_RAW(sub_8274F968){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){auto s=ctx.r3.u32;REXLOG_INFO("[ufc-shadow] zpass n={} self={:08X} technique={:08X} pass={} context={:08X}",count.load(),s,W(base,s+4),ctx.r5.u32,ctx.r4.u32);}
 Precise("zpass",ctx,base,ctx.r3.u32,uint32_t(ctx.lr),ctx.r5.u32);
 __imp__sub_8274F968(ctx,base);
 
}
REX_HOOK_RAW(sub_8274FCF0){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){auto s=ctx.r3.u32;REXLOG_INFO("[ufc-shadow] project n={} self={:08X} mesh={:08X} technique={:08X}",count.load(),s,W(base,s),W(base,s+4));}
 __imp__sub_8274FCF0(ctx,base);
}
REX_HOOK_RAW(sub_82D0A5A0){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){auto s=ctx.r3.u32;REXLOG_INFO("[ufc-shadow] actor-project n={} actor={:08X} plane-object={:08X}",count.load(),s,W(base,s+11780));}
 __imp__sub_82D0A5A0(ctx,base);
}
REX_HOOK_RAW(sub_8274F690){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){auto s=ctx.r3.u32;REXLOG_INFO("[ufc-shadow] blur n={} self={:08X} context={:08X} technique-name={:08X} handle={:08X} resolved={:08X}",count.load(),s,ctx.r4.u32,W(base,s+8),W(base,s+12),W(base,s+16));}
 Precise("blur",ctx,base,ctx.r3.u32,uint32_t(ctx.lr));
 __imp__sub_8274F690(ctx,base);
 
}

// Arena floor receiver is independent of the per-actor plane-object path.
extern "C" void __imp__sub_82E244D0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82E25FC8(PPCContext&,uint8_t*);
namespace {
uint32_t ShadowThreadState(PPCContext& c,uint8_t* b){
 const auto thread=W(b,c.r13.u32+256);if(!thread)return 0;
 const auto id=W(b,thread+332);constexpr uint32_t table=0x833C88C0;
 for(uint32_t i=0;i<32;++i){auto e=table+i*16;if(W(b,e)==0&&W(b,e+4)==id)return W(b,e+8);}
 return 0;
}
}
REX_HOOK_RAW(sub_82E244D0){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){
  auto s=ctx.r3.u32;auto g=W(base,0x833C810C);auto st=ShadowThreadState(ctx,base);
  REXLOG_INFO("[ufc-shadow] arena-floor n={} self={:08X} receiver-mesh={:08X} thread-state={:08X} map={:08X} enabled={} manager={:08X} strength-bits={:08X}",count.load(),s,W(base,s+56),st,st?W(base,st+2336):0,st?W(base,st+2340):0,g,g?W(base,g+80):0);
 }
 Precise("arena-draw",ctx,base,ctx.r3.u32,uint32_t(ctx.lr));
 __imp__sub_82E244D0(ctx,base);
 
}
REX_HOOK_RAW(sub_82E25FC8){
 static std::atomic<uint64_t> count{0};
 if(REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count)){
  auto s=ctx.r3.u32;auto st=ShadowThreadState(ctx,base);
  REXLOG_INFO("[ufc-shadow] background-project n={} self={:08X} technique-handle={:08X} technique-resolved={:08X} thread-state={:08X} map={:08X}",count.load(),s,W(base,s+20),W(base,s+24),st,st?W(base,st+2336):0);
 }
 __imp__sub_82E25FC8(ctx,base);
}

// v48: distinguish missing registration, disabled configuration and later clears.
// Read-only diagnostics; no forced casters, resource substitution or render flags.
extern "C" void __imp__sub_8274BEB0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274CCE0(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274BF28(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82E23000(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_8274BEB0){
 static std::atomic<uint64_t> count{0};const bool trace=REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count);
 const auto self=ctx.r3.u32,caster=ctx.r4.u32,caller=uint32_t(ctx.lr);
 const auto before=trace?W(base,self+996):0,gate=trace?W(base,0x8333A0BC):0;
 Precise("add-enter",ctx,base,self,caller,caster);
 __imp__sub_8274BEB0(ctx,base);
 Precise("add-leave",ctx,base,self,caller,caster);
 if(trace)REXLOG_INFO("[ufc-shadow-input] add n={} caller={:08X} manager={:08X} caster={:08X} gate={} before={} after={}",count.load(),caller,self,caster,gate,before,W(base,self+996));
}
REX_HOOK_RAW(sub_8274CCE0){
 static std::atomic<uint64_t> count{0};const bool trace=REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count);
 const auto caster=ctx.r3.u32,source=ctx.r4.u32,caller=uint32_t(ctx.lr);
 const auto manager=trace?W(base,0x833C810C):0;
 const auto before=trace&&manager?W(base,manager+996):0,gate=trace?W(base,0x8333A0BC):0;
 __imp__sub_8274CCE0(ctx,base);
 if(trace)REXLOG_INFO("[ufc-shadow-input] update-add n={} caller={:08X} caster={:08X} source={:08X} manager={:08X} gate={} before={} after={} caster-pass={}",count.load(),caller,caster,source,manager,gate,before,manager?W(base,manager+996):0,caster?W(base,caster+1408):0);
}
REX_HOOK_RAW(sub_8274BF28){
 static std::atomic<uint64_t> count{0};const bool trace=REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count);
 const auto self=ctx.r3.u32,caller=uint32_t(ctx.lr),before=trace?W(base,self+996):0;
 Precise("clear-enter",ctx,base,self,caller); ++precise_reset;
 __imp__sub_8274BF28(ctx,base);
 Precise("clear-leave",ctx,base,self,caller);
 if(trace)REXLOG_INFO("[ufc-shadow-input] clear n={} caller={:08X} manager={:08X} before={} after={}",count.load(),caller,self,before,W(base,self+996));
}
REX_HOOK_RAW(sub_82E23000){
 static std::atomic<uint64_t> count{0};const bool trace=REXCVAR_GET(ufc_shadow_diagnostics)&&Sample(count);const auto self=ctx.r3.u32;
 __imp__sub_82E23000(ctx,base);
 if(trace)REXLOG_INFO("[ufc-shadow-input] arena-loaded n={} self={:08X} stage-parameters={:08X} receiver-mesh={:08X} gate={} receivers={:08X},{:08X},{:08X},{:08X}",count.load(),self,W(base,self+9356),W(base,self+56),W(base,0x8333A0BC),W(base,self+48),W(base,self+72),W(base,self+96),W(base,self+124));
}

#include "shadow_precise.inc"
