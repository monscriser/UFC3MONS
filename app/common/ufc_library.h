#pragma once
#include "session_recorder.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
#include <array>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>
#include "ufc_text_encoding.h"
#ifdef _WIN32
#include <bcrypt.h>
#endif
namespace ufc {
namespace fs=std::filesystem;
inline constexpr const char* Version="UFC3MONS";
inline constexpr const char* OriginalSHA="39fb34cb7c42526469e85e2a1068a34eeddcb82abcd85e0937fc35b8b363d8e8";
inline constexpr const char* GuestSHA="1af0740ac854705aac209405129af6917efb03276330376b93350cfb220782e1";
inline std::string U8(const fs::path& path){auto s=path.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
inline fs::path Path(const std::string& s){return fs::u8path(s);}
inline std::string Trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
inline std::string ReadText(const fs::path& p){std::ifstream in(p,std::ios::binary);std::string s((std::istreambuf_iterator<char>(in)),{});
 if(s.size()>=2&&uint8_t(s[0])==255&&uint8_t(s[1])==254)return DecodeUtf16LE(s);
 if(s.starts_with("\xef\xbb\xbf"))s.erase(0,3);return s;}
inline std::map<std::string,std::string> ReadIni(const fs::path& p){std::map<std::string,std::string> values;std::istringstream text(ReadText(p));std::string line;while(std::getline(text,line)){auto pos=line.find('=');if(pos!=std::string::npos)values[Trim(line.substr(0,pos))]=Trim(line.substr(pos+1));}return values;}
inline fs::path RegistryDirectory(){if(auto* p=std::getenv("UFC3_SETTINGS_ROOT"))return Path(p);
#ifdef _WIN32
 if(auto* p=std::getenv("LOCALAPPDATA"))return Path(p)/"UFC3-Reborn";
#else
 if(auto* p=std::getenv("XDG_CONFIG_HOME"))return Path(p)/"ufc3-reborn";
 if(auto* p=std::getenv("HOME"))return Path(p)/".config/ufc3-reborn";
#endif
 throw std::runtime_error("Pasta de preferencias do usuario indisponivel.");}
inline fs::path Storage(const fs::path& game){return game/".ufc-reborn";}
inline bool HasGame(const fs::path& game){return !game.empty()&&fs::is_regular_file(game/"default.xex");}
// L2: an ISO is played in place (mounted by the game, like Xenia). Only a small
// folder beside it is created for saves, cache and settings.
inline constexpr const char* IsoMarker=".ufc3mons-iso";
inline fs::path IsoShell(const fs::path& iso){return iso.parent_path()/(iso.stem().wstring()+L"-UFC3MONS");}
inline fs::path IsoOf(const fs::path& shell){if(shell.empty())return {};auto text=Trim(ReadText(shell/IsoMarker));return text.empty()?fs::path{}:Path(text);}
inline std::string Hash(const fs::path& p){
#ifdef _WIN32
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return {};}
 std::ifstream in(p,std::ios::binary);std::array<char,65536> buffer;bool good=in.is_open();while(in&&good){in.read(buffer.data(),buffer.size());auto n=in.gcount();if(n&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),ULONG(n),0)<0)good=false;}
 good=good&&in.eof();std::array<unsigned char,32> value{};if(BCryptFinishHash(hash,value.data(),value.size(),0)<0)good=false;BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);if(!good)return {};
 std::string result;for(auto v:value){result+="0123456789abcdef"[v>>4];result+="0123456789abcdef"[v&15];}return result;
#else
 auto result=ufc_record::Command({"sha256sum",U8(p)});return result.size()>=64?result.substr(0,64):std::string{};
