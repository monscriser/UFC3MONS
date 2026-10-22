#pragma once
// Diagnostic only. All durations are inclusive wall time, not CPU or GPU timestamps.
// No synchronization, queue depth, fence target, query or rendering behavior changes.
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <rex/logging.h>
namespace ufc_wait_diag {
inline bool Enabled() { static const bool on = [] { const char* p = std::getenv("UFC_WAIT_DIAG"); return p && p[0]=='1'; }(); return on; }
inline uint64_t Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
enum Kind { WaitReg, Check, HostBlock, HostPoll, AwaitAll, Occlusion, RangeGpu, RangeValid, EndSubmit, Swap, GuestFence, Count };
inline constexpr const char* names[] = {"wait_reg", "check_fence", "vk_block", "vk_poll", "await_all", "occlusion", "range_gpu", "range_valid", "end_submit", "swap", "guest_fence"};
struct Metric { uint64_t ns=0, count=0, max=0; void Add(uint64_t t) { ns+=t; ++count; if(t>max)max=t; } };
struct Detail { const char* reason=nullptr; Kind kind{}; uint64_t key=0, aux=0; Metric metric; };
struct State {
 std::array<Metric,Count> frame{}, span{};
 std::array<Detail,512> details{};
 const char* reason="unspecified";
 uint64_t frame_id=0, last=0, span_frames=0, overflow=0;
};
inline State& Get() { static thread_local State state; return state; }
inline void Record(Kind kind,uint64_t ns,uint64_t key=0,uint64_t aux=0,const char* reason=nullptr) {
 auto& s=Get(); s.frame[kind].Add(ns); s.span[kind].Add(ns);
 // Memory ranges are high-cardinality (thousands per scene). Keep their exact
 // elapsed totals, but aggregate details rather than linearly scanning/logging
 // an entry for every address. This avoids tracing dominating frame CPU time.
 if(kind==RangeGpu || kind==RangeValid) { key=0;aux=0; }
 const char* why=reason?reason:s.reason;
 for(auto& d:s.details) { if(!d.reason || (d.reason==why && d.kind==kind && d.key==key && d.aux==aux)) { d.reason=why; d.kind=kind; d.key=key; d.aux=aux; d.metric.Add(ns); return; } }
 ++s.overflow;
}
struct Context { const char* old; explicit Context(const char* why):old(Get().reason){Get().reason=why;} ~Context(){Get().reason=old;} };
struct Scope {
 bool on; Kind kind; uint64_t begin=0,key,aux; const char* reason;
 Scope(Kind k,uint64_t a=0,uint64_t b=0,const char* r=nullptr):on(Enabled()),kind(k),key(a),aux(b),reason(r) { if(on)begin=Now(); }
 ~Scope(){if(on)Record(kind,Now()-begin,key,aux,reason);}
};
inline void Frame() {
 if(!Enabled())return;
 auto& s=Get(); auto now=Now(); ++s.frame_id; ++s.span_frames;
 // Thread ID is supplied by the runtime logger. qpc_ns also correlates app and DLL.
 REXLOG_INFO("[ufc-wait-frame] frame={} qpc_ns={} interval_ms={:.3f} wait_reg_ms={:.3f} wait_reg_n={} vk_block_ms={:.3f} vk_block_n={} vk_poll_ms={:.3f} check_ms={:.3f} await_ms={:.3f} occlusion_ms={:.3f} occlusion_n={} range_gpu_ms={:.3f} range_valid_ms={:.3f} end_submit_ms={:.3f} end_submit_n={} swap_ms={:.3f}",
 s.frame_id,now,s.last?double(now-s.last)/1e6:0.,double(s.frame[WaitReg].ns)/1e6,s.frame[WaitReg].count,double(s.frame[HostBlock].ns)/1e6,s.frame[HostBlock].count,double(s.frame[HostPoll].ns)/1e6,double(s.frame[Check].ns)/1e6,double(s.frame[AwaitAll].ns)/1e6,double(s.frame[Occlusion].ns)/1e6,s.frame[Occlusion].count,double(s.frame[RangeGpu].ns)/1e6,double(s.frame[RangeValid].ns)/1e6,double(s.frame[EndSubmit].ns)/1e6,s.frame[EndSubmit].count,double(s.frame[Swap].ns)/1e6);
 s.last=Now(); s.frame={};
 if(s.span_frames<60)return;
 for(auto& d:s.details) { if(!d.reason)continue;
 REXLOG_INFO("[ufc-wait-detail] end_frame={} frames={} kind={} reason={} key={:X} aux={:X} count={} total_ms={:.3f} max_ms={:.3f}",s.frame_id,s.span_frames,names[d.kind],d.reason,d.key,d.aux,d.metric.count,double(d.metric.ns)/1e6,double(d.metric.max)/1e6);
 }
 REXLOG_INFO("[ufc-wait-span] end_frame={} frames={} detail_overflow={}",s.frame_id,s.span_frames,s.overflow);
 s.span={};s.details={};s.span_frames=0;s.overflow=0;
}
struct FrameEnd { ~FrameEnd(){Frame();} };
}
