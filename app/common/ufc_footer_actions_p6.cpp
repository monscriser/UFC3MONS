#include "ufc_footer_actions_p6.h"
#include "ufc_footer_actions_api_p6.h"
uint32_t UfcFooterInspect(const uint8_t* base,uint32_t address){
 const bool enabled=REXCVAR_GET(ufc_footer_actions_fix);
 if(!enabled&&!REXCVAR_GET(ufc_footer_actions_diag))return 0;
 const auto a=ufc_footer_actions::Inspect(base,address);
 return (enabled?4:0)|(a.accept?1:0)|(a.back?2:0);
}
uint32_t UfcFooterPrefixLength(const uint8_t* base,uint32_t address,uint32_t fallback){return ufc_footer_actions::PrefixLength(base,address,fallback);}
