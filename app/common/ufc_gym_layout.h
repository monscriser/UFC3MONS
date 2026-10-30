#pragma once
#include "ufc_gym_patch.h"
#include "ufc_library.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(ufc_reborn_gym_layout,true,"UFC","Restore the Reborn UFC arena/referee carousel layout; preserves mod texture payloads");
REXCVAR_DEFINE_BOOL(ufc_reborn_gym_signature_positive,false,"Tests","Alter the expected patch signature only; must refuse every write and preserve the source");

inline void MountUfcGymLayout(rex::Runtime* runtime,const std::filesystem::path& game){
 if(!REXCVAR_GET(ufc_reborn_gym_layout)){REXLOG_INFO("[ufc-gym-layout] enabled=false applied=false reason=disabled");return;}
 try{
  const auto source=game/"DAT/menu/mad/EXH_stgref.pac";
  if(ufc::Hash(source)!=ufc_gym_patch::SourceSha){REXLOG_WARN("[ufc-gym-layout] enabled=true applied=false reason=unrecognized_source preserving=true");return;}
  std::ifstream in(source,std::ios::binary);std::string content((std::istreambuf_iterator<char>(in)),{});
  if(!ufc_gym_patch::Apply(content,REXCVAR_GET(ufc_reborn_gym_signature_positive))){REXLOG_ERROR("[ufc-gym-layout] enabled=true applied=false reason=patch_signature_mismatch positive={} preserving=true",REXCVAR_GET(ufc_reborn_gym_signature_positive));return;}
  const auto target=ufc::Storage(game)/"compatibility/gym-layout-v3"/(std::string(ufc_gym_patch::TargetSha)+".pac");
  if(std::filesystem::exists(target)){
   if(!std::filesystem::is_regular_file(target)||ufc::Hash(target)!=ufc_gym_patch::TargetSha){REXLOG_ERROR("[ufc-gym-layout] enabled=true applied=false reason=cache_signature_mismatch preserving=true");return;}
  }else{
   ufc::AtomicText(target,content);
   if(ufc::Hash(target)!=ufc_gym_patch::TargetSha){REXLOG_ERROR("[ufc-gym-layout] enabled=true applied=false reason=generated_signature_mismatch preserving=true");return;}
  }
  auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath("game:\\DAT\\menu\\mad\\EXH_stgref.pac"));
  if(!entry||!entry->OverrideReadOnlyFile(target)){REXLOG_ERROR("[ufc-gym-layout] enabled=true applied=false reason=override_failed preserving=true");return;}
  REXLOG_INFO("[ufc-gym-layout] enabled=true applied=true asset=EXH_stgref.pac source_sha256={} output_sha256={} mod_stageimage_unchanged=true external_thumbnails_unchanged=true stock_widget_bundle=true cache={}",ufc_gym_patch::SourceSha,ufc_gym_patch::TargetSha,ufc::U8(target));
 }catch(const std::exception& e){REXLOG_ERROR("[ufc-gym-layout] enabled=true applied=false reason=exception preserving=true error={}",e.what());}
}
