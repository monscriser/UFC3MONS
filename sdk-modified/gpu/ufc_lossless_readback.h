#pragma once
// Isolated candidate. The original transfer path remains available and default.
#include <rex/graphics/vulkan/command_processor.h>
#include <rex/ui/vulkan/util.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "ufc_lossless_spirv.h"
namespace ufc_lossless_readback {
using CP=rex::graphics::vulkan::VulkanCommandProcessor;
using Device=rex::ui::vulkan::VulkanDevice;
struct Slot {
 VkBuffer output=VK_NULL_HANDLE,mirror=VK_NULL_HANDLE,reference=VK_NULL_HANDLE,source=VK_NULL_HANDLE;
 VkDeviceMemory mirror_memory=VK_NULL_HANDLE,reference_memory=VK_NULL_HANDLE;
 VkDescriptorSet set=VK_NULL_HANDLE;void* reference_mapping=nullptr;
 uint64_t source_offset=0,resource_submission=0,writer_submission=0,reference_submission=0;
 uint64_t resident_writer=0,resident_reference=0;bool resident_delta=false;
 uint32_t length=0,capacity=0,logical_length=0;bool storage=false,valid=false,last_delta=false;
};
struct Counters{uint64_t dispatch=0,bytes=0,force=0,fallback=0,ready_waits=0,comparisons=0,compare_bytes=0,mismatches=0,positive=0,early_submits=0,early_blocked=0;};
struct State {
 std::map<VkBuffer,Slot> slots;std::vector<Slot> retired;
 VkDescriptorSetLayout set_layout=VK_NULL_HANDLE;VkDescriptorPool pool=VK_NULL_HANDLE;
 VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 PFN_vkFreeDescriptorSets free_sets=nullptr;
 uint64_t frame=UINT64_MAX,last_report=0,mirror_bytes=0;int mode=0;bool attempted=false,disabled=false,control_done=false;
 uint32_t filter_offset=0,filter_length=0;
 Counters counters;
};
inline State& Get(){static thread_local State s;return s;}
inline void DestroySlot(const Device* vd,Slot& v){auto& f=vd->functions();auto d=vd->device();if(v.set&&Get().pool&&Get().free_sets)Get().free_sets(d,Get().pool,1,&v.set);if(v.reference_mapping)f.vkUnmapMemory(d,v.reference_memory);if(v.reference)f.vkDestroyBuffer(d,v.reference,nullptr);if(v.reference_memory)f.vkFreeMemory(d,v.reference_memory,nullptr);if(v.mirror){Get().mirror_bytes-=v.length;f.vkDestroyBuffer(d,v.mirror,nullptr);}if(v.mirror_memory)f.vkFreeMemory(d,v.mirror_memory,nullptr);v={};}
inline void Poll(CP& cp){auto& s=Get();for(auto it=s.retired.begin();it!=s.retired.end();){if(it->resource_submission<=cp.GetCompletedSubmission()){DestroySlot(cp.GetVulkanDevice(),*it);it=s.retired.erase(it);}else ++it;}}
inline void Report(CP& cp){auto& s=Get();auto& c=s.counters;REXGPU_INFO("[ufc-lossless] frame={} mode={} dispatch={} logical_bytes={} forced={} fallback={} ready_waits={} comparisons={} compared_bytes={} mismatches={} positive={} early_submits={} early_blocked={} slots={} retired={}",cp.GetCurrentFrame(),s.mode,c.dispatch,c.bytes,c.force,c.fallback,c.ready_waits,c.comparisons,c.compare_bytes,c.mismatches,c.positive,c.early_submits,c.early_blocked,s.slots.size(),s.retired.size());}
inline int Mode(CP& cp){auto& s=Get();if(s.frame==cp.GetCurrentFrame())return s.disabled?0:s.mode;s.frame=cp.GetCurrentFrame();Poll(cp);int mode=0;uint32_t filter_offset=0,filter_length=0;const char* value=std::getenv("UFC_LOSSLESS_READBACK");if(value){std::string v=value;if(v=="1"||v=="on")mode=1;else if(v=="verify")mode=2;else if(v=="positive")mode=3;}
 if(const char* path=std::getenv("UFC_LOSSLESS_CONTROL")){std::ifstream file(path);std::string v;if(file>>v){if(v=="off")mode=0;else if(v=="on")mode=1;else if(v=="verify")mode=2;else if(v=="positive")mode=3;std::string address;if(file>>address>>filter_length){try{filter_offset=uint32_t(std::stoul(address,nullptr,0));}catch(...){mode=0;filter_length=0;}}}}
 if(mode!=s.mode||filter_offset!=s.filter_offset||filter_length!=s.filter_length){s.mode=mode;s.filter_offset=filter_offset;s.filter_length=filter_length;REXGPU_INFO("[ufc-lossless-filter] offset=0x{:08X} length={}",filter_offset,filter_length);REXGPU_INFO("[ufc-lossless-mode] frame={} mode={} disabled={}",s.frame,mode,s.disabled);if(mode==3)s.control_done=false;}
 if(s.frame-s.last_report>=60){Report(cp);s.last_report=s.frame;}return s.disabled?0:mode;
}
inline void Forget(CP& cp,VkBuffer output){auto& s=Get();auto it=s.slots.find(output);if(it==s.slots.end())return;s.retired.push_back(it->second);s.slots.erase(it);Poll(cp);}
inline void Register(CP& cp,VkBuffer output,uint32_t capacity,bool storage){Forget(cp,output);auto& v=Get().slots[output];v.output=output;v.capacity=capacity;v.storage=storage;}
// Command recording is deferred until EndSubmission. A queued overwrite does
// not replace the bytes resident in a mapped output buffer yet.
inline void BeforeWrite(CP& cp,Slot& v){if(v.writer_submission!=cp.GetCurrentSubmission()){v.resident_writer=v.writer_submission;v.resident_reference=v.reference_submission;v.resident_delta=v.last_delta;}v.writer_submission=cp.GetCurrentSubmission();}
struct VisibleVersion{uint64_t writer,reference;bool delta;};
inline VisibleVersion Visible(CP& cp,const Slot& v){if(v.writer_submission==cp.GetCurrentSubmission())return {v.resident_writer,v.resident_reference,v.resident_delta};return {v.writer_submission,v.reference_submission,v.last_delta};}
inline void Original(CP& cp,VkBuffer output,uint32_t length){auto it=Get().slots.find(output);if(it==Get().slots.end())return;auto& v=it->second;BeforeWrite(cp,v);v.logical_length=length;v.valid=false;v.last_delta=false;v.reference_submission=0;(void)length;}
inline bool Init(CP& cp){auto& s=Get();if(s.attempted)return s.pipeline!=VK_NULL_HANDLE;s.attempted=true;auto* vd=cp.GetVulkanDevice();auto& f=vd->functions();auto d=vd->device();
 s.free_sets=reinterpret_cast<PFN_vkFreeDescriptorSets>(vd->vulkan_instance()->functions().vkGetDeviceProcAddr(d,"vkFreeDescriptorSets"));if(!s.free_sets)return false;
 if(vd->properties().maxPerStageDescriptorStorageBuffers<3)return false;
 std::array<VkDescriptorSetLayoutBinding,3> b{};for(uint32_t i=0;i<3;++i){b[i].binding=i;b[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;b[i].descriptorCount=1;b[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
 VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=3;sl.pBindings=b.data();if(f.vkCreateDescriptorSetLayout(d,&sl,nullptr,&s.set_layout)!=VK_SUCCESS)return false;
 VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,8};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&s.set_layout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;if(f.vkCreatePipelineLayout(d,&pl,nullptr,&s.layout)!=VK_SUCCESS)return false;
 s.pipeline=rex::ui::vulkan::util::CreateComputePipeline(vd,s.layout,kUfcLosslessSpirv,sizeof(kUfcLosslessSpirv));if(!s.pipeline)return false;
 VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3072};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;dp.maxSets=1024;dp.poolSizeCount=1;dp.pPoolSizes=&ps;if(f.vkCreateDescriptorPool(d,&dp,nullptr,&s.pool)!=VK_SUCCESS){f.vkDestroyPipeline(d,s.pipeline,nullptr);s.pipeline=VK_NULL_HANDLE;return false;}
 REXGPU_INFO("[ufc-lossless-init] block_bytes=128 minimum_bytes=262144 maximum_bytes=1048576 resident_generations=true shader_validated_standalone=true");return true;
}
inline bool Prepare(CP& cp,Slot& v,VkBuffer source,uint32_t offset,uint32_t length,int mode){auto* vd=cp.GetVulkanDevice();auto& f=vd->functions();auto d=vd->device();
 if(v.mirror&&(v.source!=source||v.source_offset!=offset||v.length!=length))return false;
 if(!v.mirror){
  if(Get().mirror_bytes+length>256ull*1024*1024)return false;
  if(!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(vd,length,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,rex::ui::vulkan::util::MemoryPurpose::kDeviceLocal,v.mirror,v.mirror_memory))return false;
  v.source=source;v.source_offset=offset;v.length=length;v.valid=false;Get().mirror_bytes+=length;
 }
 if(!v.set){VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};a.descriptorPool=Get().pool;a.descriptorSetCount=1;a.pSetLayouts=&Get().set_layout;if(f.vkAllocateDescriptorSets(d,&a,&v.set)!=VK_SUCCESS)return false;
  std::array<VkDescriptorBufferInfo,3> info{{{source,offset,length},{v.mirror,0,length},{v.output,0,length}}};std::array<VkWriteDescriptorSet,3> w{};for(uint32_t i=0;i<3;++i){w[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w[i].dstSet=v.set;w[i].dstBinding=i;w[i].descriptorCount=1;w[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;w[i].pBufferInfo=&info[i];}f.vkUpdateDescriptorSets(d,3,w.data(),0,nullptr);
 }
 if(mode>=2&&!v.reference){
  if(!rex::ui::vulkan::util::CreateDedicatedAllocationBuffer(vd,length,VK_BUFFER_USAGE_TRANSFER_DST_BIT,rex::ui::vulkan::util::MemoryPurpose::kReadback,v.reference,v.reference_memory))return false;
  if(f.vkMapMemory(d,v.reference_memory,0,VK_WHOLE_SIZE,0,&v.reference_mapping)!=VK_SUCCESS){f.vkDestroyBuffer(d,v.reference,nullptr);f.vkFreeMemory(d,v.reference_memory,nullptr);v.reference=VK_NULL_HANDLE;v.reference_memory=VK_NULL_HANDLE;return false;}
 }
 return true;
}
inline bool Record(CP& cp,VkBuffer source,uint32_t offset,VkBuffer output,uint32_t length,bool allow){int mode=Mode(cp);auto& s=Get();auto it=s.slots.find(output);if(!mode||!allow||it==s.slots.end())return false;auto& v=it->second;auto* vd=cp.GetVulkanDevice();auto& p=vd->properties();
 if(s.filter_length&&(offset!=s.filter_offset||length!=s.filter_length))return false;
 bool eligible=v.storage&&length>=262144&&length<=1048576u&&!(length&15)&&uint64_t(offset)+length<=0x20000000ull&&length<=p.maxStorageBufferRange&&offset%p.minStorageBufferOffsetAlignment==0;
 if(!eligible||!Init(cp)||!Prepare(cp,v,source,offset,length,mode)){++s.counters.fallback;return false;}
 cp.PushBufferMemoryBarrier(source,offset,length,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,false);
 cp.PushBufferMemoryBarrier(v.mirror,0,length,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,false);
 cp.PushBufferMemoryBarrier(output,0,length,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,false);
 cp.SubmitBarriers(true);cp.BindExternalComputePipeline(s.pipeline);auto& commands=cp.deferred_command_buffer();commands.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,s.layout,0,1,&v.set,0,nullptr);uint32_t push[2]={length/16,v.valid?0u:1u};commands.CmdVkPushConstants(s.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);commands.CmdVkDispatch((length/16+63)/64,1,1);
 cp.PushBufferMemoryBarrier(output,0,length,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
 BeforeWrite(cp,v);v.logical_length=length;
 ++s.counters.dispatch;s.counters.bytes+=length;s.counters.force+=!v.valid;v.valid=true;v.last_delta=true;v.resource_submission=cp.GetCurrentSubmission();v.reference_submission=0;
 if(mode>=2){cp.PushBufferMemoryBarrier(v.reference,0,length,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,false);cp.SubmitBarriers(true);VkBufferCopy region{offset,0,length};commands.CmdVkCopyBuffer(source,v.reference,1,&region);cp.PushBufferMemoryBarrier(v.reference,0,length,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);v.reference_submission=v.writer_submission;}
 return true;
}
inline bool EarlyEnabled(){static const bool on=[](){const char* p=std::getenv("UFC_EARLY_READBACK_SUBMIT");return p&&p[0]=='1';}();return on;}
inline bool Small(uint32_t length){return length>=262144&&length<=1048576&&!(length&15);}
inline void EarlyResult(bool submitted){if(submitted)++Get().counters.early_submits;else ++Get().counters.early_blocked;}
inline bool NeedsWait(CP& cp,VkBuffer output){auto it=Get().slots.find(output);if(it==Get().slots.end())return false;auto version=Visible(cp,it->second);return (version.delta||it->second.last_delta||(EarlyEnabled()&&Small(it->second.logical_length)))&&version.writer>cp.GetCompletedSubmission();}
inline uint64_t Writer(CP& cp,VkBuffer output){auto it=Get().slots.find(output);return it==Get().slots.end()?0:Visible(cp,it->second).writer;}
inline void Waited(){++Get().counters.ready_waits;}
inline const void* CheckedRead(CP& cp,VkBuffer output,const void* data,uint32_t length){auto& s=Get();auto it=s.slots.find(output);if(it==s.slots.end())return data;auto& v=it->second;auto version=Visible(cp,v);if(!version.delta||!version.writer||version.reference!=version.writer||!v.reference_mapping||v.length!=length)return data;
 if(version.writer>cp.GetCompletedSubmission()){REXGPU_ERROR("[ufc-lossless] attempted reference comparison before completion");s.disabled=true;return data;}
 VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=v.reference_memory;range.size=VK_WHOLE_SIZE;cp.GetVulkanDevice()->functions().vkInvalidateMappedMemoryRanges(cp.GetVulkanDevice()->device(),1,&range);++s.counters.comparisons;s.counters.compare_bytes+=length;
 if(std::memcmp(data,v.reference_mapping,length)){++s.counters.mismatches;s.disabled=true;REXGPU_ERROR("[ufc-lossless-MISMATCH] bytes={} mode={} fallback_reference=true disabled=true",length,s.mode);return v.reference_mapping;}
 if(s.mode==3&&!s.control_done){std::array<uint8_t,16> probe;std::memcpy(probe.data(),v.reference_mapping,probe.size());probe[0]^=1;bool detected=std::memcmp(data,probe.data(),probe.size())!=0;probe[0]^=1;bool restored=std::memcmp(data,probe.data(),probe.size())==0;s.control_done=detected&&restored;s.counters.positive+=s.control_done;REXGPU_INFO("[ufc-lossless-positive] detected={} restored={} probe_bytes=16",detected,restored);if(!s.control_done)s.disabled=true;}
 return data;
}
inline void Shutdown(CP& cp){auto& s=Get();Report(cp);auto* vd=cp.GetVulkanDevice();for(auto& [key,v]:s.slots)DestroySlot(vd,v);for(auto& v:s.retired)DestroySlot(vd,v);auto& f=vd->functions();auto d=vd->device();if(s.pipeline)f.vkDestroyPipeline(d,s.pipeline,nullptr);if(s.layout)f.vkDestroyPipelineLayout(d,s.layout,nullptr);if(s.pool)f.vkDestroyDescriptorPool(d,s.pool,nullptr);if(s.set_layout)f.vkDestroyDescriptorSetLayout(d,s.set_layout,nullptr);s={};}
}
