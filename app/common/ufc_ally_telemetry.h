#pragma once
#ifndef _WIN32
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace ally_telemetry {
namespace fs=std::filesystem;
inline std::string Read(const fs::path& p) { std::ifstream f(p); if(!f)return "unavailable";std::ostringstream s;s<<f.rdbuf();auto t=s.str();for(auto& c:t)if(c=='\n'||c=='\r')c=';';return t.substr(0,8192); }
inline void Log(const char* tag,const fs::path& p,uint64_t now) {UFC_PERFORMANCE_LOG("[ufc-ally-{}] mono_ns={} path={} raw={}",tag,now,p.string(),Read(p));}
inline void ThreadStatus(const fs::path& p,uint64_t now) {
 // Keep scheduling, affinity and switch counters; process memory is sampled
 // once from /proc/self/status rather than repeated in every thread status.
 std::ifstream f(p);std::string line,text;
 while(std::getline(f,line))for(auto key:{"Name:","State:","Pid:","Cpus_allowed_list:","voluntary_ctxt_switches:","nonvoluntary_ctxt_switches:"})
  if(line.starts_with(key)){text+=line+';';break;}
 UFC_PERFORMANCE_LOG("[ufc-ally-thread] mono_ns={} path={} raw={}",now,p.string(),f.bad()||text.empty()?"unavailable":text);
}
inline void Sample(uint64_t now,bool first) {
 std::error_code ec;
 for(auto group:{"/proc/self/task","/sys/devices/system/cpu/cpufreq","/sys/class/drm","/sys/class/hwmon","/sys/class/power_supply"})if(!fs::is_directory(group,ec)){UFC_PERFORMANCE_LOG("[ufc-measure-unavailable] mono_ns={} group={} value=unavailable",now,group);}
 if(first){
  const char* logfile="existing_rotating_log";
  UFC_PERFORMANCE_LOG("[ufc-ally-log-location] log_file={} send_parent_folder=true",logfile);
  UFC_PERFORMANCE_LOG("[ufc-ally-meta] pid={} schema=1 interval_s=2 missing=unavailable units=sysfs_native no_root=true",getpid());
  for(auto p:{"/proc/version","/etc/os-release","/proc/cpuinfo","/sys/class/dmi/id/product_name","/sys/class/dmi/id/board_name","/sys/firmware/acpi/platform_profile"})Log("meta",p,now);
  for(auto name:{"LD_PRELOAD","VK_INSTANCE_LAYERS","VK_LAYER_PATH","VK_ICD_FILENAMES","ENABLE_LSFG","DISABLE_LSFG","MANGOHUD","XDG_SESSION_TYPE","WAYLAND_DISPLAY","DISPLAY"}){auto* v=std::getenv(name);UFC_PERFORMANCE_LOG("[ufc-ally-env] {}={}",name,v?v:"unset");}
  std::ifstream maps("/proc/self/maps");std::string line;while(std::getline(maps,line))if(line.find(".so")!=line.npos)UFC_PERFORMANCE_LOG("[ufc-ally-module] {}",line);
 }
 for(auto p:{"/proc/self/status","/proc/self/schedstat","/proc/self/io","/proc/stat","/proc/meminfo","/proc/pressure/cpu","/proc/pressure/memory","/proc/pressure/io","/proc/loadavg","/sys/firmware/acpi/platform_profile"})Log("system",p,now);
 for(auto& t:fs::directory_iterator("/proc/self/task",ec)){
  for(auto n:{"schedstat","wchan"})Log("thread",t.path()/n,now);ThreadStatus(t.path()/"status",now);
 }
 for(auto& c:fs::directory_iterator("/sys/devices/system/cpu/cpufreq",ec))if(c.is_directory(ec))for(auto n:{"scaling_cur_freq","cpuinfo_cur_freq","scaling_min_freq","scaling_max_freq","scaling_governor","energy_performance_preference","affected_cpus"})Log("cpu",c.path()/n,now);
 for(auto& c:fs::directory_iterator("/sys/class/drm",ec)){
  auto name=c.path().filename().string();if(name.find("card")!=0||name.find('-')!=name.npos)continue;
  for(auto n:{"vendor","device","gpu_busy_percent","mem_busy_percent","mem_info_vram_used","mem_info_vram_total","mem_info_gtt_used","pp_dpm_sclk","pp_dpm_mclk","power_dpm_force_performance_level"})Log("gpu",c.path()/"device"/n,now);
  if(first){auto driver=fs::read_symlink(c.path()/"device/driver",ec);UFC_PERFORMANCE_LOG("[ufc-ally-driver] device={} driver={}",name,ec?"unavailable":driver.string());}
 }
 for(auto& h:fs::directory_iterator("/sys/class/hwmon",ec)){
  auto name=Read(h.path()/"name");
  for(auto& f:fs::directory_iterator(h.path(),ec)){
   auto n=f.path().filename().string();bool sensor=n.find("temp")==0||n.find("power")==0||n.find("freq")==0||n.find("fan")==0||n.find("energy")==0;
   if(sensor&&(n.ends_with("_input")||n.ends_with("_average")||n.ends_with("_cap")||n.ends_with("_crit")||n.ends_with("_label"))) {if(first)UFC_PERFORMANCE_LOG("[ufc-ally-sensor-name] path={} name={}",h.path().string(),name);Log("sensor",f.path(),now);}
  }
 }
 for(auto& p:fs::directory_iterator("/sys/class/power_supply",ec))for(auto n:{"status","online","power_now","energy_now","capacity"})Log("battery",p.path()/n,now);
}
}
#endif
