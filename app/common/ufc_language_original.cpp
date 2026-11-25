#include "ufc3_init.h"
#include "ufc_localization.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
extern "C" void __real___imp__XGetLanguage(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__XGetLanguage(PPCContext& ctx,uint8_t* base){
 __real___imp__XGetLanguage(ctx,base);
 // Reborn's executable also branches on the console language when selecting
 // roster records. Keep that branch English; localize its text files below.
 ctx.r3.u64=uint32_t(std::stoi(rex::cvar::GetFlagByName("user_language")));
}

void MountUfcLocalizedNames(rex::Runtime* runtime,const std::filesystem::path& game){int language=0;for(int i=0;i<6;++i)if(rex::cvar::GetFlagByName("user_language")==std::to_string(ufc::LanguageIds[i]))language=i;if(!language)return;
 auto file=ufc::PrepareLocalizedNames(game,language);
 unsigned mounted=0;
 auto english=game/"DAT/ytx/EN";auto translated=game/"DAT/ytx"/ufc::LanguageCodes[language];
 for(const auto& source:std::filesystem::directory_iterator(english)){
  if(!source.is_regular_file()||source.path().extension()!=".ytx")continue;
  auto target=source.path().filename()=="generic.ytx"?file:translated/source.path().filename();
  if(!std::filesystem::is_regular_file(target))throw std::runtime_error("Tabela de idioma ausente: "+ufc::U8(target));
  for(const auto* alias:{"EN",ufc::LanguageCodes[language]}){
  auto path=std::string("game:\\DAT\\ytx\\")+alias+"\\"+ufc::U8(source.path().filename());
  auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath(path));
  if(!entry||!entry->OverrideReadOnlyFile(target))throw std::runtime_error("Nao foi possivel carregar os textos traduzidos.");
  ++mounted;
  }
 }
 REXLOG_INFO("[ufc-language] console=EN translated_tables={}",mounted);
 REXLOG_INFO("[ufc-language] selected={} roster=EN localized_generic_sha256={}",ufc::LanguageCodes[language],ufc::Hash(file));
}

#include <rex/system/xio.h>
#include <rex/system/kernel_state.h>
#include <mutex>
// Mount the corrected Portuguese labels before the game opens its first language table.
static void MountUfcOriginalPortugueseLabels(){
 if(rex::cvar::GetFlagByName("user_language")!="9")return;
 const auto game=std::filesystem::u8path(rex::cvar::GetFlagByName("game_data_root"));
 auto* filesystem=REX_KERNEL_STATE()->emulator()->file_system();
 const auto mount = [&](const char* name) {
  try{
   const auto guest_path=std::string("game:\\DAT\\ytx\\PT\\")+name;
   auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(filesystem->ResolvePath(guest_path));
   if(!entry){
    REXLOG_WARN("[ufc-language] original PT table not found path={}",guest_path);
    return;
   }
   // Resolve the source through the guest filesystem, including on case-sensitive disks.
   const auto file=ufc::PreparePortugueseLabels(game,name,entry->host_path());
   if(entry->OverrideReadOnlyFile(file))REXLOG_INFO("[ufc-language] original PT labels mounted table={} sha256={}",name,ufc::Hash(file));
   else REXLOG_WARN("[ufc-language] original PT labels not mounted table={}",name);
  }catch(const std::exception& e){REXLOG_WARN("[ufc-language] original PT labels skipped table={}: {}",name,e.what());}
 };
 for(const char* name:{"generic.ytx","career.ytx"})mount(name);
 for(const char* name:ufc::ytx::PortugueseSupplementalAwardTables)mount(name);
}
REXCVAR_DEFINE_BOOL(ufc_language_diagnostics,false,"Tests","Trace bounded YTX guest opens to identify the active language alias");
REXCVAR_DECLARE(bool, ufc_cutscene_60_log);
extern "C" void __real___imp__NtCreateFile(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__NtCreateFile(PPCContext& ctx,uint8_t* base){
 static std::once_flag portuguese_labels;
 if(ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  if(attr->name_ptr){const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+attr->name_ptr);
   if(name->pointer&&name->length>4&&name->length<4096){std::string_view path(reinterpret_cast<const char*>(base+name->pointer),name->length);
    if(path.size()>4&&(path.ends_with(".ytx")||path.ends_with(".YTX")))std::call_once(portuguese_labels,MountUfcOriginalPortugueseLabels);}}
 }
 const bool language_trace=REXCVAR_GET(ufc_language_diagnostics);
 const bool presentation_trace=REXCVAR_GET(ufc_cutscene_60_log);
 if((language_trace||presentation_trace)&&ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  const uint32_t name_addr=attr->name_ptr;
  if(name_addr){const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+name_addr);
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length>0&&length<4096){std::string path(reinterpret_cast<char*>(base+data),length);std::string lower=path;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(presentation_trace&&(lower.ends_with(".bik")||lower.ends_with(".wmv")))
     REXLOG_INFO("[ufc-presentation] video path={}",path);
    if(language_trace&&(lower.ends_with(".ytx")||lower.find("\\fonts\\")!=std::string::npos||lower.ends_with("\\fontinfo.mtt"))){
     REXLOG_INFO("[ufc-language-open] root={:08X} path={}",uint32_t(attr->root_directory),path);
     if(lower.starts_with("game:\\") &&
        (lower.ends_with(".xpr") || lower.ends_with("\\fontinfo.mtt") || lower.find("\\ytx\\pt\\")!=std::string::npos)){
      auto* filesystem=REX_KERNEL_STATE()->emulator()->file_system();
      const auto* entry=dynamic_cast<const rex::filesystem::HostPathEntry*>(filesystem->ResolvePath(path));
      if(entry){
       const auto& host=entry->host_path();
       try{
        REXLOG_INFO("[ufc-language-asset] path={} file={} bytes={} sha256={}",
                    path,ufc::U8(host.filename()),std::filesystem::file_size(host),ufc::Hash(host));
       }catch(const std::exception& e){
        REXLOG_WARN("[ufc-language-asset] path={} skipped: {}",path,e.what());
       }
      }
     }
    }
    // L2 diagnostic: files opened outside the game disc (e.g. DLC content roots).
    else if(language_trace&&lower.find(':')!=std::string::npos&&!lower.starts_with("game:")&&!lower.starts_with("d:")){static int logged=0;if(logged++<300)REXLOG_INFO("[ufc-content-open] path={}",path);}
   }
  }
 }
 __real___imp__NtCreateFile(ctx,base);
}
