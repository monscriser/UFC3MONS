#include "ufc_shirt_aliases.h"
#include "ufc_hud_trace_p6.h"
#include "ufc_button_art_p6.h"
#include "ufc_gym_layout.h"
#include "ufc3_init.h"
#include "ufc_localization.h"
#include "ufc_reborn_footer.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
#include <algorithm>
#include <cctype>
#include <string>
extern "C" void __real___imp__XGetLanguage(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__XGetLanguage(PPCContext& ctx,uint8_t* base){
 __real___imp__XGetLanguage(ctx,base);
 // Reborn's executable also branches on the console language when selecting
 // roster records. Keep that branch English; localize its text files below.
 ctx.r3.u64=1;
}

void MountUfcLocalizedNames(rex::Runtime* runtime,const std::filesystem::path& game){MountUfcShirtAliases(runtime,game);MountUfcGymLayout(runtime,game);MountUfcXboxButtonArt(runtime,game);int language=0;for(int i=0;i<6;++i)if(rex::cvar::GetFlagByName("user_language")==std::to_string(ufc::LanguageIds[i]))language=i;
 LocalizeUfcRebornFooter(runtime,language);if(!language)return;
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
// Career magazine cover (DAT\menu\mad\CAR_magazine.pac). Reborn runs with resolve readback
// off (its CAF save path needs it), so a cover the game composes on the GPU and then reads
// on the CPU came out as stale bytes (random DXT blocks over the photo and the logo). While
// the magazine screen is open, resolves are read back (exact on-demand readback: only pages
// the CPU really touches are copied); the previous mode returns at the next menu package
// opened 15 s or more later, or after two minutes. The Original already runs with readback on, so nothing changes there.
REXCVAR_DEFINE_BOOL(ufc_magazine_readback,true,"UFC","Read back GPU results while the career magazine cover screen is open");
REXCVAR_DEFINE_STRING(ufc_magazine_readback_match,"car_magazine","Tests","Menu package name (lower case) that opens the readback window");
#include <chrono>
#include <mutex>
namespace {
std::mutex magazine_mutex;
bool magazine_active=false;
std::string magazine_previous_mode;
std::chrono::steady_clock::time_point magazine_until{};
std::chrono::steady_clock::time_point magazine_min{};
void MagazineReadbackOnOpen(const std::string& lower){
 if(!REXCVAR_GET(ufc_magazine_readback))return;
 const bool package=lower.find("\\menu\\mad\\")!=std::string::npos&&lower.size()>4&&lower.compare(lower.size()-4,4,".pac")==0;
 const std::string match=REXCVAR_GET(ufc_magazine_readback_match);
 const bool magazine=package&&!match.empty()&&lower.find(match)!=std::string::npos;
 std::lock_guard<std::mutex> lock(magazine_mutex);
 const auto now=std::chrono::steady_clock::now();
 if(magazine){
  if(!magazine_active){
   magazine_previous_mode=rex::cvar::GetFlagByName("readback_resolve");
   magazine_active=true;
   if(magazine_previous_mode!="full")rex::cvar::SetFlagByName("readback_resolve","full");
   REXLOG_INFO("[ufc-magazine] cover screen: readback_resolve {} -> {}",magazine_previous_mode,rex::cvar::GetFlagByName("readback_resolve"));
  }
  magazine_until=now+std::chrono::minutes(2);
  magazine_min=now+std::chrono::seconds(15);
  return;
 }
 // The screen loads several packages at once; close only on a package opened at least 15 s
 // later (a new screen) or after the two-minute limit.
 if(magazine_active&&((package&&now>=magazine_min)||now>=magazine_until)){
  rex::cvar::SetFlagByName("readback_resolve",magazine_previous_mode.c_str());
  magazine_active=false;
  REXLOG_INFO("[ufc-magazine] cover screen closed: readback_resolve -> {}",rex::cvar::GetFlagByName("readback_resolve"));
 }
}
}  // namespace
extern "C" void __wrap___imp__NtCreateFile(PPCContext& ctx,uint8_t* base){
 std::string hud_path;uint32_t hud_root=0;const auto hud_handle_ptr=ctx.r3.u32;
 if(ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  if(const uint32_t name_addr=attr->name_ptr){
   const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+name_addr);
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length>0&&length<1024){
    std::string lower(reinterpret_cast<char*>(base+data),length);
    for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
    MagazineReadbackOnOpen(lower);
   }
  }
 }
 if(REXCVAR_GET(ufc_hud_diagnostics)&&ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  if(uint32_t(attr->name_ptr)){
   const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+uint32_t(attr->name_ptr));
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length&&length<4096){
    std::string path(reinterpret_cast<char*>(base+data),length);
    if(ufc_hud_trace::Candidate(path)){hud_path=std::move(path);hud_root=attr->root_directory;}
   }
  }
 }

 if(REXCVAR_GET(ufc_language_diagnostics)&&ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  const uint32_t name_addr=attr->name_ptr;
  if(name_addr){const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+name_addr);
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length>0&&length<4096){std::string path(reinterpret_cast<char*>(base+data),length);std::string lower=path;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(lower.find(".ytx")!=std::string::npos)REXLOG_INFO("[ufc-language-open] root={:08X} path={}",uint32_t(attr->root_directory),path);
   }
  }
 }
 __real___imp__NtCreateFile(ctx,base);
 if(!hud_path.empty()){
  uint32_t handle=0;
  if((ctx.r3.u32&0x80000000)==0&&hud_handle_ptr)handle=(uint32_t(base[hud_handle_ptr])<<24)|(uint32_t(base[hud_handle_ptr+1])<<16)|(uint32_t(base[hud_handle_ptr+2])<<8)|base[hud_handle_ptr+3];
  ufc_hud_trace::Open(hud_root,hud_path,ctx.r3.u32,handle);
 }

}
