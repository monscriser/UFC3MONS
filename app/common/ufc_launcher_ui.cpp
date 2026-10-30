#include "ufc_launcher_ui.h"
#include "ufc_session.h"
#include "ufc_iso.h"
#include "ufc_dlc.h"
#include <future>
#include "ufc_steam.h"
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_drawer.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <cstring>
#include "ufc_preserve.h"
#include "ufc_shader_cleanup.h"
#include "ufc_shader_seed.h"
#include "ufc_mods.h"
#include <rex/filesystem.h>
namespace ufc {
struct FolderResult{std::mutex mutex;std::string chosen;};
// P14.9 Mods tab: several files can be picked at once.
struct ModPick{std::mutex mutex;std::vector<std::string> files;};
static void ModsSelected(void* pointer,const char* const* files,int){std::unique_ptr<std::shared_ptr<ModPick>> state(static_cast<std::shared_ptr<ModPick>*>(pointer));if(files){std::lock_guard lock((*state)->mutex);for(auto f=files;*f;++f)(*state)->files.push_back(*f);}}
static void Selected(void* pointer,const char* const* files,int){std::unique_ptr<std::shared_ptr<FolderResult>> state(static_cast<std::shared_ptr<FolderResult>*>(pointer));if(files&&files[0]){std::lock_guard lock((*state)->mutex);(*state)->chosen=files[0];}}
// The icon is the first icon embedded in this executable (the UFC icon from CMake).
void ApplyWindowIcon(void* native_window){
#ifdef _WIN32
 auto window=static_cast<HWND>(native_window);if(!window)return;
 wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
 HICON icon=ExtractIconW(GetModuleHandleW(nullptr),path,0);
 if(icon&&reinterpret_cast<INT_PTR>(icon)!=1){SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));}
#else
 (void)native_window;
 const auto bytes=ReadText(rex::filesystem::GetExecutableFolder()/"steam_art/icon.png");
 int width=0,height=0;
 auto rgba=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),width,height);
 if(rgba.empty()||width<=0||height<=0)return;
 auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,rgba.data(),width*4);
 if(!surface)return;
 int count=0;auto** windows=SDL_GetWindows(&count);
 if(windows){for(int i=0;i<count;++i)SDL_SetWindowIcon(windows[i],surface);SDL_free(windows);}
 SDL_DestroySurface(surface);
