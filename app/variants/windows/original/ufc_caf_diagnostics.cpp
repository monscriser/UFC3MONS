#include "ufc_wait_diag.h"
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include "ufc_linux_crash.h"
#endif
REXCVAR_DEFINE_BOOL(ufc_caf_diagnostics,false,"Tests","Trace isolated CAF-save reproduction");
REXCVAR_DEFINE_STRING(ufc_crash_capture,"","Tests","Explicit isolated crash dump path");
REXCVAR_DEFINE_BOOL(ufc_caf_queue_submit,true,"UFC","Run the Reborn CAF serial capture and command submission on the render queue");
REXCVAR_DEFINE_BOOL(ufc_caf_queue_fence,true,"UFC","Run the Reborn CAF device fence wait on the existing render queue");
REXCVAR_DEFINE_BOOL(ufc_caf_render_queue,true,"UFC","Run Reborn CAF texture copies on the existing render queue");
namespace {
struct CafJob{std::mutex mutex;std::condition_variable completed;bool done=false;bool fence=false;bool submit=false;uint32_t arguments[5]{};uint64_t result=0;};
std::mutex jobs_mutex;std::map<uint32_t,std::shared_ptr<CafJob>> jobs;
std::shared_ptr<CafJob> FindJob(uint32_t self){std::lock_guard lock(jobs_mutex);auto it=jobs.find(self);return it==jobs.end()?nullptr:it->second;}
thread_local bool in_caf_save=false;
uint32_t Word(uint8_t* base,uint32_t address){
 if(address<0x10000||address>0xfffffffbu)return 0xdeadbeef;
#ifdef _WIN32
 MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(base+address,&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return 0xdeadbeef;
#endif
 uint32_t value;std::memcpy(&value,base+address,4);return __builtin_bswap32(value);
}
#ifdef _WIN32
LONG CALLBACK Capture(EXCEPTION_POINTERS* p){
 if(!in_caf_save||p->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||p->ExceptionRecord->NumberParameters<2||p->ExceptionRecord->ExceptionInformation[1]!=0x100000018ULL)return EXCEPTION_CONTINUE_SEARCH;
 auto path=REXCVAR_GET(ufc_crash_capture);if(path.empty())return EXCEPTION_CONTINUE_SEARCH;
 auto file=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return EXCEPTION_CONTINUE_SEARCH;
 MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),p,FALSE};MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,nullptr,nullptr);CloseHandle(file);return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}
