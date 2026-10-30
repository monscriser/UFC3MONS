#pragma once
// Opt-in sampler. Uses the existing bounded application logger; no file sink.
#ifndef UFC_PERFORMANCE_LOG
#include <rex/logging.h>
#define UFC_PERFORMANCE_LOG REXLOG_INFO
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include "ufc_ally_telemetry.h"
#endif
namespace ufc_measure {
inline constexpr const char* Version="P5-schema2-2s";
struct Sample{std::string name;unsigned long long ticks=0;};
inline std::map<unsigned long,Sample> Read(){
 std::map<unsigned long,Sample> out;
#ifdef _WIN32
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);if(snap==INVALID_HANDLE_VALUE)return out;
 THREADENTRY32 e{};e.dwSize=sizeof(e);
 if(Thread32First(snap,&e))do{if(e.th32OwnerProcessID!=GetCurrentProcessId())continue;HANDLE h=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,e.th32ThreadID);if(!h)continue;
 FILETIME a,b,k,u;if(GetThreadTimes(h,&a,&b,&k,&u)){ULARGE_INTEGER kt{},ut{};kt.LowPart=k.dwLowDateTime;kt.HighPart=k.dwHighDateTime;ut.LowPart=u.dwLowDateTime;ut.HighPart=u.dwHighDateTime;
 Sample s{"thread",kt.QuadPart+ut.QuadPart};PWSTR desc=nullptr;if(SUCCEEDED(GetThreadDescription(h,&desc))&&desc){s.name.clear();for(auto* p=desc;*p;++p)s.name+=char(*p<128?*p:'?');LocalFree(desc);}out[e.th32ThreadID]=std::move(s);}CloseHandle(h);
 }while(Thread32Next(snap,&e));CloseHandle(snap);
#else
 DIR* dir=opendir("/proc/self/task");if(!dir)return out;
 while(auto* e=readdir(dir)){if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
 try{std::string base=std::string("/proc/self/task/")+e->d_name,line,field;std::ifstream stat(base+"/stat");if(!std::getline(stat,line))continue;auto close=line.rfind(')');if(close==line.npos)continue;
 std::istringstream fields(line.substr(close+2));Sample s;for(int i=3;fields>>field;++i){if(i==14)s.ticks=std::stoull(field);if(i==15){s.ticks+=std::stoull(field);break;}}
 std::ifstream(base+"/comm")>>s.name;out[std::stoul(e->d_name)]=std::move(s);}catch(...){}
 }closedir(dir);
#endif
 return out;
}
inline bool ExactOn(){auto*p=std::getenv("UFC_MEASURE");return p&&std::string_view(p)=="1";}
inline bool Allowed(const std::string& path,bool gameplay){return gameplay&&ExactOn()&&!path.empty()&&path!="NUL"&&path!="/dev/null";}
inline void Set(const char* key,const char* value){
#ifdef _WIN32
 _putenv_s(key,value);
#else
 setenv(key,value,1);
#endif
}
struct Collector{
 std::mutex mutex;std::condition_variable cv;bool stopping=false;std::thread thread;
 Collector():thread([this]{Run();}){}
 ~Collector(){Stop();}
 void Stop(){{std::lock_guard lock(mutex);stopping=true;}cv.notify_all();if(thread.joinable())thread.join();}
 void Run(){
#ifdef _WIN32
 SetThreadDescription(GetCurrentThread(),L"UFC telemetry");
#else
 pthread_setname_np(pthread_self(),"UFC telemetry");
#endif
#ifdef _WIN32
 const double tick_ms=0.0001;UFC_PERFORMANCE_LOG("[ufc-measure-availability] windows_clock_power_sensors=unavailable source=thread_cpu_only");
#else
 const double tick_ms=1000./double(sysconf(_SC_CLK_TCK));
#endif
 auto previous=Read();auto last=std::chrono::steady_clock::now();bool first=true;
 for(;;){{std::unique_lock lock(mutex);if(cv.wait_for(lock,std::chrono::seconds(2),[this]{return stopping;}))return;}
 try{auto sample_start=std::chrono::steady_clock::now();auto current=Read();auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-last).count(),total=0;std::vector<std::pair<double,std::string>> rows;
 for(auto&[tid,s]:current){auto old=previous.find(tid);if(old==previous.end()||s.ticks<old->second.ticks)continue;double ms=double(s.ticks-old->second.ticks)*tick_ms/dt;total+=ms;rows.emplace_back(ms,s.name+"/"+std::to_string(tid));}
 std::sort(rows.rbegin(),rows.rend());std::string text;for(auto&[ms,name]:rows)text+=' '+name+'='+std::to_string(int(ms));
 auto mono=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
 UFC_PERFORMANCE_LOG("[ufc-thread-cpu] mono_ns={} total_ms_per_s={} interval_ms={} threads={} top:{}",mono,int(total),int(dt*1000),rows.size(),text);
 if(current.empty())UFC_PERFORMANCE_LOG("[ufc-measure-unavailable] source=thread_cpu");
#ifndef _WIN32
 ally_telemetry::Sample(mono,first);
#endif
 UFC_PERFORMANCE_LOG("[ufc-measure-cost] mono_ns={} wall_ms={:.3f} includes_logging=true",mono,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sample_start).count());
 first=false;previous=std::move(current);last=now;
 }catch(const std::exception&e){UFC_PERFORMANCE_LOG("[ufc-measure-error] error={}",e.what());}}
 }
};
inline Collector*& Instance(){static Collector* instance=nullptr;return instance;}
inline bool& Started(){static bool value=false;return value;}
struct Cleanup{~Cleanup(){delete Instance();Instance()=nullptr;}};
inline void Stop(){if(Instance())Instance()->Stop();}
inline void Start(const std::string& log,bool gameplay){
 const bool on=Allowed(log,gameplay);
 const char* gpu_request=std::getenv("UFC_MEASURE_GPU");
 const char* resource_request=std::getenv("UFC_QA_RESOURCE_TIMING");
 const bool resource=gameplay&&!log.empty()&&resource_request&&std::string_view(resource_request)=="1";
 const bool gpu=(on||resource) && gpu_request && std::string_view(gpu_request)=="1";
 Set("UFC_WAIT_DIAG",on?"1":"0");Set("UFC_GPU_TIMING",gpu?"1":"0");Set("UFC_RESOURCE_TIMING",resource&&gpu?"1":"0");Set("UFC_MEASURE",on?"1":"0");
 if(resource&&gpu)UFC_PERFORMANCE_LOG("[ufc-resource-profile-config] enabled=true explicit_qa_request=true cpu_sampler={} resource_timing=true",on);
 if(!on||Started())return;Started()=true;
 UFC_PERFORMANCE_LOG("[ufc-measure] enabled=true schema={} interval_s=2 cpu_per_thread=true gpu_timestamps={} wait_frames=true resource_timing={} sink=existing_rotating_log in_game_mode=not_observed_by_launcher",Version,gpu,resource&&gpu);
 static Cleanup cleanup;Instance()=new Collector;
}
}
inline void UfcStartMeasurement(const std::string& log,bool gameplay){ufc_measure::Start(log,gameplay);}