#endif
}
namespace {
// Launcher language: 0 = Portugues (Brasil), 1 = English, 2 = Espanol. Kept in its own
// file so the shared launcher.ini keeps 0/1 (other parts of the app read it as pt/en).
int& Lang(){static int value=0;return value;}
const char* L(const char* pt,const char* en,const char* es){return Lang()==2?es:Lang()==1?en:pt;}
fs::path LanguageFile(){return RegistryDirectory()/"config/launcher-language.ini";}
// Fonts baked by the app: 0 = 16 px, 1 = 21 px, 2 = 28 px (fallback: scaled 16 px).
void PushSize(int index){auto& fonts=ImGui::GetIO().Fonts->Fonts;if(index<fonts.Size)ImGui::PushFont(fonts[index],fonts[index]->LegacySize);else ImGui::PushFont(fonts[0],index==1?21.f:index==2?28.f:16.f);}
ImU32 Col(float r,float g,float b,float a=1){return ImGui::GetColorU32(ImVec4(r,g,b,a));}
const ImVec4 kRed(.84f,.11f,.16f,1),kRedHover(.93f,.17f,.22f,1),kRedActive(.70f,.08f,.12f,1),kGreen(.20f,.85f,.42f,1),kMuted(.70f,.73f,.78f,1);
void CheckMark(ImDrawList* draw,ImVec2 c,float r,ImU32 color,float thickness){ImVec2 pts[3]={ImVec2(c.x-r*.45f,c.y+r*.02f),ImVec2(c.x-r*.12f,c.y+r*.36f),ImVec2(c.x+r*.48f,c.y-r*.34f)};draw->AddPolyline(pts,3,color,ImDrawFlags_None,thickness);}
void FilledCheck(ImDrawList* draw,ImVec2 c,float r){draw->AddCircleFilled(c,r,Col(.13f,.72f,.33f),32);CheckMark(draw,c,r,Col(.04f,.08f,.05f),r*.24f);}
void Warning(ImDrawList* draw,ImVec2 c,float r){draw->AddCircleFilled(c,r,Col(.92f,.55f,.12f),32);draw->AddLine(ImVec2(c.x,c.y-r*.5f),ImVec2(c.x,c.y+r*.15f),Col(.08f,.05f,.02f),r*.22f);draw->AddCircleFilled(ImVec2(c.x,c.y+r*.45f),r*.13f,Col(.08f,.05f,.02f));}
void Flag(ImDrawList* draw,ImVec2 p,int language){
 const float w=24,h=16;ImVec2 q(p.x+w,p.y+h);
 if(language==0){draw->AddRectFilled(p,q,IM_COL32(0,151,57,255),2);ImVec2 c(p.x+w/2,p.y+h/2);draw->AddQuadFilled(ImVec2(c.x,p.y+1.5f),ImVec2(q.x-2.5f,c.y),ImVec2(c.x,q.y-1.5f),ImVec2(p.x+2.5f,c.y),IM_COL32(254,221,0,255));draw->AddCircleFilled(c,h*.27f,IM_COL32(1,33,105,255),20);}
 else if(language==1){draw->AddRectFilled(p,q,IM_COL32(255,255,255,255),2);for(int i=0;i<7;i+=2)draw->AddRectFilled(ImVec2(p.x,p.y+i*h/7),ImVec2(q.x,p.y+(i+1)*h/7),IM_COL32(178,34,52,255));draw->AddRectFilled(p,ImVec2(p.x+w*.45f,p.y+h*4/7),IM_COL32(60,59,110,255));}
 else{draw->AddRectFilled(p,q,IM_COL32(170,21,27,255),2);draw->AddRectFilled(ImVec2(p.x,p.y+h/4),ImVec2(q.x,p.y+h*3/4),IM_COL32(241,191,0,255));}
 draw->AddRect(p,q,IM_COL32(0,0,0,90),2);
}
bool StyledButton(const char* label,ImVec2 size,bool primary){
 if(primary){ImGui::PushStyleColor(ImGuiCol_Button,kRed);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,kRedHover);ImGui::PushStyleColor(ImGuiCol_ButtonActive,kRedActive);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);}
 else{ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.10f,.11f,.135f,1));ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.16f,.17f,.21f,1));ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.20f,.21f,.26f,1));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);}
 ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.30f,.32f,.37f,1));
 bool pressed=ImGui::Button(label,size);ImGui::PopStyleColor(4);ImGui::PopStyleVar();return pressed;
}
void Muted(const char* text){ImGui::PushStyleColor(ImGuiCol_Text,kMuted);ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();}
void CardTitle(const char* title,const char* subtitle){PushSize(1);ImGui::TextUnformatted(title);ImGui::PopFont();if(subtitle&&*subtitle)Muted(subtitle);}
void BeginCard(const char* id,ImVec2 size,ImGuiWindowFlags flags=0){
 ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.075f,.085f,.10f,1));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.18f,.20f,.24f,1));
 ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,8);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,12));ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,1);
 ImGui::BeginChild(id,size,ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding|(size.y==0?ImGuiChildFlags_AutoResizeY:0),flags);
}
void EndCard(){ImGui::EndChild();ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);}
}
class Launcher final:public rex::ui::ImGuiDialog{
 rex::ui::ImmediateDrawer* texture_drawer_;fs::path bundle_;Installation installation_;Settings settings_;Session session_;std::shared_ptr<FolderResult> picker_=std::make_shared<FolderResult>();
 std::string status_,last_session_,saved_,saved_cheats_,measured_;std::array<char,4096> directory_{};std::array<std::array<char,240>,24> binds_{};
 std::unique_ptr<rex::ui::ImmediateTexture> art_,logo_;int art_w_=0,art_h_=0,logo_w_=0,logo_h_=0;bool tried_art_=false,previous_running_=false;int profile_index_=0,source_index_=0,saved_lang_=-1,tab_=0;
 std::array<fs::path,3> source_paths_{};bool original_=false,prepared_=false;std::future<fs::path> preparation_;std::atomic<uint64_t> prepared_bytes_{0},total_bytes_{0};
 bool Preparing()const{return preparation_.valid();}
 // Optional DLC, installed separately into the selected source's content folder.
 std::shared_ptr<FolderResult> dlc_picker_=std::make_shared<FolderResult>();std::future<dlc::Report> dlc_job_;dlc::Progress dlc_progress_;std::vector<dlc::Installed> dlcs_;std::vector<std::string> dlc_missing_;
 bool DlcBusy()const{return dlc_job_.valid();}
 bool Busy()const{return session_.Running()||Preparing()||DlcBusy();}
 void RefreshDlc(){dlcs_.clear();if(Ready(installation_))Guard([&]{dlcs_=dlc::List(installation_.game,original_);});dlc_missing_=dlc::MissingKnown(dlcs_);}
 void StartDlc(const fs::path& source){
  if(!Ready(installation_))throw std::runtime_error(L("Escolha o jogo antes de instalar DLC.","Choose the game before installing DLC.","Elige el juego antes de instalar DLC."));
  auto game=installation_.game;bool original=original_;status_=L("Instalando DLC; aguarde.","Installing DLC; please wait.","Instalando DLC; espera.");
  dlc_job_=std::async(std::launch::async,[this,game,original,source]{return dlc::Install(game,original,source,dlc_progress_);});
 }
 void DlcSection(){
  CardTitle(L("Conteúdo adicional (DLC)","Additional content (DLC)","Contenido adicional (DLC)"),nullptr);
  const int installed=dlc::kKnownCount-int(dlc_missing_.size());
  if(!Ready(installation_))Muted(L("Opcional. Escolha o jogo primeiro.","Optional. Choose the game first.","Opcional. Elige el juego primero."));
  else if(dlc_missing_.empty()){ImGui::PushStyleColor(ImGuiCol_Text,kGreen);ImGui::TextWrapped("%s",L("Todas as DLCs disponíveis para o UFC estão instaladas.","All DLCs available for UFC are installed.","Todos los DLC disponibles para UFC están instalados."));ImGui::PopStyleColor();}
  else if(installed==0&&dlcs_.empty())Muted(L("Opcional: escolha a pasta onde estão os pacotes de DLC (por exemplo 5451087D/00000002). Os arquivos escolhidos são apenas lidos.","Optional: choose the folder that holds the DLC packages (for example 5451087D/00000002). The chosen files are only read.","Opcional: elige la carpeta con los paquetes de DLC (por ejemplo 5451087D/00000002). Los archivos elegidos solo se leen."));
  else{
   char line[160];const bool one=dlc_missing_.size()==1;std::snprintf(line,sizeof line,L(one?"%d de %d instaladas. Falta:":"%d de %d instaladas. Faltam:","%d of %d installed. Missing:",one?"%d de %d instalados. Falta:":"%d de %d instalados. Faltan:"),installed,dlc::kKnownCount);
   std::string list=line;bool first=true;for(auto& m:dlc_missing_){list+=first?" ":", ";list+=m;first=false;}ImGui::TextWrapped("%s",list.c_str());
  }
  if(DlcBusy()){auto total=dlc_progress_.total.load();ImGui::ProgressBar(total?float(double(dlc_progress_.done.load())/double(total)):0.f,ImVec2(-1,0));ImGui::Text(L("Pacote %d de %d","Package %d of %d","Paquete %d de %d"),dlc_progress_.package.load(),dlc_progress_.packages.load());return;}
  ImGui::BeginDisabled(Busy()||!Ready(installation_));
  const float width=std::min(260.f,ImGui::GetContentRegionAvail().x);
  if(!dlc_missing_.empty()&&StyledButton(L("Instalar DLC (pasta)...","Install DLC (folder)...","Instalar DLC (carpeta)..."),ImVec2(width,30),true))SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(dlc_picker_),nullptr,nullptr,false);
  if(!dlcs_.empty()&&StyledButton(L("Remover DLCs","Remove DLCs","Quitar DLC"),ImVec2(width,30),false))ImGui::OpenPopup("###remove-dlc");
  ImGui::EndDisabled();
  if(ImGui::BeginPopupModal(L("Remover DLCs###remove-dlc","Remove DLCs###remove-dlc","Quitar DLC###remove-dlc"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
   ImGui::PushTextWrapPos(ImGui::GetFontSize()*28);
   ImGui::TextWrapped("%s",L("Todas as DLCs instaladas nesta fonte serão apagadas definitivamente. Não será possível recuperar. Os pacotes de onde você instalou não são afetados.","All DLCs installed for this source will be permanently deleted. They cannot be recovered. The packages you installed from are not affected.","Todos los DLC instalados en esta fuente se borrarán definitivamente. No se podrán recuperar. Los paquetes desde los que instalaste no se modifican."));
   ImGui::PopTextWrapPos();ImGui::Spacing();
   if(StyledButton(L("Remover definitivamente","Delete permanently","Borrar definitivamente"),ImVec2(0,34),true)){Guard([&]{auto removed=dlc::RemoveAll(installation_.game,original_);char line[160];std::snprintf(line,sizeof line,L("%d DLC(s) removida(s) definitivamente.","%d DLC(s) permanently removed.","%d DLC eliminado(s) definitivamente."),removed);status_=line;});RefreshDlc();ImGui::CloseCurrentPopup();}
   ImGui::SameLine();if(StyledButton(L("Cancelar","Cancel","Cancelar"),ImVec2(0,34),false))ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
 }
 void RememberSource(){std::string text="selected="+std::to_string(source_index_)+"\n";for(int i=0;i<3;++i)text+="path"+std::to_string(i)+"="+U8(source_paths_[i])+"\n";AtomicText(RegistryDirectory()/"sources.ini",text);}
 void SelectSource(const fs::path& selected){
  installation_={};prepared_=false;fs::path p=selected;if(source_index_!=2&&fs::is_regular_file(p)){if(p.filename()!="default.xex")throw std::runtime_error(L("Escolha default.xex.","Choose default.xex.","Elige default.xex."));p=p.parent_path();}
  if(source_index_==2){auto value=IsoInstallation(p);Register(value);installation_=value;original_=true;settings_.Load(value.game);CopySettings();saved_=settings_.Text();source_paths_[2]=p;prepared_=Missing().empty();RefreshDlc();status_=std::string(L("ISO reconhecida: o jogo lê a imagem diretamente, sem extrair. Saves e configurações ficam em ","ISO recognized: the game reads the image directly, nothing is extracted. Saves and settings are kept in ","ISO reconocida: el juego lee la imagen directamente, sin extraer. Las partidas y ajustes se guardan en "))+U8(value.game);}
  else{auto hash=Hash(p/"default.xex");if(hash!=(source_index_==0?GuestSHA:OriginalSHA))throw std::runtime_error(L("O executável não corresponde à fonte escolhida.","Executable does not match the selected source.","El ejecutable no corresponde a la fuente elegida."));UseFolder(p);source_paths_[source_index_]=p;prepared_=Missing().empty();}
  std::snprintf(directory_.data(),directory_.size(),"%s",U8(source_paths_[source_index_]).c_str());RememberSource();
  // P14.6: no "Prepare files" button any more; the folders are created as soon as the source is located.
  if(Ready(installation_))Prepare();
 }
 void SwitchSource(int index){source_index_=index;installation_={};prepared_=false;dlcs_.clear();dlc_missing_.clear();status_=L("Localize a fonte escolhida.","Locate the selected source.","Localiza la fuente elegida.");std::snprintf(directory_.data(),directory_.size(),"%s",U8(source_paths_[index]).c_str());if(!source_paths_[index].empty())Guard([&]{SelectSource(source_paths_[index]);});else Guard([&]{RememberSource();});}
 std::vector<fs::path> profiles_;
 void CopySettings(){for(size_t i=0;i<binds_.size();++i)std::snprintf(binds_[i].data(),binds_[i].size(),"%s",settings_.binds[i].c_str());}
 void SyncBinds(){for(size_t i=0;i<binds_.size();++i)settings_.binds[i]=binds_[i].data();}
 void UseFolder(const fs::path& game,const fs::path& old={}){Installation value{game,old};Register(value);installation_=value;original_=Hash(game/"default.xex")==OriginalSHA;settings_.Load(game);CopySettings();saved_=settings_.Text();std::snprintf(directory_.data(),directory_.size(),"%s",U8(game).c_str());status_=L("Instalação reconhecida. Seus saves e cache ficam na pasta do jogo.","Installation recognized. Saves and cache stay in the game folder.","Instalación reconocida. Tus partidas y caché quedan en la carpeta del juego.");RefreshDlc();}
 void Save(){SyncBinds();settings_.Save(installation_.game);saved_=settings_.Text();status_=L("Preferências salvas.","Preferences saved.","Preferencias guardadas.");}
 // The last guarded action failed: the footer shows the warning icon instead of the check until the next one succeeds.
 bool status_error_=false;
 // P14.8: optional "Pre-compile shaders" run in progress (only when the player clicks the button).
 bool precompiling_=false;
 // P14.10: progress of the pre-compile run. The game logs one "Creating graphics pipeline state" line per pipeline
 // it creates from the storage; the total is the number of records in the player's pipeline file after the merge.
 int precompile_total_=0,precompile_done_=0;std::chrono::steady_clock::time_point precompile_poll_{};
 void PollPrecompile(){
  const auto now=std::chrono::steady_clock::now();if(now-precompile_poll_<std::chrono::milliseconds(300))return;precompile_poll_=now;
  const auto log=ReadText(PrecompileLog(installation_.game));int count=0;
  for(size_t at=log.find("Creating graphics pipeline state");at!=std::string::npos;at=log.find("Creating graphics pipeline state",at+1))++count;
  precompile_done_=std::min(count,precompile_total_);
 }
 // P14.9 Mods tab.
 std::shared_ptr<ModPick> mod_picker_=std::make_shared<ModPick>();std::vector<ModFile> mods_;bool mods_loaded_=false;
 void Mods(){
  BeginCard("ModsCard",ImVec2(0,0));
  CardTitle(L("Mods","Mods","Mods"),L("Arquivos feitos pela comunidade (.pac, .mpc...). O launcher guarda uma cópia e o jogo usa no lugar do original; a pasta do jogo não é alterada.","Community-made files (.pac, .mpc...). The launcher keeps a copy and the game uses it instead of the original; the game folder is not changed.","Archivos hechos por la comunidad (.pac, .mpc...). El launcher guarda una copia y el juego la usa en lugar del original; la carpeta del juego no se modifica."));
  const bool iso=!installation_.iso.empty(),can=Ready(installation_)&&!iso&&!Busy();
  if(iso)Muted(L("Mods não funcionam lendo a ISO direto: use a pasta do Reborn ou o Original extraído.","Mods do not work when reading the ISO directly: use the Reborn folder or the extracted Original.","Los mods no funcionan leyendo la ISO directamente: usa la carpeta de Reborn o el Original extraído."));
  ImGui::BeginDisabled(!can);
  if(StyledButton(L("Adicionar mod...","Add mod...","Añadir mod..."),ImVec2(200,34),true)){static const SDL_DialogFileFilter filter[]={ {"Mods (.pac, .mpc)","pac;mpc"},{"*","*"} };SDL_ShowOpenFileDialog(ModsSelected,new std::shared_ptr<ModPick>(mod_picker_),nullptr,filter,2,nullptr,true);}
  ImGui::SameLine();if(StyledButton(L("Abrir pasta dos mods","Open mods folder","Abrir carpeta de mods"),ImVec2(0,34),false))Guard([&]{OpenFolder(ModsDir(installation_.game));});
  ImGui::EndDisabled();ImGui::Spacing();
  if(!mods_loaded_){mods_=Ready(installation_)?ListMods(installation_.game):std::vector<ModFile>{};mods_loaded_=true;}
  if(mods_.empty())Muted(L("Nenhum mod instalado.","No mods installed.","Ningún mod instalado."));
  else if(ImGui::BeginTable("mods",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)){
   ImGui::TableSetupColumn("on",ImGuiTableColumnFlags_WidthFixed,40);ImGui::TableSetupColumn("file",ImGuiTableColumnFlags_WidthStretch);ImGui::TableSetupColumn("remove",ImGuiTableColumnFlags_WidthFixed,110);
   for(size_t i=0;i<mods_.size();++i){
    ImGui::PushID(int(i));ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);bool on=mods_[i].enabled;
    ImGui::BeginDisabled(!can);if(ImGui::Checkbox("##on",&on)){auto relative=mods_[i].relative;Guard([&]{SetModEnabled(installation_.game,relative,on);});mods_loaded_=false;}ImGui::EndDisabled();
    ImGui::TableSetColumnIndex(1);ImGui::AlignTextToFramePadding();ImGui::Text("%s  (%.1f MB)",ModKey(mods_[i].relative).c_str(),double(mods_[i].size)/1048576.0);
    ImGui::TableSetColumnIndex(2);ImGui::BeginDisabled(!can);if(StyledButton(L("Remover","Remove","Quitar"),ImVec2(100,0),false)){auto relative=mods_[i].relative;Guard([&]{RemoveMod(installation_.game,relative);status_=L("Mod removido: o arquivo original volta na próxima vez que o jogo abrir.","Mod removed: the original file comes back next time the game starts.","Mod quitado: el archivo original vuelve la próxima vez que se abra el juego.");});mods_loaded_=false;}ImGui::EndDisabled();
    ImGui::PopID();
   }
   ImGui::EndTable();
  }
  ImGui::Spacing();Muted(L("Desmarque para desligar um mod sem removê-lo. As mudanças valem na próxima vez que o jogo abrir.","Untick to switch a mod off without removing it. Changes apply the next time the game starts.","Desmarca para desactivar un mod sin quitarlo. Los cambios valen la próxima vez que se abra el juego."));
  EndCard();
 }
 void StartPrecompile(){
  Prepare();if(!prepared_||!Missing().empty())return;Save();
  MergeShaderSeed(bundle_,installation_.game);
  {std::error_code e;const auto file=ShaderStorageDir(installation_.game)/(std::string(kShaderTitle)+".fbo.vk.xpso");const auto size=fs::file_size(file,e);
   precompile_total_=!e&&size>12?int((size-12)/kPipelineRecord):0;precompile_done_=0;}
  session_.Start(bundle_,installation_,settings_,true);precompiling_=true;
 }
 void Guard(const std::function<void()>& action){status_error_=false;try{action();}catch(const std::exception& e){status_=e.what();status_error_=true;}}
 // Check and create what the game needs before JOGAR.
 std::vector<std::string> Missing(){
  std::vector<std::string> missing;auto exe=GameBinary(bundle_,original_);
  if(!fs::is_regular_file(exe))missing.push_back(U8(exe.filename()));
  if(!Ready(installation_))missing.push_back(L("default.xex na pasta do jogo","default.xex in the game folder","default.xex en la carpeta del juego"));
#ifdef _WIN32
  for(auto name:{"librexruntime.dll","rexgpu-xenos.dll","libc++.dll","libunwind.dll"})if(!fs::is_regular_file(bundle_/name))missing.push_back(name);
#endif
  return missing;
 }
 bool PreparedFolders()const{if(!Ready(installation_))return false;auto root=Storage(installation_.game);for(auto folder:{"cache","config","user"})if(!fs::is_directory(root/folder))return false;return true;}
 void Prepare(){
  if(source_index_==2&&!Ready(installation_)){auto iso=source_paths_[2];if(iso.empty())throw std::runtime_error(L("Localize a ISO primeiro.","Locate the ISO first.","Localiza la ISO primero."));SelectSource(iso);}
  if(!Ready(installation_))throw std::runtime_error(L("Escolha a pasta do jogo.","Choose the game folder.","Elige la carpeta del juego."));
  auto root=Storage(installation_.game);for(auto folder:{"cache","config","user"})fs::create_directories(root/folder);
  auto missing=Missing();if(missing.empty()){prepared_=true;status_=L("Arquivos prontos para jogar.","Files ready to play.","Archivos listos para jugar.");return;}
  std::string list;for(auto& item:missing){if(!list.empty())list+=", ";list+=item;}
  status_=std::string(L("Faltam arquivos: ","Missing files: ","Faltan archivos: "))+list;
 }
 std::unique_ptr<rex::ui::ImmediateTexture> LoadArt(const fs::path& file,int& w,int& h){auto bytes=ReadText(file);auto rgba=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),w,h);if(rgba.empty())return {};return texture_drawer_->CreateTexture(w,h,rex::ui::ImmediateTextureFilter::kLinear,false,rgba.data());}
 void LanguageCombo(){
  static const char* names[]={"Português (Brasil)","English","Español"};
  ImGui::SetNextItemWidth(230);auto* draw=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetCursorScreenPos();float h=ImGui::GetFrameHeight();
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.06f,.03f,.04f,.88f));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.16f,.06f,.08f,.92f));ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.55f,.16f,.20f,1));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);
  if(ImGui::BeginCombo("##language","")){
   for(int i=0;i<3;++i){ImGui::PushID(i);ImVec2 q=ImGui::GetCursorScreenPos();if(ImGui::Selectable("##item",Lang()==i,0,ImVec2(0,24)))Lang()=i;auto* popup=ImGui::GetWindowDrawList();Flag(popup,ImVec2(q.x+4,q.y+4),i);popup->AddText(ImVec2(q.x+38,q.y+3),ImGui::GetColorU32(ImGuiCol_Text),names[i]);ImGui::PopID();}
   ImGui::EndCombo();
  }
  ImGui::PopStyleVar();ImGui::PopStyleColor(3);
  Flag(draw,ImVec2(p.x+10,p.y+(h-16)/2),Lang());draw->AddText(ImVec2(p.x+44,p.y+(h-ImGui::GetFontSize())/2),ImGui::GetColorU32(ImGuiCol_Text),names[Lang()]);
 }
 // Header art (from Ian's layout): background anchored right, logo at left, subtitle and language selector below it.
 float DrawHeader(ImGuiIO& io){
  auto* draw=ImGui::GetWindowDrawList();auto origin=ImGui::GetWindowPos();
  if(!tried_art_){tried_art_=true;art_=LoadArt(bundle_/"ui/header-bg.png",art_w_,art_h_);logo_=LoadArt(bundle_/"ui/header-logo.png",logo_w_,logo_h_);}
  const float base_w=art_w_?float(art_w_):1409.f,base_h=art_h_?float(art_h_):260.f;
  const float scale=std::min(io.DisplaySize.x/base_w,std::max(.62f,io.DisplaySize.y*.27f/base_h));
  const float height=std::floor(base_h*scale),width=base_w*scale;
  draw->AddRectFilled(origin,ImVec2(origin.x+io.DisplaySize.x,origin.y+height),IM_COL32(12,6,8,255));
  if(art_){float left=io.DisplaySize.x-width;if(left>0)draw->AddImage(reinterpret_cast<ImTextureID>(art_.get()),origin,ImVec2(origin.x+left+1,origin.y+height),ImVec2(0,0),ImVec2(.002f,1));draw->AddImage(reinterpret_cast<ImTextureID>(art_.get()),ImVec2(origin.x+left,origin.y),ImVec2(origin.x+io.DisplaySize.x,origin.y+height));}
  const float lx=std::max(18.f,30*scale),ly=std::max(8.f,14*scale);
  if(logo_)draw->AddImage(reinterpret_cast<ImTextureID>(logo_.get()),ImVec2(origin.x+lx,origin.y+ly),ImVec2(origin.x+lx+logo_w_*scale,origin.y+ly+logo_h_*scale));
  else{PushSize(2);draw->AddText(ImVec2(origin.x+lx,origin.y+ly),IM_COL32(247,247,250,255),"UFC3MONS");ImGui::PopFont();}
  // Language selector right under the logo (no slogan, as Ian asked).
  const float y=ly+(logo_?logo_h_*scale:40)+12;
  ImGui::SetCursorScreenPos(ImVec2(origin.x+lx,origin.y+y));LanguageCombo();
  return std::max(height,y+ImGui::GetFrameHeight()+42);
 }
 void ReadyCard(const std::vector<std::string>& missing){
  BeginCard("ReadyCard",ImVec2(0,62),ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  auto* draw=ImGui::GetWindowDrawList();ImVec2 p=ImGui::GetCursorScreenPos();float right=ImGui::GetWindowPos().x+ImGui::GetWindowWidth();
  const bool ready=Ready(installation_);
  if(ready)FilledCheck(draw,ImVec2(p.x+16,p.y+20),15);else Warning(draw,ImVec2(p.x+16,p.y+20),15);
  ImGui::SetCursorScreenPos(ImVec2(p.x+46,p.y-2));
  ImGui::BeginGroup();CardTitle(ready?L("Tudo configurado","All set","Todo configurado"):L("Vamos preparar sua primeira luta","Let's prepare your first fight","Preparemos tu primera pelea"),nullptr);
  ImGui::TextUnformatted(ready?L("Sua instalação foi reconhecida. É só jogar.","Your installation was recognized. Just play.","Tu instalación fue reconocida. Solo juega."):L("Selecione Reborn, Original ou ISO abaixo.","Select Reborn, Original or ISO below.","Selecciona Reborn, Original o ISO abajo."));ImGui::EndGroup();
  // Only a problem is shown on the right; "ready" is already said by the title.
  if(ready&&!missing.empty()){
   const char* text=L("Faltam arquivos","Missing files","Faltan archivos");
   PushSize(1);float tw=ImGui::CalcTextSize(text).x;float x=right-tw-26;if(x>p.x+380){Warning(draw,ImVec2(x-26,p.y+20),15);draw->AddText(ImVec2(x,p.y+8),ImGui::GetColorU32(ImVec4(1,.55f,.45f,1)),text);}ImGui::PopFont();
  }
  EndCard();
 }
 void SourceCard(float height){
  BeginCard("SourceCard",ImVec2(0,height),ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  CardTitle(L("Fonte do jogo","Game source","Fuente del juego"),L("Selecione a origem dos arquivos do jogo.","Select where the game files come from.","Selecciona el origen de los archivos del juego."));
  ImGui::Spacing();
  const char* names[]={"Reborn","Original","ISO"};
  const char* subs[]={L("(pasta do jogo)","(game folder)","(carpeta del juego)"),L("(XEX ou pasta)","(XEX or folder)","(XEX o carpeta)"),L("(imagem do disco)","(disc image)","(imagen del disco)")};
  const char* descriptions[]={L("Pasta do Reborn.","Reborn folder.","Carpeta de Reborn."),L("Pasta ou default.xex.","Folder or default.xex.","Carpeta o default.xex."),L("Sem extrair.","No extraction.","Sin extraer.")};
  const float gap=10,w=std::floor((ImGui::GetContentRegionAvail().x-2*gap)/3),h=std::clamp(ImGui::GetContentRegionAvail().y-2,74.f,112.f);
  ImGui::BeginDisabled(Busy());
  for(int i=0;i<3;++i){
   ImGui::PushID(i);ImVec2 p=ImGui::GetCursorScreenPos();
   if(ImGui::InvisibleButton("source",ImVec2(w,h))&&source_index_!=i)SwitchSource(i);
   const bool selected=source_index_==i,hovered=ImGui::IsItemHovered();
   auto* draw=ImGui::GetWindowDrawList();ImVec2 q(p.x+w,p.y+h);
   draw->AddRectFilled(p,q,selected?Col(.24f,.05f,.07f):hovered?Col(.12f,.13f,.16f):Col(.09f,.10f,.12f),7);
   draw->AddRect(p,q,selected?Col(.90f,.15f,.20f):Col(.22f,.24f,.28f),7,0,selected?2.f:1.f);
   ImVec2 rc(p.x+18,p.y+20);draw->AddCircle(rc,8,selected?Col(.95f,.25f,.30f):Col(.55f,.58f,.63f),20,1.6f);if(selected)draw->AddCircleFilled(rc,4.5f,Col(.95f,.25f,.30f),16);
   draw->PushClipRect(p,q,true);
   PushSize(1);draw->AddText(ImVec2(p.x+34,p.y+8),ImGui::GetColorU32(ImGuiCol_Text),names[i]);ImGui::PopFont();
   draw->AddText(ImVec2(p.x+34,p.y+33),ImGui::GetColorU32(kMuted),subs[i]);
   draw->AddText(ImVec2(p.x+12,p.y+h-26),ImGui::GetColorU32(ImVec4(.85f,.86f,.89f,1)),descriptions[i]);
   draw->PopClipRect();ImGui::PopID();if(i<2)ImGui::SameLine(0,gap);
  }
  ImGui::EndDisabled();EndCard();
 }
 void PrepareCard(){
  BeginCard("PrepareCard",ImVec2(0,0));
  CardTitle(L("Preparar e jogar","Prepare and play","Preparar y jugar"),L("A ISO é lida diretamente, sem extrair. As preferências ficam na pasta fixa do usuário; saves e cache ficam junto do jogo.","The ISO is read directly, without extraction. Preferences use your fixed user folder; saves and cache stay with the game.","La ISO se lee directamente, sin extraer. Las preferencias quedan en la carpeta fija del usuario; partidas y caché quedan junto al juego."));
  if(Preparing()){auto total=total_bytes_.load();ImGui::ProgressBar(total?float(double(prepared_bytes_.load())/double(total)):0.f);ImGui::TextUnformatted(L("Lendo a ISO...","Reading the ISO...","Leyendo la ISO..."));}
  ImGui::BeginDisabled(Busy());
  const float locate=source_index_==1?300.f:150.f;
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.07f,.12f,.19f,1));ImGui::SetNextItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x-locate-10));
  if(ImGui::InputText("##gamepath",directory_.data(),directory_.size(),ImGuiInputTextFlags_EnterReturnsTrue))Guard([&]{SelectSource(Path(directory_.data()));});
  ImGui::PopStyleColor();ImGui::SameLine();
  if(StyledButton(source_index_==1?L("Localizar XEX...","Locate XEX...","Buscar XEX..."):L("Localizar...","Locate...","Buscar..."),ImVec2(140,0),false)){if(source_index_==2){static const SDL_DialogFileFilter filter[]={ {"ISO Xbox 360","iso"} };SDL_ShowOpenFileDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,filter,1,nullptr,false);}else if(source_index_==1){static const SDL_DialogFileFilter filter[]={ {"Xbox 360 XEX","xex"} };SDL_ShowOpenFileDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,filter,1,nullptr,false);}else SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,nullptr,false);}
  if(source_index_==1){ImGui::SameLine();if(StyledButton(L("Localizar pasta...","Locate folder...","Buscar carpeta..."),ImVec2(150,0),false))SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,nullptr,false);}
  ImGui::EndDisabled();
  // P14.6: preparing is automatic when the source is located. P14.8: in its old place, the optional
  // shader pre-compile (nothing happens unless the player clicks it).
  ImGui::Spacing();
  ImGui::BeginDisabled(Busy()||!prepared_||!Ready(installation_)||!Missing().empty());
  if(StyledButton(L("Pré-compilar shaders","Pre-compile shaders","Precompilar shaders"),ImVec2(220,34),false))Guard([&]{StartPrecompile();});
  ImGui::EndDisabled();ImGui::SameLine();
  if(precompiling_&&session_.Running()){
   PollPrecompile();char overlay[96];
   if(precompile_done_)std::snprintf(overlay,sizeof overlay,L("%d de %d shaders","%d of %d shaders","%d de %d shaders"),precompile_done_,precompile_total_);
   else std::snprintf(overlay,sizeof overlay,"%s",L("Preparando...","Preparing...","Preparando..."));
   ImGui::ProgressBar(precompile_total_?float(precompile_done_)/float(precompile_total_):0.f,ImVec2(-1,34),overlay);
  }else{
  ImGui::BeginGroup();ImGui::PushTextWrapPos(0);
  Muted(L("Opcional. Compila os efeitos do jogo na sua placa e evita travadinhas na primeira vez que aparecem.","Optional. Compiles the game's effects on your GPU and avoids hitches the first time they appear.","Opcional. Compila los efectos del juego en tu tarjeta y evita tirones la primera vez que aparecen."));
  ImGui::PopTextWrapPos();ImGui::EndGroup();
  }
  EndCard();
 }
 void Home(){
  auto missing=Missing();
  ReadyCard(missing);
  const float row=std::clamp(ImGui::GetContentRegionAvail().y-220,150.f,230.f);  // P14.8: room for the pre-compile row
  if(ImGui::BeginTable("SourceAndDlc",2,ImGuiTableFlags_SizingStretchProp)){
   ImGui::TableSetupColumn("source",ImGuiTableColumnFlags_WidthStretch,.60f);
   ImGui::TableSetupColumn("dlc",ImGuiTableColumnFlags_WidthStretch,.40f);
   ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);SourceCard(row);
   ImGui::TableSetColumnIndex(1);BeginCard("DlcCard",ImVec2(0,row));DlcSection();EndCard();
   ImGui::EndTable();
  }
  PrepareCard();
 }
 void SteamButton(){
  ImGui::BeginDisabled(Busy());
  if(StyledButton(L("Adicionar à Steam","Add to Steam","Añadir a Steam"),ImVec2(200,46),false)){profiles_=steam::Profiles();profile_index_=0;ImGui::OpenPopup("###steam-account");}
  if(ImGui::BeginPopupModal(L("Conta Steam###steam-account","Steam account###steam-account","Cuenta de Steam###steam-account"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
   ImGui::PushTextWrapPos(ImGui::GetFontSize()*28);
   ImGui::TextWrapped("%s",L("Feche a Steam antes de adicionar. O atalho abre o jogo diretamente e recebe as artes do UFC.","Close Steam before adding. The shortcut opens the game directly and receives the UFC art.","Cierra Steam antes de añadir. El acceso directo abre el juego y recibe las imágenes de UFC."));
   if(profiles_.empty())ImGui::TextWrapped("%s",L("Nenhuma conta Steam foi encontrada neste computador. Abra e feche a Steam uma vez.","No Steam account was found on this computer. Open and close Steam once.","No se encontró ninguna cuenta de Steam en este equipo. Abre y cierra Steam una vez."));
   ImGui::PopTextWrapPos();
   for(size_t i=0;i<profiles_.size();++i){auto label=U8(profiles_[i].parent_path().filename());ImGui::RadioButton(label.c_str(),&profile_index_,int(i));}
   ImGui::BeginDisabled(profiles_.empty()||!Ready(installation_));if(StyledButton(L("Criar atalho com artes","Create shortcut with art","Crear acceso con imágenes"),ImVec2(0,34),true))Guard([&]{Save();auto id=steam::Export(bundle_,profiles_.at(size_t(profile_index_)));status_=L("Atalho UFC criado na Steam. As artes foram instaladas. ID: ","UFC shortcut created in Steam. Art installed. ID: ","Acceso de UFC creado en Steam. Imágenes instaladas. ID: ")+std::to_string(id);ImGui::CloseCurrentPopup();});ImGui::EndDisabled();ImGui::SameLine();if(StyledButton(L("Voltar","Back","Volver"),ImVec2(0,34),false))ImGui::CloseCurrentPopup();ImGui::EndPopup();
  }ImGui::EndDisabled();
 }
 void Video(){
  BeginCard("GameLanguage",ImVec2(0,0));
  CardTitle(L("Idioma do jogo","Game language","Idioma del juego"),nullptr);
  const char* languages[]={"English","Português (Brasil)","Español","Français","Italiano","Deutsch"};ImGui::SetNextItemWidth(300);ImGui::Combo("##game-language",&settings_.game_language,languages,6);
  Muted(L("Nomes do elenco Reborn preservados em inglês. Menus e textos usam o idioma escolhido. O idioma do launcher fica no topo da janela.","Reborn roster names stay in English. Menus and texts use the chosen language. The launcher language is at the top of the window.","Los nombres del elenco Reborn quedan en inglés. Menús y textos usan el idioma elegido. El idioma del launcher está arriba de la ventana."));
  EndCard();
  BeginCard("VideoCard",ImVec2(0,0));
  CardTitle(L("Vídeo","Video","Vídeo"),nullptr);
  const char* sizes[]={"1280 x 720","1920 x 1080","2560 x 1440","3840 x 2160"};ImGui::SetNextItemWidth(300);ImGui::Combo(L("Resolução","Resolution","Resolución"),&settings_.output,sizes,4);
  Muted(L("1080p usa renderização 2x (1440p), reduzida para 1080p; as demais usam 1x/2x/3x. Aplicado ao iniciar o jogo.","1080p renders at 2x (1440p), downsampled to 1080p; the other options use 1x/2x/3x. Applied when starting the game.","1080p renderiza a 2x (1440p) y reduce a 1080p; las demás usan 1x/2x/3x. Se aplica al iniciar el juego."));
  ImGui::Checkbox(L("Abrir o jogo em tela cheia","Open the game fullscreen","Abrir el juego en pantalla completa"),&settings_.fullscreen);ImGui::Checkbox(L("Mostrar contador de FPS","Show FPS counter","Mostrar contador de FPS"),&settings_.fps);
  Muted(L("Taxa de quadros fixa em 60 FPS: a simulação do UFC 3 avança um passo por quadro, então mais quadros deixariam a luta acelerada.","Frame rate fixed at 60 FPS: UFC 3 advances its simulation one step per frame, so more frames would speed up the fight.","Cuadros fijos en 60 FPS: UFC 3 avanza su simulación un paso por cuadro, así que más cuadros acelerarían la pelea."));
  {const char* presents[]={L("Mailbox (recomendado)","Mailbox (recommended)","Mailbox (recomendado)"),"FIFO",L("Desligado","Off","Desactivado")};
   ImGui::SetNextItemWidth(300);ImGui::Combo(L("Vsync","VSync","VSync"),&settings_.present_mode,presents,3);
   Muted(L("Mailbox: sem tearing e com resposta rápida. FIFO: vsync clássico, sem tearing, resposta um pouco mais lenta. Desligado: resposta mais rápida, mas a imagem pode rasgar. Se a placa não tiver Mailbox, o jogo usa FIFO.","Mailbox: no tearing and quick response. FIFO: classic VSync, no tearing, slightly slower response. Off: quickest response, but the picture may tear. If your graphics card has no Mailbox, the game uses FIFO.","Mailbox: sin tearing y respuesta rápida. FIFO: VSync clásico, sin tearing, respuesta algo más lenta. Desactivado: respuesta más rápida, pero la imagen puede rasgarse. Si la tarjeta no tiene Mailbox, el juego usa FIFO."));}
  {const char* filters[]={L("Como o jogo pede","As the game asks","Como pide el juego"),L("Desligada","Off","Desactivado"),"2x",L("4x (padrão)","4x (default)","4x (predeterminado)"),"8x","16x"};
   ImGui::SetNextItemWidth(300);ImGui::Combo(L("Filtragem de texturas (anisotrópica)","Texture filtering (anisotropic)","Filtrado de texturas (anisotrópico)"),&settings_.texture_filter,filters,6);
   Muted(L("Deixa o chão, o octógono e a lona nítidos quando vistos de lado. Custa pouco na placa de vídeo; 16x é o mais nítido.","Keeps the floor, octagon and canvas sharp when seen at an angle. Light on the graphics card; 16x is the sharpest.","Mantiene nítidos el suelo, el octágono y la lona vistos de lado. Cuesta poco a la tarjeta gráfica; 16x es lo más nítido."));}
  ImGui::Checkbox(L("Ampliação AMD FSR 1 (menos uso da placa de vídeo)","AMD FSR 1 upscaling (lower graphics card use)","Escalado AMD FSR 1 (menos uso de la tarjeta gráfica)"),&settings_.fsr1);
  Muted(L("Ligado: o jogo desenha em 720p e o FSR 1 amplia a imagem para a resolução escolhida, com bordas mais nítidas que a ampliação comum. Usa cerca de metade da placa de vídeo da renderização 2x (bom para ROG Ally e PCs fracos; em telas 16:10, como a do Steam Deck, não tem efeito). Desligado: renderização normal.","On: the game renders at 720p and FSR 1 enlarges the image to the chosen resolution, with sharper edges than a plain upscale. Uses about half the graphics card of 2x rendering (good for ROG Ally and weaker PCs; no effect on 16:10 screens such as the Steam Deck). Off: normal rendering.","Activado: el juego dibuja a 720p y FSR 1 amplía la imagen a la resolución elegida, con bordes más nítidos que el escalado común. Usa cerca de la mitad de la tarjeta gráfica que el renderizado 2x (bueno para ROG Ally y PCs modestos; sin efecto en pantallas 16:10 como la del Steam Deck). Desactivado: renderizado normal."));
  EndCard();
 }
 void Keyboard(){
  ImGui::Checkbox(L("Usar teclado","Use keyboard","Usar teclado"),&settings_.keyboard);ImGui::SameLine();ImGui::Checkbox(L("Mouse no analógico direito","Mouse on right stick","Ratón en el stick derecho"),&settings_.mouse);ImGui::SetNextItemWidth(240);ImGui::SliderFloat(L("Sensibilidade do mouse","Mouse sensitivity","Sensibilidad del ratón"),&settings_.sensitivity,.05f,5.f,"%.2f");
  Muted(L("Use nomes como Space, Return, Up, Shift+Up. Separe alternativas com vírgula. Um controle compatível também pode ser usado.","Use names like Space, Return, Up, Shift+Up. Separate alternatives with commas. A compatible controller can also be used.","Usa nombres como Space, Return, Up, Shift+Up. Separa alternativas con comas. También se puede usar un mando compatible."));
  static const char* labels_pt[]={"A / confirmar","B / voltar","X","Y","Gatilho esquerdo","Gatilho direito","LB","RB","Analógico E: cima","Analógico E: baixo","Analógico E: esquerda","Analógico E: direita","Analógico E: clique","Analógico D: clique","Analógico D: cima","Analógico D: baixo","Analógico D: esquerda","Analógico D: direita","Direcional: cima","Direcional: baixo","Direcional: esquerda","Direcional: direita","Back","Start / pausa"};
  static const char* labels_en[]={"A / confirm","B / back","X","Y","Left trigger","Right trigger","LB","RB","Left stick: up","Left stick: down","Left stick: left","Left stick: right","Left stick: press","Right stick: press","Right stick: up","Right stick: down","Right stick: left","Right stick: right","D-pad: up","D-pad: down","D-pad: left","D-pad: right","Back","Start / pause"};
  static const char* labels_es[]={"A / confirmar","B / volver","X","Y","Gatillo izquierdo","Gatillo derecho","LB","RB","Stick izq.: arriba","Stick izq.: abajo","Stick izq.: izquierda","Stick izq.: derecha","Stick izq.: pulsar","Stick der.: pulsar","Stick der.: arriba","Stick der.: abajo","Stick der.: izquierda","Stick der.: derecha","Cruceta: arriba","Cruceta: abajo","Cruceta: izquierda","Cruceta: derecha","Back","Start / pausa"};
  const auto* labels=Lang()==2?labels_es:Lang()==1?labels_en:labels_pt;
  const float input=std::clamp((ImGui::GetContentRegionAvail().x-2*170)/2-20,140.f,320.f);
  if(ImGui::BeginTable("keys",4,ImGuiTableFlags_SizingFixedFit)){for(size_t row=0;row<12;++row){ImGui::TableNextRow();for(size_t col=0;col<2;++col){auto i=row+col*12;ImGui::TableSetColumnIndex(int(col*2));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(labels[i]);ImGui::TableSetColumnIndex(int(col*2+1));ImGui::SetNextItemWidth(input);ImGui::PushID(int(i));ImGui::InputText("##key",binds_[i].data(),binds_[i].size());ImGui::PopID();}}ImGui::EndTable();}
  if(StyledButton(L("Restaurar teclas padrão","Restore default keys","Restaurar teclas predeterminadas"),ImVec2(0,34),false)){for(size_t i=0;i<binds_.size();++i)std::snprintf(binds_[i].data(),binds_[i].size(),"%s",BindDefaults[i]);}
 }
 void Cheats(){
  BeginCard("CheatsCard",ImVec2(0,0));
  CardTitle(L("Carreira","Career","Carrera"),nullptr);
  ImGui::Checkbox(L("Recompensa completa no treino automático","Full Training Rewards (Auto Mode)","Recompensa completa en el entrenamiento automático"),&settings_.full_training_rewards);
  Muted(L("Ligado: ao escolher AUTO numa sessão de treino da Carreira, o lutador recebe a mesma recompensa da nota máxima do treino manual (4 estrelas). Desligado: o jogo original.","On: choosing AUTO in a Career training session gives the fighter the same reward as the best Manual Training grade (4 stars). Off: original game.","Activado: al elegir AUTO en una sesión de entrenamiento de la Carrera, el luchador recibe la misma recompensa que la nota máxima del entrenamiento manual (4 estrellas). Desactivado: el juego original."));
  ImGui::Spacing();
  ImGui::Checkbox(L("Dinheiro em dobro (Creds ganhos nas lutas)","Double money (Creds earned from fights)","Dinero doble (Creds ganados en las peleas)"),&settings_.double_creds);
  Muted(L("Ligado: os Creds ganhos em cada luta da Carreira contam duas vezes (respeitando o limite máximo do jogo). Desligado: o jogo original.","On: Creds earned in each Career fight count twice (within the game's maximum). Off: original game.","Activado: los Creds ganados en cada pelea de la Carrera cuentan dos veces (respetando el límite máximo del juego). Desactivado: el juego original."));
  Muted(session_.Running()?L("O jogo está aberto: a mudança vale a partir da próxima luta ou treino automático.","The game is running: the change applies from the next fight or automatic training.","El juego está abierto: el cambio vale desde la próxima pelea o entrenamiento automático."):L("O jogo está fechado: a opção fica salva e vale quando ele for aberto.","The game is closed: the option is saved and applies when it starts.","El juego está cerrado: la opción queda guardada y vale cuando se abra."));
  EndCard();
 }
 void Saves(){
  Muted(L("As preferências ficam na pasta fixa do usuário. Seus saves, cache e registros ficam na pasta do jogo. Atualizar o launcher ou AppImage conserva esses dados.","Preferences use your fixed user folder. Saves, cache and logs stay in the game folder. Updating the launcher or AppImage keeps this data.","Las preferencias quedan en la carpeta fija del usuario. Partidas, caché y registros quedan en la carpeta del juego. Actualizar el launcher o la AppImage conserva estos datos."));ImGui::TextWrapped("%s",U8(RegistryDirectory()/"config").c_str());
  if(Ready(installation_)){ImGui::TextWrapped("%s",U8(installation_.iso.empty()?installation_.game:installation_.iso).c_str());ImGui::TextWrapped("%s",U8(Storage(installation_.game)/"user").c_str());}
  ImGui::Spacing();
  ImGui::BeginDisabled(!Ready(installation_)||session_.Running());
  if(StyledButton(L("Abrir pasta de saves","Open saves folder","Abrir carpeta de partidas"),ImVec2(0,34),false))Guard([&]{OpenFolder(Storage(installation_.game)/"user");});ImGui::SameLine();
  if(StyledButton(L("Criar cópia dos saves","Back up saves","Copiar partidas"),ImVec2(0,34),false))Guard([&]{auto root=Storage(installation_.game),dest=root/"backups"/("saves-"+Stamp());if(!fs::is_directory(root/"user"))throw std::runtime_error(L("Esta instalação ainda não tem saves.","This installation has no saves yet.","Esta instalación aún no tiene partidas."));fs::create_directories(dest.parent_path());fs::copy(root/"user",dest,fs::copy_options::recursive);status_=std::string(L("Cópia criada em ","Backup created in ","Copia creada en "))+U8(dest);});
  ImGui::SameLine();if(StyledButton(L("Limpar shaders","Clear shaders","Borrar shaders"),ImVec2(0,34),false))Guard([&]{const auto removed=ClearShaderCachePermanently(installation_.game);status_=removed?L("Cache de shaders apagado definitivamente.","Shader cache permanently deleted.","Caché de shaders borrada definitivamente."):L("Nenhum cache de shaders encontrado.","No shader cache found.","No se encontró caché de shaders.");});
  if(StyledButton(L("Abrir todos os registros","Open all logs","Abrir todos los registros"),ImVec2(0,34),false))Guard([&]{OpenFolder(Storage(installation_.game)/"logs");});ImGui::SameLine();if(StyledButton(L("Abrir cache","Open cache","Abrir caché"),ImVec2(0,34),false))Guard([&]{OpenFolder(Storage(installation_.game)/"cache");});ImGui::EndDisabled();
  ImGui::Spacing();ImGui::Separator();
  ImGui::BeginDisabled(session_.Running());
  ImGui::Checkbox(L("Gravar logs no armazenamento","Save logs to storage","Guardar registros en el disco"),&settings_.write_logs);
  Muted(L("Desligado por padrão. Ligue quando quiser guardar registros para investigar um problema.","Off by default. Enable to keep logs when investigating a problem.","Desactivado por defecto. Actívalo para guardar registros al investigar un problema."));
  ImGui::EndDisabled();
 }
 void Footer(ImGuiIO& io,bool running){
  auto* draw=ImGui::GetWindowDrawList();auto origin=ImGui::GetWindowPos();const float top=io.DisplaySize.y-74;
  draw->AddRectFilled(ImVec2(origin.x,origin.y+top),ImVec2(origin.x+io.DisplaySize.x,origin.y+io.DisplaySize.y),IM_COL32(10,11,14,255));
  draw->AddLine(ImVec2(origin.x,origin.y+top),ImVec2(origin.x+io.DisplaySize.x,origin.y+top),IM_COL32(40,43,50,255));
  const bool ok=Ready(installation_)&&Missing().empty()&&!status_error_;
  if(ok)FilledCheck(draw,ImVec2(origin.x+34,origin.y+top+37),14);else Warning(draw,ImVec2(origin.x+34,origin.y+top+37),14);
  const float buttons=200+12+200+20;
  ImGui::SetCursorScreenPos(ImVec2(origin.x+58,origin.y+top+12));
  ImGui::BeginChild("Status",ImVec2(std::max(120.f,io.DisplaySize.x-58-buttons-16),52),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoBackground);
  const std::string text=running&&precompiling_?L("Pré-compilando shaders para a sua placa. Pode levar alguns minutos na primeira vez.","Pre-compiling shaders for your GPU. The first time can take a few minutes.","Precompilando shaders para tu tarjeta. La primera vez puede tardar unos minutos."):running?L("Partida aberta. Ao terminar, feche a janela do jogo.","Match open. When you finish, close the game window.","Partida abierta. Al terminar, cierra la ventana del juego."):status_;
  ImGui::TextWrapped("%s",text.c_str());ImGui::EndChild();
  ImGui::SetCursorScreenPos(ImVec2(origin.x+io.DisplaySize.x-buttons,origin.y+top+14));
  SteamButton();ImGui::SameLine(0,12);
  ImGui::BeginDisabled(running||Preparing()||DlcBusy()||!prepared_||!Ready(installation_)||!Missing().empty());
  PushSize(1);
  if(StyledButton(L("JOGAR","PLAY","JUGAR"),ImVec2(200,46),true))Guard([&]{Prepare();Save();session_.Start(bundle_,installation_,settings_);status_=L("Jogo iniciado.","Game started.","Juego iniciado.");});
  ImGui::PopFont();ImGui::EndDisabled();
 }
 void SyncLanguage(){
  UiEnglish()=Lang()!=0;
  if(Lang()!=saved_lang_){saved_lang_=Lang();settings_.ui_language=Lang()==0?0:1;Guard([&]{AtomicText(LanguageFile(),"language="+std::to_string(Lang())+"\n");});}
 }
 void OnDraw(ImGuiIO& io)override{
  SyncLanguage();
  // Test only (UFC_TEST_AUTOSTART_PRECOMPILE=1): start the pre-compile once, for screenshots without clicks.
  {static bool started=false;if(!started&&std::getenv("UFC_TEST_AUTOSTART_PRECOMPILE")&&Ready(installation_)&&prepared_&&!Busy()){started=true;Guard([&]{StartPrecompile();});}}
  if(Preparing()&&preparation_.wait_for(std::chrono::seconds(0))==std::future_status::ready){Guard([&]{auto game=preparation_.get();UseFolder(game);Prepare();});}
  {std::string chosen;{std::lock_guard lock(picker_->mutex);chosen.swap(picker_->chosen);}if(!chosen.empty())Guard([&]{SelectSource(Path(chosen));});}
  {std::vector<std::string> files;{std::lock_guard lock(mod_picker_->mutex);files.swap(mod_picker_->files);}
   if(!files.empty()&&Ready(installation_)){std::string added;Guard([&]{for(auto& f:files){auto relative=AddMod(installation_.game,Path(f));if(!added.empty())added+=", ";added+=ModKey(relative);}status_=std::string(L("Mod adicionado: ","Mod added: ","Mod añadido: "))+added;});mods_loaded_=false;}}
  {std::string chosen;{std::lock_guard lock(dlc_picker_->mutex);chosen.swap(dlc_picker_->chosen);}if(!chosen.empty()&&!DlcBusy())Guard([&]{StartDlc(Path(chosen));});}
  if(DlcBusy()&&dlc_job_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
   Guard([&]{auto report=dlc_job_.get();char line[160];std::snprintf(line,sizeof line,L("DLC: %d instalada(s), %d já estava(m) instalada(s).","DLC: %d installed, %d already installed.","DLC: %d instalado(s), %d ya estaba(n) instalado(s)."),report.installed,report.already);status_=line;});
   if(dlc_job_.valid())dlc_job_={};RefreshDlc();
  }
  bool running=session_.Running();
  if(previous_running_&&!running){
   last_session_=session_.Directory().empty()?std::string{}:U8(session_.Directory());
   double fps=session_.Directory().empty()?-1:MeasuredFps(session_.Directory());char line[200];
   if(fps>0)std::snprintf(line,sizeof line,L("Taxa recente registrada: %.1f FPS (pode incluir menus).","Recent logged rate: %.1f FPS (may include menus).","Tasa reciente registrada: %.1f FPS (puede incluir menús)."),fps);
   else std::snprintf(line,sizeof line,"%s",L("Ative os logs para registrar a próxima sessão. Cada sessão usa uma pasta separada.","Enable logs to record the next session. Each session uses a separate folder.","Activa los registros para guardar la próxima sesión. Cada sesión usa una carpeta separada."));
   measured_=line;
   if(precompiling_){precompiling_=false;int created=0,requested=0;
    if(PrecompileReport(installation_.game,created,requested)){char done[220];std::snprintf(done,sizeof done,L("Shaders prontos: %d efeitos compilados para a sua placa.","Shaders ready: %d effects compiled for your GPU.","Shaders listos: %d efectos compilados para tu tarjeta."),created);status_=done;status_error_=false;}
    else{status_=L("A pré-compilação não terminou. Tente de novo; o jogo funciona normalmente sem ela.","Pre-compiling did not finish. Try again; the game works normally without it.","La precompilación no terminó. Inténtalo de nuevo; el juego funciona normalmente sin ella.");status_error_=true;}
   }else
   status_=session_.ExitCode()==0?L("Jogo fechado.","Game closed.","Juego cerrado."):(session_.Directory().empty()?L("O jogo terminou com erro. Ative os logs para investigar uma próxima sessão.","The game ended with an error. Enable logs to investigate a future session.","El juego terminó con un error. Activa los registros para investigar una próxima sesión."):L("O jogo terminou com erro. Os registros foram preservados.","The game ended with an error. Logs were kept.","El juego terminó con un error. Los registros se conservaron."));
  }
  previous_running_=running;
  // Autosave: any change is written as soon as the game folder is registered.
  SyncBinds();
  if(!running){auto now=settings_.Text();if(now!=saved_){Guard([&]{settings_.Save(installation_.game);saved_=now;});}}
  // Cheats are read by the running game from cheats.ini, so they are written even while it runs.
  {auto cheats=CheatsText(settings_);if(cheats!=saved_cheats_){Guard([&]{AtomicText(CheatsFile(),cheats);saved_cheats_=cheats;});}}
  ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(io.DisplaySize,ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,8));
  ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.047f,.051f,.063f,1));
  // Theme from the layout: dark fields, red accents (instead of ImGui's default blue).
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.11f,.12f,.15f,1));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.16f,.17f,.21f,1));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(.20f,.21f,.26f,1));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive,ImVec4(.45f,.07f,.11f,1));ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.45f,.08f,.12f,1));ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(.60f,.11f,.16f,1));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab,kRed);ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.07f,.075f,.09f,.98f));ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,ImVec4(0,0,0,.55f));
  ImGui::Begin("###ufc_setup",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  const float header=DrawHeader(io);
  const float side=std::clamp(io.DisplaySize.x*.025f,14.f,40.f);
  // Tabs sit on the lower edge of the art, as in the layout.
  ImGui::SetCursorPos(ImVec2(side,header-38));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(22,10));ImGui::PushStyleVar(ImGuiStyleVar_TabRounding,7);ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize,0);
  ImGui::PushStyleColor(ImGuiCol_Tab,ImVec4(.10f,.10f,.12f,.92f));ImGui::PushStyleColor(ImGuiCol_TabHovered,ImVec4(.55f,.10f,.14f,1));ImGui::PushStyleColor(ImGuiCol_TabSelected,kRed);ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline,kRed);
  ImGui::PushItemWidth(io.DisplaySize.x-2*side);
  if(ImGui::BeginTabBar("Configurador")){
   if(ImGui::BeginTabItem(L("Início###home","Home###home","Inicio###home"))){tab_=0;ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(L("Vídeo e idioma###video","Video and language###video","Vídeo e idioma###video"))){tab_=1;ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(L("Teclado e controle###input","Keyboard and controller###input","Teclado y mando###input"))){tab_=2;ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(L("Saves e registros###saves","Saves and logs###saves","Partidas y registros###saves"))){tab_=3;ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(L("Cheats###cheats","Cheats###cheats","Trucos###cheats"))){tab_=4;ImGui::EndTabItem();}
   if(ImGui::BeginTabItem("Mods###mods")){if(tab_!=5)mods_loaded_=false;tab_=5;ImGui::EndTabItem();}
   ImGui::EndTabBar();
  }
  ImGui::PopItemWidth();ImGui::PopStyleColor(4);ImGui::PopStyleVar(3);
  {auto* draw=ImGui::GetWindowDrawList();auto o=ImGui::GetWindowPos();draw->AddLine(ImVec2(o.x,o.y+header),ImVec2(o.x+io.DisplaySize.x,o.y+header),ImGui::GetColorU32(kRed),2);}
  ImGui::SetCursorPos(ImVec2(side,header+12));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10,7));
  ImGui::BeginChild("LauncherBody",ImVec2(io.DisplaySize.x-2*side,std::max(120.f,io.DisplaySize.y-header-12-74-10)),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground|(tab_==0?ImGuiWindowFlags_NoScrollbar:0));
  switch(tab_){case 0:Home();break;case 1:Video();break;case 2:Keyboard();break;case 4:Cheats();break;case 5:Mods();break;default:Saves();break;}
  ImGui::EndChild();
  Footer(io,running);
  ImGui::PopStyleVar();
  ImGui::End();ImGui::PopStyleColor(10);ImGui::PopStyleVar(3);
 }
 public:
 ~Launcher()override{if(preparation_.valid())preparation_.wait();if(dlc_job_.valid())dlc_job_.wait();}
 Launcher(rex::ui::ImGuiDrawer* drawer,rex::ui::ImmediateDrawer* texture_drawer,fs::path bundle):ImGuiDialog(drawer),texture_drawer_(texture_drawer),bundle_(std::move(bundle)){
  try{settings_.Load({});}catch(...){}
  {auto ini=ReadIni(LanguageFile());int value=settings_.ui_language==1?1:0;if(ini.contains("language")){try{value=std::clamp(std::stoi(ini["language"]),0,2);}catch(...){}}Lang()=value;saved_lang_=value;UiEnglish()=value!=0;}
  try{installation_=Discover(bundle_);if(!installation_.iso.empty()){auto iso=installation_.iso;source_index_=2;source_paths_[2]=iso;SelectSource(iso);}else if(Ready(installation_)){UseFolder(installation_.game,installation_.legacy_data);source_index_=original_?1:0;source_paths_[source_index_]=installation_.game;prepared_=Missing().empty();}else status_=L("Escolha a pasta do Reborn para começar.","Choose the Reborn folder to start.","Elige la carpeta de Reborn para empezar.");}catch(const std::exception& e){status_=e.what();}
  auto remembered=ReadIni(RegistryDirectory()/"sources.ini");for(int i=0;i<3;++i)if(remembered.contains("path"+std::to_string(i)))source_paths_[i]=Path(remembered["path"+std::to_string(i)]);if(remembered.contains("selected")){try{SwitchSource(std::clamp(std::stoi(remembered["selected"]),0,2));}catch(const std::exception& e){status_=e.what();}}
  CopySettings();saved_=settings_.Text();saved_cheats_.clear();
  if(Ready(installation_)&&!prepared_)Guard([&]{Prepare();});
 }
 bool Running()const{return Busy();}
};
std::shared_ptr<Launcher> CreateLauncher(rex::ui::ImGuiDrawer* drawer,rex::ui::ImmediateDrawer* texture_drawer,const fs::path& bundle){
#ifdef UFC_DLC_SELFTEST
 // Test-only build (never packaged): install DLC from UFC_DLC_TEST_SOURCE into the copy UFC_DLC_TEST_GAME,
 // write the outcome to UFC_DLC_TEST_RESULT and quit, without touching the user's launcher settings.
 if(auto* game=std::getenv("UFC_DLC_TEST_GAME")){
  std::string result;
  try{dlc::Progress progress;auto r=dlc::Install(Path(game),true,Path(std::getenv("UFC_DLC_TEST_SOURCE")),progress);result="OK installed="+std::to_string(r.installed)+" already="+std::to_string(r.already);}
  catch(const std::exception& e){result=std::string("ERROR ")+e.what();}
  AtomicText(Path(std::getenv("UFC_DLC_TEST_RESULT")),result+"\n");std::_Exit(0);
 }
#endif
 return std::make_shared<Launcher>(drawer,texture_drawer,bundle);}
bool LauncherRunning(const Launcher* launcher){return launcher&&launcher->Running();}
void DestroyLauncher(std::shared_ptr<Launcher>& launcher){launcher.reset();}
}
