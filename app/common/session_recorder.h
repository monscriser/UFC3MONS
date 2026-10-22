#pragma once
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
#include "ufc_linux_gpu_metrics.h"
#include <vector>
#endif
namespace ufc_record {
namespace fs=std::filesystem;
inline std::thread worker;
inline std::atomic<bool> stop{false};
inline double cpu_sum=0, gpu_sum=0;inline unsigned samples=0,gpu_samples=0;
inline void Begin(const fs::path& dir){std::ofstream(dir/"RELATORIO.md")<<"# UFC Reborn — sessão 1.0.0 experimental\n\nSessão iniciada. O resumo será concluído ao fechar o jogo. Se esta mensagem persistir, o launcher também foi encerrado abruptamente; desempenho.csv e ufc3.log preservam as amostras já gravadas.\n";}
#ifdef _WIN32
inline double Cpu(HANDLE p) { FILETIME a,b,k,u;if(!GetProcessTimes(p,&a,&b,&k,&u))return -1;
 auto n=[](FILETIME t){ULARGE_INTEGER v;v.LowPart=t.dwLowDateTime;v.HighPart=t.dwHighDateTime;return double(v.QuadPart)/10000.;};return n(k)+n(u); }
inline double CounterMax(PDH_HCOUNTER counter,DWORD pid,bool sum=false) {
 DWORD bytes=0,count=0;auto status=PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&bytes,&count,nullptr);
 if(status!=PDH_MORE_DATA)return -1;std::vector<unsigned char> storage(bytes);
 auto* values=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
 if(PdhGetFormattedCounterArrayW(counter,PDH_FMT_DOUBLE,&bytes,&count,values)!=ERROR_SUCCESS)return -1;
 double result=-1;auto pattern=L"pid_"+std::to_wstring(pid)+L"_";
 for(DWORD i=0;i<count;++i)if(std::wstring(values[i].szName).find(pattern)!=std::wstring::npos&&values[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA){
  auto x=values[i].FmtValue.doubleValue;result=sum?std::max(0.,result)+x:std::max(result,x);
 }return result;
}
inline void Start(HANDLE process,const fs::path& dir) {
 Begin(dir);stop=false;cpu_sum=gpu_sum=0;samples=gpu_samples=0;auto pid=GetProcessId(process);
 worker=std::thread([process,dir,pid]{
  PDH_HQUERY query{};PDH_HCOUNTER gpu{},vram{};
  PdhOpenQueryW(nullptr,0,&query);
  bool gpu_ok=query&&PdhAddEnglishCounterW(query,L"\\GPU Engine(*)\\Utilization Percentage",0,&gpu)==ERROR_SUCCESS;
  bool ram_ok=query&&PdhAddEnglishCounterW(query,L"\\GPU Process Memory(*)\\Dedicated Usage",0,&vram)==ERROR_SUCCESS;
  if(query)PdhCollectQueryData(query);
  std::ofstream out(dir/"desempenho.csv");out<<"seconds,cpu_ms_per_second,ram_bytes,gpu_busiest_engine_percent,gpu_dedicated_bytes\n";
  auto start=std::chrono::steady_clock::now(),last=start;double previous=Cpu(process);
  while(!stop&&WaitForSingleObject(process,1000)==WAIT_TIMEOUT){
   auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-last).count(),current=Cpu(process);
   if(query)PdhCollectQueryData(query);double load=gpu_ok?CounterMax(gpu,pid):-1,mem=ram_ok?CounterMax(vram,pid,true):-1;
   PROCESS_MEMORY_COUNTERS pm{};bool have_ram=GetProcessMemoryInfo(process,&pm,sizeof(pm));
   double cpu=(current-previous)/dt;out<<std::chrono::duration<double>(now-start).count()<<','<<cpu<<',';
   if(have_ram)out<<pm.WorkingSetSize;out<<',';if(load>=0)out<<load;out<<',';if(mem>=0)out<<mem;out<<'\n';out.flush();
   cpu_sum+=cpu;++samples;if(load>=0){gpu_sum+=load;++gpu_samples;}previous=current;last=now;
  }if(query)PdhCloseQuery(query);
 });
}
#else
inline std::string Command(const std::vector<std::string>& args) {
 int descriptors[2];if(pipe(descriptors))return {};pid_t p=-1;
 std::vector<char*> argv;for(auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
 posix_spawn_file_actions_adddup2(&actions,descriptors[1],STDOUT_FILENO);
 posix_spawn_file_actions_addclose(&actions,descriptors[0]);
 posix_spawn_file_actions_addclose(&actions,descriptors[1]);
 posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0600);
 int error=posix_spawnp(&p,argv[0],&actions,nullptr,argv.data(),environ);
 posix_spawn_file_actions_destroy(&actions);
 if(error){close(descriptors[0]);close(descriptors[1]);return {};}
 close(descriptors[1]);std::string value;char buffer[4096];ssize_t n;while((n=read(descriptors[0],buffer,sizeof(buffer)))>0)value.append(buffer,n);close(descriptors[0]);int status=0;if(p>0)waitpid(p,&status,0);return value;
}
inline double Cpu(pid_t pid){std::ifstream f("/proc/"+std::to_string(pid)+"/stat");std::string text;std::getline(f,text);auto end=text.rfind(')');if(end==std::string::npos)return -1;
 std::istringstream fields(text.substr(end+2));std::string v;unsigned long long u=0,k=0;for(int field=3;field<=15;++field){fields>>v;if(field==14)u=std::stoull(v);if(field==15)k=std::stoull(v);}return (u+k)*1000./sysconf(_SC_CLK_TCK);}
