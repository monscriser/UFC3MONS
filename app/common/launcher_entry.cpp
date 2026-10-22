#include "ufc_session.h"
#include <iostream>
#ifdef _WIN32
#include <shellapi.h>
#else
#include <signal.h>
#endif
namespace {
int Run(const std::vector<std::string>& options,const ufc::fs::path& root){
 auto bundle=root/"data";bool play=false,configure=false;ufc::fs::path explicit_game;
#ifdef _WIN32
 play=true;
#endif
 for(size_t i=0;i<options.size();++i){const auto& a=options[i];if(a=="--play")play=true;else if(a=="--configure")configure=true;else if(a=="--game"&&i+1<options.size())explicit_game=ufc::Path(options[++i]);else if(a=="--help"){std::cout<<"UFC3MONS [--play] [--configure] [--game PASTA]\n";return 0;}else throw std::runtime_error("Opcao desconhecida: "+a);}
 auto installation=explicit_game.empty()?ufc::Discover(bundle):ufc::Installation{ufc::fs::absolute(explicit_game),{}};
 if(ufc::Ready(installation))ufc::Register(installation);
 if(configure||!play||!ufc::Ready(installation)){
  auto exe=ufc::GameBinary(bundle);if(!ufc::fs::is_regular_file(exe))throw std::runtime_error("Extraia o pacote completo, incluindo a pasta data.");
#ifdef _WIN32
  auto command=ufc::Quote(exe.wstring())+L" --gpu_plugin=xenos --ufc_configurator=true --fullscreen=false --window_width=1280 --window_height=960";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,bundle.c_str(),&startup,&process))throw std::runtime_error("Nao foi possivel abrir o configurador.");CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
#else
  auto path=ufc::U8(exe);chdir(bundle.c_str());execl(path.c_str(),path.c_str(),"--gpu_plugin=xenos","--ufc_configurator=true","--fullscreen=false","--window_width=1280","--window_height=960",nullptr);throw std::runtime_error("Nao foi possivel abrir o configurador.");
#endif
 }
 ufc::Settings settings;settings.Load(installation.game);ufc::Session session;
#ifndef _WIN32
 ufc::InstallSignalForwarding();
#endif
 session.Start(bundle,installation,settings);session.Wait();return session.ExitCode();
}
}
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){try{wchar_t filename[32768];GetModuleFileNameW(nullptr,filename,32768);int count=0;auto** args=CommandLineToArgvW(GetCommandLineW(),&count);std::vector<std::string> options;for(int i=1;i<count;++i)options.push_back(ufc::U8(ufc::fs::path(args[i])));LocalFree(args);auto path=ufc::fs::path(filename);if(CompareStringOrdinal(path.filename().c_str(),-1,L"Launcher-Config.exe",-1,TRUE)==CSTR_EQUAL)options.push_back("--configure");return Run(options,path.parent_path());}catch(const std::exception& e){MessageBoxW(nullptr,ufc::Wide(e.what()).c_str(),L"UFC3MONS",MB_OK|MB_ICONERROR);return 1;}}
#else
int main(int argc,char** argv){try{std::vector<std::string> options;for(int i=1;i<argc;++i)options.emplace_back(argv[i]);return Run(options,ufc::fs::canonical("/proc/self/exe").parent_path());}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
