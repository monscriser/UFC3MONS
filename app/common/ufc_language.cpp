#include "ufc_shirt_org.h"
#include "ufc_font_accents.h"
#include "ufc_titles.h"
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
#include <rex/system/xmemory.h>
#include <algorithm>
#include <cctype>
#include <string>
REXCVAR_DEFINE_BOOL(ufc_reborn_translated_text,false,"UFC","Reborn: replace Reborn's English texts with the Original game's translation (off: Reborn's own texts, no flicker between the two in the career menus)");
void UfcModelGuardNoteOpen(const std::string& guest_path);  // ufc_model_guard.cpp
extern "C" void __real___imp__XGetLanguage(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__XGetLanguage(PPCContext& ctx,uint8_t* base){
 __real___imp__XGetLanguage(ctx,base);
 // Reborn's executable also branches on the console language when selecting
 // roster records. Keep that branch English; localize its text files below.
 ctx.r3.u64=1;
}

// BETA 1.8.1: Reborn text kept, but complete in the selected language (see ufc::PrepareRebornLanguageTable): the
// tables Reborn changed are mounted for that language only (the EN alias stays Reborn's own, no flicker), and the
// Flyweight caption the Reborn Event patch writes in English (UTF-16 at 0x83654180, 0x80 bytes reserved before the
// next patch block) is written in Portuguese.
static void MountRebornLanguageTables(rex::Runtime* runtime,const std::filesystem::path& game,int language){
 unsigned mounted=0;
 const auto mount = [&](const char* file, bool native_portuguese) {
  try{
   auto path=std::string("game:\\DAT\\ytx\\")+ufc::LanguageCodes[language]+"\\"+file;
   auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath(path));
   if(native_portuguese&&!entry){
    REXLOG_WARN("[ufc-language] reborn PT table not found path={}",path);
    return;
   }
   auto table=native_portuguese ? ufc::PreparePortugueseLabels(game,file,entry->host_path())
                                : ufc::PrepareRebornLanguageTable(game,language,file);
   if(entry&&entry->OverrideReadOnlyFile(table))++mounted;
   else REXLOG_WARN("[ufc-language] reborn {} table not mounted path={} entry={} readonly={} table={} exists={}",file,path,(void*)entry,entry?entry->is_read_only():false,ufc::U8(table),std::filesystem::is_regular_file(table));
  }catch(const std::exception& e){REXLOG_WARN("[ufc-language] reborn {} table skipped: {}",file,e.what());}
 };
 for(const char* file:{"generic.ytx","on_comm.ytx","on_exhi.ytx"})mount(file,false);
 if(language==1){
  mount("career.ytx",false);
  mount("exhibition.ytx",false);
  for(const char* file:ufc::ytx::PortugueseSupplementalAwardTables)mount(file,true);
 }
 if(language==1){
  static const char16_t caption[]=u"PESO-MOSCA";
  auto* memory=runtime->memory();auto* heap=memory->LookupHeap(0x83654180u);uint32_t previous=0;
  if(heap&&heap->Protect(0x83654180u,0x80,rex::memory::kMemoryProtectRead|rex::memory::kMemoryProtectWrite,&previous)){
   uint8_t* at=memory->virtual_membase()+0x83654180u;
   if(at[0]==0&&at[1]=='F'&&at[2]==0&&at[3]=='L'){  // the Reborn caption, as written by ApplyUfcOfflineMemory
    for(size_t i=0;i<sizeof(caption)/sizeof(caption[0]);++i){at[2*i]=uint8_t(caption[i]>>8);at[2*i+1]=uint8_t(caption[i]&255);}
    REXLOG_INFO("[ufc-language] reborn flyweight caption=PESO-MOSCA");
   }
   heap->Protect(0x83654180u,0x80,previous);
  }
 }
 REXLOG_INFO("[ufc-language] reborn text tables for {} mounted={}",ufc::LanguageCodes[language],mounted);
}

