#pragma once
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(ufc_vector_diagnostics,true,"UFC","Record the original vector exception context without swallowing the error");
namespace ufc_vector_diag {
struct Entry{uint32_t caller=0,src=0,dst=0,begin=0,end=0,count=0;uint64_t calls=0;};
inline thread_local Entry entry;
}
// These strong public hooks keep the generated __imp__ entries untouched.
// Neither hook changes guest registers, memory, exception handling or scheduling.
extern "C" void sub_82E270A0(PPCContext& ctx,uint8_t* base) {
 if(!REXCVAR_GET(ufc_vector_diagnostics)){__imp__sub_82E270A0(ctx,base);return;}
 auto old=ufc_vector_diag::entry;auto& e=ufc_vector_diag::entry;
 e={ctx.lr,ctx.r4.u32,ctx.r3.u32,REX_LOAD_U32(ctx.r4.u32),REX_LOAD_U32(ctx.r4.u32+4),0,old.calls+1};e.count=(e.end-e.begin)/4;
 if(e.calls<=4)REXLOG_INFO("[ufc-vector-entry] calls={} caller={:08X} source={:08X} destination={:08X} begin={:08X} end={:08X} count={}",e.calls,e.caller,e.src,e.dst,e.begin,e.end,e.count);
 __imp__sub_82E270A0(ctx,base);
 // Keep a cumulative counter while retaining any outer invocation context.
 old.calls=e.calls;ufc_vector_diag::entry=old;
}
extern "C" void sub_823CAE30(PPCContext& ctx,uint8_t* base) {
 if(REXCVAR_GET(ufc_vector_diagnostics)&&ctx.lr==0x82E271F8&&ctx.r3.u32==0x8213303C){
  // The original bounds check just accessed these exact source/count fields.
  const uint32_t begin=REX_LOAD_U32(ctx.r27.u32),end=REX_LOAD_U32(ctx.r27.u32+4),n=REX_LOAD_U32(ctx.r28.u32+12);
  const auto& e=ufc_vector_diag::entry;
  REXLOG_ERROR("[ufc-vector-error] reason=invalid_vector_subscript lr={:08X} index={} source={:08X} begin={:08X} end={:08X} source_count={} destination={:08X} destination_count={} entry_caller={:08X} entry_count={} entry_source={:08X} entry_destination={:08X} entry_calls={} original_exception_continues=true",uint32_t(ctx.lr),ctx.r31.u32,ctx.r27.u32,begin,end,(end-begin)/4,ctx.r28.u32,n,e.caller,e.count,e.src,e.dst,e.calls);
 }
 __imp__sub_823CAE30(ctx,base);
}
