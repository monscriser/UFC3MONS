#pragma once
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xmemory.h>
#include <array>
#include <cstring>
#include <stdexcept>
REXCVAR_DEFINE_BOOL(ufc_missing_initializer_thunk,true,"UFC","Register verified eight-instruction initializer at82E271F8");
REXCVAR_DEFINE_BOOL(ufc_initializer_probe,false,"Tests","Exercise the initializer through the real dispatcher before guest startup");
REXCVAR_DEFINE_BOOL(ufc_initializer_probe_positive,false,"Tests","Perturb only the isolated initializer reference");
REXCVAR_DEFINE_BOOL(ufc_initializer_signature_positive,false,"Tests","Perturb only the expected code signature to test refusal");
static void UfcInitializer271F8(PPCContext& ctx,uint8_t* base) {
 REX_FUNC_PROLOGUE();
 PPCRegister temp{};
 // Exact guest instructions, including the source load, scratch registers and f0.
 ctx.r11.s64=-2112618496LL;
 ctx.r10.s64=1;
 REX_STORE_U32(ctx.r3.u32+8,ctx.r10.u32);
 ctx.fpscr.disableFlushMode();
 temp.u32=REX_LOAD_U32(ctx.r11.u32-25124);
 ctx.f0.f64=double(temp.f32);
 temp.f32=float(ctx.f0.f64);
 REX_STORE_U32(ctx.r3.u32,temp.u32);
 REX_STORE_U32(ctx.r3.u32+4,temp.u32);
 REX_STORE_U32(ctx.r3.u32+12,temp.u32);
 // Return retains LR and every non-written guest register, as blr does.
 static thread_local uint64_t calls=0;
 if(++calls<=8)REXLOG_INFO("[ufc-initializer-call] calls={} thunk=82E271F8 object={:08X} source_bits={:08X}",calls,ctx.r3.u32,temp.u32);
}
inline void UfcInstallInitializerFix(rex::memory::Memory* memory) {
 auto* dispatcher=rex::Runtime::instance()->function_dispatcher();auto* base=memory->virtual_membase();
 const bool enabled=REXCVAR_GET(ufc_missing_initializer_thunk),probe=REXCVAR_GET(ufc_initializer_probe);
 if(!enabled){
  REXLOG_INFO("[ufc-initializer-fix] enabled=false thunk=82E271F8");
  if(probe){PPCContext ctx{};ctx.last_indirect_target=0x82E271F8;rex::runtime::ResolveIndirectFunction(0x82E271F8)(ctx,base);}
  return;
 }
 uint32_t code[]={0x3D608214,0x39400001,0x91430008,0xC00B9DDC,0xD0030000,0xD0030004,0xD003000C,0x4E800020};
 if(REXCVAR_GET(ufc_initializer_signature_positive))code[0]^=1;
 for(unsigned i=0;i<8;++i){auto* p=base+0x82E271F8+i*4;const uint32_t actual=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
  if(actual!=code[i]){REXLOG_ERROR("[ufc-initializer-fix] refused=true instruction={} expected={:08X} actual={:08X}",i,code[i],actual);throw std::runtime_error("Initializer fix: unexpected guest code signature");}}
 if(dispatcher->GetFunction(0x82E271F8))throw std::runtime_error("Initializer fix: address is already registered; audit existing implementation");
 if(!dispatcher->SetFunction(0x82E271F8,UfcInitializer271F8))throw std::runtime_error("Initializer fix: registration failed");
 REXLOG_INFO("[ufc-initializer-fix] enabled=true signature=8_instructions_verified thunk=82E271F8");
 if(!probe)return;
 const uint32_t scratch=memory->SystemHeapAlloc(128);if(!scratch)throw std::runtime_error("Initializer probe allocation failed");
 std::array<uint8_t,128> reference;reference.fill(0xA5);std::memcpy(base+scratch,reference.data(),reference.size());
 const uint32_t object=scratch+32;const uint32_t source=uint32_t(base[0x82139DDC])<<24|uint32_t(base[0x82139DDD])<<16|uint32_t(base[0x82139DDE])<<8|base[0x82139DDF];
 auto put=[&](unsigned offset,uint32_t value){for(unsigned i=0;i<4;++i)reference[32+offset+i]=uint8_t(value>>(24-8*i));};
 put(0,source);put(4,source);put(8,1);put(12,source);
 const bool positive=REXCVAR_GET(ufc_initializer_probe_positive);if(positive)reference[43]^=1;
 PPCContext ctx{};PPCRegister* registers[]={&ctx.r0,&ctx.r1,&ctx.r2,&ctx.r3,&ctx.r4,&ctx.r5,&ctx.r6,&ctx.r7,&ctx.r8,&ctx.r9,&ctx.r10,&ctx.r11,&ctx.r12,&ctx.r13,&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,&ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
 for(unsigned i=0;i<32;++i)registers[i]->u64=0x1122334400000000ULL+i;
 ctx.r3.u64=object;ctx.lr=0x1234ABCD;ctx.last_indirect_target=0x82E271F8;
 rex::runtime::ResolveIndirectFunction(0x82E271F8)(ctx,base);
 bool same=std::memcmp(base+scratch,reference.data(),reference.size())==0;
 for(unsigned i=0;i<32;++i)if(i!=3&&i!=10&&i!=11)same&=registers[i]->u64==0x1122334400000000ULL+i;
 same&=ctx.r3.u64==object&&ctx.r10.u64==1&&ctx.r11.s64==-2112618496LL&&ctx.lr==0x1234ABCD;
 // Both audited XEX images have +0.0 at the source address; verify the loaded f0.
 same&=source==0&&ctx.f0.f64==0.0;
 memory->SystemHeapFree(scratch);
 REXLOG_INFO("[ufc-initializer-probe] equivalent={} positive={} detected={} redzones=true registers=true dispatcher=true",same,positive,positive&&!same);
 if(same==positive)throw std::runtime_error("Initializer probe failed");
}
