#pragma once
#include "renderdoc_app.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace ufc_renderdoc {
enum class Result{Idle,Started,Ended,Unavailable,EndFailed};
struct Capture{
 RENDERDOC_API_1_6_0* api=nullptr;int remaining=0;
 Result Tick(bool request){
  if(request&&!remaining){
#ifdef _WIN32
   auto module=GetModuleHandleW(L"renderdoc.dll");
   auto get=module?reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module,"RENDERDOC_GetAPI")):nullptr;
#else
   auto get=reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(RTLD_DEFAULT,"RENDERDOC_GetAPI"));
   void* module=nullptr;
   if(!get){module=dlopen("librenderdoc.so",RTLD_NOW|RTLD_NOLOAD);if(module)get=reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(module,"RENDERDOC_GetAPI"));}
#endif
   bool available=get&&get(eRENDERDOC_API_Version_1_6_0,reinterpret_cast<void**>(&api));
#ifndef _WIN32
   if(module)dlclose(module);
#endif
   if(!available||!api){api=nullptr;return Result::Unavailable;}
   api->SetCaptureOptionU32(eRENDERDOC_Option_RefAllResources,1);
   api->StartFrameCapture(nullptr,nullptr);remaining=20;return Result::Started;
  }
  if(remaining&&!--remaining)return api->EndFrameCapture(nullptr,nullptr)?Result::Ended:Result::EndFailed;
  return Result::Idle;
 }
};
}
