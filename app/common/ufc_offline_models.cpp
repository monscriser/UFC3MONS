#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdint>
REXCVAR_DEFINE_BOOL(ufc_career_model_buffer,true,"UFC","Use the Offline Updater model heap selection (27 instead of 21)");
extern "C" int64_t UfcCareerModelHeapId(){
  static std::atomic<uint64_t> calls{0};auto n=++calls;const auto id=REXCVAR_GET(ufc_career_model_buffer)?27:21;
  if(n<=4||(n&(n-1))==0)REXLOG_INFO("[ufc-model-buffer] calls={} heap_id={} original=21 updater=27",n,id);
  return id;
}
