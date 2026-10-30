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
#include "ufc_measure_environment.h"
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
 // precompile=true: optional "Pre-compile shaders" run. The game creates every stored pipeline with this
 // GPU's driver and closes before the title starts (windowed, log in cache/precompile.log).
 void Start(const fs::path& bundle,const Installation& installation,const Settings& settings,bool precompile=false){
  if(Running())throw std::runtime_error("Ja existe uma partida aberta por este launcher.");Wait();Register(installation);settings.Save(installation.game);
  bool original=!installation.iso.empty()||Hash(installation.game/"default.xex")==OriginalSHA;
  if(!original)PrepareLocalizedNames(installation.game,settings.game_language);
  auto qa_bool=[](const char* key,bool normal){const auto* v=std::getenv(key);return v&&std::string_view(v)=="1"?true:v&&std::string_view(v)=="0"?false:normal;};
  const bool shirt_aliases=!original&&qa_bool("UFC_QA_SHIRT_ALIASES",true);
  const bool memory_batch=qa_bool("UFC_QA_MEMORY_BATCH",true);
  const bool texture_cache=qa_bool("UFC_QA_TEXTURE_CACHE",true);
  const bool early_submit=qa_bool("UFC_QA_EARLY_SUBMIT",true);
  const bool memory_watch_blocks=qa_bool("UFC_QA_MEMORY_WATCH_BLOCKS",true);
  const bool gym_layout_fix=qa_bool("UFC_QA_GYM_LAYOUT_FIX",true);
  const bool forced_logs=std::getenv("UFC_FORCE_SESSION_LOGS")&&std::string_view(std::getenv("UFC_FORCE_SESSION_LOGS"))=="1";
  const bool write_logs=settings.write_logs||forced_logs;const bool performance_diag=write_logs&&qa_bool("UFC_QA_MEASURE",true);const bool gpu_timing_diag=performance_diag&&qa_bool("UFC_QA_GPU_TIMING",false);diagnostics_=write_logs&&qa_bool("UFC_QA_DIAGNOSTICS",false);auto root=Storage(installation.game);auto exe=GameBinary(bundle,original);if(!fs::is_regular_file(exe))throw std::runtime_error("O executavel nativo nao acompanha este launcher. Extraia o pacote completo.");
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
  const auto session_id=NewLogSessionId();
  directory_=write_logs?root/"logs"/(std::string(platform)+"-"+(original?"original":"reborn")+"-"+session_id):fs::path{};if(write_logs)fs::create_directories(directory_);
#ifdef _WIN32
  const std::string null_device="NUL";
#else
  const std::string null_device="/dev/null";
#endif

  std::vector<std::string> args={U8(exe),"--gpu_plugin=xenos","--ufc_configurator=false","--game_data_root="+U8(installation.game),"--log_file="+(write_logs?U8(directory_/"ufc3.log"):null_device),write_logs?"--log_level=info":"--log_level=off","--readback_resolve=full","--ufc_cp_fast_registers=false",settings.fullscreen?"--fullscreen=true":"--fullscreen=false",settings.fps?"--ufc_show_fps=true":"--ufc_show_fps=false","--vsync=true",settings.keyboard?"--mnk_mode=true":"--mnk_mode=false",settings.mouse?"--mnk_mouse=true":"--mnk_mouse=false","--mnk_sensitivity="+std::to_string(settings.sensitivity)};
  if(write_logs){args.push_back("--ufc_session_id="+session_id);args.push_back(std::string("--ufc_session_edition=")+(original?"original":"reborn"));}
  if(!original)args.push_back(std::string("--ufc_reborn_gym_layout=")+(gym_layout_fix?"true":"false"));
  if(!original)args.push_back(std::string("--ufc_reborn_shirt_aliases=")+(shirt_aliases?"true":"false"));
  args.push_back(std::string("--ufc_submit_before_wait=")+(early_submit?"true":"false"));
  args.push_back(std::string("--ufc_memory_watch_blocks=")+(memory_watch_blocks?"true":"false"));
  args.push_back("--ufc_memory_watch_verify=false");
  args.push_back(std::string("--ufc_memory_watch_batch=")+(memory_batch?"true":"false"));
  args.push_back("--ufc_memory_watch_batch_verify=false");
  args.push_back(std::string("--ufc_texture_set_cache=")+(texture_cache?"true":"false"));
  args.push_back(std::string("--ufc_present_parameter_diagnostics=")+(write_logs?"true":"false"));

  AppendResolutionArguments(args,settings.output,settings.fsr1);
  // P14.11 texture filtering: launcher choice -> SDK anisotropic_override (-1 game, 0 off, 2 = 2x, 3 = 4x (SDK default), 4 = 8x, 5 = 16x).
  {static const int aniso[]={-1,0,2,3,4,5};args.push_back("--anisotropic_override="+std::to_string(aniso[std::clamp(settings.texture_filter,0,5)]));}
  if(!installation.iso.empty())args.push_back("--ufc_iso_path="+U8(installation.iso));
  // L2: installed DLC is only usable when its license is active (same as Xenia's license_mask).
  {std::error_code error;auto dlc=Storage(installation.game)/"user"/(original?"Original-static-v48":"Reborn-static-r9")/"0000000000000000"/"5451087D"/"00000002";
   if(fs::is_directory(dlc,error)&&!fs::is_empty(dlc,error))args.push_back("--license_mask=4294967295");}
  // Fixed: the game simulation is frame-locked (measured v71), more frames speed the fight up.
  args.push_back("--ufc_fps_limit=60");
  // GPU work recorded and submitted on a second thread (Hydro 9.1.0). A/B 28/10: GPU-commands thread
  // -28% CPU, submission 1.22 -> 0.08 ms/frame; 26,686 streams verified, 0 differences. false = old path.
  args.push_back("--ufc_async_submit=true");
  // P14.8: between 60 and 30 FPS the game step follows the real frame time (normal speed, only less smooth).
  // A/B 29/10 at 44 FPS: 80% speed without, 100% with; at 60 FPS the game's values are untouched.
  args.push_back("--ufc_adaptive_rate=true");
  // P14.9 Mods tab: enabled files in <storage>/mods replace the game's files (read-only redirect).
  args.push_back("--ufc_mods_dir="+U8(Storage(installation.game)/"mods"));
  args.push_back(std::string("--ufc_full_training_rewards=")+(settings.full_training_rewards?"true":"false"));
  AtomicText(CheatsFile(),CheatsText(settings));args.push_back("--ufc_cheats_file="+U8(CheatsFile()));
  // The preserving synchronous backend is required until async fidelity is validated.
  args.push_back(std::string("--ufc_async_shaders=")+("false"));
  args.push_back(diagnostics_?"--ufc_shadow_diagnostics=true":"--ufc_shadow_diagnostics=false");
  args.push_back("--ufc_shadow_precise=false");
  if(performance_diag){args.push_back("--log_max_file_size_mb=16");args.push_back("--log_max_files=8");}
  else if(!diagnostics_){args.push_back("--log_max_file_size_mb=4");args.push_back("--log_max_files=2");}
  if(diagnostics_)args.push_back("--ufc_language_diagnostics=true");
  args.push_back("--user_language="+std::to_string(LanguageIds[std::clamp(settings.game_language,0,5)]));
  for(size_t i=0;i<settings.binds.size();++i)args.push_back(std::string("--keybind_")+BindNames[i]+"="+settings.binds[i]);
  if(precompile){for(auto& a:args){if(a.rfind("--fullscreen=",0)==0)a="--fullscreen=false";else if(a.rfind("--log_file=",0)==0)a="--log_file="+U8(root/"cache"/"precompile.log");else if(a.rfind("--log_level=",0)==0)a="--log_level=info";}
   std::error_code e;fs::create_directories(root/"cache",e);fs::remove(root/"cache"/"precompile.log",e);args.push_back("--ufc_precompile_only=true");}
  if(write_logs){std::ofstream manifest(directory_/"launch.txt");manifest<<"manifest_format=2\nbuild_id="<<BuildId<<"\nsession_id="<<session_id<<"\nedition="<<(original?"original":"reborn")<<"\nlaunch_mode=gameplay\nin_game_mode=not_observed_by_launcher\nlogs_enabled=1\ndiagnostics="<<diagnostics_<<"\nperformance_diag="<<performance_diag<<"\nmeasurement_schema=P5-schema2-2s\nmeasurement_interval_s=2\ngpu_timing_requested="<<gpu_timing_diag<<"\nmeasurement_gpu_timestamps="<<gpu_timing_diag<<"\nmeasurement_wait_frames="<<performance_diag<<"\nmeasurement_resource_timing=0\nmeasurement_limit=existing_rotating_log;16MiB;8_files;driver_sensors_optional;no_scene_detection\ngym_layout_fix="<<(!original&&gym_layout_fix)<<"\nufc_reborn_shirt_aliases="<<shirt_aliases<<"\nufc_memory_watch_batch="<<memory_batch<<"\nufc_texture_set_cache="<<texture_cache<<"\nufc_memory_watch_blocks="<<memory_watch_blocks<<"\nufc_memory_watch_verify=0"<<"\nearly_submit="<<early_submit<<"\nversion="<<Version<<"\nplatform="<<platform<<"\ngame="<<U8(installation.game)<<"\nexe_sha256="<<Hash(exe)<<"\nxex_sha256="<<(original?OriginalSHA:GuestSHA)<<"\nreadback_resolve=full\n";for(auto& a:args)manifest<<std::quoted(a)<<'\n';
  for(auto& f:fs::directory_iterator(bundle))if(f.is_regular_file()&&(f.path().extension()==".dll"||f.path().extension()==".so"))manifest<<U8(f.path().filename())<<'='<<Hash(f.path())<<'\n';manifest.close();}
#ifdef _WIN32
  // v70: exact on-demand resolve readback (about 60 FPS in fight; FULL waited and ran at 30).
  {static const wchar_t* modes[]={L"mailbox",L"fifo",L"immediate"};SetEnvironmentVariableW(L"UFC_PRESENT_MODE",modes[std::clamp(settings.present_mode,0,2)]);}
  SetEnvironmentVariableW(L"UFC_ONDEMAND_READBACK",L"1");SetEnvironmentVariableW(L"UFC_ONDEMAND_MIN",L"1");
  std::wstring command;for(auto& a:args){if(!command.empty())command+=L' ';command+=Quote(Wide(a));}STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
  auto stdout_path=write_logs?directory_/"stdout.txt":fs::path(L"NUL");auto stderr_path=write_logs?directory_/"stderr.txt":fs::path(L"NUL");
  HANDLE output=CreateFileW(stdout_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  HANDLE errors=CreateFileW(stderr_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,0,nullptr);
  if(output==INVALID_HANDLE_VALUE||errors==INVALID_HANDLE_VALUE||input==INVALID_HANDLE_VALUE){if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);if(errors!=INVALID_HANDLE_VALUE)CloseHandle(errors);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);CloseHandle(lock);throw std::runtime_error("Nao foi possivel preparar a saida do jogo.");}
  // P14.10: pre-compile run starts hidden (the first ShowWindow of the game uses this; the game also hides itself).
  if(precompile){startup.dwFlags|=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;}
  startup.dwFlags|=STARTF_USESTDHANDLES;startup.hStdOutput=output;startup.hStdError=errors;startup.hStdInput=input;
  // Explicit child-only environment; hostile host flags cannot enable collection.
  auto measurement_environment=[&]{
   auto inherited=MeasurementEnvironmentBlock(performance_diag);std::vector<std::wstring> entries;
   const std::wstring gpu_key=L"UFC_GPU_TIMING=",request_key=L"UFC_MEASURE_GPU=";
   for(auto* p=inherited.data();*p;p+=wcslen(p)+1){
    if(_wcsnicmp(p,request_key.c_str(),request_key.size())==0)continue;
    if(_wcsnicmp(p,gpu_key.c_str(),gpu_key.size())==0)entries.push_back(gpu_key+(gpu_timing_diag?L"1":L"0"));else entries.emplace_back(p);
   }
   entries.push_back(request_key+(gpu_timing_diag?L"1":L"0"));
   std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return _wcsicmp(a.c_str(),b.c_str())<0;});
   std::vector<wchar_t> result;for(auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}result.push_back(0);return result;
  }();
  BOOL launched=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_UNICODE_ENVIRONMENT,measurement_environment.data(),bundle.c_str(),&startup,&process);
  CloseHandle(output);CloseHandle(errors);CloseHandle(input);
  if(!launched){CloseHandle(lock);throw std::runtime_error("O Windows nao conseguiu iniciar o jogo.");}
  CloseHandle(process.hThread);running_=true;code_=-1;if(diagnostics_)ufc_record::Start(process.hProcess,directory_);
  waiter_=std::thread([this,process,lock]{WaitForSingleObject(process.hProcess,INFINITE);DWORD code=0;GetExitCodeProcess(process.hProcess,&code);if(diagnostics_)ufc_record::Finish(directory_,int(code));CloseHandle(process.hProcess);CloseHandle(lock);code_=int(code);running_=false;});