inline double Number(const fs::path& path){std::ifstream in(path);double n=-1;in>>n;return n;}
inline void Start(pid_t pid,const fs::path& dir){Begin(dir);stop=false;cpu_sum=gpu_sum=0;samples=gpu_samples=0;
 worker=std::thread([pid,dir]{std::ofstream out(dir/"desempenho.csv");out<<"seconds,cpu_ms_per_second,ram_bytes,gpu_busiest_engine_percent,gpu_dedicated_bytes,gpu_source\n";
 auto start=std::chrono::steady_clock::now(),last=start;double previous=Cpu(pid);ufc_linux_metrics::DrmCounters drm;
 while(!stop){for(int i=0;i<10&&!stop;++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));if(stop)break;
 auto now=std::chrono::steady_clock::now();double current=Cpu(pid);if(current<0)break;
 double dt=std::chrono::duration<double>(now-last).count(),cpu=(current-previous)/dt;unsigned long total=0,resident=0;std::ifstream("/proc/"+std::to_string(pid)+"/statm")>>total>>resident;
 auto metric=drm.Read(pid,dt);
 if(metric.source=="unavailable")metric=ufc_linux_metrics::ParseNvidia(Command({"nvidia-smi","pmon","-s","um","-c","1"}),pid);
 double gpu=metric.load,mem=metric.memory;const auto& source=metric.source;

 out<<std::chrono::duration<double>(now-start).count()<<','<<cpu<<','<<resident*sysconf(_SC_PAGESIZE)<<',';if(gpu>=0)out<<gpu;out<<',';if(mem>=0)out<<mem;out<<','<<source<<'\n';out.flush();
 cpu_sum+=cpu;++samples;if(gpu>=0){gpu_sum+=gpu;++gpu_samples;}last=now;previous=current;
 }});
}
#endif
inline void Finish(const fs::path& dir,int exit_code){stop=true;if(worker.joinable())worker.join();
 std::ifstream log(dir/"ufc3.log");std::ofstream events(dir/"avisos-texturas-erros.txt"),fps(dir/"fps.csv");fps<<"log_line\n";
 std::string line;size_t warnings=0,textures=0,errors=0,frames=0;double fps_sum=0,fps_min=1e9,fps_max=0;size_t fps_values=0;
 while(std::getline(log,line)){
  bool texture=line.find("Texture fetch constant")!=std::string::npos||line.find("texture")!=std::string::npos||line.find("Texture")!=std::string::npos;
  bool warning=line.find("[warning]")!=std::string::npos||line.find("[warn]")!=std::string::npos;
  bool error=line.find("[error]")!=std::string::npos||line.find("[critical]")!=std::string::npos||line.find("FATAL")!=std::string::npos;
  if(warning)++warnings;if(error)++errors;if(texture&&(warning||error))++textures;
  if(warning||error)events<<line<<'\n';
  if(line.find("[ufc-fps]")!=std::string::npos){++frames;fps<<'"';for(char c:line){if(c=='"')fps<<'"';fps<<c;}fps<<"\"\n";
   auto key=line.find(" fps=");if(key!=std::string::npos){try{double value=std::stod(line.substr(key+5));fps_sum+=value;fps_min=std::min(fps_min,value);fps_max=std::max(fps_max,value);++fps_values;}catch(...){}}
  }
 }
 std::ofstream report(dir/"RELATORIO.md");report<<"# UFC Reborn — sessão 1.0.0 experimental\n\nSaída do jogo: "<<exit_code<<"\n\nAmostras de desempenho: "<<samples<<"\n\nCPU média (ms por segundo; 1000 = um núcleo): ";if(samples)report<<cpu_sum/samples;else report<<"indisponível";
 report<<"\n\nGPU média na métrica disponível: ";if(gpu_samples)report<<gpu_sum/gpu_samples<<'%';else report<<"indisponível";
 report<<"\n\nFPS médio / mínimo / máximo da sessão inteira (inclui menus e carregamentos): ";if(fps_values)report<<fps_sum/fps_values<<" / "<<fps_min<<" / "<<fps_max;else report<<"indisponível";
 report<<"\n\nAmostras FPS: "<<frames<<". Avisos: "<<warnings<<". Erros: "<<errors<<". Avisos/erros de texturas: "<<textures<<".\n\nArquivos: desempenho.csv, fps.csv, avisos-texturas-erros.txt, ufc3.log e launch.txt.\n\nFPS mede submissões do jogo. GPU Windows: maior motor do processo; Linux: processo via DRM fdinfo ou nvidia-smi pmon, com origem na coluna gpu_source; disponibilidade e motores dependem do driver. Valores ausentes não são zero. Percentual de GPU não mede energia elétrica. Avisos não provam defeito visual, nem ausência de avisos prova imagem correta. Telemetria leve permanece ligada em todas as sessões. Para análise, traga esta pasta inteira e descreva qualquer defeito visual observado.\n";
}
}