#ifndef _WIN32
namespace {
std::string linux_crash_path;
bool CaptureLinux(const ufc_linux::Fault& fault){
 if(in_caf_save&&uintptr_t(fault.address)==0x100000018ULL&&!linux_crash_path.empty())ufc_linux_crash::Capture(linux_crash_path.c_str(),fault);
 return false;
}
}
#endif
void InstallUfcCafDiagnostics(){
#ifdef _WIN32
 if(REXCVAR_GET(ufc_caf_diagnostics)&&!REXCVAR_GET(ufc_crash_capture).empty())AddVectoredExceptionHandler(1,Capture);
#else
 if(REXCVAR_GET(ufc_caf_diagnostics)&&!REXCVAR_GET(ufc_crash_capture).empty()){linux_crash_path=REXCVAR_GET(ufc_crash_capture);if(!ufc_linux::AddFaultHandler(CaptureLinux))REXLOG_WARN("[ufc-caf] Linux crash capture could not be installed");}
#endif
}
extern "C" void __imp__sub_82794700(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82794378(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82384100(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8236F328(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82370568(PPCContext&,uint8_t*);
extern "C" void __imp__sub_8274A0D8(PPCContext&,uint8_t*);
REX_HOOK_RAW(sub_82794700){
 if(uint32_t(ctx.lr)!=0x8325f094){__imp__sub_82794700(ctx,base);return;}
 in_caf_save=true;auto self=ctx.r3.u32;auto desc=Word(base,self+4);auto global=Word(base,0x833c8100);
 if(REXCVAR_GET(ufc_caf_diagnostics))REXLOG_WARN("[caf-save] enter lr={:08X} sp={:08X} self={:08X} desc={:08X} global={:08X} descriptor={:08X},{:08X},{:08X},{:08X},{:08X} state={:08X}",uint32_t(ctx.lr),ctx.r1.u32,self,desc,global,Word(base,desc),Word(base,desc+4),Word(base,desc+8),Word(base,desc+12),Word(base,desc+68),Word(base,self+480));rex::FlushLogging();
 if(REXCVAR_GET(ufc_caf_render_queue)&&desc>=0x10000&&desc<=0xffffff9fu&&Word(base,desc+68)==0){
  // Native game queue already dispatches this exact callback. Keep its stack
  // descriptor alive until the existing completion word is set by the callback.
  auto job=std::make_shared<CafJob>();{
   std::lock_guard lock(jobs_mutex);if(!jobs.emplace(self,job).second)throw std::runtime_error("Duplicate CAF texture-copy request");
  }
  REX_STORE_U32(desc+68,1);
  __imp__sub_82794700(ctx,base);
  {std::unique_lock lock(job->mutex);job->completed.wait(lock,[&]{return job->done;});}
  REX_STORE_U32(desc+68,0);{std::lock_guard lock(jobs_mutex);jobs.erase(self);}
  REXLOG_INFO("[ufc-caf] render-queue copy completed self={:08X}",self);
 }else __imp__sub_82794700(ctx,base);if(REXCVAR_GET(ufc_caf_diagnostics))REXLOG_WARN("[caf-save] leave r3={:08X} sp={:08X}",ctx.r3.u32,ctx.r1.u32);in_caf_save=false;
}
REX_HOOK_RAW(sub_82794378){
 auto job=FindJob(ctx.r3.u32);bool queued=bool(job);
 if(job&&(job->fence||job->submit)){
  // Preserve the dispatcher's context and stack. Execute the unmodified D3D
  // wait on the render thread, using its own guest stack and thread-local state.
  PPCContext call=ctx;
  call.r3.u64=job->arguments[0];call.r4.u64=job->arguments[1];
  call.r5.u64=job->arguments[2];call.r6.u64=job->arguments[3];call.r7.u64=job->arguments[4];
  if(job->submit)__imp__sub_82370568(call,base);
  else __imp__sub_8236F328(call,base);
  {std::lock_guard lock(job->mutex);job->result=call.r3.u64;job->done=true;}
  job->completed.notify_one();return;
 }

 if(queued){in_caf_save=true;if(REXCVAR_GET(ufc_caf_diagnostics))REXLOG_WARN("[caf-queue] callback self={:08X}",ctx.r3.u32);}
 if(in_caf_save&&REXCVAR_GET(ufc_caf_diagnostics)){auto desc=Word(base,ctx.r3.u32+4);REXLOG_WARN("[caf-save] immediate desc={:08X} source={:08X} destination={:08X}",desc,Word(base,desc+4),Word(base,desc+8));rex::FlushLogging();}
 __imp__sub_82794378(ctx,base);if(job){
  // Publish host completion only after the guest callback has fully returned;
  // the caller may now reuse its stack and release texture resources safely.
  {std::lock_guard lock(job->mutex);job->done=true;}job->completed.notify_one();in_caf_save=false;
 }
}
REX_HOOK_RAW(sub_82384100){
 if(in_caf_save&&REXCVAR_GET(ufc_caf_diagnostics)){REXLOG_WARN("[caf-save] draw-indexed lr={:08X} device={:08X} index-buffer={:08X} primitive={} start={} count={} r7={:08X}",uint32_t(ctx.lr),ctx.r3.u32,Word(base,ctx.r3.u32+12684),ctx.r4.u32,ctx.r5.u32,ctx.r6.u32,ctx.r7.u32);rex::FlushLogging();}
 __imp__sub_82384100(ctx,base);
}

// The mod's copy was queued, but its following fence/flush still accessed the
// shared D3D device from the save worker. Queue that exact call as well.
REX_HOOK_RAW(sub_8236F328){
 if(!REXCVAR_GET(ufc_caf_queue_fence)||uint32_t(ctx.lr)!=0x8325f0dc){
  if(!ufc_wait_diag::Enabled()){__imp__sub_8236F328(ctx,base);return;}
  const uint32_t device=ctx.r3.u32,target=ctx.r4.u32,lr=uint32_t(ctx.lr);
  const uint32_t ptr=target?REX_LOAD_U32(device+11024):0,before=target?REX_LOAD_U32(ptr):0,issued=target?REX_LOAD_U32(device+11036):0;
  const auto begin=ufc_wait_diag::Now();
  __imp__sub_8236F328(ctx,base);
  const auto end=ufc_wait_diag::Now();
  struct Sample{uint32_t lr=0,device=0,target=0,before=0,after=0,issued=0;uint64_t n=0,ns=0,max=0;};
  static thread_local std::array<Sample,32> samples{};
  static thread_local uint64_t last=begin;
  for(auto& sample:samples)if(!sample.n||sample.lr==lr){sample.lr=lr;sample.device=device;sample.target=target;sample.before=before;sample.after=target?REX_LOAD_U32(ptr):0;sample.issued=issued;++sample.n;sample.ns+=end-begin;if(end-begin>sample.max)sample.max=end-begin;break;}
  if(end-last>=1000000000ULL){
   for(const auto& sample:samples)if(sample.n)REXLOG_INFO("[ufc-guest-fence] qpc_ns={} span_ms={:.3f} lr={:08X} device={:08X} target={:08X} before={:08X} after={:08X} issued={:08X} count={} total_ms={:.3f} max_ms={:.3f}",end,double(end-last)/1e6,sample.lr,sample.device,sample.target,sample.before,sample.after,sample.issued,sample.n,double(sample.ns)/1e6,double(sample.max)/1e6);
   samples={};last=end;
  }
  return;
 }
 auto job=std::make_shared<CafJob>();job->fence=true;
 job->arguments[0]=ctx.r3.u32;job->arguments[1]=ctx.r4.u32;job->arguments[2]=ctx.r5.u32;job->arguments[3]=ctx.r6.u32;job->arguments[4]=ctx.r7.u32;
 const auto key=ctx.r1.u32; // Opaque live request token; never dereferenced as an object.
 {std::lock_guard lock(jobs_mutex);if(!jobs.emplace(key,job).second)throw std::runtime_error("Duplicate CAF fence request");}
 ctx.r3.u64=Word(base,0x833c8100);ctx.r4.u64=0x82794378;ctx.r5.u64=key;
 __imp__sub_8274A0D8(ctx,base);
 {std::unique_lock lock(job->mutex);job->completed.wait(lock,[&]{return job->done;});ctx.r3.u64=job->result;}
 {std::lock_guard lock(jobs_mutex);jobs.erase(key);}
 REXLOG_INFO("[ufc-caf] render-queue fence completed device={:08X} serial={:08X}",job->arguments[0],job->arguments[1]);
}

// Static audit EXP-043: 82370568 is NOT a read-only serial getter. It reads
// device+11036, writes device+11056 and calls 82370460 -> 823702D8, which
// submits and advances shared command-buffer pointers. v41 queued only the
// following wait; this intervening flush still raced the render producer.
// Preserve the original serial capture, flush and result on the render queue.
REX_HOOK_RAW(sub_82370568){
 if(!REXCVAR_GET(ufc_caf_queue_submit)||uint32_t(ctx.lr)!=0x8325f0c4){__imp__sub_82370568(ctx,base);return;}
 auto job=std::make_shared<CafJob>();job->submit=true;
 job->arguments[0]=ctx.r3.u32;job->arguments[1]=ctx.r4.u32;job->arguments[2]=ctx.r5.u32;job->arguments[3]=ctx.r6.u32;job->arguments[4]=ctx.r7.u32;
 const auto key=ctx.r1.u32;
 {std::lock_guard lock(jobs_mutex);if(!jobs.emplace(key,job).second)throw std::runtime_error("Duplicate CAF submit request");}
 ctx.r3.u64=Word(base,0x833c8100);ctx.r4.u64=0x82794378;ctx.r5.u64=key;
 __imp__sub_8274A0D8(ctx,base);
 {std::unique_lock lock(job->mutex);job->completed.wait(lock,[&]{return job->done;});ctx.r3.u64=job->result;}
 {std::lock_guard lock(jobs_mutex);jobs.erase(key);}
 REXLOG_INFO("[ufc-caf] render-queue submit completed device={:08X} serial={:08X}",job->arguments[0],uint32_t(job->result));
}

#include <immintrin.h>
REXCVAR_DEFINE_BOOL(ufc_spin_backoff,false,"UFC","Bounded pause/yield between original guest GPU wait checks");
extern "C" void __imp__sub_82374768(PPCContext&,uint8_t*);
REXCVAR_DEFINE_INT32(ufc_spin_sleep_us,200,"UFC","Maximum timer delay between long guest wait checks, microseconds");
REX_HOOK_RAW(sub_82374768) {
 if(REXCVAR_GET(ufc_spin_backoff)) {
  thread_local uint32_t steps=0;
  thread_local std::chrono::steady_clock::time_point last{};
  const auto now=std::chrono::steady_clock::now();
  if(now-last>std::chrono::milliseconds(2))steps=0;
  if(steps<128)++steps;
  if(steps<=64){for(int i=0;i<8;++i)_mm_pause();}
  else if(steps<128)std::this_thread::yield();
  else {
#ifdef _WIN32
   struct SpinTimer{HANDLE value=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);~SpinTimer(){if(value)CloseHandle(value);}};
   thread_local SpinTimer timer;
   const int us=REXCVAR_GET(ufc_spin_sleep_us);
   LARGE_INTEGER due;due.QuadPart=-int64_t(std::clamp(us,0,1000))*10;
   if(us>0 && timer.value && SetWaitableTimer(timer.value,&due,0,nullptr,nullptr,FALSE))WaitForSingleObject(timer.value,INFINITE);
   else std::this_thread::yield();
#else
   const int us=std::clamp(REXCVAR_GET(ufc_spin_sleep_us),0,1000);if(us>0)ufc_linux::WaitMicroseconds(us);else std::this_thread::yield();
#endif
  }
  last=std::chrono::steady_clock::now();
 }
 __imp__sub_82374768(ctx,base);
}
