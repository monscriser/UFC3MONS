#pragma once
#include "ufc_reborn_footer_text.h"
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/logging.h>
#include <rex/cvar.h>
REXCVAR_DEFINE_BOOL(ufc_reborn_footer_localization,true,"UFC","Localize Reborn's embedded selection footer without changing the roster language");
REXCVAR_DEFINE_BOOL(ufc_reborn_footer_native_buttons,false,"Tests","Use native translated accept/back labels instead of the embedded duplicate (test only)");
REXCVAR_DEFINE_BOOL(ufc_reborn_footer_signature_positive,false,"Tests","Alter only the expected footer signature to prove that mismatches preserve all guest bytes");
inline void LocalizeUfcRebornFooter(rex::Runtime* runtime,int language){
 if(!REXCVAR_GET(ufc_reborn_footer_localization)||language<=0||language>5){REXLOG_INFO("[ufc-footer] localized=false language={}",language);return;}
 auto* memory=runtime->memory();auto* base=memory->virtual_membase();
 auto slots=ufc_footer::Slots(language,REXCVAR_GET(ufc_reborn_footer_native_buttons));
 const bool positive=REXCVAR_GET(ufc_reborn_footer_signature_positive);
 if(positive)slots.front().before.front()^=1;
 // Verify every complete slot, including zero padding, before touching any slot.
 for(const auto& slot:slots)if(std::memcmp(base+slot.address,slot.before.data(),slot.capacity)){REXLOG_ERROR("[ufc-footer] signature_mismatch={:08X} positive={} detected=true; preserving all original footer strings",slot.address,positive);return;}
 unsigned writes=0;
 for(const auto& slot:slots){
  auto* heap=memory->LookupHeap(slot.address);uint32_t previous=0;
  if(!heap||!heap->Protect(slot.address,uint32_t(slot.capacity),rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite,&previous))throw std::runtime_error("Cannot protect Reborn footer slot");
  std::memcpy(base+slot.address,slot.after.data(),slot.capacity);
  const bool same=std::memcmp(base+slot.address,slot.after.data(),slot.capacity)==0;
  if(!heap->Protect(slot.address,uint32_t(slot.capacity),previous)||!same)throw std::runtime_error("Cannot verify localized Reborn footer");
  ++writes;
 }
 REXLOG_INFO("[ufc-footer] localized=true language={} slots={} verified=true native_buttons={}",language,writes,REXCVAR_GET(ufc_reborn_footer_native_buttons));
}
