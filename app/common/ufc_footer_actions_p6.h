#pragma once
#include <string_view>
#include <string>
#include <atomic>
#include <rex/cvar.h>
#include <rex/logging.h>
REXCVAR_DEFINE_BOOL(ufc_footer_actions_fix,true,"UFC","Recognize native localized footer actions before composing Reborn's extra prompts");
REXCVAR_DEFINE_BOOL(ufc_footer_actions_diag,false,"Tests","Log a bounded set of native footer strings and composition decisions");
namespace ufc_footer_actions {
inline bool Word(std::u16string_view text,std::u16string_view word){
 size_t at=0;
 auto alpha=[](char16_t c){return (c>=u'A'&&c<=u'Z')||(c>=u'a'&&c<=u'z')||c>=0xc0;};
 while((at=text.find(word,at))!=text.npos){
  const size_t end=at+word.size();
  if((!at||!alpha(text[at-1]))&&(end==text.size()||!alpha(text[end])))return true;
  ++at;
 }
 return false;
}
struct Actions{bool accept=false,back=false;};
inline Actions Classify(std::u16string_view text){
 Actions a;
 for(auto word:{u"ACCEPT",u"ACEITAR",u"ACEPTAR",u"VALIDER",u"ACCETTA",u"OK"})a.accept|=Word(text,word);
 for(auto word:{u"BACK",u"VOLTAR",u"VOLVER",u"RETOUR",u"INDIETRO",u"ZURÜCK"})a.back|=Word(text,word);
 a.accept|=text.find(u"<&Cross_A>")!=text.npos;
 a.back|=text.find(u"<&Circle_B>")!=text.npos;
 return a;
}
inline std::u16string Read(const uint8_t* base,uint32_t address,size_t limit){
 std::u16string s;s.reserve(limit);
 for(size_t i=0;i<limit;++i){const auto at=address+uint32_t(i*2);char16_t c=char16_t((uint16_t(base[at])<<8)|base[at+1]);if(!c)break;s+=c;}
 return s;
}
inline Actions Inspect(const uint8_t* base,uint32_t address){
 const auto text=Read(base,address,480);auto a=Classify(text);
 if(REXCVAR_GET(ufc_footer_actions_diag)){
  static std::atomic_uint calls=0;
  if(calls.fetch_add(1)<24){std::string ascii;for(char16_t c:text)ascii+=c<128?char(c):'?';REXLOG_INFO("[ufc-footer-actions] enabled={} accept={} back={} units={} input={}",REXCVAR_GET(ufc_footer_actions_fix),a.accept,a.back,text.size(),ascii);}
 }
 return a;
}
inline uint32_t PrefixLength(const uint8_t* base,uint32_t address,uint32_t fallback){
 return REXCVAR_GET(ufc_footer_actions_fix)?uint32_t(Read(base,address,39).size()):fallback;
}
}
