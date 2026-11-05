#!/usr/bin/env python3
"""UFC3MONS (05/11/2026): fast exact ufc_linux::Query on Linux.

Steam Deck (Original edition) froze for seconds: every on-demand readback deferral protects each
4 KB page in the 3 guest views, and each protect called ufc_linux::Query twice; Query opened and parsed
the whole /proc/self/maps (thousands of lines, growing as pages get protected one by one).
Linux 6.11+ answers the same question with the PROCMAP_QUERY ioctl on that same /proc/self/maps file:
the covering VMA's start/end and read/write/exec flags, from the same kernel VMA tree, in O(log n).

Behavior: same Region fields as the text parser (start, size, Committed, r/w/x -> Protection).
Unsupported kernel (ENOTTY/EINVAL/...) -> falls back to the original parser for the whole session.
UFC_PROCMAP_QUERY=0 -> original parser only. UFC_PROCMAP_VERIFY=1 -> runs both on every query and counts
differences (stderr every 65536 queries); UFC_PROCMAP_VERIFY=positive -> flips the Write bit of the
fast answer in the comparison only (positive control: must report differences; the result used is the
original parser's). Usage: patch_procmap.py <ufc_linux_native.h in> <out>"""
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, encoding="utf-8").read().replace("\r\n", "\n")

def rep(old, new):
    global t
    if t.count(old) != 1:
        sys.exit("anchor: " + old[:80])
    t = t.replace(old, new)

rep("#include <dlfcn.h>\n", "#include <dlfcn.h>\n#include <sys/ioctl.h>\n#include <cstdlib>\n")
OLD = """inline bool Query(void* address,Region* out,size_t){
 if(!out)return false;int fd=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return false;"""
NEW = """// Original text parser (kept: fallback, verification reference).
inline bool QueryParse(void* address,Region* out){
 if(!out)return false;int fd=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return false;"""
rep(OLD, NEW)
# After the parser's closing brace, add the fast path and the new Query.
END_PARSE = """  if(found)break;
 }close(fd);return found;
}
"""
FAST = END_PARSE + r"""// UFC3MONS 05/11: PROCMAP_QUERY (Linux 6.11+, include/uapi/linux/fs.h), declared here because the
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
"""
rep(END_PARSE, FAST)
open(dst, "w", encoding="utf-8", newline="\n").write(t)
print("ok", dst)
