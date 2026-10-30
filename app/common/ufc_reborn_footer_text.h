#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
namespace ufc_footer {
struct Slot{uint32_t address;size_t capacity;std::string before,after;};
inline std::string Utf16(const std::u16string& text){std::string out;for(char16_t c:text){out+=char(c>>8);out+=char(c&255);}return out;}
inline std::vector<Slot> Slots(int language,bool native_buttons){
 if(language<=0||language>5)return {};
 static const char16_t* accept[]={u"ACEITAR",u"ACEPTAR",u"VALIDER",u"ACCETTA",u"OK"};
 static const char16_t* back[]={u"VOLTAR",u"VOLVER",u"RETOUR",u"INDIETRO",u"ZURÜCK"};
 static const char16_t* movement[]={u"MOVER / ROLAR",u"MOVER / DESPLAZAR",u"DÉPLACER / DÉFILER",u"MUOVI / SCORRI",u"BEWEGEN / SCROLLEN"};
 static const char16_t* random[]={u"ALEATÓRIO",u"ALEATORIO",u"ALÉATOIRE",u"CASUALE",u"ZUFÄLLIG"};
 static const char16_t* attire[]={u"TRAJE ALTERNATIVO",u"ATUENDO ALTERNATIVO",u"TENUE ALTERNATIVE",u"TENUTA ALTERNATIVA",u"ALTERNATIVE KLEIDUNG"};
 static const char16_t* skip[]={u"PULAR MÚSICA",u"SALTAR CANCIÓN",u"PASSER LA MUSIQUE",u"SALTA BRANO",u"LIED ÜBERSPRINGEN"};
 const size_t i=size_t(language-1);
 const std::u16string a=std::u16string(u"<&Cross_A>")+accept[i];
 const std::u16string b=std::u16string(u" <&Circle_B>")+back[i];
 std::vector<Slot> result={
  {0x83684E60,256,Utf16(u"<&Dir> / <&Lstick> MOVE / SCROLL   <&L2_LT> / <&R2_RT> UFC / PRIDE / RANDOM"),Utf16(std::u16string(u"<&Dir> / <&Lstick> ")+movement[i]+u"   <&L2_LT> / <&R2_RT> UFC / PRIDE / "+random[i])},
  {0x83684F60,80,Utf16(u"<&Cross_A>ACCEPT  <&Circle_B>BACK  "),native_buttons?std::string{}:Utf16(a+b)},
  {0x83684FB0,80,Utf16(u"<&Cross_A>ACCEPT  "),native_buttons?std::string{}:Utf16(a)},
  {0x83685000,256,Utf16(u"<&Cross_A>ACCEPT  <&Circle_B>BACK  <&L3_LS>ALT ATTIRE  <&R3_RS>SKIP SONG"),Utf16(a+b+u" <&L3_LS>"+attire[i]+u" <&R3_RS>"+skip[i])}
 };
 for(auto& slot:result){if(slot.before.size()+2>slot.capacity||slot.after.size()+2>slot.capacity)throw std::runtime_error("Reborn footer text exceeds its reserved slot");slot.before.resize(slot.capacity,0);slot.after.resize(slot.capacity,0);}
 return result;
}
}
