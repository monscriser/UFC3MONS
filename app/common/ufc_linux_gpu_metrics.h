#pragma once
// Native process-scoped GPU counters. Missing driver data remains unavailable.
// Formats: kernel.org/doc/html/latest/gpu/drm-usage-stats.html and
// docs.nvidia.com/deploy/nvidia-smi/index.html (Process Monitoring).
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
namespace ufc_linux_metrics {
struct Sample{double load=-1,memory=-1;std::string source="unavailable";};
struct Client{std::string id,device;std::map<std::string,uint64_t> engines,capacity;double memory=-1;};
inline std::string Trim(std::string text){auto a=text.find_first_not_of(" \t\r\n"),b=text.find_last_not_of(" \t\r\n");return a==std::string::npos?"":text.substr(a,b-a+1);}
inline Client ParseDrm(std::istream& input){
 Client c;std::string line;double resident=-1,legacy=-1;
 while(std::getline(input,line)){auto colon=line.find(':');if(colon==std::string::npos)continue;auto key=line.substr(0,colon),value=Trim(line.substr(colon+1));
  if(key=="drm-client-id")c.id=value;else if(key=="drm-pdev")c.device=value;
  else if(key.rfind("drm-engine-capacity-",0)==0){uint64_t n=0;std::istringstream(value)>>n;if(n)c.capacity[key.substr(20)]=n;}
  else if(key.rfind("drm-engine-",0)==0){uint64_t n=0;std::string unit;std::istringstream v(value);if(v>>n>>unit;v&&unit=="ns")c.engines[key.substr(11)]=n;}
  else if(key=="drm-resident-vram"||key=="drm-memory-vram"){
   double n;std::string unit;std::istringstream v(value);if(v>>n>>unit){if(unit=="KiB")n*=1024;else if(unit=="MiB")n*=1048576;else if(unit!="B")continue;(key=="drm-resident-vram"?resident:legacy)=n;}
  }
 }c.memory=resident>=0?resident:legacy;return c;
}
struct DrmCounters{
 std::map<std::string,uint64_t> previous;
 Sample Update(const std::vector<Client>& clients,double elapsed){
  Sample result;bool supported=false;std::set<std::string> seen;std::map<std::string,double> engines;std::map<std::string,uint64_t> next;
  for(const auto& client:clients){if(client.id.empty())continue;auto identity=client.device+"/"+client.id;if(!seen.insert(identity).second)continue;
   if(!client.engines.empty()||client.memory>=0)supported=true;
   if(client.memory>=0)result.memory=std::max(0.,result.memory)+client.memory;
   for(const auto& [name,value]:client.engines){auto key=identity+"/"+name;auto old=previous.find(key);next[key]=old==previous.end()?value:std::max(old->second,value);
    if(old==previous.end()||elapsed<=0)continue;auto capacity=client.capacity.find(name);auto divisor=capacity==client.capacity.end()?1:capacity->second;
    auto delta=value>=old->second?value-old->second:0;engines[client.device+"/"+name]+=double(delta)/1e9/elapsed/divisor*100.;
   }
  }
  previous=std::move(next);for(const auto& [_,load]:engines)result.load=std::max(result.load,std::clamp(load,0.,100.));
  if(supported)result.source="drm-fdinfo-process";return result;
 }
 Sample Read(int pid,double elapsed){
  std::vector<Client> clients;std::error_code error;
  for(const auto& file:std::filesystem::directory_iterator("/proc/"+std::to_string(pid)+"/fdinfo",error)){std::ifstream input(file.path());auto client=ParseDrm(input);if(!client.id.empty())clients.push_back(std::move(client));}
  return Update(clients,elapsed);
 }
};
inline Sample ParseNvidia(const std::string& text,int pid){
 Sample result;std::vector<std::string> columns;std::istringstream lines(text);std::string line;
 while(std::getline(lines,line)){std::istringstream fields(line);std::vector<std::string> values;std::string word;while(fields>>word)values.push_back(word);if(values.empty())continue;
  if(values[0]=="#"){
   values.erase(values.begin());for(auto& v:values)std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return char(std::tolower(c));});
   if(std::find(values.begin(),values.end(),"pid")!=values.end())columns=std::move(values);continue;
  }
  auto column=[&](const char* key)->std::string{auto it=std::find(columns.begin(),columns.end(),key);auto n=size_t(it-columns.begin());return it==columns.end()||n>=values.size()?"":values[n];};
  if(column("pid")!=std::to_string(pid))continue;
  auto number=[&](const char* key){auto value=column(key);if(value.empty()||value=="-"||value=="N/A")return -1.;try{return std::stod(value);}catch(...){return -1.;}};
  for(auto key:{"sm","enc","dec","jpg","ofa"}){double load=number(key);if(load>=0)result.load=std::max(result.load,std::clamp(load,0.,100.));}
  double memory=number("fb");if(memory>=0)result.memory=std::max(0.,result.memory)+memory*1048576.;
  result.source="nvidia-pmon-process";
 }return result;
}
}
