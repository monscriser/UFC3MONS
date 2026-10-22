#pragma once
// Sampled completion intervals. No extra waits/barriers. Sampled frames are
// diagnostic only; intervals include preceding dependencies and can overlap work.
#include "ufc_gpu_timing.h"
#include <set>
#include <tuple>
namespace ufc_resource_timing {
constexpr uint32_t N=8192, R=8;
struct Event {const char* kind;uint64_t pipeline,target,units;uint32_t width,height;};
struct Slot {uint64_t submission=0,frame=0;std::vector<Event> events;};
struct State {VkQueryPool pool=VK_NULL_HANDLE;std::array<Slot,R> slots;std::set<uint64_t> shaders;bool attempted=false;uint64_t dropped=0;};
inline State& Get(){static thread_local State s;return s;}
inline bool Enabled(){static bool on=[](){auto* p=std::getenv("UFC_RESOURCE_TIMING");return p&&p[0]=='1';}();return on;}
inline void Shader(VkPipeline p,uint64_t vs,uint64_t ps){if(!Enabled())return;auto& s=Get();if(s.shaders.insert(uint64_t(p)).second)REXGPU_INFO("[ufc-resource-shader] pipeline={} vs={:016X} ps={:016X}",uint64_t(p),vs,ps);}
inline bool Init(const rex::ui::vulkan::VulkanDevice* vd){auto& s=Get();if(s.attempted)return s.pool!=VK_NULL_HANDLE;s.attempted=true;if(!ufc_gpu_timing::Init(vd))return false;VkQueryPoolCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=N*R;if(vd->functions().vkCreateQueryPool(vd->device(),&info,nullptr,&s.pool)!=VK_SUCCESS)return false;REXGPU_INFO("[ufc-resource-init] sample_period=30 query_capacity={} no_added_waits=true",N);return true;}
inline void Poll(const rex::ui::vulkan::VulkanDevice* vd,uint64_t completed){if(!Enabled())return;auto& s=Get();if(!s.pool)return;for(uint32_t i=0;i<R;++i){auto& slot=s.slots[i];if(!slot.submission||slot.submission>completed)continue;std::vector<uint64_t> data((slot.events.size()+1)*2);auto result=ufc_gpu_timing::Get().get(vd->device(),s.pool,i*N,uint32_t(slot.events.size()+1),data.size()*8,data.data(),16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);if(result!=VK_SUCCESS)continue;bool ready=true;for(size_t j=0;j<data.size()/2;++j)ready&=bool(data[j*2+1]);if(!ready)continue;
 using Key=std::tuple<std::string,uint64_t,uint64_t,uint32_t,uint32_t>;struct Total{double ms=0;uint64_t calls=0,units=0;};std::map<Key,Total> totals;
 for(size_t j=0;j<slot.events.size();++j){auto& e=slot.events[j];if(std::string_view(e.kind)=="copy_begin")continue;auto& t=totals[{e.kind,e.pipeline,e.target,e.width,e.height}];t.ms+=double(ufc_gpu_timing::Delta(data[(j+1)*2],data[j*2]))*ufc_gpu_timing::Get().period/1e6;++t.calls;t.units+=e.units;}
 for(auto& [k,t]:totals)REXGPU_INFO("[ufc-resource] frame={} submission={} kind={} pipeline={} target={} width={} height={} calls={} units={} completion_ms={:.6f}",slot.frame,slot.submission,std::get<0>(k),std::get<1>(k),std::get<2>(k),std::get<3>(k),std::get<4>(k),t.calls,t.units,t.ms);
 REXGPU_INFO("[ufc-resource-sample] frame={} submission={} events={} dropped={} completion_ms={:.6f}",slot.frame,slot.submission,slot.events.size(),s.dropped,double(ufc_gpu_timing::Delta(data[data.size()-2],data[0]))*ufc_gpu_timing::Get().period/1e6);slot={};}}
struct Recorder {
 const rex::ui::vulkan::VulkanDevice* vd;VkCommandBuffer cmd;Slot* slot=nullptr;uint32_t base=0;uint64_t graphics=0,compute=0,target=0;uint32_t width=0,height=0;
 Recorder(const rex::ui::vulkan::VulkanDevice* device,VkCommandBuffer cb,uint64_t submission,uint64_t frame):vd(device),cmd(cb){if(!Enabled()||frame%30||!Init(vd))return;auto& s=Get();uint32_t i=submission%R;if(s.slots[i].submission){++s.dropped;return;}slot=&s.slots[i];slot->submission=submission;slot->frame=frame;base=i*N;vd->functions().vkCmdResetQueryPool(cmd,s.pool,base,N);ufc_gpu_timing::Get().write(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s.pool,base);}
 void Bind(VkPipelineBindPoint point,VkPipeline p){if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)graphics=uint64_t(p);else compute=uint64_t(p);}
 void Target(uint64_t id,uint32_t w,uint32_t h){target=id;width=w;height=h;}
 void Mark(const char*,uint64_t,uint64_t){}
 void CopyStamp(const char* kind,uint64_t id,uint64_t bytes){if(!slot)return;if(slot->events.size()+2>=N){++Get().dropped;return;}slot->events.push_back({kind,0,id,bytes,0,0});ufc_gpu_timing::Get().write(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,Get().pool,base+uint32_t(slot->events.size()));}
 void BeforeCopy(uint64_t id,uint64_t bytes){if(bytes>=262144)CopyStamp("copy_begin",id,bytes);}
 void AfterCopy(uint64_t id,uint64_t bytes){if(bytes>=262144)CopyStamp("copy_buffer",id,bytes);}

};
inline void Shutdown(const rex::ui::vulkan::VulkanDevice* vd,uint64_t completed){if(!Enabled())return;Poll(vd,completed);auto& s=Get();if(s.pool)vd->functions().vkDestroyQueryPool(vd->device(),s.pool,nullptr);s={};}
}