#endif
}
inline void AtomicText(const fs::path& target,const std::string& value){fs::create_directories(target.parent_path());auto temp=target;temp+=".new";{std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<value;out.close();if(!out)throw std::runtime_error("Nao foi possivel salvar as preferencias.");}
#ifdef _WIN32
 if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Nao foi possivel concluir o arquivo de preferencias.");
#else
 fs::rename(temp,target);
#endif
}
struct Installation{fs::path game,legacy_data,iso;};
inline bool Ready(const Installation& i){return i.iso.empty()?HasGame(i.game):(fs::is_regular_file(i.iso)&&fs::is_directory(i.game));}
inline Installation Discover(const fs::path& binary){
 auto remembered=Trim(ReadText(RegistryDirectory()/"installation.txt"));if(HasGame(Path(remembered)))return {Path(remembered),{}};{auto iso=IsoOf(Path(remembered));if(!remembered.empty()&&fs::is_regular_file(iso))return {Path(remembered),{},iso};}
 std::vector<fs::path> bases={binary,binary.parent_path(),binary.parent_path().parent_path()};
#ifdef _WIN32
 if(auto* p=std::getenv("USERPROFILE")){bases.push_back(Path(p)/"Downloads");bases.push_back(Path(p)/"Desktop");}
#else
 if(auto* p=std::getenv("HOME")){bases.push_back(Path(p)/"Downloads");bases.push_back(Path(p)/"Desktop");}
#endif
 auto candidates=bases;for(auto& base:bases){std::error_code error;unsigned count=0;for(fs::directory_iterator it(base,error),end;!error&&it!=end&&count++<256;it.increment(error)){if(!it->is_directory(error))continue;auto name=U8(it->path().filename());if(name.starts_with("UFC")||name.starts_with("ufc")||name.starts_with("linux-v"))candidates.push_back(it->path());}}
 for(auto& base:candidates){if(HasGame(base))return {base,{}};for(auto& data:{base/"data",base}){
  auto ini=ReadIni(data/"launcher.ini");auto game=Path(ini["game"]);if(HasGame(game))return {game,data};
  game=Path(Trim(ReadText(data/"game-path.txt")));if(HasGame(game))return {game,data};
 }}return {};
}
inline void Register(const Installation& installation){
 if(!installation.iso.empty()){
  // The ISO itself is validated when it is selected (default.xex read in place).
  if(!fs::is_regular_file(installation.iso))throw std::runtime_error("A ISO escolhida nao foi encontrada.");
  fs::create_directories(Storage(installation.game));
  AtomicText(installation.game/IsoMarker,U8(fs::weakly_canonical(installation.iso))+"\n");
  AtomicText(RegistryDirectory()/"installation.txt",U8(fs::weakly_canonical(installation.game))+"\n");
  return;
 }
 if(!HasGame(installation.game))throw std::runtime_error("Escolha a pasta que contem default.xex.");auto guest_hash=Hash(installation.game/"default.xex");if(guest_hash!=GuestSHA&&guest_hash!=OriginalSHA)throw std::runtime_error("Este default.xex nao corresponde ao UFC3 Original ou Reborn suportado.");
 auto root=Storage(installation.game);fs::create_directories(root);
 if(guest_hash==GuestSHA&&!installation.legacy_data.empty()&&fs::is_directory(installation.legacy_data/"user")&&!fs::exists(root/"user"))fs::copy(installation.legacy_data/"user",root/"user",fs::copy_options::recursive);
 if(guest_hash==GuestSHA&&!installation.legacy_data.empty()&&fs::is_regular_file(installation.legacy_data/"launcher.ini")&&!fs::exists(root/"config/launcher.ini")&&!fs::exists(root/"config/legacy-v29.ini"))AtomicText(root/"config/legacy-v29.ini",ReadText(installation.legacy_data/"launcher.ini"));
 // A small locator is the only item kept outside the game. Never contains saves.
 AtomicText(RegistryDirectory()/"installation.txt",U8(fs::weakly_canonical(installation.game))+"\n");
}
inline constexpr std::array<const char*,24> BindNames={"a","b","x","y","left_trigger","right_trigger","left_shoulder","right_shoulder","lstick_up","lstick_down","lstick_left","lstick_right","lstick_press","rstick_press","rstick_up","rstick_down","rstick_left","rstick_right","dpad_up","dpad_down","dpad_left","dpad_right","back","start"};
inline constexpr std::array<const char*,24> BindDefaults={"Semicolon,Space","Quote,Backspace","L","P","Q,I","E,O","1","3","W","S","A","D","F","K","Up","Down","Left","Right","Shift+Up","Shift+Down","Shift+Left","Shift+Right","Z,Tab","X,Return"};
// Launcher interface language (item 7). Game language is a separate setting.
inline bool& UiEnglish(){static bool value=false;return value;}
inline const char* T(const char* pt,const char* en){return UiEnglish()?en:pt;}
struct Settings{
 bool fullscreen=true,fps=true,keyboard=true,mouse=false,vsync=true,diagnostics=false,async_shaders=false,fsr1=false;int output=0,game_language=1,ui_language=0;float sensitivity=1.f;
 std::array<std::string,24> binds;
 Settings(){for(size_t i=0;i<binds.size();++i)binds[i]=BindDefaults[i];}
 void Load(const fs::path& game){*this=Settings{};auto fixed=RegistryDirectory()/"config/launcher.ini";auto ini=ReadIni(fixed);if(!fs::exists(fixed)&&!game.empty()){auto legacy=Storage(game)/"config/launcher.ini";if(!fs::is_regular_file(legacy))legacy=Storage(game)/"config/legacy-v29.ini";if(fs::is_regular_file(legacy)){auto text=ReadText(legacy);AtomicText(RegistryDirectory()/"migration/launcher-before-user-folder.ini",text);AtomicText(fixed,text);ini=ReadIni(fixed);}}auto value=[&](const char* k,const char* fallback){return ini.contains(k)?ini[k]:std::string(fallback);};
  fullscreen=value("fullscreen","1")=="1";fps=value("fps","1")=="1";keyboard=value("keyboard","1")=="1";mouse=value("mouse","0")=="1";vsync=value("vsync","1")=="1";
  diagnostics=value("diagnostics","0")=="1";async_shaders=value("async_shaders","0")=="1";fsr1=value("fsr1","0")=="1";
  try{output=std::clamp(std::stoi(value("output","0")),0,3);game_language=std::clamp(std::stoi(value("game_language","1")),0,5);ui_language=std::clamp(std::stoi(value("ui_language","0")),0,1);sensitivity=std::clamp(std::stof(value("sensitivity","1")),0.01f,20.f);}catch(...){}
  for(size_t i=0;i<binds.size();++i){auto k=std::string("keybind_")+BindNames[i];if(!ini.contains(k)&&ini.contains(BindNames[i]))ini[k]=ini[BindNames[i]];if(ini.contains(k)&&ini[k].size()<240)binds[i]=ini[k];}
 }
 std::string Text()const{std::ostringstream out;out<<"[Launcher]\nfullscreen="<<fullscreen<<"\nfps="<<fps<<"\nkeyboard="<<keyboard<<"\nmouse="<<mouse<<"\nvsync="<<vsync<<"\ndiagnostics="<<diagnostics<<"\nasync_shaders="<<async_shaders<<"\nfsr1="<<fsr1<<"\nui_language="<<ui_language<<"\noutput="<<output<<"\ngame_language="<<game_language<<"\nsensitivity="<<sensitivity<<'\n';for(size_t i=0;i<binds.size();++i)out<<"keybind_"<<BindNames[i]<<'='<<binds[i]<<'\n';return out.str();}
 void Save(const fs::path&)const{AtomicText(RegistryDirectory()/"config/launcher.ini",Text());}
};
}
