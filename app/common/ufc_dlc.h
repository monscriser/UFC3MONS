#pragma once
// L2: optional DLC installation, separate from the game (like Hydro Thunder).
// Each Xbox 360 content package (STFS "LIVE"/"PIRS"/"CON ") of UFC Undisputed 3
// is validated (title 5451087D, marketplace content) and extracted to
//   <content root>/0000000000000000/5451087D/00000002/<package file name>/
// with its header at
//   <content root>/0000000000000000/5451087D/Headers/00000002/<name>.header
// which is the layout the SDK ContentManager lists and mounts. The content
// root is the saves folder of the selected source (Reborn or Original). The
// selected packages are only read. "Remover DLCs" deletes the installed copies permanently.
#include "ufc_library.h"
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/content_device.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <span>
#include <string>
#include <vector>
namespace ufc::dlc {
inline constexpr uint32_t kTitleId=0x5451087D;
inline fs::path ContentRoot(const fs::path& game,bool original){return Storage(game)/"user"/(original?"Original-static-v48":"Reborn-static-r9");}
inline fs::path PackageDir(const fs::path& game,bool original){return ContentRoot(game,original)/"0000000000000000"/"5451087D"/"00000002";}
inline fs::path HeaderDir(const fs::path& game,bool original){return ContentRoot(game,original)/"0000000000000000"/"5451087D"/"Headers"/"00000002";}
inline bool LooksLikePackage(const fs::path& file){
 std::ifstream in(file,std::ios::binary);char magic[4]{};in.read(magic,4);if(in.gcount()!=4)return false;
 std::string m(magic,4);return m=="LIVE"||m=="PIRS"||m=="CON ";
}
// A single package file, or every package file found inside a folder (for
// example DLCS/5451087D/00000002 copied from an Xbox 360 or Xenia).
inline std::vector<fs::path> FindPackages(const fs::path& source){
 std::vector<fs::path> found;
 if(fs::is_regular_file(source)){if(LooksLikePackage(source))found.push_back(source);return found;}
 if(!fs::is_directory(source))return found;
 std::error_code error;unsigned visited=0;
 for(fs::recursive_directory_iterator it(source,fs::directory_options::skip_permission_denied,error),end;!error&&it!=end&&visited++<20000;it.increment(error)){
  if(it.depth()>6){it.disable_recursion_pending();continue;}
  if(it->is_regular_file(error)&&LooksLikePackage(it->path()))found.push_back(it->path());
 }
 std::sort(found.begin(),found.end());return found;
}
inline std::string Utf8(const std::u16string& text){
 std::string out;for(char16_t c:text){if(!c)break;uint32_t u=c;
  if(u<0x80)out+=char(u);else if(u<0x800){out+=char(0xC0|(u>>6));out+=char(0x80|(u&63));}
  else if(u>=0xD800&&u<0xE000)out+='?';else{out+=char(0xE0|(u>>12));out+=char(0x80|((u>>6)&63));out+=char(0x80|(u&63));}}
 return out;}
struct Installed{std::string file_name,display_name;uint64_t bytes=0;};
inline std::string DisplayFromHeader(const fs::path& header){
 std::ifstream in(header,std::ios::binary);rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
 in.read(reinterpret_cast<char*>(&data),sizeof(data));if(in.gcount()!=std::streamsize(sizeof(data)))return {};
 return Utf8(data.display_name());
}
inline std::vector<Installed> List(const fs::path& game,bool original){
 std::vector<Installed> result;std::error_code error;auto dir=PackageDir(game,original);
 for(fs::directory_iterator it(dir,error),end;!error&&it!=end;it.increment(error)){
  if(!it->is_directory(error))continue;auto name=U8(it->path().filename());if(name.starts_with("."))continue;
  Installed item;item.file_name=name;item.display_name=DisplayFromHeader(HeaderDir(game,original)/(name+".header"));
  if(item.display_name.empty())item.display_name=name;
  for(fs::recursive_directory_iterator f(it->path(),error),fend;!error&&f!=fend;f.increment(error))if(f->is_regular_file(error))item.bytes+=f->file_size(error);
  result.push_back(std::move(item));
 }
 return result;
}
// The 11 UFC Undisputed 3 marketplace packages (package file name -> short name).
struct Known{const char* file_name;const char* name;};
inline constexpr Known kKnown[]={
 {"2F7CA09E4E70C0D5593148560FDCDA9221D1FB6454","Early Access Pass"},
 {"56D512E28E246F360172FDA7113977C72ABE156D54","Contender Fighter Pack"},
 {"76E7D5A6C0384BCA7FBB7EE0D20C22C5C0D6A38954","Contender Fighter Pack (2)"},
 {"281E5A013E0E7A0A462702CA4CB25F9583445B2B54","Alistair Overeem"},
 {"67802F6C6CEF11D952557F3A46E45AA6A724E69E54","Rivals Pack"},
 {"B855365B52DC462CFD7B9BF64B2694FEB37EC33454","Upsets Pack"},
 {"BD37095108F9AD47D2EC1089CCACCCA3AC2123CC54","International Pack"},
 {"C63283F75A795934E111FBB00DCFC4B404F0BE5054","Fight of the Night"},
 {"CD207533C9DAFD3AE750FB903B01598C869F244754","Submissions Pack"},
 {"993FCA844D6F2AFF9E8E41922747387FBF54F44F54","Ultimate Fights: Knockout Pack"},
 {"ABF29125AEC6A4CEAA7604C7FB0CBA165121902554","Mixed Bag"},
};
inline constexpr int kKnownCount=int(sizeof(kKnown)/sizeof(kKnown[0]));
inline bool SameName(const std::string& a,const char* b){return a.size()==std::strlen(b)&&std::equal(a.begin(),a.end(),b,[](char x,char y){return std::toupper((unsigned char)x)==std::toupper((unsigned char)y);});}
// Names of the known packages that are not installed.
inline std::vector<std::string> MissingKnown(const std::vector<Installed>& installed){
 std::vector<std::string> missing;
 for(auto& k:kKnown){bool found=false;for(auto& i:installed)if(SameName(i.file_name,k.file_name)){found=true;break;}if(!found)missing.push_back(k.name);}
 return missing;
}
// Permanently deletes every installed DLC of this source (packages, headers and
// unfinished extractions). The packages the user chose to install from are not touched.
inline int RemoveAll(const fs::path& game,bool original){
 int removed=0;std::error_code error;auto dir=PackageDir(game,original),headers=HeaderDir(game,original);
 std::vector<fs::path> targets;
 for(fs::directory_iterator it(dir,error),end;!error&&it!=end;it.increment(error))if(it->is_directory(error))targets.push_back(it->path());
 for(auto& t:targets){
  auto name=U8(t.filename());if(fs::is_symlink(t))fs::remove(t,error);else fs::remove_all(t,error);if(error)throw std::runtime_error(std::string("DLC: ")+error.message());
  if(!name.starts_with(".")){++removed;fs::remove(headers/Path(name+".header"),error);error.clear();}
 }
 return removed;
}
struct Progress{std::atomic<uint64_t> done{0},total{0};std::atomic<int> package{0},packages{0};};
struct Report{int installed=0,already=0;std::vector<std::string> names;};
namespace detail {
using rex::X_STATUS;
inline bool WriteEntry(rex::filesystem::Entry* entry,const fs::path& dest,Progress& progress,fs::path& failed){
 failed=dest;
 rex::filesystem::File* file=nullptr;
 if(entry->Open(rex::filesystem::FileAccess::kGenericRead,&file)!=X_STATUS_SUCCESS||!file)return false;
 std::ofstream out(dest,std::ios::binary|std::ios::trunc);if(!out){file->Destroy();return false;}
 std::vector<uint8_t> chunk(4*1024*1024);size_t offset=0;const size_t total=entry->size();bool ok=true;
 while(offset<total){size_t want=std::min(chunk.size(),total-offset),got=0;
  if(file->ReadSync(std::span<uint8_t>(chunk.data(),want),offset,&got)!=X_STATUS_SUCCESS||!got){ok=false;break;}
  out.write(reinterpret_cast<const char*>(chunk.data()),std::streamsize(got));if(!out){ok=false;break;}
  offset+=got;progress.done+=got;}
 out.close();file->Destroy();return ok&&bool(out);
}
inline bool ExtractTree(rex::filesystem::Entry* entry,const fs::path& dest,Progress& progress,fs::path& failed){
 for(const auto& child:entry->children()){
  const fs::path leaf=Path(child->name());
  if(leaf.empty()||leaf.is_absolute()||leaf.has_parent_path()||leaf=="."||leaf==".."){failed=dest/leaf;return false;}
  auto target=dest/leaf;
  if(child->attributes()&rex::filesystem::kFileAttributeDirectory){std::error_code e;fs::create_directories(target,e);if(e){failed=target;return false;}if(!ExtractTree(child.get(),target,progress,failed))return false;}
  else if(!WriteEntry(child.get(),target,progress,failed))return false;
 }
 return true;
}
// Longest path (in the platform's path units) any entry of the package will have once installed under `dest`.
inline size_t LongestPath(rex::filesystem::Entry* entry,const fs::path& dest){
 // Folders must stay 12 characters shorter than files (Windows creates directories only below 248).
 size_t longest=dest.native().size()+12;
 for(const auto& child:entry->children()){
  auto target=dest/Path(child->name());
  if(child->attributes()&rex::filesystem::kFileAttributeDirectory)longest=std::max(longest,LongestPath(child.get(),target));
  else longest=std::max(longest,target.native().size());
 }
 return longest;
}
// Our own unfinished extraction folders (from this or older launchers).
inline bool IsStaging(const std::string& name){return name.starts_with(".inst-")||name.starts_with(".instalando-");}
inline uint64_t TreeSize(rex::filesystem::Entry* entry){uint64_t total=0;for(const auto& child:entry->children())total+=(child->attributes()&rex::filesystem::kFileAttributeDirectory)?TreeSize(child.get()):child->size();return total;}
}
// Installs every UFC 3 package found in `source`. Packages already installed are kept as they are.
inline Report Install(const fs::path& game,bool original,const fs::path& source,Progress& progress){
 if(game.empty()||!fs::is_directory(game))throw std::runtime_error(T("Escolha o jogo antes de instalar DLC.","Choose the game before installing DLC."));
 auto packages=FindPackages(source);
 if(packages.empty())throw std::runtime_error(T("Nenhum pacote de conteudo Xbox 360 foi encontrado nesse local.","No Xbox 360 content package was found there."));
 std::vector<fs::path> valid;
 for(auto& p:packages){
  auto header=rex::filesystem::StfsContainerDevice::ReadPackageHeader(p);
  if(!header)continue;
  if(uint32_t(header->metadata.execution_info.title_id)!=kTitleId)continue;
  if(header->metadata.content_type!=rex::system::XContentType::kMarketplaceContent)continue;
  valid.push_back(p);
 }
 if(valid.empty())throw std::runtime_error(T("Os pacotes encontrados nao sao DLC do UFC Undisputed 3.","The packages found are not UFC Undisputed 3 DLC."));
 Report report;progress.packages=int(valid.size());progress.package=0;progress.done=0;progress.total=0;
 auto dir=PackageDir(game,original),headers=HeaderDir(game,original);fs::create_directories(dir);fs::create_directories(headers);
 for(auto& p:valid){
  ++progress.package;auto name=U8(p.filename());
  if(fs::exists(dir/p.filename())){++report.already;continue;}
  {std::error_code e;std::vector<fs::path> stale;
   for(fs::directory_iterator it(dir,e),end;!e&&it!=end;it.increment(e))if(it->is_directory(e)&&detail::IsStaging(U8(it->path().filename())))stale.push_back(it->path());
   for(auto& old:stale){std::error_code r;fs::remove_all(old,r);}}
  rex::filesystem::StfsContainerDevice device("\\UFCDLC",p);
  if(!device.Initialize())throw std::runtime_error(std::string(T("Nao foi possivel abrir o pacote: ","Could not open the package: "))+name);
  auto* root=device.ResolvePath("");if(!root)throw std::runtime_error(std::string(T("Pacote vazio: ","Empty package: "))+name);
  progress.total+=detail::TreeSize(root);
#ifdef _WIN32
  // Windows refuses paths of 260 characters or more for this program: check the final installed paths first.
  if(const size_t longest=detail::LongestPath(root,dir/p.filename());longest>=260)
   throw std::runtime_error(std::string(T("O caminho da pasta do jogo e longo demais para o Windows (a DLC precisaria de ","The game folder path is too long for Windows (the DLC would need "))+std::to_string(longest)+T(" caracteres; o limite e 259). Mova a pasta do jogo para um lugar mais curto, por exemplo C:\\Jogos\\UFC3, escolha a pasta nova e instale de novo."," characters; the limit is 259). Move the game folder somewhere shorter, for example C:\\Games\\UFC3, select the new folder and install again."));
#endif
  // Short name on purpose: the staging folder sits next to the final one and must not lengthen the paths.
  auto staging=dir/(".inst-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()%10000000000LL));
  if(!fs::create_directory(staging))throw std::runtime_error(T("Nao foi possivel criar a pasta temporaria da DLC.","Could not create the DLC staging folder."));
  fs::path failed;
  if(!detail::ExtractTree(root,staging,progress,failed)){
   std::error_code e;fs::remove_all(staging,e);
   throw std::runtime_error(std::string(T("Nao foi possivel gravar um arquivo da DLC ","Could not write a DLC file "))+name+": "+U8(failed.filename())+
    T(" (pasta sem permissao de gravacao, disco cheio ou caminho longo demais). Nada foi instalado deste pacote."," (folder not writable, disk full or path too long). Nothing from this package was installed."));
  }
  // Header in the same format as ContentManager::WriteContentHeaderFile.
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id=static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  data.content_type=rex::system::XContentType::kMarketplaceContent;
  data.title_id=kTitleId;data.xuid=0;data.set_file_name(name);
  auto display=device.header().metadata.display_name(rex::system::XLanguage::kEnglish);
  if(display.empty())data.set_display_name(std::u16string(name.begin(),name.end()));else data.set_display_name(display);
  uint32_t license_mask=0;for(size_t i=0;i<0x10;++i)if(device.header().header.licenses[i].license_flags)license_mask|=device.header().header.licenses[i].license_bits;
  std::string header_bytes(reinterpret_cast<const char*>(&data),sizeof(data));
  if(license_mask)header_bytes.append(reinterpret_cast<const char*>(&license_mask),sizeof(license_mask));
  AtomicText(headers/(name+".header"),header_bytes);
  fs::rename(staging,dir/p.filename());
  ++report.installed;std::string shown=Utf8(display);report.names.push_back(shown.empty()?name:shown);
 }
 return report;
}
}
