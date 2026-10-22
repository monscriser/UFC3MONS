#pragma once
// L2: optional DLC installation, separate from the game (like Hydro Thunder).
// Each Xbox 360 content package (STFS "LIVE"/"PIRS"/"CON ") of UFC Undisputed 3
// is validated (title 5451087D, marketplace content) and extracted to
//   <content root>/0000000000000000/5451087D/00000002/<package file name>/
// with its header at
//   <content root>/0000000000000000/5451087D/Headers/00000002/<name>.header
// which is the layout the SDK ContentManager lists and mounts. The content
// root is the saves folder of the selected source (Reborn or Original). The
// selected packages are only read. Removing a DLC sends it to the Recycle Bin.
#include "ufc_library.h"
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/content_device.h>
#include <atomic>
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
struct Progress{std::atomic<uint64_t> done{0},total{0};std::atomic<int> package{0},packages{0};};
struct Report{int installed=0,already=0;std::vector<std::string> names;};
namespace detail {
using rex::X_STATUS;
inline bool WriteEntry(rex::filesystem::Entry* entry,const fs::path& dest,Progress& progress){
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
inline bool ExtractTree(rex::filesystem::Entry* entry,const fs::path& dest,Progress& progress){
 for(const auto& child:entry->children()){
  const fs::path leaf=Path(child->name());
  if(leaf.empty()||leaf.is_absolute()||leaf.has_parent_path()||leaf=="."||leaf=="..")return false;
  auto target=dest/leaf;
  if(child->attributes()&rex::filesystem::kFileAttributeDirectory){fs::create_directories(target);if(!ExtractTree(child.get(),target,progress))return false;}
  else if(!WriteEntry(child.get(),target,progress))return false;
 }
 return true;
}
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
  rex::filesystem::StfsContainerDevice device("\\UFCDLC",p);
  if(!device.Initialize())throw std::runtime_error(std::string(T("Nao foi possivel abrir o pacote: ","Could not open the package: "))+name);
  auto* root=device.ResolvePath("");if(!root)throw std::runtime_error(std::string(T("Pacote vazio: ","Empty package: "))+name);
  progress.total+=detail::TreeSize(root);
  auto staging=dir/(".instalando-"+name+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if(!fs::create_directory(staging))throw std::runtime_error(T("Nao foi possivel criar a pasta temporaria da DLC.","Could not create the DLC staging folder."));
  if(!detail::ExtractTree(root,staging,progress))throw std::runtime_error(std::string(T("A extracao parou no meio (espaco em disco?). A pasta parcial foi mantida: ","Extraction stopped part-way (disk space?). The partial folder was kept: "))+U8(staging));
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
