#pragma once
#include "ufc_shirt_alias_data.h"
#include "ufc_library.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(ufc_reborn_shirt_aliases,false,"UFC","Experimental frozen varied shirt assignments for all 342 Reborn fighter models");
REXCVAR_DEFINE_BOOL(ufc_shirt_alias_signature_positive,false,"Tests","Refuse shirt alias signatures without modifying source or cache");
inline void MountUfcShirtAliases(rex::Runtime* runtime,const std::filesystem::path& game){
 if(!REXCVAR_GET(ufc_reborn_shirt_aliases)){REXLOG_INFO("[ufc-shirt-aliases] enabled=false applied=false");return;}
 namespace fs=std::filesystem;
 try{
  const char* relative[]={"DAT/chara/figacce.mpc","DAT/static_x.mpc"};
  const char* guest[]={"game:\\DAT\\chara\\figacce.mpc","game:\\DAT\\static_x.mpc"};
  const char* before[]={ufc_shirt_alias::Before0,ufc_shirt_alias::Before1};
  const char* after[]={ufc_shirt_alias::After0,ufc_shirt_alias::After1};
  std::array<rex::filesystem::HostPathEntry*,2> entries{};std::array<fs::path,2> source{},target{};
  // Validate the pair before creating or mounting either output.
  for(unsigned i=0;i<2;++i){entries[i]=dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath(guest[i]));if(!entries[i]){REXLOG_WARN("[ufc-shirt-aliases] enabled=true applied=false reason=missing_entry");return;}source[i]=entries[i]->host_path();if(ufc::Hash(source[i])!=before[i]){REXLOG_WARN("[ufc-shirt-aliases] enabled=true applied=false reason=unrecognized_source asset={} preserving=true",relative[i]);return;}}
  if(REXCVAR_GET(ufc_shirt_alias_signature_positive)){REXLOG_INFO("[ufc-shirt-aliases] enabled=true applied=false reason=positive_signature_refused preserving=true");return;}
  for(unsigned i=0;i<2;++i){
   target[i]=ufc::Storage(game)/"compatibility/shirt-alias-v3-variety"/(std::string(after[i])+".mpc");
   if(fs::exists(target[i])){if(!fs::is_regular_file(target[i])||ufc::Hash(target[i])!=after[i]){REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=cache_signature_mismatch preserving=true");return;}}
   else{std::ifstream input(source[i],std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});if(!ufc_shirt_alias::Apply(bytes,i)){REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=patch_signature_mismatch preserving=true");return;}ufc::AtomicText(target[i],bytes);if(ufc::Hash(target[i])!=after[i])throw std::runtime_error("Generated shirt alias hash mismatch");}
  }
  if(!entries[0]->OverrideReadOnlyFile(target[0])){REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=first_override_failed");return;}
  if(!entries[1]->OverrideReadOnlyFile(target[1])){const bool restored=entries[0]->OverrideReadOnlyFile(source[0]);REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=second_override_failed rollback={}",restored);return;}
  REXLOG_INFO("[ufc-shirt-aliases] enabled=true applied=true new_aliases=192 covered_models=342 assigned_variants=114 assignment_version=1 files=2 source_unchanged=true visual_validation=pending");
 }catch(const std::exception& e){REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=exception preserving=true error={}",e.what());}
}
