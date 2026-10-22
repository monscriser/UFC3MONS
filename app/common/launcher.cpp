#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>

#include "session_recorder.h"
namespace fs = std::filesystem;
static fs::path root, ini, session;
static HWND main_window, folder_edit, sensitivity_edit, fps_check, full_check, keys_check,
    mouse_check, cp_check, status_label, play_button;
static HFONT font, title_font;
static HBRUSH background;
static HANDLE game_process = nullptr;
static bool configuring = false;
static int dpi = 96;
struct Bind { const wchar_t* name; const wchar_t* label; const wchar_t* value; HWND edit{}; };
static std::array<Bind, 24> binds{{
 {L"a", L"A / confirmar", L"Semicolon,Space"}, {L"b", L"B / voltar", L"Quote,Backspace"},
 {L"x", L"X", L"L"}, {L"y", L"Y", L"P"},
 {L"left_trigger", L"Gatilho esquerdo", L"Q,I"}, {L"right_trigger", L"Gatilho direito", L"E,O"},
 {L"left_shoulder", L"LB", L"1"}, {L"right_shoulder", L"RB", L"3"},
 {L"lstick_up", L"Analógico E: cima", L"W"}, {L"lstick_down", L"Analógico E: baixo", L"S"},
 {L"lstick_left", L"Analógico E: esquerda", L"A"}, {L"lstick_right", L"Analógico E: direita", L"D"},
 {L"lstick_press", L"Analógico E: clique", L"F"}, {L"rstick_press", L"Analógico D: clique", L"K"},
 {L"rstick_up", L"Analógico D: cima", L"Up"}, {L"rstick_down", L"Analógico D: baixo", L"Down"},
 {L"rstick_left", L"Analógico D: esquerda", L"Left"}, {L"rstick_right", L"Analógico D: direita", L"Right"},
 {L"dpad_up", L"Direcional: cima", L"Shift+Up"}, {L"dpad_down", L"Direcional: baixo", L"Shift+Down"},
 {L"dpad_left", L"Direcional: esquerda", L"Shift+Left"}, {L"dpad_right", L"Direcional: direita", L"Shift+Right"},
 {L"back", L"Back", L"Z,Tab"}, {L"start", L"Start / pausa", L"X,Return"}
}};
int S(int v) { return MulDiv(v, dpi, 96); }
std::wstring Read(const wchar_t* key, const wchar_t* fallback, const wchar_t* section=L"Launcher") {
  wchar_t value[32768]; GetPrivateProfileStringW(section,key,fallback,value,32768,ini.c_str()); return value;
}
void Write(const wchar_t* key, const std::wstring& value, const wchar_t* section=L"Launcher") {
  if (!WritePrivateProfileStringW(section,key,value.c_str(),ini.c_str()))
    throw std::runtime_error("Não foi possível salvar as opções em data/launcher.ini.");
}
std::wstring Text(HWND control) {
  int n=GetWindowTextLengthW(control); std::wstring text(n+1,L'\0');
  GetWindowTextW(control,text.data(),n+1); text.resize(n); return text;
}
HWND Control(const wchar_t* klass, const std::wstring& text, DWORD style, int x,int y,int w,int h,int id=0) {
  auto result=CreateWindowExW(klass==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0, klass,text.c_str(),
      WS_CHILD|WS_VISIBLE|style,S(x),S(y),S(w),S(h),main_window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
  SendMessageW(result,WM_SETFONT,(WPARAM)font,TRUE); return result;
}
void Label(const std::wstring& text,int x,int y,int w,int h=24) { Control(L"STATIC",text,0,x,y,w,h); }
HWND Check(const wchar_t* text,int x,int y,int w,int id,const wchar_t* key,bool default_value) {
  HWND control=Control(L"BUTTON",text,BS_AUTOCHECKBOX|WS_TABSTOP,x,y,w,28,id);
  SendMessageW(control,BM_SETCHECK,Read(key,default_value?L"1":L"0")==L"1"?BST_CHECKED:BST_UNCHECKED,0);
  return control;
}
bool Checked(HWND control) { return SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED; }
std::wstring Quote(const std::wstring& arg) {
  std::wstring out=L"\""; size_t slashes=0;
  for (wchar_t c:arg) {
    if (c==L'\\') {++slashes; continue;}
    if (c==L'\"') {out.append(slashes*2+1,L'\\');out+=c;}
    else {out.append(slashes,L'\\');out+=c;} slashes=0;
  }
  out.append(slashes*2,L'\\');out+=L'\"';return out;
}
std::wstring Hash(const fs::path& file) {
  BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
  if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return {};
  if (BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) {
    BCryptCloseAlgorithmProvider(algorithm,0);return {};
  }
  std::ifstream stream(file,std::ios::binary); std::array<char,65536> buffer;
  bool ok=stream.is_open();
  while (stream && ok) {
    stream.read(buffer.data(),buffer.size()); auto count=stream.gcount();
    if(count && BCryptHashData(hash,(PUCHAR)buffer.data(),(ULONG)count,0)<0) ok=false;
  }
  ok=ok && stream.eof(); std::array<UCHAR,32> digest{};
  if(BCryptFinishHash(hash,digest.data(),digest.size(),0)<0) ok=false;
  BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
  if(!ok) return {}; std::wstring result;
  for(auto byte:digest) {result+=L"0123456789abcdef"[byte>>4];result+=L"0123456789abcdef"[byte&15];}
  return result;
}
bool OtherGame() {
  HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snapshot==INVALID_HANDLE_VALUE) throw std::runtime_error("Não foi possível verificar jogos abertos.");
  PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
  if(Process32FirstW(snapshot,&entry)) do {
    std::wstring name=entry.szExeFile;std::transform(name.begin(),name.end(),name.begin(),::towlower);
    if(name.starts_with(L"ufc3") && name.ends_with(L".exe")) {found=true;break;}
  } while(Process32NextW(snapshot,&entry));
  CloseHandle(snapshot);return found;
}
void SaveOptions() {
  Write(L"game",Text(folder_edit));Write(L"fps",Checked(fps_check)?L"1":L"0");
  Write(L"fullscreen",Checked(full_check)?L"1":L"0");Write(L"keyboard",Checked(keys_check)?L"1":L"0");
  Write(L"mouse",Checked(mouse_check)?L"1":L"0");
  Write(L"cpu_optimization",Checked(cp_check)?L"1":L"0");
  auto value=Text(sensitivity_edit); wchar_t* end=nullptr;double n=wcstod(value.c_str(),&end);
  if(end==value.c_str() || *end || !std::isfinite(n) || n<0.01 || n>20)
    throw std::runtime_error("A sensibilidade deve ser um número de 0.01 a 20, com ponto decimal.");
  Write(L"sensitivity",value);
}
void Browse() {
  IFileOpenDialog* dialog=nullptr;
  if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) return;
  DWORD flags;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
  dialog->SetTitle(L"Selecione a pasta do Reborn que contém default.xex");
  if(SUCCEEDED(dialog->Show(main_window))) {
    IShellItem* item=nullptr;
    if(SUCCEEDED(dialog->GetResult(&item))) {
      PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
        SetWindowTextW(folder_edit,path);CoTaskMemFree(path);
      }item->Release();
    }
  }dialog->Release();
}
void Open(const fs::path& file,const wchar_t* args=nullptr) {
  if((INT_PTR)ShellExecuteW(main_window,L"open",file.c_str(),args,root.c_str(),SW_SHOWNORMAL)<=32)
    throw std::runtime_error("Não foi possível abrir o arquivo solicitado.");
}
void Launch() {
  if(OtherGame()) throw std::runtime_error("Já existe um UFC aberto. Feche-o antes de iniciar outra sessão.");
  SaveOptions();
  fs::path game=Text(folder_edit), data=root/L"data", exe=data/L"ufc3-v31.exe";
  if(Hash(game/L"default.xex")!=L"1af0740ac854705aac209405129af6917efb03276330376b93350cfb220782e1")
    throw std::runtime_error("Selecione a cópia Reborn usada na v26. O default.xex está ausente ou é de outra versão.");
  if(!fs::exists(exe)) throw std::runtime_error("data/ufc3-v31.exe não foi encontrado.");
  SYSTEMTIME t;GetLocalTime(&t);wchar_t stamp[80];
  swprintf_s(stamp,L"manual-v31-%04u%02u%02u-%02u%02u%02u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
  session=fs::path(Read(L"evidence_root",(data/L"logs").c_str()))/stamp;fs::create_directories(session);
  std::vector<std::wstring> args{exe.wstring(),L"--gpu_plugin=xenos",L"--game_data_root="+game.wstring(),
      L"--log_file="+(session/L"ufc3.log").wstring(),L"--log_level=info",
      Checked(fps_check)?L"--ufc_show_fps=true":L"--ufc_show_fps=false",
      Checked(cp_check)?L"--ufc_cp_fast_registers=true":L"--ufc_cp_fast_registers=false",
      Checked(full_check)?L"--fullscreen=true":L"--fullscreen=false",
      Checked(keys_check)?L"--mnk_mode=true":L"--mnk_mode=false",
      Checked(mouse_check)?L"--mnk_mouse=true":L"--mnk_mouse=false",L"--mnk_sensitivity="+Text(sensitivity_edit)};
  for(const auto& b:binds) args.push_back(L"--keybind_"+std::wstring(b.name)+L"="+Read(b.name,b.value,L"Bindings"));
  std::wstring command;
  for(const auto& arg:args) {if(!command.empty()) command+=L' ';command+=Quote(arg);}
  std::wofstream manifest(session/L"launch.txt");
  manifest<<L"version=v31\nexe_sha256="<<Hash(exe)<<L"\nxex_sha256="<<Hash(game/L"default.xex")<<L"\n"<<command<<L"\n";
  for(auto name:{L"librexruntime.dll",L"rexgpu-xenos.dll",L"libc++.dll",L"libunwind.dll"})
    manifest<<name<<L"="<<Hash(data/name)<<L"\n";
  manifest.close();
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,data.c_str(),&startup,&process))
    throw std::runtime_error("O Windows não conseguiu iniciar o jogo. Verifique as DLLs em data.");
  game_process=process.hProcess;CloseHandle(process.hThread);
  ufc_record::Start(game_process,session);
  EnableWindow(play_button,FALSE);SetWindowTextW(status_label,L"Jogo aberto. Ao terminar, feche a janela do jogo.");
  SetTimer(main_window,1,500,nullptr);ShowWindow(main_window,SW_MINIMIZE);
}
void BuildUI() {
  if(configuring) {
    auto title=Control(L"STATIC",L"Seu teclado, seus comandos",0,24,20,700,34);SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
    Label(L"Use nomes como Space, Return, Up, Shift+Up. Separe alternativas com vírgula.",24,63,720);
    for(size_t i=0;i<binds.size();++i) {
      int col=(int)i/12,row=(int)i%12,x=24+col*365,y=105+row*35;
      Label(binds[i].label,x,y+4,185);
      binds[i].edit=Control(L"EDIT",Read(binds[i].name,binds[i].value,L"Bindings"),WS_TABSTOP|ES_AUTOHSCROLL,x+190,y,150,27);
      SendMessageW(binds[i].edit,EM_SETLIMITTEXT,240,0);
    }
    Label(L"O mouse controla o analógico direito quando ativado no launcher.",24,536,720);
    Control(L"BUTTON",L"Salvar teclas",WS_TABSTOP|BS_DEFPUSHBUTTON,565,575,175,38,201);
    Control(L"BUTTON",L"Restaurar padrões",WS_TABSTOP,24,575,200,38,202);
    return;
  }
  auto title=Control(L"STATIC",L"UFC UNDISPUTED 3",0,28,22,700,36);SendMessageW(title,WM_SETFONT,(WPARAM)title_font,TRUE);
  Label(L"REBORN  /  PORT NATIVO WINDOWS  /  v31",30,64,700);
  Label(L"Pasta do jogo",30,115,650);
  folder_edit=Control(L"EDIT",Read(L"game",L"E:\\UFC3-RexGlue-Lab\\baseline\\game"),WS_TABSTOP|ES_AUTOHSCROLL,30,145,558,32);
  Control(L"BUTTON",L"Escolher…",WS_TABSTOP,600,145,125,32,101);
  Label(L"Selecione a pasta que contém default.xex da sua cópia Reborn v26.",30,188,700);
  fps_check=Check(L"Mostrar contador de FPS",30,239,340,102,L"fps",true);
  full_check=Check(L"Abrir em tela cheia",390,239,330,103,L"fullscreen",false);
  keys_check=Check(L"Usar teclado",30,282,340,104,L"keyboard",true);
  mouse_check=Check(L"Mouse no analógico direito",390,282,330,105,L"mouse",true);
  Label(L"Sensibilidade do mouse",30,337,230);
  sensitivity_edit=Control(L"EDIT",Read(L"sensitivity",L"1.0"),WS_TABSTOP|ES_AUTOHSCROLL,265,332,95,30);
  SendMessageW(sensitivity_edit,EM_SETLIMITTEXT,16,0);
  Control(L"BUTTON",L"Configurar teclas",WS_TABSTOP,390,332,335,32,106);
  cp_check=Check(L"Otimização de CPU do Hydro (experimental)",30,365,690,110,L"cpu_optimization",false);
  Label(L"Durante o jogo: F6 mostra/oculta FPS · F4 abre as configurações.",30,395,700);
  Label(L"FPS conta os quadros enviados pelo jogo, sem geração de quadros.",30,425,700);
  status_label=Control(L"STATIC",L"Pronto para jogar. Som ainda não validado pelo Ian.",0,30,475,700,50);
  Control(L"BUTTON",L"Abrir registros",WS_TABSTOP,30,549,180,42,107);
  Control(L"BUTTON",L"Salvar opções",WS_TABSTOP,225,549,180,42,108);
  play_button=Control(L"BUTTON",L"JOGAR  v31",WS_TABSTOP|BS_DEFPUSHBUTTON,465,540,260,56,109);
}
LRESULT CALLBACK WndProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  switch(message) {
    case WM_CREATE: main_window=window;BuildUI();return 0;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
      SetTextColor((HDC)w,RGB(235,237,241));SetBkColor((HDC)w,RGB(25,28,34));return (LRESULT)background;
    case WM_COMMAND:
      try {
        switch(LOWORD(w)) {
          case 101:Browse();break;
          case 106:SaveOptions();Open(root/L"UFC-Configurador-v31.exe");break;
          case 107:{auto logs=fs::path(Read(L"evidence_root",(root/L"data"/L"logs").c_str()));fs::create_directories(logs);Open(logs);break;}
          case 108:SaveOptions();SetWindowTextW(status_label,L"Opções salvas.");break;
          case 109:Launch();break;
          case 201:
            for(auto& b:binds) Write(b.name,Text(b.edit),L"Bindings");
            MessageBoxW(window,L"Teclas salvas. Elas serão usadas na próxima abertura do jogo.",L"UFC 3",MB_OK);break;
          case 202:for(auto& b:binds)SetWindowTextW(b.edit,b.value);break;
        }
      } catch(const std::exception& e) {
        int n=MultiByteToWideChar(CP_UTF8,0,e.what(),-1,nullptr,0);std::wstring msg(n,L'\0');
        MultiByteToWideChar(CP_UTF8,0,e.what(),-1,msg.data(),n);MessageBoxW(window,msg.c_str(),L"UFC 3 — atenção",MB_OK|MB_ICONWARNING);
      }return 0;
    case WM_TIMER:
      if(game_process && WaitForSingleObject(game_process,0)==WAIT_OBJECT_0) {
        DWORD code;GetExitCodeProcess(game_process,&code);ufc_record::Finish(session,(int)code);
        CloseHandle(game_process);game_process=nullptr;
        std::ofstream(session/L"exit.txt")<<"exit_code="<<code<<"\n";
        KillTimer(window,1);EnableWindow(play_button,TRUE);
        SetWindowTextW(status_label,code==0?L"Jogo fechado. Registros salvos.":L"O jogo terminou com erro. Registros salvos para análise.");
        ShowWindow(window,SW_RESTORE);
      }return 0;
    case WM_CLOSE: if(game_process){ShowWindow(window,SW_MINIMIZE);return 0;}DestroyWindow(window);return 0;
    case WM_DESTROY: if(game_process)CloseHandle(game_process);PostQuitMessage(0);return 0;
  }
  return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);root=fs::path(path).parent_path();
  configuring=fs::path(path).filename().wstring().find(L"Configurador")!=std::wstring::npos;
  HANDLE singleton=CreateMutexW(nullptr,FALSE,configuring?L"Local\\Ufc3ConfiguratorV29":L"Local\\Ufc3LauncherV29");
  if(!singleton || GetLastError()==ERROR_ALREADY_EXISTS) return 0;
  fs::create_directories(root/L"data");ini=root/L"data"/L"launcher.ini";
  dpi=GetDpiForSystem();
  font=CreateFontW(-S(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
  title_font=CreateFontW(-S(27),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
  background=CreateSolidBrush(RGB(25,28,34));
  WNDCLASSW klass{};klass.lpfnWndProc=WndProc;klass.hInstance=instance;klass.lpszClassName=L"Ufc3LauncherV29";
  klass.hbrBackground=background;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassW(&klass);
  RECT rect{0,0,S(765),S(635)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
  AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
  HWND window=CreateWindowW(klass.lpszClassName,configuring?L"UFC 3 — Configurar teclas v31":L"UFC 3 Reborn — Launcher v31",
      style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
  ShowWindow(window,show);MSG message;
  while(GetMessageW(&message,nullptr,0,0)>0) {if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
  DeleteObject(font);DeleteObject(title_font);DeleteObject(background);CloseHandle(singleton);CoUninitialize();return 0;
}
