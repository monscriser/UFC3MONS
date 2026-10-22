#pragma once
#include <string>
#include <string_view>
#include <cstdint>
namespace ufc {
inline std::string DecodeUtf16LE(std::string_view input){
 std::string out;
 auto append=[&](uint32_t cp){
  if(cp<0x80)out+=char(cp);
  else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
  else if(cp<0x10000){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
  else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
 };
 auto unit=[&](size_t i){return uint32_t(uint8_t(input[i]))|(uint32_t(uint8_t(input[i+1]))<<8);};
 for(size_t i=2;i+1<input.size();i+=2){
  uint32_t cp=unit(i);
  if(cp>=0xd800&&cp<=0xdbff){
   if(i+3<input.size()){auto low=unit(i+2);if(low>=0xdc00&&low<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);i+=2;}else cp=0xfffd;}
   else cp=0xfffd;
  }else if(cp>=0xdc00&&cp<=0xdfff)cp=0xfffd;
  append(cp);
 }
 if(input.size()&1)append(0xfffd);
 return out;
}
}
