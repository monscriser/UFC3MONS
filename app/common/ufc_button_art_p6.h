#pragma once
#include "ufc_button_art_data_p6.h"
#include "ufc_library.h"
#include <rex/runtime.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
REXCVAR_DEFINE_BOOL(ufc_xbox_button_art,true,"UFC","Restore existing Reborn Xbox A/B texture in menus containing the known PlayStation artwork");
REXCVAR_DEFINE_BOOL(ufc_xbox_button_signature_positive,false,"Tests","Change only expected texture signature; refuse every overlay");
namespace ufc_button_art {
inline uint32_t U32(const std::string& b,size_t at){return(uint32_t(uint8_t(b.at(at)))<<24)|(uint32_t(uint8_t(b.at(at+1)))<<16)|(uint32_t(uint8_t(b.at(at+2)))<<8)|uint8_t(b.at(at+3));}
inline bool Apply(std::string& bytes,bool positive=false){
 if(bytes.size()<0x50)return false;
 const size_t size=U32(bytes,0x44),at=U32(bytes,0x48);
 if(at>bytes.size()||size>bytes.size()-at||size<16)return false;
 const size_t count=U32(bytes,at);if(count>(size-16)/32)return false;
 for(size_t i=0;i<count;++i){
  const size_t row=at+16+i*32;
  if(std::memcmp(bytes.data()+row,"btn36000\0",9))continue;
  const size_t length=U32(bytes,row+20),offset=U32(bytes,row+24);
  if(length!=sizeof(Before)||offset>size||length>size-offset)return false;
  const auto* data=bytes.data()+at+offset;
  if(uint8_t(data[0])!=(Before[0]^(positive?1:0))||std::memcmp(data+1,Before+1,sizeof(Before)-1))return false;
  std::memcpy(bytes.data()+at+offset,After,sizeof(After));return true;
 }
 return false;
}
}
inline void MountUfcXboxButtonArt(rex::Runtime* runtime,const std::filesystem::path& game){
 if(!REXCVAR_GET(ufc_xbox_button_art)){REXLOG_INFO("[ufc-button-art] enabled=false overlays=0");return;}
 unsigned applied=0,refused=0,already=0;
 for(const auto& known:ufc_button_art::Assets){
  const auto path=std::string("game:\\DAT\\menu\\mad\\")+known.name;
  auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath(path));
  if(!entry){++refused;continue;}
  const auto source=entry->host_path();const auto digest=ufc::Hash(source);
  const bool gym=std::string_view(known.name)=="EXH_stgref.pac"&&digest=="26d78ff0c349d85270397d37d2ad456b7382edcac0aaa01b15f6863b9c1884a1";
  if(gym){++already;continue;} // P4 already restored the original Xbox texture here.
  if(digest!=known.source&&!gym){++refused;continue;}
  std::ifstream input(source,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
  if(!ufc_button_art::Apply(bytes,REXCVAR_GET(ufc_xbox_button_signature_positive))){++refused;continue;}
  const auto target=ufc::Storage(game)/"compatibility/xbox-button-art-p6"/(digest+".pac");
  // Verify the complete generated file before reusing it. Never modify a source asset.
  if(std::filesystem::exists(target)){
   std::ifstream cached(target,std::ios::binary);std::string old((std::istreambuf_iterator<char>(cached)),{});
   if(old!=bytes){++refused;continue;}
  }else ufc::AtomicText(target,bytes);
  if(!entry->OverrideReadOnlyFile(target)){++refused;continue;}
  ++applied;
 }
 REXLOG_INFO("[ufc-button-art] enabled=true overlays={} refused={} positive={} already_correct={} input_mapping_unchanged=true",applied,refused,REXCVAR_GET(ufc_xbox_button_signature_positive),already);
}
