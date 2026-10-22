#pragma once
#include "ufc_library.h"
#include <set>
namespace ufc {
inline constexpr const char* LanguageCodes[]={"EN","PT","ES","FR","IT","DE"};
inline constexpr uint32_t LanguageIds[]={1,9,5,4,6,3};
namespace ytx {
struct Text {uint32_t id;std::string bytes;bool operator==(const Text&)const=default;};
struct Group{uint32_t id;std::vector<Text> strings;bool operator==(const Group&)const=default;};
inline uint32_t Get(const std::string& data,size_t p){if(p+4>data.size())throw std::runtime_error("Tabela de idioma incompleta.");return uint32_t(uint8_t(data[p]))<<24|uint32_t(uint8_t(data[p+1]))<<16|uint32_t(uint8_t(data[p+2]))<<8|uint8_t(data[p+3]);}
inline void Put(std::string& data,size_t p,uint32_t value){if(p+4>data.size())data.resize(p+4);for(int i=0;i<4;++i)data[p+i]=char(value>>(24-8*i));}
inline void Pad(std::string& data){while(data.size()%4)data+='\0';}
inline std::vector<Group> Parse(const std::string& file){
 if(file.size()<48||file.size()>8*1024*1024||file.substr(0,4)!="DUMY"||file.substr(24,4)!="POF0")throw std::runtime_error("Formato YTX de idioma nao reconhecido.");
 auto data=file.substr(32,Get(file,28));auto count=Get(data,0),directory=Get(data,4);if(!count||count>256||directory+size_t(count)*12>data.size())throw std::runtime_error("Diretorio YTX invalido.");
 std::vector<Group> groups;std::set<uint32_t> ids;
 for(uint32_t i=0;i<count;++i){auto p=directory+i*12;Group group{Get(data,p),{}};auto n=Get(data,p+4),off=Get(data,p+8);if(n>100000||off+size_t(n)*8>data.size()||!ids.insert(group.id).second)throw std::runtime_error("Grupo YTX invalido.");std::set<uint32_t> keys;
  for(uint32_t j=0;j<n;++j){auto at=off+j*8,id=Get(data,at),begin=Get(data,at+4);size_t end=begin;if(begin%2)throw std::runtime_error("Texto YTX desalinhado.");while(end+2<=data.size()&&data.substr(end,2)!=std::string(2,0))end+=2;if(end+2>data.size()||!keys.insert(id).second)throw std::runtime_error("Texto YTX invalido.");group.strings.push_back({id,data.substr(begin,end-begin)});}
  groups.push_back(std::move(group));
 }return groups;
}
inline std::string Serialize(const std::vector<Group>& groups){
 std::string data(8+groups.size()*12,0);std::vector<uint32_t> pointers{4};Put(data,0,uint32_t(groups.size()));Put(data,4,8);
 struct Slot{uint32_t at;const std::string* text;};std::vector<Slot> slots;
 for(size_t i=0;i<groups.size();++i){auto p=uint32_t(8+i*12);Put(data,p,groups[i].id);Put(data,p+4,uint32_t(groups[i].strings.size()));Put(data,p+8,uint32_t(data.size()));pointers.push_back(p+8);
  for(auto& text:groups[i].strings){auto at=uint32_t(data.size());data.resize(at+8);Put(data,at,text.id);slots.push_back({at+4,&text.bytes});pointers.push_back(at+4);}
 }
 for(auto& slot:slots){Pad(data);Put(data,slot.at,uint32_t(data.size()));data+=*slot.text;data.append(2,0);}Pad(data);
 std::sort(pointers.begin(),pointers.end());std::string relocation;uint32_t last=0;for(auto p:pointers){auto delta=(p-last)/4;if(delta<64)relocation+=char(0x40|delta);else if(delta<16384){relocation+=char(0x80|(delta>>8));relocation+=char(delta);}else{relocation+=char(0xc0|(delta>>24));relocation+=char(delta>>16);relocation+=char(delta>>8);relocation+=char(delta);}last=p;}Pad(relocation);
 std::string out="DUMY";out.resize(24,0);Put(out,4,16);out+="POF0";Put(out,28,uint32_t(data.size()));out+=data;auto p=out.size();out+="POF0";Put(out,p+4,uint32_t(relocation.size()));out+=relocation;return out;
}
inline std::string Utf16(const char16_t* text){std::string bytes;for(;*text;++text){bytes+=char(*text>>8);bytes+=char(*text&255);}return bytes;}
inline std::string MergeNames(const std::string& english,const std::string& translated,int language=1){
 auto names=Parse(english),target=Parse(translated);
 auto group=[](auto& groups,uint32_t id){return std::find_if(groups.begin(),groups.end(),[&](const Group& g){return g.id==id;});};
 auto source=group(names,0x050c5d71),dest=group(target,0x050c5d71);
 if(source==names.end()||dest==target.end()||source->strings.size()<1000)throw std::runtime_error("Tabela do elenco Reborn nao encontrada.");
 *dest=*source;
 // CAF name presets also contain Reborn fighter first names and nicknames.
 source=group(names,0x3a97a3a9);dest=group(target,0x3a97a3a9);
 if(source!=names.end()&&dest!=target.end())*dest=*source;
 auto put=[&](uint32_t gid,uint32_t key,const std::string& bytes){auto g=group(target,gid);if(g==target.end())throw std::runtime_error("Grupo de traducao ausente.");auto t=std::find_if(g->strings.begin(),g->strings.end(),[&](const Text& v){return v.id==key;});if(t==g->strings.end())g->strings.push_back({key,bytes});else t->bytes=bytes;};
 // Preserve only these proper-name edits in the mixed generic group.
 source=group(names,0x050c5d78);
 if(source!=names.end())for(uint32_t key:{0x05244A98u,0x0424492Bu,0x032447B5u,0x7629798Du,0xDE509F8Eu,0xDB509B3Cu,0xDB4E5C5Eu,0xD44E515Bu}){auto t=std::find_if(source->strings.begin(),source->strings.end(),[&](const Text& v){return v.id==key;});if(t!=source->strings.end())put(source->id,key,t->bytes);}
 // Reborn repurposes these keys: official translations describe the old actions.
 if(language>=1&&language<=5){const size_t i=size_t(language-1);
  static const char16_t* skip[]={u"PULAR MÚSICA",u"SALTAR CANCIÓN",u"PASSER LA MUSIQUE",u"SALTA BRANO",u"LIED ÜBERSPRINGEN"};
  static const char16_t* attire[]={u"TRAJE ALTERNATIVO",u"ATUENDO ALTERNATIVO",u"TENUE ALTERNATIVE",u"TENUTA ALTERNATIVA",u"ALTERNATIVE KLEIDUNG"};
  static const char16_t* id[]={u"ID ONLINE",u"ID ONLINE",u"ID EN LIGNE",u"ID ONLINE",u"ONLINE-ID"};
  static const char16_t* invite[]={u"<&L3_LS> Receber convites",u"<&L3_LS> Recibir invitaciones",u"<&L3_LS> Recevoir les invitations",u"<&L3_LS> Ricevi inviti",u"<&L3_LS> Einladungen empfangen"};
  static const char16_t* fly[]={u"PESO-MOSCA",u"PESO MOSCA",u"POIDS MOUCHES",u"PESI MOSCA",u"FLIEGENGEWICHT"};
  put(0x69772238,0x00E3286E,Utf16(skip[i]));put(0x69772238,0x208BC8C7,Utf16(attire[i]));put(0x69772238,0x1A57EA5E,Utf16(id[i]));put(0x69772238,0x5AC63877,Utf16(invite[i]));put(0x5A770ADD,0xF17E0001,Utf16(fly[i]));
 }
 auto result=Serialize(target);if(Parse(result)!=target)throw std::runtime_error("Falha ao verificar os textos preservados.");return result;
}

}
inline fs::path PrepareLocalizedNames(const fs::path& game,int language){if(language<=0||language>=6)return {};auto code=LanguageCodes[language];auto english=game/"DAT/ytx/EN/generic.ytx",translated=game/"DAT/ytx"/code/"generic.ytx";auto output=Storage(game)/"localization"/code/"generic.ytx";
 auto fingerprints=Hash(english)+"\n"+Hash(translated)+"\nformat=2\n";auto metadata=output;metadata+=".sources";if(fs::is_regular_file(output)&&ReadText(metadata)==fingerprints)return output;
 auto result=ytx::MergeNames(ReadText(english),ReadText(translated),language);AtomicText(output,result);AtomicText(metadata,fingerprints);return output;
}
}