void MountUfcLocalizedNames(rex::Runtime* runtime,const std::filesystem::path& game){MountUfcShirtAliases(runtime,game);MountUfcGymLayout(runtime,game);MountUfcFontAccents(runtime,game);MountUfcXboxButtonArt(runtime,game);int language=0;for(int i=0;i<6;++i)if(rex::cvar::GetFlagByName("user_language")==std::to_string(ufc::LanguageIds[i]))language=i;
 ufc_titles::Mount(runtime,game,language);  // 1.9: flyweight card title + translated titles (ufc_titles.h)

 // BETA 1.8: in Portuguese (and the other translations) the Reborn career menus flickered between Reborn's
 // own English text/font (texts built into its menus and its mod code) and the Original translation that
 // replaced Reborn's EN tables. Reborn's text is now the main text: its tables are left as they are unless
 // --ufc_reborn_translated_text=true (the BETA 1.7 behaviour).
 // BETA 1.8.1: the footer (Reborn's own tip strings in memory) follows the selected language in both modes; with the
 // Reborn text kept it stayed English while the other tips came from the translated tables, so it alternated.
 LocalizeUfcRebornFooter(runtime,language);
 if(language&&!REXCVAR_GET(ufc_reborn_translated_text)){MountRebornLanguageTables(runtime,game,language);REXLOG_INFO("[ufc-language] reborn text kept (selected={}, translation off)",ufc::LanguageCodes[language]);return;}
 if(!language)return;
 auto file=ufc::PrepareLocalizedNames(game,language);
 unsigned mounted=0;
 auto english=game/"DAT/ytx/EN";auto translated=game/"DAT/ytx"/ufc::LanguageCodes[language];
 for(const auto& source:std::filesystem::directory_iterator(english)){
  if(!source.is_regular_file()||source.path().extension()!=".ytx")continue;
  const auto name=ufc::U8(source.path().filename());
  auto target=name=="generic.ytx"?file:translated/source.path().filename();
  if(!std::filesystem::is_regular_file(target))throw std::runtime_error("Tabela de idioma ausente: "+ufc::U8(target));
  if(language==1&&(name=="career.ytx"||ufc::ytx::IsPortugueseSupplementalAwardTable(name)))
   target=ufc::PreparePortugueseLabels(game,name,target);
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
#include <rex/system/xfile.h>
REXCVAR_DEFINE_BOOL(ufc_language_diagnostics,false,"Tests","Trace bounded YTX guest opens to identify the active language alias");
REXCVAR_DECLARE(bool, ufc_match_crash_diagnostics);
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
 std::string hud_path, ytx_path, match_path;uint32_t hud_root=0;const auto hud_handle_ptr=ctx.r3.u32;
 if(ctx.r5.u32){
  const auto* attr=reinterpret_cast<const rex::system::X_OBJECT_ATTRIBUTES*>(base+ctx.r5.u32);
  if(const uint32_t name_addr=attr->name_ptr){
   const auto* name=reinterpret_cast<const rex::system::X_ANSI_STRING*>(base+name_addr);
   const uint32_t data=name->pointer;const uint16_t length=name->length;
   if(data&&length>0&&length<1024){
    std::string lower(reinterpret_cast<char*>(base+data),length);
    for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
    MagazineReadbackOnOpen(lower);
    ufc_shirt_org::OnOpen(lower);  // 1.9: shirts by organization (ufc_shirt_org.h)
    ufc_titles::OnOpen(lower,base);  // 1.9: category titles (ufc_titles.h)
    if(lower.find("_acce.mpc")!=std::string::npos)UfcModelGuardNoteOpen(std::string(reinterpret_cast<char*>(base+data),length));  // ufc_model_guard.cpp
    if(REXCVAR_GET(ufc_match_crash_diagnostics)&&
       (lower.starts_with("game:\\dat\\stage\\")||
        (lower.starts_with("game:\\dat\\menu\\")&&lower.find("ppv")!=std::string::npos)||
        (lower.starts_with("game:\\dat\\chara\\")&&(lower.ends_with(".pac")||lower.ends_with(".mpc")))||
        lower.find("static_x")!=std::string::npos||lower.find("pride")!=std::string::npos))
     match_path=std::move(lower);
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
    if(lower.ends_with(".ytx"))ytx_path=std::move(path);
#ifdef UFC_CS60_DIAG
    else{static int logged=0;if(logged++<4000)REXLOG_INFO("[ufc-open] root={:08X} path={}",uint32_t(attr->root_directory),path);}
#endif
   }
  }
 }
 __real___imp__NtCreateFile(ctx,base);
 if(!match_path.empty())
  REXLOG_INFO("[ufc-match-asset] status={:08X} path={}",ctx.r3.u32,match_path);
 if(!ytx_path.empty()){
  const auto status=ctx.r3.u32;
  const rex::filesystem::HostPathEntry* entry=nullptr;
  if((status&0x80000000)==0&&hud_handle_ptr){
   auto handle=*reinterpret_cast<const rex::be<uint32_t>*>(base+hud_handle_ptr);
   auto file=REX_KERNEL_OBJECTS()->LookupObject<rex::system::XFile>(handle);
   if(file)entry=dynamic_cast<const rex::filesystem::HostPathEntry*>(file->entry());
  }
  REXLOG_INFO("[ufc-language-open] status={:08X} path={} host={}",status,ytx_path,entry?ufc::U8(entry->host_path()):std::string());
 }
 if(!hud_path.empty()){
  uint32_t handle=0;
  if((ctx.r3.u32&0x80000000)==0&&hud_handle_ptr)handle=(uint32_t(base[hud_handle_ptr])<<24)|(uint32_t(base[hud_handle_ptr+1])<<16)|(uint32_t(base[hud_handle_ptr+2])<<8)|base[hud_handle_ptr+3];
  ufc_hud_trace::Open(hud_root,hud_path,ctx.r3.u32,handle);
 }

}
