#pragma once
#include "ufc_library.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <vector>
namespace ufc {
// Mods tab (P14.9). A mod is a game file made by players (e.g. DAT\chara\029\029.pac = one fighter).
// The launcher keeps a COPY in <storage>/mods mirroring the game folder (mods/DAT/chara/029/029.pac);
// the game opens that copy instead of the original (HostPathEntry::OverrideReadOnlyFile, the same
// redirect used for the translated texts). The game folder itself is never changed: removing or
// switching off a mod brings the original back at the next start.
inline fs::path ModsDir(const fs::path& game){return Storage(game)/"mods";}
inline fs::path ModsDisabledFile(const fs::path& game){return ModsDir(game)/"disabled.txt";}
// Relative paths use '/' and keep the game's own spelling (DAT/chara/029/029.pac).
inline std::string ModKey(const fs::path& relative){auto key=U8(relative);std::replace(key.begin(),key.end(),'\\','/');return key;}
inline std::set<std::string> DisabledMods(const fs::path& game){
 std::set<std::string> out;std::istringstream in(ReadText(ModsDisabledFile(game)));std::string line;
 while(std::getline(in,line)){while(!line.empty()&&(line.back()=='\r'||line.back()==' '))line.pop_back();if(!line.empty())out.insert(line);}
 return out;
}
inline void SaveDisabledMods(const fs::path& game,const std::set<std::string>& disabled){
 std::string text;for(auto& key:disabled)text+=key+"\n";AtomicText(ModsDisabledFile(game),text);
}
struct ModFile{fs::path relative;bool enabled=true;uintmax_t size=0;};
inline std::vector<ModFile> ListMods(const fs::path& game){
 std::vector<ModFile> out;std::error_code e;const auto root=ModsDir(game);
 if(!fs::is_directory(root/"DAT",e))return out;
 const auto disabled=DisabledMods(game);
 for(fs::recursive_directory_iterator it(root/"DAT",e),end;!e&&it!=end;it.increment(e)){
  if(!it->is_regular_file(e)||U8(it->path().filename()).find(".new")!=std::string::npos)continue;
  auto relative=it->path().lexically_relative(root);
  out.push_back({relative,!disabled.count(ModKey(relative)),it->file_size(e)});
 }
 std::sort(out.begin(),out.end(),[](const ModFile& a,const ModFile& b){return a.relative<b.relative;});
 return out;
}
inline std::string Lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
// Which game file a mod replaces, found by its name: fighter files (NNN.pac, NNN_acce.mpc) go straight to
// DAT/chara/NNN; any other name must exist exactly once in the game's DAT folder.
inline fs::path ModTarget(const fs::path& game,const fs::path& file){
 const auto name=file.filename();const auto lower=Lower(U8(name));
 if(lower.size()>=7&&std::isdigit((unsigned char)lower[0])&&std::isdigit((unsigned char)lower[1])&&std::isdigit((unsigned char)lower[2])&&(lower.substr(3)==".pac"||lower.substr(3)=="_acce.mpc")){
  auto relative=fs::path("DAT")/"chara"/lower.substr(0,3)/name;
  if(fs::is_regular_file(game/relative))return relative;
 }
 std::vector<fs::path> found;std::error_code e;
 for(fs::recursive_directory_iterator it(game/"DAT",e),end;!e&&it!=end;it.increment(e))
  if(it->is_regular_file(e)&&Lower(U8(it->path().filename()))==lower)found.push_back(it->path().lexically_relative(game));
 if(found.size()==1)return found[0];
 if(found.empty())throw std::runtime_error(std::string(T("O jogo não tem nenhum arquivo chamado ","The game has no file named "))+U8(name)+T(". Confira se o mod é para o UFC Undisputed 3 / Reborn."," Check that the mod is for UFC Undisputed 3 / Reborn."));
 throw std::runtime_error(std::string(T("Existe mais de um arquivo chamado ","There is more than one file named "))+U8(name)+T(" no jogo; não dá para saber qual o mod substitui."," in the game; it is not clear which one the mod replaces."));
}
inline fs::path AddMod(const fs::path& game,const fs::path& file){
 std::error_code e;if(!fs::is_regular_file(file,e)||fs::file_size(file,e)==0)throw std::runtime_error(T("Arquivo de mod vazio ou não encontrado.","Mod file is empty or missing."));
 const auto relative=ModTarget(game,file);const auto target=ModsDir(game)/relative;
 fs::create_directories(target.parent_path());
 auto temp=target;temp+=".new";fs::copy_file(file,temp,fs::copy_options::overwrite_existing);
 if(fs::file_size(temp)!=fs::file_size(file)){fs::remove(temp,e);throw std::runtime_error(T("A cópia do mod ficou incompleta (disco cheio?).","The mod copy is incomplete (disk full?)."));}
 fs::rename(temp,target);
 auto disabled=DisabledMods(game);if(disabled.erase(ModKey(relative)))SaveDisabledMods(game,disabled);
 return relative;
}
inline void SetModEnabled(const fs::path& game,const fs::path& relative,bool enabled){
 auto disabled=DisabledMods(game);if(enabled)disabled.erase(ModKey(relative));else disabled.insert(ModKey(relative));SaveDisabledMods(game,disabled);
}
// Removes only the launcher's copy in <storage>/mods; the original game file was never touched.
inline void RemoveMod(const fs::path& game,const fs::path& relative){
 const auto root=ModsDir(game);auto target=(root/relative).lexically_normal();
 if(target.lexically_relative(root).empty()||*target.lexically_relative(root).begin()!="DAT")throw std::runtime_error("Mod invalido.");
 std::error_code e;fs::remove(target,e);
 for(auto dir=target.parent_path();dir!=root&&fs::is_empty(dir,e);dir=dir.parent_path())fs::remove(dir,e);
 SetModEnabled(game,relative,true);
}
}
