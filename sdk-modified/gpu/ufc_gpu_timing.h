#pragma once
// Diagnostics only: no fence waits, query-result WAIT flags or queue-order changes.
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <vector>
#include <rex/ui/vulkan/device.h>
#include <rex/logging.h>
namespace ufc_gpu_timing {
inline bool Enabled(){static const bool on=[](){const char* p=std::getenv("UFC_GPU_TIMING");return p&&p[0]=='1';}();return on;}
inline uint64_t Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Slot{uint64_t submission=0,frame=0,cpu_record_ns=0,cpu_submit_ns=0;bool closes=false;};
struct Frame{uint64_t first=UINT64_MAX,last=0,sum=0,submissions=0,record_ns=0,submit_ns=0;};
struct State{
 VkQueryPool pool=VK_NULL_HANDLE;PFN_vkCmdWriteTimestamp write=nullptr;PFN_vkGetQueryPoolResults get=nullptr;
 std::array<Slot,256> slots{};std::map<uint64_t,Frame> frames;
 uint32_t bits=0;double period=0;bool attempted=false;uint64_t dropped=0;
};
inline State& Get(){static thread_local State s;return s;}
inline bool Init(const rex::ui::vulkan::VulkanDevice* vd){
 auto& s=Get();if(s.attempted)return s.pool!=VK_NULL_HANDLE;s.attempted=true;
 const auto& ins=vd->vulkan_instance()->functions();
 VkPhysicalDeviceProperties properties{};ins.vkGetPhysicalDeviceProperties(vd->physical_device(),&properties);
 uint32_t count=0;ins.vkGetPhysicalDeviceQueueFamilyProperties(vd->physical_device(),&count,nullptr);
 std::vector<VkQueueFamilyProperties> queues(count);ins.vkGetPhysicalDeviceQueueFamilyProperties(vd->physical_device(),&count,queues.data());
 s.bits=queues.at(vd->queue_family_graphics_compute()).timestampValidBits;s.period=properties.limits.timestampPeriod;
 s.write=reinterpret_cast<PFN_vkCmdWriteTimestamp>(ins.vkGetDeviceProcAddr(vd->device(),"vkCmdWriteTimestamp"));
 s.get=reinterpret_cast<PFN_vkGetQueryPoolResults>(ins.vkGetDeviceProcAddr(vd->device(),"vkGetQueryPoolResults"));
 if(!s.bits||!s.write||!s.get||s.period<=0){REXGPU_WARN("[ufc-gpu-time] unsupported bits={} period={}",s.bits,s.period);return false;}
 VkQueryPoolCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=512;
 if(vd->functions().vkCreateQueryPool(vd->device(),&info,nullptr,&s.pool)!=VK_SUCCESS){s.pool=VK_NULL_HANDLE;return false;}
 REXGPU_INFO("[ufc-gpu-time] initialized period_ns={} valid_bits={} slots=256 nonblocking=true",s.period,s.bits);return true;
}
inline uint64_t Delta(uint64_t end,uint64_t begin){auto bits=Get().bits;return bits==64?end-begin:(end-begin)&((uint64_t(1)<<bits)-1);}
inline void Poll(const rex::ui::vulkan::VulkanDevice* vd,uint64_t completed){
 if(!Enabled())return;auto& s=Get();if(!s.pool)return;
 // Completed submissions are read in submission order, independent of ring index.
 std::vector<uint32_t> ready;for(uint32_t i=0;i<256;++i)if(s.slots[i].submission&&s.slots[i].submission<=completed)ready.push_back(i);
 std::sort(ready.begin(),ready.end(),[&](auto a,auto b){return s.slots[a].submission<s.slots[b].submission;});
 for(uint32_t i:ready){auto& slot=s.slots[i];uint64_t data[4]{};
  const VkResult result=s.get(vd->device(),s.pool,i*2,2,sizeof(data),data,16,VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if(result!=VK_SUCCESS||!data[1]||!data[3])continue;
  auto& f=s.frames[slot.frame];f.first=std::min(f.first,data[0]);f.last=std::max(f.last,data[2]);f.sum+=Delta(data[2],data[0]);++f.submissions;f.record_ns+=slot.cpu_record_ns;f.submit_ns+=slot.cpu_submit_ns;
  if(slot.closes){
   REXGPU_INFO("[ufc-gpu-frame] frame={} submission={} gpu_envelope_ms={:.4f} gpu_sum_submission_ms={:.4f} submissions={} record_cpu_ms={:.4f} submit_cpu_ms={:.4f} query_dropped={}",slot.frame,slot.submission,double(Delta(f.last,f.first))*s.period/1e6,double(f.sum)*s.period/1e6,f.submissions,double(f.record_ns)/1e6,double(f.submit_ns)/1e6,s.dropped);
   s.frames.erase(slot.frame);
  }
  slot={};
 }
}
inline int Begin(const rex::ui::vulkan::VulkanDevice* vd,VkCommandBuffer cmd,uint64_t submission,uint64_t frame,bool closes){
 if(!Enabled()||!Init(vd))return -1;auto& s=Get();const uint32_t i=uint32_t(submission%256);
 if(s.slots[i].submission){++s.dropped;return -1;}
 s.slots[i]={submission,frame,0,0,closes};vd->functions().vkCmdResetQueryPool(cmd,s.pool,i*2,2);
 s.write(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,s.pool,i*2);return int(i);
}
inline void End(VkCommandBuffer cmd,int i,uint64_t record_ns){if(i<0)return;auto& s=Get();s.slots[i].cpu_record_ns=record_ns;s.write(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s.pool,uint32_t(i)*2+1);}
inline void Submitted(int i,uint64_t submit_ns){if(i>=0)Get().slots[i].cpu_submit_ns=submit_ns;}
inline void Shutdown(const rex::ui::vulkan::VulkanDevice* vd,uint64_t completed){if(!Enabled())return;Poll(vd,completed);auto& s=Get();if(s.pool)vd->functions().vkDestroyQueryPool(vd->device(),s.pool,nullptr);s={};}
}