#else
  std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_addchdir_np(&actions,bundle.c_str());
  auto stdout_path=write_logs?directory_/"stdout.txt":fs::path("/dev/null");auto stderr_path=write_logs?directory_/"stderr.txt":fs::path("/dev/null");
  posix_spawn_file_actions_addopen(&actions,1,stdout_path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);posix_spawn_file_actions_addopen(&actions,2,stderr_path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
  std::vector<std::string> environment;
  for(char** item=environ;*item;++item){std::string value=*item;if(value.rfind("UFC_ONDEMAND_READBACK=",0)&&value.rfind("UFC_ONDEMAND_MIN=",0)&&!IsMeasurementEnvironment(value)&&value.rfind("UFC_MEASURE_GPU=",0))environment.push_back(std::move(value));}
  environment.push_back("UFC_ONDEMAND_READBACK=1");environment.push_back("UFC_ONDEMAND_MIN=1");for(auto key:MeasurementVariables)environment.push_back(std::string(key)+"="+(std::string_view(key)=="UFC_GPU_TIMING"?(gpu_timing_diag?"1":"0"):MeasurementValue(key,performance_diag)));environment.push_back(std::string("UFC_MEASURE_GPU=")+(gpu_timing_diag?"1":"0"));
  {static const char* modes[]={"mailbox","fifo","immediate"};std::erase_if(environment,[](const std::string& v){return v.rfind("UFC_PRESENT_MODE=",0)==0;});environment.push_back(std::string("UFC_PRESENT_MODE=")+modes[std::clamp(settings.present_mode,0,2)]);}
  std::vector<char*> child_environment;for(auto& value:environment)child_environment.push_back(value.data());child_environment.push_back(nullptr);
  pid_t pid=-1;int error=posix_spawn(&pid,exe.c_str(),&actions,nullptr,argv.data(),child_environment.data());posix_spawn_file_actions_destroy(&actions);if(error){close(lock);throw std::runtime_error("Nao foi possivel iniciar o jogo Linux: "+std::to_string(error));}
  signal_child=pid;running_=true;code_=-1;if(diagnostics_)ufc_record::Start(pid,directory_);waiter_=std::thread([this,pid,lock]{int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){};signal_child=0;int code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);if(diagnostics_)ufc_record::Finish(directory_,code);close(lock);code_=code;running_=false;});
#endif
 }
};
}



