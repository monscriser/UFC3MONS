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
REXCVAR_DEFINE_BOOL(ufc_language_diagnostics,false,"Tests","Trace bounded YTX guest opens to identify the active language alias");
extern "C" void __real___imp__NtCreateFile(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__NtCreateFile(PPCContext& ctx,uint8_t* base){
 if(REXCVAR_GET(ufc_language_diagnostics)&&ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  const uint32_t name_addr=attr->name_ptr;
  if(name_addr){const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+name_addr);
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length>0&&length<4096){std::string path(reinterpret_cast<char*>(base+data),length);std::string lower=path;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(lower.find(".ytx")!=std::string::npos)REXLOG_INFO("[ufc-language-open] root={:08X} path={}",uint32_t(attr->root_directory),path);
    // L2 diagnostic: files opened outside the game disc (e.g. DLC content roots).
    else if(lower.find(':')!=std::string::npos&&!lower.starts_with("game:")&&!lower.starts_with("d:")){static int logged=0;if(logged++<300)REXLOG_INFO("[ufc-content-open] path={}",path);}
   }
  }
 }
 __real___imp__NtCreateFile(ctx,base);
}
