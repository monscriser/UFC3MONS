#pragma once
#include <rex/cvar.h>
#include <rex/logging.h>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <string>
REXCVAR_DEFINE_BOOL(ufc_hud_diagnostics,false,"Tests","Bounded read-only trace of original HUD asset opens; no GPU queries or per-frame logging");
namespace ufc_hud_trace {
inline bool Candidate(const std::string& value){
 std::string lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
 return lower.ends_with(".pac")&&(lower.find("hud")!=lower.npos||lower.ends_with("sample.pac")||lower.ends_with("sample_pr.pac")||lower.ends_with("spar.pac")||lower.ends_with("spar_po.pac"));
}
inline void Open(uint32_t root,const std::string& path,uint32_t status,uint32_t handle){
 static std::mutex mutex;static std::unordered_set<std::string> seen;
 std::lock_guard lock(mutex);if(seen.size()>=128)return;
 const auto key=std::to_string(root)+":"+path+":"+std::to_string(status);
 if(!seen.insert(key).second)return;
 REXLOG_INFO("[ufc-hud-open] root={:08X} path={} status={:08X} handle={:08X}",root,path,status,handle);
}
}
