#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <string>
#include <mutex>
#include <unordered_set>
#include <array>
#include <atomic>

REXCVAR_DEFINE_BOOL(ufc_hud_state_diagnostics,false,"Tests","Bounded read-only HUD initialization/state trace; never forces visibility or changes controls");
namespace {
uint32_t U32(const uint8_t* base,uint32_t at){return(uint32_t(base[at])<<24)|(uint32_t(base[at+1])<<16)|(uint32_t(base[at+2])<<8)|base[at+3];}
uint16_t U16(const uint8_t* base,uint32_t at){return(uint16_t(base[at])<<8)|base[at+1];}
bool Pointer(uint32_t p){return p>=0x40000000u&&p<0xc0000000u&&(p&3)==0;}
void Trace(const char* event,const uint8_t* base,uint32_t object,uint64_t caller,bool file,bool side,bool children){
 if(!Pointer(object))return;
 const uint32_t options=U32(base,0x83577434u);
 const bool valid=Pointer(options);const uint32_t flags=valid?U32(base,options+128):0;
 std::string resource;
 if(file)for(uint32_t i=0;i<128;++i){const char c=char(base[object+308+i]);if(!c)break;if(uint8_t(c)<32||uint8_t(c)>126){resource="<non-text>";break;}resource+=c;}
 const uint32_t corner=side?U32(base,object+572):0xffffffffu;
 const uint32_t count=children?U16(base,object+296):0xffffffffu;
 const auto key=std::string(event)+":"+std::to_string(object)+":"+std::to_string(flags)+":"+resource+":"+std::to_string(corner)+":"+std::to_string(count);
 static std::mutex mutex;static std::unordered_set<std::string> seen;
 std::lock_guard lock(mutex);if(seen.size()>=64||!seen.insert(key).second)return;
 REXLOG_INFO("[ufc-hud-state] event={} object={:08X} vtable={:08X} caller={:08X} options={:08X} options_valid={} flags128={:08X} native_s_bar_on={} corner={:08X} children={:08X} resource={}",event,object,U32(base,object),uint32_t(caller),options,valid,flags,(flags&0x02000000u)!=0,corner,count,resource);
}
void TraceGate(const char* event,const uint8_t* base,uint32_t object,uint64_t caller,uint32_t result){
 if(!Pointer(object))return;
 const auto options=U32(base,0x83577434u);
 if(!Pointer(options))return;
 const auto flags=U32(base,options+128),mode=U32(base,options+196);
 const auto object_flags=U16(base,object+280);
 // Visibility may run every frame. Deduplicate without allocating strings or
 // taking a lock. Fingerprints only suppress diagnostic duplicates; they never
 // affect game state. A full table silently stops additional logging.
 uint64_t key=1469598103934665603ull;
 for(const char* p=event;*p;++p)key=(key^uint8_t(*p))*1099511628211ull;
 for(uint32_t value:{object,result,flags,mode,uint32_t(object_flags)})key=(key^value)*1099511628211ull;
 if(!key)key=1;
 static std::array<std::atomic<uint64_t>,64> seen{};
 bool fresh=false;
 for(size_t i=0;i<seen.size();++i){auto& slot=seen[(key+i)%seen.size()];auto value=slot.load(std::memory_order_relaxed);if(value==key)return;if(!value&&slot.compare_exchange_strong(value,key,std::memory_order_relaxed)){fresh=true;break;}if(value==key)return;}
 if(!fresh)return;
 REXLOG_INFO("[ufc-hud-gate] event={} object={:08X} vtable={:08X} caller={:08X} result={} flags128={:08X} options196={} object_flags280={:04X}",event,object,U32(base,object),uint32_t(caller),result,flags,mode,object_flags);
}
}

// Strong overrides of the generated weak aliases. Always call the original
// __imp__ implementation with the same context; diagnostics never write it.
extern "C" void sub_82D2C500(PPCContext& ctx,uint8_t* base){
 if(REXCVAR_GET(ufc_hud_state_diagnostics))Trace("stamina_state",base,ctx.r3.u32,ctx.lr,false,false,true);
 __imp__sub_82D2C500(ctx,base);
}
extern "C" void sub_82D37188(PPCContext& ctx,uint8_t* base){
 const auto object=ctx.r3.u32;const auto caller=ctx.lr;
 __imp__sub_82D37188(ctx,base);
 if(REXCVAR_GET(ufc_hud_state_diagnostics))Trace("stamina_file",base,object,caller,true,true,false);
}
extern "C" void sub_82D1CC68(PPCContext& ctx,uint8_t* base){
 const auto object=ctx.r3.u32;const auto caller=ctx.lr;
 __imp__sub_82D1CC68(ctx,base);
 if(REXCVAR_GET(ufc_hud_state_diagnostics))Trace("clock_file_a",base,object,caller,true,false,false);
}
extern "C" void sub_82D38F10(PPCContext& ctx,uint8_t* base){
 const auto object=ctx.r3.u32;const auto caller=ctx.lr;
 __imp__sub_82D38F10(ctx,base);
 if(REXCVAR_GET(ufc_hud_state_diagnostics))Trace("clock_file_b",base,object,caller,true,false,false);
}
extern "C" void sub_82D371B8(PPCContext& ctx,uint8_t* base){
 const auto object=ctx.r3.u32;const auto caller=ctx.lr;
 __imp__sub_82D371B8(ctx,base);
 if(REXCVAR_GET(ufc_hud_state_diagnostics))TraceGate("native_visibility",base,object,caller,ctx.r3.u32);
}
extern "C" void sub_83659800(PPCContext& ctx,uint8_t* base){
 const auto object=ctx.r3.u32;const auto caller=ctx.lr;
 __imp__sub_83659800(ctx,base);
 if(REXCVAR_GET(ufc_hud_state_diagnostics))TraceGate("reborn_visibility",base,object,caller,ctx.r3.u32);
}
