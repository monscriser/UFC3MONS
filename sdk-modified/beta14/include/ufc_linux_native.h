#pragma once
// Linux x86-64 primitives for optional Windows-origin GPU diagnostics.
// No protection changes or signal handler are installed until explicitly used.
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <cstdlib>
namespace ufc_linux {
using Protection=uint32_t;
constexpr Protection NoAccess=1,Read=2,Write=4,Execute=8,Guard=16;
constexpr uint32_t Committed=1;
struct Region{void* BaseAddress=nullptr;size_t RegionSize=0;uint32_t State=0;Protection Protect=NoAccess;};
inline bool ParseRegion(const char* line,uintptr_t address,Region& out){
 const char* p=line;
 auto hex=[&](){uintptr_t v=0;while((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')){v=(v<<4)+(*p<='9'?*p-'0':*p-'a'+10);++p;}return v;};
 uintptr_t begin=hex();if(*p++!='-')return false;uintptr_t end=hex();
 if(address<begin||address>=end||*p++!=' ')return false;
 Protection access=0;if(p[0]=='r')access|=Read;if(p[1]=='w')access|=Write;if(p[2]=='x')access|=Execute;
 out={reinterpret_cast<void*>(begin),end-begin,Committed,access?access:NoAccess};return true;
}
// Original text parser (kept: fallback, verification reference).
inline bool QueryParse(void* address,Region* out){
 if(!out)return false;int fd=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 char chunk[4096],line[96];size_t n=0;bool found=false;
 for(;;){ssize_t count=read(fd,chunk,sizeof(chunk));if(count<0&&errno==EINTR)continue;if(count<=0)break;
  for(ssize_t i=0;i<count;++i){char c=chunk[i];if(c=='\n'){line[std::min(n,sizeof(line)-1)]=0;if(ParseRegion(line,uintptr_t(address),*out)){found=true;break;}n=0;}else if(n<sizeof(line)-1)line[n++]=c;}
  if(found)break;
 }close(fd);return found;
}
// UFC3MONS 05/11: PROCMAP_QUERY (Linux 6.11+, include/uapi/linux/fs.h), declared here because the
// build machine's kernel headers predate it. Layout and ioctl number are the kernel ABI.
struct UfcProcmapQuery{uint64_t size,query_flags,query_addr,vma_start,vma_end,vma_flags,vma_page_size,vma_offset,inode;
 uint32_t dev_major,dev_minor,vma_name_size,build_id_size;uint64_t vma_name_addr,build_id_addr;};
static_assert(sizeof(UfcProcmapQuery)==104,"procmap_query ABI");
constexpr unsigned long kUfcProcmapQuery=_IOWR('f',17,UfcProcmapQuery);
// -2 = not opened yet, -1 = disabled/unsupported, >=0 = fd of /proc/self/maps kept open.
inline std::atomic<int>& ProcmapFd(){static std::atomic<int> fd{-2};return fd;}
inline int ProcmapVerifyMode(){static const int v=[]{const char* e=std::getenv("UFC_PROCMAP_VERIFY");
 if(!e)return 0;if(std::strcmp(e,"positive")==0)return 2;return e[0]=='1'?1:0;}();return v;}
struct ProcmapStats{std::atomic<uint64_t> fast{0},parse{0},compared{0},mismatches{0};};
inline ProcmapStats& GetProcmapStats(){static ProcmapStats s;return s;}
// 1 = found, 0 = address not mapped, -1 = unavailable (use the parser).
inline int QueryFast(void* address,Region* out){
 auto& slot=ProcmapFd();int fd=slot.load(std::memory_order_acquire);
 if(fd==-2){const char* e=std::getenv("UFC_PROCMAP_QUERY");int f=(e&&e[0]=='0')?-1:open("/proc/self/maps",O_RDONLY|O_CLOEXEC);
  if(f<0)f=-1;int expected=-2;if(!slot.compare_exchange_strong(expected,f)){if(f>=0)close(f);fd=expected;}else fd=f;}
 if(fd<0)return -1;
 UfcProcmapQuery q{};q.size=sizeof(q);q.query_flags=0;q.query_addr=uint64_t(uintptr_t(address));
 int r;do{r=ioctl(fd,kUfcProcmapQuery,&q);}while(r<0&&errno==EINTR);
 if(r==0){Protection access=0;if(q.vma_flags&1)access|=Read;if(q.vma_flags&2)access|=Write;if(q.vma_flags&4)access|=Execute;
  *out={reinterpret_cast<void*>(uintptr_t(q.vma_start)),size_t(q.vma_end-q.vma_start),Committed,access?access:NoAccess};return 1;}
 if(errno==ENOENT)return 0;
 slot.store(-1,std::memory_order_release);return -1;  // kernel without PROCMAP_QUERY: parser from now on
}
inline bool Query(void* address,Region* out,size_t){
 if(!out)return false;
 Region fast;const int f=QueryFast(address,&fast);
 auto& st=GetProcmapStats();
 if(f<0){st.parse.fetch_add(1,std::memory_order_relaxed);return QueryParse(address,out);}
 st.fast.fetch_add(1,std::memory_order_relaxed);
 if(const int vm=ProcmapVerifyMode()){
  Region ref;const bool rf=QueryParse(address,&ref);
  Region cmp=fast;if(vm==2&&f==1)cmp.Protect^=Write;
  const bool same=(rf==(f==1))&&(!rf||(ref.BaseAddress==cmp.BaseAddress&&ref.RegionSize==cmp.RegionSize&&ref.State==cmp.State&&ref.Protect==cmp.Protect));
  const uint64_t n=st.compared.fetch_add(1,std::memory_order_relaxed)+1;
  if(!same)st.mismatches.fetch_add(1,std::memory_order_relaxed);
  if((n&0xFFFF)==0)std::fprintf(stderr,"[ufc-procmap-verify] compared=%llu mismatches=%llu mode=%d\n",(unsigned long long)n,(unsigned long long)st.mismatches.load(),vm);
  if(rf)*out=ref;return rf;  // verification runs on the original answer
 }
 if(f==1)*out=fast;return f==1;
}
inline int PosixProtection(Protection access){int p=0;if(access&Read)p|=PROT_READ;if(access&Write)p|=PROT_WRITE;if(access&Execute)p|=PROT_EXEC;return p;}
inline bool Protect(void* address,size_t length,Protection access,Protection* previous){
 if(!length||access&Guard)return false;
 Region r;if(!Query(address,&r,sizeof(r)))return false;
 const auto size=uintptr_t(sysconf(_SC_PAGESIZE));if(!size)return false;
 auto begin=uintptr_t(address)&~(size-1),end=(uintptr_t(address)+length+size-1)&~(size-1);
 if(end<=begin)return false;
 if(mprotect(reinterpret_cast<void*>(begin),end-begin,PosixProtection(access)))return false;
 if(previous)*previous=r.Protect;return true;
}
inline uint32_t ThreadId(){return uint32_t(syscall(SYS_gettid));}
inline bool WaitMicroseconds(int value){
 value=std::clamp(value,1,1000);timespec until{};if(clock_gettime(CLOCK_MONOTONIC,&until))return false;
 until.tv_nsec+=long(value)*1000;if(until.tv_nsec>=1000000000){++until.tv_sec;until.tv_nsec-=1000000000;}
 int result;do{result=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&until,nullptr);}while(result==EINTR);
 return result==0;
}
struct Fault{void* address;uintptr_t instruction;bool write;bool execute;ucontext_t* context=nullptr;siginfo_t* info=nullptr;};
using FaultHandler=bool(*)(const Fault&);
struct FaultState{std::atomic<FaultHandler> handlers[16]{};std::atomic<unsigned> count{0};std::mutex install;struct sigaction previous{};bool installed=false;};
inline FaultState& Faults(){static FaultState state;return state;}
inline void DispatchFault(int signal,siginfo_t* info,void* context){
 auto& state=Faults();auto* uc=static_cast<ucontext_t*>(context);
 const auto error=uintptr_t(uc->uc_mcontext.gregs[REG_ERR]);
 Fault fault{info->si_addr,uintptr_t(uc->uc_mcontext.gregs[REG_RIP]),bool(error&2),bool(error&16),uc,info};
 for(unsigned i=state.count.load(std::memory_order_acquire);i>0;--i){auto handler=state.handlers[i-1].load(std::memory_order_acquire);if(handler&&handler(fault))return;}
 // Another CPU may have restored this page while our fault was pending.
 // Retry only when current native permissions allow this exact operation;
 // read-only writes still reach the SDK's write-watch handler.
 if(info->si_code==SEGV_ACCERR||info->si_code==SEGV_MAPERR){Region region;
  if(Query(fault.address,&region,sizeof(region))&&(region.Protect&(fault.execute?Execute:(fault.write?Write:Read))))return;
 }
 const auto& previous=state.previous;
 if(previous.sa_handler!=SIG_DFL&&previous.sa_handler!=SIG_IGN){
  if(previous.sa_flags&SA_SIGINFO)previous.sa_sigaction(signal,info,context);else previous.sa_handler(signal);return;
 }
 struct sigaction original{};original.sa_handler=SIG_DFL;sigemptyset(&original.sa_mask);
 sigaction(signal,&original,nullptr);raise(signal);
}
inline void* AddFaultHandler(FaultHandler handler){
 auto& state=Faults();std::lock_guard lock(state.install);
 unsigned count=state.count.load();if(!handler||count==16)return nullptr;
 state.handlers[count].store(handler,std::memory_order_release);state.count.store(count+1,std::memory_order_release);
 if(!state.installed){
  if(sigaction(SIGSEGV,nullptr,&state.previous)){state.count.store(count);return nullptr;}
  struct sigaction action=state.previous;action.sa_sigaction=DispatchFault;
  action.sa_flags=SA_SIGINFO|(state.previous.sa_flags&(SA_ONSTACK|SA_RESTART));
  if(sigaction(SIGSEGV,&action,nullptr)){state.count.store(count);return nullptr;}
  state.installed=true;
 }
 return reinterpret_cast<void*>(uintptr_t(count+1));
}
inline std::string DescribeAddress(uintptr_t address){
 Dl_info info{};char text[512];if(dladdr(reinterpret_cast<void*>(address),&info)){
  const char* name=info.dli_fname?std::strrchr(info.dli_fname,'/'):nullptr;
  std::snprintf(text,sizeof(text),"%s+0x%llX",name?name+1:(info.dli_fname?info.dli_fname:"?"),(unsigned long long)(address-uintptr_t(info.dli_fbase)));
 }else std::snprintf(text,sizeof(text),"0x%llX",(unsigned long long)address);return text;
}
inline std::string Backtrace(){void* frames[24];int count=backtrace(frames,24);std::string text;for(int i=0;i<count;++i){text+=DescribeAddress(uintptr_t(frames[i]));text+=' ';}return text;}
}
