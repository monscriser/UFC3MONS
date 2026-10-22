#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/runtime.h>
#include <rex/graphics/graphics_system.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#endif
#include <atomic>
#include "ufc_renderdoc_capture.h"
std::atomic<int> shadow_capture_request{0};
void ShadowCaptureTick(){
 static ufc_renderdoc::Capture capture;
 auto result=capture.Tick(shadow_capture_request.exchange(0)!=0);
 if(result==ufc_renderdoc::Result::Started)REXLOG_INFO("[ufc-gpu-capture] begin guest_swap_span=20 active={}",capture.api->IsFrameCapturing());
 else if(result==ufc_renderdoc::Result::Ended||result==ufc_renderdoc::Result::EndFailed)REXLOG_INFO("[ufc-gpu-capture] end success={}",result==ufc_renderdoc::Result::Ended);
 else if(result==ufc_renderdoc::Result::Unavailable)REXLOG_WARN("[ufc-gpu-capture] RenderDoc API unavailable; launch through RenderDoc to capture");
}

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>

// Reproducible in-game controller fixture. Does not send any OS keyboard input.
// Disabled unless an explicit test command file is supplied on the command line.
REXCVAR_DEFINE_STRING(ufc_test_input_file, "", "Tests", "Isolated functional test controller file");
extern "C" void __real___imp__XamInputGetState(PPCContext&,uint8_t*);
extern "C" void __wrap___imp__XamInputGetState(PPCContext& ctx,uint8_t* base) {
 const auto user=ctx.r3.u32,flags=ctx.r4.u32,ptr=ctx.r5.u32;
 __real___imp__XamInputGetState(ctx,base);
 static const auto file=REXCVAR_GET(ufc_test_input_file);
 if(file.empty()||user!=0||!ptr||((flags&0xff)&&(flags&1)==0))return;
 static std::mutex mutex;std::lock_guard lock(mutex);
 using Clock=std::chrono::steady_clock;
 static auto check=Clock::now(),until=Clock::now();
 static uint64_t serial=0;static uint32_t packet=0;static uint16_t buttons=0;static int16_t lx=0,ly=0;
 auto now=Clock::now();
 if(now>=check){check=now+std::chrono::milliseconds(50);std::ifstream in(file);uint64_t next=0;std::string command;
  if(in>>next>>command;in&&next>serial){
   if(command=="pad"){unsigned mask=0,hold=0;int x=0,y=0;
    if(in>>mask>>x>>y>>hold;in&&mask<=65535&&hold<=3000&&x>=-32768&&x<=32767&&y>=-32768&&y<=32767){
     serial=next;buttons=uint16_t(mask);lx=int16_t(x);ly=int16_t(y);until=now+std::chrono::milliseconds(hold);++packet;
     REXLOG_INFO("[ufc-test-input] sequence={} buttons={} lx={} ly={} hold={}",serial,mask,x,y,hold);
    }
   }else if(command=="capture"){std::string path;if(in>>std::quoted(path);in){
    serial=next;rex::ui::RawImage image;
    auto* presenter=REX_KERNEL_STATE()->emulator()->graphics_system()->presenter();
    if(presenter&&presenter->CaptureGuestOutput(image)){
     std::ofstream out(path,std::ios::binary);out<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
     for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)out.write(reinterpret_cast<const char*>(image.data.data()+y*image.stride+x*4),3);
     REXLOG_INFO("[ufc-test-input] captured {} {}x{}",path,image.width,image.height);
    }else REXLOG_WARN("[ufc-test-input] capture unavailable");
   }}else if(command=="gpu_capture"){serial=next;shadow_capture_request.store(1);REXLOG_INFO("[ufc-gpu-capture] requested sequence={}",serial);
   }else if(command=="invalidate_gpu"){serial=next;static_cast<rex::graphics::GraphicsSystem*>(REX_KERNEL_STATE()->emulator()->graphics_system())->InvalidateGpuMemory();REXLOG_INFO("[ufc-test-input] GPU memory invalidation requested");
   }else if(command=="quit"){serial=next;REX_KERNEL_STATE()->emulator()->app_context()->RequestDeferredQuit();
   }else if(command=="set"){std::string key,value;if(in>>key>>value;key=="ufc_shadow_receiver_positive"||key=="ufc_shadow_gpu_trace"||key=="ufc_shadow_gpu_trace_path"||key=="execute_unclipped_draw_vs_on_cpu"||key=="ufc_shadow_flip_receiver_y"||key=="ufc_shadow_precise"||key=="ufc_fps_limit"||key=="async_shader_compilation"||key=="clear_memory_page_state"||key=="depth_float24_convert_in_pixel_shader"||key=="depth_float24_round"||key=="readback_resolve"||key=="ht_direct_resolve"||key=="ht_direct_resolve_verify"||key=="ht_direct_resolve_verify_break"||key=="ht_float_constant_runs"||key=="ht_float_constant_runs_verify"||key=="ht_float_constant_runs_verify_break"||key=="ufc_spin_sleep_us"||key=="ufc_spin_backoff"||key=="ufc_cp_fast_registers"||key=="ufc_cp_verify"||key=="ufc_cp_verify_break"){serial=next;rex::cvar::SetFlagByName(key,value);REXLOG_INFO("[ufc-test-input] {}={}",key,value);}
   }else if(command=="readback"){std::string mode;if(in>>mode;mode=="none"||mode=="full"){
    serial=next;rex::cvar::SetFlagByName("readback_resolve",mode);REXLOG_INFO("[ufc-test-input] readback_resolve={}",mode);
   }}
  }
 }
 if(now>=until&&(buttons||lx||ly)){buttons=0;lx=ly=0;++packet;}
 rex::input::X_INPUT_STATE state{};state.packet_number=packet;state.gamepad.buttons=buttons;state.gamepad.thumb_lx=lx;state.gamepad.thumb_ly=ly;
 std::memcpy(base+ptr,&state,sizeof(state));ctx.r3.u64=0;
}
