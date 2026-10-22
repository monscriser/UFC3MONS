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
#include <cstdio>
#include <mutex>
#include <cstring>
#include "ufc_preserve.h"
#include <rex/filesystem.h>
namespace ufc {
struct FolderResult{std::mutex mutex;std::string chosen;};
static void Selected(void* pointer,const char* const* files,int){std::unique_ptr<std::shared_ptr<FolderResult>> state(static_cast<std::shared_ptr<FolderResult>*>(pointer));if(files&&files[0]){std::lock_guard lock((*state)->mutex);(*state)->chosen=files[0];}}
// Cache/DLC removals are recoverable on both platforms.
inline void ToRecycleBin(const fs::path& path){
 PreserveForRemoval(path);
}
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
class Launcher final:public rex::ui::ImGuiDialog{
 rex::ui::ImmediateDrawer* texture_drawer_;fs::path bundle_;Installation installation_;Settings settings_;Session session_;std::shared_ptr<FolderResult> picker_=std::make_shared<FolderResult>();
 std::string status_,last_session_,saved_,measured_;std::array<char,4096> directory_{};std::array<std::array<char,240>,24> binds_{};
 std::unique_ptr<rex::ui::ImmediateTexture> art_,logo_;bool tried_art_=false,previous_running_=false,folder_details_=false;int profile_index_=0,source_index_=0;
 std::array<fs::path,3> source_paths_{};bool original_=false,prepared_=false;std::future<fs::path> preparation_;std::atomic<uint64_t> prepared_bytes_{0},total_bytes_{0};
 bool Preparing()const{return preparation_.valid();}
 // L2: optional DLC, installed separately into the selected source's content folder.
 std::shared_ptr<FolderResult> dlc_picker_=std::make_shared<FolderResult>();std::future<dlc::Report> dlc_job_;dlc::Progress dlc_progress_;std::vector<dlc::Installed> dlcs_;
 bool DlcBusy()const{return dlc_job_.valid();}
 void RefreshDlc(){dlcs_.clear();if(Ready(installation_))Guard([&]{dlcs_=dlc::List(installation_.game,original_);});}
 void StartDlc(const fs::path& source){
  if(!Ready(installation_))throw std::runtime_error(T("Escolha o jogo antes de instalar DLC.","Choose the game before installing DLC."));
  auto game=installation_.game;bool original=original_;status_=T("Instalando DLC; aguarde.","Installing DLC; please wait.");
  dlc_job_=std::async(std::launch::async,[this,game,original,source]{return dlc::Install(game,original,source,dlc_progress_);});
 }
 void DlcSection(){
  ImGui::Spacing();ImGui::TextUnformatted(T("Conteudo adicional (DLC)","Additional content (DLC)"));ImGui::SameLine(std::max(250.f,ImGui::GetContentRegionAvail().x-200));
  if(dlcs_.empty())ImGui::TextColored(ImVec4(.7f,.74f,.8f,1),"%s",T("Opcional","Optional"));else ImGui::TextColored(ImVec4(.45f,.9f,.73f,1),T("%d instalada(s)","%d installed"),int(dlcs_.size()));
  ImGui::TextWrapped("%s",T("Opcional: escolha um pacote de DLC ou a pasta onde estao os pacotes (por exemplo 5451087D/00000002). Os arquivos escolhidos sao apenas lidos; a DLC fica junto dos saves desta fonte.","Optional: choose a DLC package or the folder that holds the packages (for example 5451087D/00000002). The chosen files are only read; the DLC is kept with this source's saves."));
  ImGui::BeginDisabled(session_.Running()||Preparing()||DlcBusy());
  if(ImGui::Button(T("Instalar DLC (arquivo)...","Install DLC (file)...")))SDL_ShowOpenFileDialog(Selected,new std::shared_ptr<FolderResult>(dlc_picker_),nullptr,nullptr,0,nullptr,false);
  ImGui::SameLine();if(ImGui::Button(T("Instalar DLC (pasta)...","Install DLC (folder)...")))SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(dlc_picker_),nullptr,nullptr,false);
  ImGui::EndDisabled();
  if(DlcBusy()){auto total=dlc_progress_.total.load();ImGui::ProgressBar(total?float(double(dlc_progress_.done.load())/double(total)):0.f);ImGui::Text(T("Pacote %d de %d","Package %d of %d"),dlc_progress_.package.load(),dlc_progress_.packages.load());}
  for(size_t i=0;i<dlcs_.size();++i){
   ImGui::BulletText("%s  (%.0f MB)",dlcs_[i].display_name.c_str(),double(dlcs_[i].bytes)/1048576.0);ImGui::SameLine();ImGui::PushID(int(i));
   ImGui::BeginDisabled(session_.Running()||DlcBusy());
   if(ImGui::SmallButton(T("Remover (recuperavel)","Remove (recoverable)"))){auto name=dlcs_[i].file_name;Guard([&]{ToRecycleBin(dlc::PackageDir(installation_.game,original_)/Path(name));auto header=dlc::HeaderDir(installation_.game,original_)/Path(name+".header");if(fs::exists(header))ToRecycleBin(header);status_=T("DLC removida e preservada para recuperacao.","DLC removed and preserved for recovery.");});RefreshDlc();}
   ImGui::EndDisabled();ImGui::PopID();
  }
 }
 void RememberSource(){std::string text="selected="+std::to_string(source_index_)+"\n";for(int i=0;i<3;++i)text+="path"+std::to_string(i)+"="+U8(source_paths_[i])+"\n";AtomicText(RegistryDirectory()/"sources.ini",text);}
 void SelectSource(const fs::path& selected){
  installation_={};prepared_=false;fs::path p=selected;if(source_index_!=2&&fs::is_regular_file(p)){if(p.filename()!="default.xex")throw std::runtime_error("Escolha default.xex.");p=p.parent_path();}
  if(source_index_==2){auto value=IsoInstallation(p);Register(value);installation_=value;original_=true;settings_.Load(value.game);CopySettings();saved_=settings_.Text();source_paths_[2]=p;prepared_=Missing().empty();RefreshDlc();status_=std::string(T("ISO reconhecida: o jogo le a imagem diretamente, sem extrair. Saves e configuracoes ficam em ","ISO recognized: the game reads the image directly, nothing is extracted. Saves and settings are kept in "))+U8(value.game);}
  else{auto hash=Hash(p/"default.xex");if(hash!=(source_index_==0?GuestSHA:OriginalSHA))throw std::runtime_error(T("O executavel nao corresponde a fonte escolhida.","Executable does not match the selected source."));UseFolder(p);source_paths_[source_index_]=p;prepared_=Missing().empty();}
  std::snprintf(directory_.data(),directory_.size(),"%s",U8(source_paths_[source_index_]).c_str());RememberSource();
 }
 void SwitchSource(int index){source_index_=index;installation_={};prepared_=false;dlcs_.clear();status_=T("Localize a fonte escolhida e prepare os arquivos.","Locate the selected source and prepare the files.");std::snprintf(directory_.data(),directory_.size(),"%s",U8(source_paths_[index]).c_str());if(!source_paths_[index].empty())Guard([&]{SelectSource(source_paths_[index]);});else Guard([&]{RememberSource();});}
std::vector<fs::path> profiles_;
 void CopySettings(){for(size_t i=0;i<binds_.size();++i)std::snprintf(binds_[i].data(),binds_[i].size(),"%s",settings_.binds[i].c_str());}
 void SyncBinds(){for(size_t i=0;i<binds_.size();++i)settings_.binds[i]=binds_[i].data();}
 void UseFolder(const fs::path& game,const fs::path& old={}){Installation value{game,old};Register(value);installation_=value;original_=Hash(game/"default.xex")==OriginalSHA;settings_.Load(game);CopySettings();saved_=settings_.Text();std::snprintf(directory_.data(),directory_.size(),"%s",U8(game).c_str());status_=T("Instalacao reconhecida. Seus saves e cache ficam na pasta do jogo.","Installation recognized. Saves and cache stay in the game folder.");RefreshDlc();}
 void Save(){SyncBinds();settings_.Save(installation_.game);saved_=settings_.Text();status_=T("Preferencias salvas.","Preferences saved.");}
 void Guard(const std::function<void()>& action){try{action();}catch(const std::exception& e){status_=e.what();}}
 // Item 13: check and create what the game needs before JOGAR.
 std::vector<std::string> Missing(){
  std::vector<std::string> missing;auto exe=GameBinary(bundle_,original_);
  if(!fs::is_regular_file(exe))missing.push_back(U8(exe.filename()));
  if(!Ready(installation_))missing.push_back(T("default.xex na pasta do jogo","default.xex in the game folder"));
#ifdef _WIN32
  for(auto name:{"librexruntime.dll","rexgpu-xenos.dll","libc++.dll","libunwind.dll"})if(!fs::is_regular_file(bundle_/name))missing.push_back(name);
#endif
  return missing;
 }
 void Prepare(){
  if(source_index_==2&&!Ready(installation_)){auto iso=source_paths_[2];if(iso.empty())throw std::runtime_error(T("Localize a ISO primeiro.","Locate the ISO first."));SelectSource(iso);}
  if(!Ready(installation_))throw std::runtime_error(T("Escolha a pasta do jogo antes de preparar.","Choose the game folder before preparing."));
  auto root=Storage(installation_.game);for(auto folder:{"logs","cache","config","user"})fs::create_directories(root/folder);
  auto missing=Missing();if(missing.empty()){prepared_=true;status_=T("Arquivos prontos para jogar.","Files ready to play.");return;}
  std::string list;for(auto& item:missing){if(!list.empty())list+=", ";list+=item;}
  status_=std::string(T("Faltam arquivos: ","Missing files: "))+list;
 }
 void DrawHeader(ImGuiIO& io){
  auto* draw=ImGui::GetWindowDrawList();auto origin=ImGui::GetWindowPos();float height=io.DisplaySize.y>700?180.f:146.f;ImU32 red=IM_COL32(49,17,23,255);
  draw->AddRectFilled(origin,ImVec2(origin.x+io.DisplaySize.x,origin.y+height),red);
  if(!tried_art_){tried_art_=true;auto load=[&](const char* file){auto bytes=ReadText(bundle_/"steam_art"/file);int w=0,h=0;auto rgba=rex::ui::DecodeImageRGBA(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),w,h);return rgba.empty()?std::unique_ptr<rex::ui::ImmediateTexture>{}:texture_drawer_->CreateTexture(w,h,rex::ui::ImmediateTextureFilter::kLinear,false,rgba.data());};art_=load("hero.png");logo_=load("logo.png");}
  if(art_){float left=io.DisplaySize.x*.51f;draw->AddImage(reinterpret_cast<ImTextureID>(art_.get()),ImVec2(origin.x+left,origin.y),ImVec2(origin.x+io.DisplaySize.x,origin.y+height),ImVec2(.35f,0),ImVec2(1,1));draw->AddRectFilledMultiColor(ImVec2(origin.x+left,origin.y),ImVec2(origin.x+left+130,origin.y+height),red,IM_COL32(49,17,23,0),IM_COL32(49,17,23,0),red);}
  draw->AddText(ImGui::GetFont(),34,ImVec2(origin.x+30,origin.y+23),IM_COL32(247,247,250,255),"UFC3MONS");draw->AddText(ImGui::GetFont(),24,ImVec2(origin.x+30,origin.y+66),IM_COL32(245,90,101,255),"UFC Undisputed 3");draw->AddText(ImGui::GetFont(),16,ImVec2(origin.x+30,origin.y+104),IM_COL32(220,199,203,255),T("Sua proxima luta comeca aqui.","Your next fight starts here."));
  ImGui::SetCursorPos(ImVec2(30,height+14));
 }
 void Home(){
  auto missing=Missing();
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,8);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,14));
  ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.065f,.077f,.09f,1));
  if(Ready(installation_)){
   ImGui::BeginChild("ReadyCard",ImVec2(0,88),true);
   ImGui::SetWindowFontScale(1.35f);ImGui::TextColored(ImVec4(.78f,.97f,.90f,1),"%s",T("Tudo configurado","All set"));ImGui::SetWindowFontScale(1);ImGui::SameLine(std::max(250.f,ImGui::GetContentRegionAvail().x-200));
   if(missing.empty())ImGui::TextColored(ImVec4(.45f,.9f,.73f,1),"%s",T("Prontos para jogar","Ready to play"));else ImGui::TextColored(ImVec4(1,.5f,.5f,1),"%s",T("Faltam arquivos","Missing files"));
   ImGui::TextWrapped("%s",T("Sua instalacao foi reconhecida. E so jogar.","Your installation was recognized. Just play."));
   ImGui::EndChild();
   ImGui::BeginChild("FolderCard",ImVec2(0,folder_details_?116.f:52.f),true);
   folder_details_=ImGui::TreeNode(T("Pasta do jogo e saves","Game folder and saves"));if(folder_details_){ImGui::TextWrapped("%s",U8(installation_.iso.empty()?installation_.game:installation_.iso).c_str());ImGui::TextWrapped("%s",U8(Storage(installation_.game)/"user").c_str());ImGui::TreePop();}
   ImGui::EndChild();
  }else{ImGui::Spacing();ImGui::TextUnformatted(T("Vamos preparar sua primeira luta","Let's prepare your first fight"));ImGui::TextWrapped("%s",T("Selecione Reborn, o XEX Original ou a ISO. O launcher lembra a fonte escolhida.","Select Reborn, the Original XEX or an ISO. The launcher remembers the selected source."));}
  if(ImGui::BeginTable("SourceAndDlc",2,ImGuiTableFlags_SizingStretchProp)){
   ImGui::TableSetupColumn("source",ImGuiTableColumnFlags_WidthStretch,.60f);
   ImGui::TableSetupColumn("dlc",ImGuiTableColumnFlags_WidthStretch,.40f);
   ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
   ImGui::BeginChild("SourceCard",ImVec2(0,190),true);
  ImGui::BeginDisabled(session_.Running()||Preparing()||DlcBusy());ImGui::TextUnformatted(T("Fonte do jogo","Game source"));
  if(ImGui::BeginTable("Sources",3,ImGuiTableFlags_SizingStretchSame)){
   const char* names[]={T("Reborn (pasta do jogo)","Reborn (game folder)"),T("Original (XEX ou pasta)","Original (XEX or folder)"),"ISO"};
   const char* descriptions[]={T("Pasta do Reborn.","Reborn game folder."),T("Pasta ou default.xex.","Folder or default.xex."),T("Sem extrair.","No extraction.")};
   for(int i=0;i<3;++i){ImGui::TableNextColumn();ImGui::PushID(i);ImGui::BeginChild("SourceOption",ImVec2(0,92),true);if(ImGui::RadioButton(names[i],source_index_==i))SwitchSource(i);ImGui::TextWrapped("%s",descriptions[i]);ImGui::EndChild();ImGui::PopID();}
   ImGui::EndTable();
  }
  ImGui::EndDisabled();
  ImGui::EndChild();ImGui::TableSetColumnIndex(1);ImGui::BeginChild("DlcCard",ImVec2(0,190),true);DlcSection();ImGui::EndChild();ImGui::EndTable();
  }
  ImGui::BeginChild("PrepareCard",ImVec2(0,178),true);
  ImGui::TextUnformatted(T("Preparar e jogar","Prepare and play"));
  ImGui::TextWrapped("%s",T("A ISO e lida diretamente, sem extrair. As preferencias ficam na pasta fixa do usuario; saves e cache ficam junto do jogo.","The ISO is read directly, without extraction. Preferences use your fixed user folder; saves and cache stay with the game."));
  if(Preparing()){auto total=total_bytes_.load();ImGui::ProgressBar(total?float(double(prepared_bytes_.load())/double(total)):0.f);ImGui::TextUnformatted(T("Preparando arquivos da ISO...","Preparing ISO files..."));}
  ImGui::Spacing();ImGui::BeginDisabled(session_.Running()||Preparing()||DlcBusy());
  ImGui::SetNextItemWidth(std::max(120.f,ImGui::GetContentRegionAvail().x-195));if(ImGui::InputText("##gamepath",directory_.data(),directory_.size(),ImGuiInputTextFlags_EnterReturnsTrue))Guard([&]{SelectSource(Path(directory_.data()));});
  ImGui::SameLine();
  if(ImGui::Button(T("Localizar...","Locate..."),ImVec2(170,30))){if(source_index_==2){static const SDL_DialogFileFilter filter[]={ {"ISO Xbox 360","iso"} };SDL_ShowOpenFileDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,filter,1,nullptr,false);}else if(source_index_==1){static const SDL_DialogFileFilter filter[]={ {"Executavel Xbox 360","xex"} };SDL_ShowOpenFileDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,filter,1,nullptr,false);}else SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,nullptr,false);}
  if(ImGui::Button(T("Preparar arquivos","Prepare files"),ImVec2(230,36)))Guard([&]{Prepare();});
  if(source_index_==1){ImGui::SameLine();if(ImGui::Button(T("Localizar pasta...","Locate folder..."),ImVec2(190,36)))SDL_ShowOpenFolderDialog(Selected,new std::shared_ptr<FolderResult>(picker_),nullptr,nullptr,false);}
  ImGui::EndDisabled();ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar(2);
 }
 void SteamButton(){
  ImGui::BeginDisabled(session_.Running()||Preparing()||DlcBusy());
  ImGui::Spacing();if(ImGui::Button(T("Adicionar a Steam","Add to Steam"))){profiles_=steam::Profiles();profile_index_=0;ImGui::OpenPopup(T("Conta Steam###steam-account","Steam account###steam-account"));}
  if(ImGui::BeginPopupModal(T("Conta Steam###steam-account","Steam account###steam-account"),nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
   ImGui::TextWrapped("%s",T("Feche a Steam antes de adicionar. O atalho abre o jogo diretamente e recebe as artes do UFC.","Close Steam before adding. The shortcut opens the game directly and receives the UFC art."));
   if(profiles_.empty())ImGui::TextWrapped("%s",T("Nenhuma conta Steam foi encontrada neste computador. Abra e feche a Steam uma vez.","No Steam account was found on this computer. Open and close Steam once."));
   for(size_t i=0;i<profiles_.size();++i){auto label=U8(profiles_[i].parent_path().filename());ImGui::RadioButton(label.c_str(),&profile_index_,int(i));}
   ImGui::BeginDisabled(profiles_.empty()||!Ready(installation_));if(ImGui::Button(T("Criar atalho com artes","Create shortcut with art")))Guard([&]{Save();auto id=steam::Export(bundle_,profiles_.at(size_t(profile_index_)));status_=T("Atalho UFC criado na Steam. As artes foram instaladas. ID: ","UFC shortcut created in Steam. Art installed. ID: ")+std::to_string(id);ImGui::CloseCurrentPopup();});ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button(T("Voltar","Back")))ImGui::CloseCurrentPopup();ImGui::EndPopup();
  }ImGui::EndDisabled();
 }
 void Video(){
  ImGui::Spacing();ImGui::TextUnformatted(T("Idioma","Language"));
  ImGui::SameLine();ImGui::RadioButton(T("Interface: Portugues","Interface: Portugues"),&settings_.ui_language,0);
  ImGui::SameLine();ImGui::RadioButton("Interface: English",&settings_.ui_language,1);
  ImGui::Spacing();const char* languages[]={"English","Portugues (Brasil)","Espanol","Francais","Italiano","Deutsch"};ImGui::SetNextItemWidth(300);ImGui::Combo(T("Idioma do jogo","Game language"),&settings_.game_language,languages,6);
  ImGui::TextWrapped("%s",T("Nomes do elenco Reborn preservados em ingles. Menus e textos usam o idioma escolhido.","Reborn roster names stay in English. Menus and texts use the chosen language."));
  ImGui::Spacing();ImGui::Separator();
  const char* sizes[]={"1280 x 720","1920 x 1080","2560 x 1440","3840 x 2160"};ImGui::SetNextItemWidth(300);ImGui::Combo(T("Resolucao","Resolution"),&settings_.output,sizes,4);
  ImGui::TextWrapped("%s",T("1080p usa renderizacao 2x (1440p), reduzida para 1080p; as demais usam 1x/2x/3x. Aplicado ao iniciar o jogo.","1080p renders at 2x (1440p), downsampled to 1080p; the other options use 1x/2x/3x. Applied when starting the game."));
  ImGui::Checkbox(T("Abrir o jogo em tela cheia","Open the game fullscreen"),&settings_.fullscreen);ImGui::Checkbox(T("Mostrar contador de FPS","Show FPS counter"),&settings_.fps);
  ImGui::TextWrapped("%s",T("Taxa de quadros fixa em 60 FPS: a simulacao do UFC 3 avanca um passo por quadro, entao mais quadros deixariam a luta acelerada.","Frame rate fixed at 60 FPS: UFC 3 advances its simulation one step per frame, so more frames would speed up the fight."));
  ImGui::Spacing();ImGui::Separator();
  ImGui::BeginDisabled();ImGui::Checkbox(T("Escalonamento FSR 1 (ainda nao disponivel)","FSR 1 upscaling (not available yet)"),&settings_.fsr1);ImGui::EndDisabled();
  bool safe_async=false;ImGui::BeginDisabled(true);ImGui::Checkbox(T("Shaders em segundo plano (em correcao)","Background shaders (being repaired)"),&safe_async);ImGui::EndDisabled();
  ImGui::TextWrapped("%s",T("Correcoes de fidelidade do Reborn sao aplicadas automaticamente. A compilacao em segundo plano esta desativada nesta candidata para preservar os desenhos das texturas.","Reborn fidelity fixes are applied automatically. Background compilation is disabled in this candidate to preserve texture draws."));
 }
 void Keyboard(){
  ImGui::Spacing();ImGui::Checkbox(T("Usar teclado","Use keyboard"),&settings_.keyboard);ImGui::SameLine();ImGui::Checkbox(T("Mouse no analogico direito","Mouse on right stick"),&settings_.mouse);ImGui::SetNextItemWidth(240);ImGui::SliderFloat(T("Sensibilidade do mouse","Mouse sensitivity"),&settings_.sensitivity,.05f,5.f,"%.2f");
  ImGui::TextWrapped("%s",T("Use nomes como Space, Return, Up, Shift+Up. Separe alternativas com virgula. Um controle compativel tambem pode ser usado.","Use names like Space, Return, Up, Shift+Up. Separate alternatives with commas. A compatible controller can also be used."));
  static const char* labels_pt[]={"A / confirmar","B / voltar","X","Y","Gatilho esquerdo","Gatilho direito","LB","RB","Analogico E: cima","Analogico E: baixo","Analogico E: esquerda","Analogico E: direita","Analogico E: clique","Analogico D: clique","Analogico D: cima","Analogico D: baixo","Analogico D: esquerda","Analogico D: direita","Direcional: cima","Direcional: baixo","Direcional: esquerda","Direcional: direita","Back","Start / pausa"};
  static const char* labels_en[]={"A / confirm","B / back","X","Y","Left trigger","Right trigger","LB","RB","Left stick: up","Left stick: down","Left stick: left","Left stick: right","Left stick: press","Right stick: press","Right stick: up","Right stick: down","Right stick: left","Right stick: right","D-pad: up","D-pad: down","D-pad: left","D-pad: right","Back","Start / pause"};
  const auto* labels=UiEnglish()?labels_en:labels_pt;
  if(ImGui::BeginTable("keys",4,ImGuiTableFlags_SizingFixedFit)){for(size_t row=0;row<12;++row){ImGui::TableNextRow();for(size_t col=0;col<2;++col){auto i=row+col*12;ImGui::TableSetColumnIndex(int(col*2));ImGui::TextUnformatted(labels[i]);ImGui::TableSetColumnIndex(int(col*2+1));ImGui::SetNextItemWidth(200);ImGui::PushID(int(i));ImGui::InputText("##key",binds_[i].data(),binds_[i].size());ImGui::PopID();}}ImGui::EndTable();}
  if(ImGui::Button(T("Restaurar teclas padrao","Restore default keys"))){for(size_t i=0;i<binds_.size();++i)std::snprintf(binds_[i].data(),binds_[i].size(),"%s",BindDefaults[i]);}
 }
 void Saves(){
  ImGui::Spacing();ImGui::TextWrapped("%s",T("As preferencias ficam na pasta fixa do usuario. Seus saves, cache e registros ficam na pasta do jogo. Atualizar o launcher ou AppImage conserva esses dados.","Preferences use your fixed user folder. Saves, cache and logs stay in the game folder. Updating the launcher or AppImage keeps this data."));ImGui::TextWrapped("%s",U8(RegistryDirectory()/"config").c_str());ImGui::Spacing();
  ImGui::BeginDisabled(!Ready(installation_)||session_.Running());
  if(ImGui::Button(T("Abrir pasta de saves","Open saves folder")))Guard([&]{OpenFolder(Storage(installation_.game)/"user");});ImGui::SameLine();
  if(ImGui::Button(T("Criar copia dos saves","Back up saves")))Guard([&]{auto root=Storage(installation_.game),dest=root/"backups"/("saves-"+Stamp());if(!fs::is_directory(root/"user"))throw std::runtime_error(T("Esta instalacao ainda nao tem saves.","This installation has no saves yet."));fs::create_directories(dest.parent_path());fs::copy(root/"user",dest,fs::copy_options::recursive);status_=std::string(T("Copia criada em ","Backup created in "))+U8(dest);});
  ImGui::SameLine();if(ImGui::Button(T("Limpar cache (recuperavel)","Clear cache (recoverable)")))Guard([&]{ToRecycleBin(Storage(installation_.game)/"cache");status_=T("Cache removido e preservado para recuperacao.","Cache removed and preserved for recovery.");});
  if(ImGui::Button(T("Abrir todos os registros","Open all logs")))Guard([&]{OpenFolder(Storage(installation_.game)/"logs");});ImGui::SameLine();if(ImGui::Button(T("Abrir cache","Open cache")))Guard([&]{OpenFolder(Storage(installation_.game)/"cache");});ImGui::EndDisabled();
  ImGui::Spacing();ImGui::Separator();
  ImGui::Checkbox(T("Gerar relatorio de diagnostico em cada partida (desligado por padrao)","Write a diagnostic report for each match (off by default)"),&settings_.diagnostics);
  ImGui::TextWrapped("%s",T("Ligado: guarda um relatorio por sessao. Desligado: conserva apenas registros basicos com tamanho limitado em latest-reborn ou latest-original.","On: keeps one report per session. Off: keeps only size-limited basic logs in latest-reborn or latest-original."));
  if(!measured_.empty())ImGui::TextWrapped("%s %s",T("Ultima sessao:","Last session:"),measured_.c_str());
  if(!last_session_.empty()){ImGui::TextWrapped("%s %s",T("Ultima sessao:","Last session:"),last_session_.c_str());if(ImGui::Button(T("Abrir ultimo relatorio","Open last report")))Guard([&]{OpenFolder(Path(last_session_));});}
 }
 void OnDraw(ImGuiIO& io)override{
  UiEnglish()=settings_.ui_language==1;
  if(Preparing()&&preparation_.wait_for(std::chrono::seconds(0))==std::future_status::ready){Guard([&]{auto game=preparation_.get();UseFolder(game);Prepare();});}

  {std::string chosen;{std::lock_guard lock(picker_->mutex);chosen.swap(picker_->chosen);}if(!chosen.empty())Guard([&]{SelectSource(Path(chosen));});}
  {std::string chosen;{std::lock_guard lock(dlc_picker_->mutex);chosen.swap(dlc_picker_->chosen);}if(!chosen.empty()&&!DlcBusy())Guard([&]{StartDlc(Path(chosen));});}
  if(DlcBusy()&&dlc_job_.wait_for(std::chrono::seconds(0))==std::future_status::ready){
   Guard([&]{auto report=dlc_job_.get();char line[160];std::snprintf(line,sizeof line,T("DLC: %d instalada(s), %d ja estava(m) instalada(s).","DLC: %d installed, %d already installed."),report.installed,report.already);status_=line;});
   if(dlc_job_.valid())dlc_job_={};RefreshDlc();
  }
  bool running=session_.Running();
  if(previous_running_&&!running){
   last_session_=settings_.diagnostics?U8(session_.Directory()):std::string{};
   double fps=settings_.diagnostics?MeasuredFps(session_.Directory()):-1;char line[200];
   if(fps>0)std::snprintf(line,sizeof line,T("Taxa recente registrada: %.1f FPS (pode incluir menus).","Recent logged rate: %.1f FPS (may include menus)."),fps);
   else std::snprintf(line,sizeof line,"%s",T("Ative o diagnostico para registrar a taxa de quadros da proxima sessao.","Enable diagnostics to record the frame rate of the next session."));
   measured_=line;
   status_=session_.ExitCode()==0?T("Jogo fechado.","Game closed."):T("O jogo terminou com erro. Os registros foram preservados.","The game ended with an error. Logs were kept.");
  }
  previous_running_=running;
  // Item 2: autosave. Any change is written as soon as the game folder is registered.
  SyncBinds();
  if(!running){auto now=settings_.Text();if(now!=saved_){Guard([&]{settings_.Save(installation_.game);saved_=now;status_=T("Alteracoes salvas automaticamente.","Changes saved automatically.");});}}
  ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);ImGui::SetNextWindowSize(io.DisplaySize,ImGuiCond_Always);ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(30,18));ImGui::Begin("###ufc_setup",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);DrawHeader(io);
  // Item 4: fixed body height so the tabs do not move when content changes.
  ImGui::BeginChild("LauncherBody",ImVec2(0,std::max(150.f,ImGui::GetContentRegionAvail().y-88)),false);
  if(ImGui::BeginTabBar("Configurador")){
   if(ImGui::BeginTabItem(T("Inicio###home","Home###home"))){Home();ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(T("Video e idioma###video","Video and language###video"))){Video();ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(T("Teclado e controle###input","Keyboard and controller###input"))){Keyboard();ImGui::EndTabItem();}
   if(ImGui::BeginTabItem(T("Saves e registros###saves","Saves and logs###saves"))){Saves();ImGui::EndTabItem();}
   ImGui::EndTabBar();
  }
  ImGui::EndChild();ImGui::Separator();
  ImGui::TextWrapped("%s",running?T("Partida aberta. Ao terminar, feche a janela do jogo para concluir o relatorio.","Match open. When you finish, close the game window."):status_.c_str());
  SteamButton();ImGui::SameLine();
  ImGui::BeginDisabled(running||Preparing()||DlcBusy()||!prepared_||!Ready(installation_)||!Missing().empty());
  ImGui::SetCursorPosX(std::max(240.f,io.DisplaySize.x-285));
  if(ImGui::Button(T("JOGAR","PLAY"),ImVec2(220,42)))Guard([&]{Prepare();Save();session_.Start(bundle_,installation_,settings_);status_=T("Jogo iniciado.","Game started.");});
  ImGui::EndDisabled();ImGui::End();ImGui::PopStyleVar(2);
 }
 public:
 ~Launcher()override{if(preparation_.valid())preparation_.wait();if(dlc_job_.valid())dlc_job_.wait();}
 Launcher(rex::ui::ImGuiDrawer* drawer,rex::ui::ImmediateDrawer* texture_drawer,fs::path bundle):ImGuiDialog(drawer),texture_drawer_(texture_drawer),bundle_(std::move(bundle)){try{settings_.Load({});installation_=Discover(bundle_);if(!installation_.iso.empty()){auto iso=installation_.iso;source_index_=2;source_paths_[2]=iso;SelectSource(iso);}else if(Ready(installation_)){UseFolder(installation_.game,installation_.legacy_data);source_index_=original_?1:0;source_paths_[source_index_]=installation_.game;prepared_=Missing().empty();}else status_=T("Escolha a pasta do Reborn para comecar.","Choose the Reborn folder to start.");}catch(const std::exception& e){status_=e.what();}auto remembered=ReadIni(RegistryDirectory()/"sources.ini");for(int i=0;i<3;++i)if(remembered.contains("path"+std::to_string(i)))source_paths_[i]=Path(remembered["path"+std::to_string(i)]);if(remembered.contains("selected")){try{SwitchSource(std::clamp(std::stoi(remembered["selected"]),0,2));}catch(const std::exception& e){status_=e.what();}}CopySettings();saved_=settings_.Text();}
 bool Running()const{return session_.Running()||Preparing()||DlcBusy();}
};
std::shared_ptr<Launcher> CreateLauncher(rex::ui::ImGuiDrawer* drawer,rex::ui::ImmediateDrawer* texture_drawer,const fs::path& bundle){return std::make_shared<Launcher>(drawer,texture_drawer,bundle);}
bool LauncherRunning(const Launcher* launcher){return launcher&&launcher->Running();}
void DestroyLauncher(std::shared_ptr<Launcher>& launcher){launcher.reset();}
}
