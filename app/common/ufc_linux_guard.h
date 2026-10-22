#pragma once
#include "ufc_linux_native.h"
#include <unordered_map>
namespace ufc_linux_guard {
struct State{std::recursive_mutex mutex;std::unordered_map<uintptr_t,ufc_linux::Protection> pages;};
inline State& Get(){static State s;return s;}
inline bool Consume(void* address){
 auto& s=Get();std::lock_guard lock(s.mutex);auto page=uintptr_t(address)&~uintptr_t(4095);
 auto it=s.pages.find(page);if(it==s.pages.end()||!it->second)return false;
 ufc_linux::Protection previous;
 if(!ufc_linux::Protect(reinterpret_cast<void*>(page),4096,it->second,&previous))return false;
 it->second=0;return true;
}
inline void Arm(uint8_t* base,uint32_t length,std::atomic<uint64_t>& regions,std::atomic<uint64_t>& failures){
 auto& s=Get();std::lock_guard lock(s.mutex);
 for(uintptr_t p=uintptr_t(base);p<uintptr_t(base)+length;p+=4096){
  auto existing=s.pages.find(p);if(existing!=s.pages.end()&&existing->second)continue;
  ufc_linux::Region info;if(!ufc_linux::Query(reinterpret_cast<void*>(p),&info,sizeof(info))){++failures;continue;}
  if(info.Protect==ufc_linux::NoAccess)continue;
  // Bounded metadata growth; never protect a page without a recovery record.
  if(existing==s.pages.end()&&s.pages.size()>=393216){++failures;continue;}
  auto& original=s.pages[p];original=info.Protect;ufc_linux::Protection previous;
  if(!ufc_linux::Protect(reinterpret_cast<void*>(p),4096,ufc_linux::NoAccess,&previous)){original=0;++failures;}else ++regions;
 }
}
}
