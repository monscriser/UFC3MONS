#pragma once
#include "ufc_library.h"
#include <cstdint>
#include <cstring>
#include <unordered_set>
namespace ufc {
// Optional "Pre-compile shaders" (launcher button). The package ships only the LIST of shaders and
// pipeline descriptions the game uses (data/shaders/<platform>/, built by tools/shader_seed.py from
// real play sessions). The list is merged into the player's own shareable storage and the game then
// compiles it with the player's driver for the player's GPU (the SDK drops what the GPU cannot do).
// Nothing compiled for another machine is shipped. Without the button nothing changes.
constexpr const char* kShaderTitle="5451087D";
#ifdef _WIN32
constexpr const char* kShaderPlatform="windows";
constexpr size_t kPipelineRecord=68;  // sizeof(PipelineStoredDescription), MS bitfield layout
#else
constexpr const char* kShaderPlatform="linux";
constexpr size_t kPipelineRecord=66;  // same struct, Itanium bitfield layout (no 2-byte gap before render_targets)
#endif
inline fs::path ShaderStorageDir(const fs::path& game){return Storage(game)/"cache"/(std::string(kShaderPlatform)+"-vulkan")/"shaders"/"shareable";}
inline fs::path ShaderSeedDir(const fs::path& bundle){return bundle/"shaders"/kShaderPlatform;}
inline fs::path PrecompileLog(const fs::path& game){return Storage(game)/"cache"/"precompile.log";}
struct SeedMerge{size_t shaders=0,pipelines=0;bool seed_found=false,format_mismatch=false;};
namespace seed_detail {
inline std::string Bytes(const fs::path& p){std::error_code e;return fs::is_regular_file(p,e)?ReadText(p):std::string();}
inline uint64_t U64(const std::string& s,size_t o){uint64_t v;std::memcpy(&v,s.data()+o,8);return v;}
inline uint32_t U32(const std::string& s,size_t o){uint32_t v;std::memcpy(&v,s.data()+o,4);return v;}
// .xsh: 8-byte header, then {u64 ucode hash, u32 dword count (31 bits) | type}, ucode dwords.
inline std::vector<std::pair<uint64_t,std::string>> Shaders(const std::string& d){
 std::vector<std::pair<uint64_t,std::string>> out;size_t o=8;
 while(o+12<=d.size()){size_t n=size_t(U32(d,o+8)&0x7FFFFFFFu)*4;if(o+12+n>d.size())break;out.emplace_back(U64(d,o),d.substr(o,12+n));o+=12+n;}
 return out;
}
// Appends the seed records the player's file lacks; the player's own records stay first and untouched.
// Returns the number of added records, or SIZE_MAX when the two files are not the same format.
inline size_t Merge(const fs::path& mine,const fs::path& seed,size_t header,size_t record){
 const std::string s=Bytes(seed);if(s.size()<header)return 0;
 std::string m=Bytes(mine);
 if(m.size()<header)m=s.substr(0,header);
 else if(m.compare(0,header,s,0,header)!=0)return SIZE_MAX;
 std::unordered_set<uint64_t> have;std::string add;size_t added=0;
 if(record){
  if((s.size()-header)%record)return SIZE_MAX;
  m.resize(header+(m.size()-header)/record*record);  // the game also drops a torn last record
  for(size_t o=header;o<m.size();o+=record)have.insert(U64(m,o));
  for(size_t o=header;o<s.size();o+=record)if(have.insert(U64(s,o)).second){add.append(s,o,record);++added;}
 }else{
  auto mine_records=Shaders(m);size_t valid=8;for(auto& r:mine_records){have.insert(r.first);valid+=r.second.size();}m.resize(valid);
  for(auto& r:Shaders(s))if(have.insert(r.first).second){add+=r.second;++added;}
 }
 if(added){fs::create_directories(mine.parent_path());AtomicText(mine,m+add);}
 return added;
}
}
inline SeedMerge MergeShaderSeed(const fs::path& bundle,const fs::path& game){
 SeedMerge result;const auto seed=ShaderSeedDir(bundle),mine=ShaderStorageDir(game);
 const auto xsh=std::string(kShaderTitle)+".xsh",xpso=std::string(kShaderTitle)+".fbo.vk.xpso";
 std::error_code e;if(!fs::is_regular_file(seed/xsh,e)||!fs::is_regular_file(seed/xpso,e))return result;
 result.seed_found=true;
 // Check both formats first so a mismatch never leaves one file merged and the other not.
 const auto sp=seed_detail::Bytes(seed/xpso),mp=seed_detail::Bytes(mine/xpso),sx=seed_detail::Bytes(seed/xsh),mx=seed_detail::Bytes(mine/xsh);
 if((mp.size()>=12&&(sp.size()<12||mp.compare(0,12,sp,0,12)!=0))||(mx.size()>=8&&(sx.size()<8||mx.compare(0,8,sx,0,8)!=0))){result.format_mismatch=true;return result;}
 const size_t shaders=seed_detail::Merge(mine/xsh,seed/xsh,8,0);
 const size_t pipelines=seed_detail::Merge(mine/xpso,seed/xpso,12,kPipelineRecord);
 if(shaders==SIZE_MAX||pipelines==SIZE_MAX){result.format_mismatch=true;return result;}
 result.shaders=shaders;result.pipelines=pipelines;return result;
}
// Reads the game's own report from the pre-compile run: "Created N graphics pipelines from Vulkan storage (M requested)".
inline bool PrecompileReport(const fs::path& game,int& created,int& requested){
 const auto log=seed_detail::Bytes(PrecompileLog(game));created=requested=-1;
 if(log.find("[ufc-precompile] done")==std::string::npos)return false;
 const auto at=log.rfind("Created ");
 if(at!=std::string::npos)std::sscanf(log.c_str()+at,"Created %d graphics pipelines from Vulkan storage (%d requested)",&created,&requested);
 if(created<0)created=requested=0;  // nothing stored yet
 return true;
}
}
