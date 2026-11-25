#pragma once
#include "ufc_shirt_alias_data.h"
#include "ufc_shirt_org.h"
#include "ufc_shirt_org_data.h"
#include <fstream>
#include <iterator>
#include "ufc_library.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(ufc_reborn_shirt_aliases,false,"UFC","Experimental frozen varied shirt assignments for all 342 Reborn fighter models");
REXCVAR_DEFINE_BOOL(ufc_shirt_by_organization,true,"UFC","Reborn: UFC fights use UFC/neutral shirts and PRIDE fights PRIDE/neutral shirts (needs ufc_reborn_shirt_aliases)");
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
  // 1.9: shirts by organization (ufc_shirt_org.h). Built from the v3 archive just mounted; on any problem the v3
  // archive stays mounted (previous behaviour).
  if(REXCVAR_GET(ufc_shirt_by_organization)){
   if(ufc_shirt_org::SourceSize!=fs::file_size(target[0])||std::string(after[0])!=ufc_shirt_org::SourceHash){REXLOG_WARN("[ufc-shirt-org] enabled=true applied=false reason=unexpected_v3_source");return;}
   const auto dir=ufc::Storage(game)/"compatibility/shirt-org-v1";
   const fs::path ufc_path=dir/(std::string(ufc_shirt_org::UfcHash)+".mpc"),pride_path=dir/(std::string(ufc_shirt_org::PrideHash)+".mpc");
   std::string v3;
   auto build=[&](const fs::path& path,const char* hash,const auto& patches)->bool{
    if(fs::exists(path))return fs::is_regular_file(path)&&ufc::Hash(path)==hash;
    if(v3.empty()){std::ifstream input(target[0],std::ios::binary);v3.assign((std::istreambuf_iterator<char>(input)),{});}
    if(v3.size()!=ufc_shirt_org::SourceSize)return false;
    std::string bytes=v3;
    for(const auto& p:patches){if(p.pos+8>bytes.size())return false;const uint32_t v[2]={p.first,p.count};for(int k=0;k<2;++k)for(int b=0;b<4;++b)bytes[p.pos+4*k+b]=char((v[k]>>(24-8*b))&0xFF);}
    ufc::AtomicText(path,bytes);return ufc::Hash(path)==hash;
   };
   if(!build(ufc_path,ufc_shirt_org::UfcHash,ufc_shirt_org::UfcPatches)||!build(pride_path,ufc_shirt_org::PrideHash,ufc_shirt_org::PridePatches)){REXLOG_ERROR("[ufc-shirt-org] enabled=true applied=false reason=variant_signature_mismatch preserving=true");return;}
   if(!entries[0]->OverrideReadOnlyFile(ufc_path)){entries[0]->OverrideReadOnlyFile(target[0]);REXLOG_ERROR("[ufc-shirt-org] enabled=true applied=false reason=override_failed");return;}
   ufc_shirt_org::Register(entries[0],ufc_path,pride_path);
   REXLOG_INFO("[ufc-shirt-org] enabled=true applied=true ufc_patches={} pride_patches={} initial=UFC",std::size(ufc_shirt_org::UfcPatches),std::size(ufc_shirt_org::PridePatches));
  }
 }catch(const std::exception& e){REXLOG_ERROR("[ufc-shirt-aliases] enabled=true applied=false reason=exception preserving=true error={}",e.what());}
}
