#pragma once
#include "ufc_localization.h"
#include "ufc_resolution.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <shellapi.h>
#else
#include <sys/file.h>
#include <signal.h>
#endif
namespace ufc {
#ifndef _WIN32
inline volatile sig_atomic_t signal_child=0;
inline void ForwardSignal(int value){if(signal_child>0)kill(signal_child,value);}
inline void InstallSignalForwarding(){signal(SIGTERM,ForwardSignal);signal(SIGINT,ForwardSignal);}
#endif
inline std::string Stamp(){auto now=std::chrono::system_clock::now();auto t=std::chrono::system_clock::to_time_t(now);std::ostringstream out;out<<std::put_time(std::localtime(&t),"%Y%m%d-%H%M%S")<<'-'<<std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();return out.str();}
inline fs::path GameBinary(const fs::path& bundle,bool original=false){
#ifdef _WIN32
 return bundle/(original?"ufc3-original-v48.exe":"ufc3-launcher-l1.exe");
#else
 return bundle/(original?"ufc3-original-v48":"ufc3-launcher-l1");
#endif
}
inline void OpenFolder(const fs::path& path){fs::create_directories(path);
#ifdef _WIN32
 ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
#else
 auto name=U8(path);pid_t pid;char command[]="xdg-open";char* args[]={command,name.data(),nullptr};posix_spawnp(&pid,command,nullptr,nullptr,args,environ);
#endif
}
inline std::wstring Wide(const std::string& s){
#ifdef _WIN32
 int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;
#else
 return {};
#endif
}
#ifdef _WIN32
inline std::wstring Quote(const std::wstring& arg){std::wstring result=L"\"";size_t slashes=0;for(auto c:arg){if(c==L'\\'){++slashes;continue;}if(c==L'\"')result.append(slashes*2+1,L'\\');else result.append(slashes,L'\\');result+=c;slashes=0;}result.append(slashes*2,L'\\');result+=L'\"';return result;}
#endif
// Item 3: the real frame rate of the last match, from the game's own [ufc-fps] lines.
// Median of the last 30 samples; -1 when there are too few samples to trust.
inline double MeasuredFps(const fs::path& directory){
 std::istringstream lines(ReadText(directory/"ufc3.log"));std::string line;std::vector<double> values;
 while(std::getline(lines,line)){if(line.find("[ufc-fps]")==std::string::npos)continue;auto at=line.find(" fps=");if(at==std::string::npos)continue;try{values.push_back(std::stod(line.substr(at+5)));}catch(...){}}
 if(values.size()<5)return -1;
 std::vector<double> tail(values.end()-std::min<size_t>(values.size(),30),values.end());std::sort(tail.begin(),tail.end());return tail[tail.size()/2];
}
class Session{
 std::thread waiter_;std::atomic<bool> running_{false};std::atomic<int> code_{-1};fs::path directory_;bool diagnostics_=false;
 public:
 ~Session(){Wait();}
 bool Running()const{return running_.load();}int ExitCode()const{return code_.load();}fs::path Directory()const{return directory_;}
 void Wait(){if(waiter_.joinable())waiter_.join();}
 void Start(const fs::path& bundle,const Installation& installation,const Settings& settings){
  if(Running())throw std::runtime_error("Ja existe uma partida aberta por este launcher.");Wait();Register(installation);settings.Save(installation.game);
  bool original=!installation.iso.empty()||Hash(installation.game/"default.xex")==OriginalSHA;
  if(!original)PrepareLocalizedNames(installation.game,settings.game_language);
  diagnostics_=settings.diagnostics;auto root=Storage(installation.game);auto exe=GameBinary(bundle,original);if(!fs::is_regular_file(exe))throw std::runtime_error("O executavel nativo nao acompanha este launcher. Extraia o pacote completo.");
#ifdef _WIN32
  // Item 13: check the runtime files before starting, so a missing DLL is reported here and not as a silent crash.
  for(auto name:{"librexruntime.dll","rexgpu-xenos.dll","libc++.dll","libunwind.dll"})if(!fs::is_regular_file(bundle/name))throw std::runtime_error(std::string("Arquivo ausente na pasta data: ")+name+". Extraia o pacote completo.");
#endif
#ifdef _WIN32
  HANDLE lock=CreateFileW((root/"game.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Outra sessao esta usando esta instalacao. Feche-a antes de jogar.");
  const char* platform="windows";
#else
  int lock=open((root/"game.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);throw std::runtime_error("Outra sessao esta usando esta instalacao.");}
  const char* platform="linux";
#endif
  directory_=root/"logs"/(settings.diagnostics ? std::string(platform)+"-launcher-L1-"+Stamp() : std::string("latest-")+(original?"original":"reborn"));fs::create_directories(directory_);

  std::vector<std::string> args={U8(exe),"--gpu_plugin=xenos","--ufc_configurator=false","--game_data_root="+U8(installation.game),"--log_file="+U8(directory_/"ufc3.log"),"--log_level=info","--readback_resolve=full","--ufc_cp_fast_registers=false",settings.fullscreen?"--fullscreen=true":"--fullscreen=false",settings.fps?"--ufc_show_fps=true":"--ufc_show_fps=false","--vsync=true",settings.keyboard?"--mnk_mode=true":"--mnk_mode=false",settings.mouse?"--mnk_mouse=true":"--mnk_mouse=false","--mnk_sensitivity="+std::to_string(settings.sensitivity)};
  AppendResolutionArguments(args,settings.output);
  if(!installation.iso.empty())args.push_back("--ufc_iso_path="+U8(installation.iso));
  // L2: installed DLC is only usable when its license is active (same as Xenia's license_mask).
  {std::error_code error;auto dlc=Storage(installation.game)/"user"/(original?"Original-static-v48":"Reborn-static-r9")/"0000000000000000"/"5451087D"/"00000002";
   if(fs::is_directory(dlc,error)&&!fs::is_empty(dlc,error))args.push_back("--license_mask=4294967295");}
  // Fixed: the game simulation is frame-locked (measured v71), more frames speed the fight up.
  args.push_back("--ufc_fps_limit=60");
  // The preserving synchronous backend is required until async fidelity is validated.
  args.push_back(std::string("--ufc_async_shaders=")+("false"));
  args.push_back(settings.diagnostics?"--ufc_shadow_diagnostics=true":"--ufc_shadow_diagnostics=false");
  args.push_back("--ufc_shadow_precise=false");
  if(!settings.diagnostics){args.push_back("--log_max_file_size_mb=4");args.push_back("--log_max_files=2");}
  if(settings.diagnostics)args.push_back("--ufc_language_diagnostics=true");
  args.push_back("--user_language="+std::to_string(LanguageIds[std::clamp(settings.game_language,0,5)]));
  for(size_t i=0;i<settings.binds.size();++i)args.push_back(std::string("--keybind_")+BindNames[i]+"="+settings.binds[i]);
  std::ofstream manifest(directory_/"launch.txt");manifest<<"version="<<Version<<"\nplatform="<<platform<<"\ngame="<<U8(installation.game)<<"\nexe_sha256="<<Hash(exe)<<"\nxex_sha256="<<(original?OriginalSHA:GuestSHA)<<"\nreadback_resolve=full\n";for(auto& a:args)manifest<<std::quoted(a)<<'\n';
  for(auto& f:fs::directory_iterator(bundle))if(f.is_regular_file()&&(f.path().extension()==".dll"||f.path().extension()==".so"))manifest<<U8(f.path().filename())<<'='<<Hash(f.path())<<'\n';manifest.close();
#ifdef _WIN32
  // v70: exact on-demand resolve readback (about 60 FPS in fight; FULL waited and ran at 30).
  SetEnvironmentVariableW(L"UFC_ONDEMAND_READBACK",L"1");SetEnvironmentVariableW(L"UFC_ONDEMAND_MIN",L"1");
  std::wstring command;for(auto& a:args){if(!command.empty())command+=L' ';command+=Quote(Wide(a));}STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,bundle.c_str(),&startup,&process)){CloseHandle(lock);throw std::runtime_error("O Windows nao conseguiu iniciar o jogo.");}
  CloseHandle(process.hThread);running_=true;code_=-1;if(diagnostics_)ufc_record::Start(process.hProcess,directory_);
  waiter_=std::thread([this,process,lock]{WaitForSingleObject(process.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(process.hProcess,&code);if(diagnostics_)ufc_record::Finish(directory_,int(code));CloseHandle(process.hProcess);CloseHandle(lock);code_=int(code);running_=false;});
#else
  std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_addchdir_np(&actions,bundle.c_str());
  posix_spawn_file_actions_addopen(&actions,1,(directory_/"stdout.txt").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);posix_spawn_file_actions_addopen(&actions,2,(directory_/"stderr.txt").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
  std::vector<std::string> environment;
  for(char** item=environ;*item;++item){std::string value=*item;if(value.rfind("UFC_ONDEMAND_READBACK=",0)&&value.rfind("UFC_ONDEMAND_MIN=",0))environment.push_back(std::move(value));}
  environment.push_back("UFC_ONDEMAND_READBACK=1");environment.push_back("UFC_ONDEMAND_MIN=1");
  std::vector<char*> child_environment;for(auto& value:environment)child_environment.push_back(value.data());child_environment.push_back(nullptr);
  pid_t pid=-1;int error=posix_spawn(&pid,exe.c_str(),&actions,nullptr,argv.data(),child_environment.data());posix_spawn_file_actions_destroy(&actions);if(error){close(lock);throw std::runtime_error("Nao foi possivel iniciar o jogo Linux: "+std::to_string(error));}
  signal_child=pid;running_=true;code_=-1;if(diagnostics_)ufc_record::Start(pid,directory_);waiter_=std::thread([this,pid,lock]{int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){};signal_child=0;int code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);if(diagnostics_)ufc_record::Finish(directory_,code);close(lock);code_=code;running_=false;});
#endif
 }
};
}
