#pragma once
#include "ufc_library.h"
#include <set>
#include <string_view>
#include "ufc_reborn_pt_overrides.h"
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
inline void LocalizePortuguesePauseMenu(std::vector<Group>& groups) {
 const auto group = std::find_if(groups.begin(), groups.end(), [](const Group& value) {
  return value.id == 0x617715D0u;
 });
 if (group == groups.end()) return;
 const auto previous = Utf16(u"MENU PAUSE");
 const auto translated = Utf16(u"MENU DE PAUSA");
 for (auto& text : group->strings) {
  if ((text.id == 0x76384BAAu || text.id == 0x643CAC42u) && text.bytes == previous)
   text.bytes = translated;
 }
}
inline constexpr const char* PortugueseSupplementalAwardTables[] = {
 "defense.ytx", "guide.ytx", "shop.ytx", "ultimate.ytx",
};
inline bool IsPortugueseSupplementalAwardTable(std::string_view file) {
 for (const auto* name : PortugueseSupplementalAwardTables)
  if (file == name) return true;
 return false;
}
inline void ApplyPortugueseAwards(std::vector<Group>& groups, std::string_view file) {
 const bool is_career = file == "career.ytx";
 const bool has_annual_awards = is_career || file == "generic.ytx";
 if (!has_annual_awards && !IsPortugueseSupplementalAwardTable(file)) return;
 // The UFC's Portuguese name for this event bonus.
 const std::pair<std::string, std::string> bonuses[] = {
  {Utf16(u"SUBMISSÃO DA NOITE"), Utf16(u"FINALIZAÇÃO DA NOITE")},
  {Utf16(u"Submissão da Noite"), Utf16(u"Finalização da Noite")},
  {Utf16(u"submissão da noite"), Utf16(u"finalização da noite")},
 };
 for (auto& group : groups) {
  for (auto& text : group.strings) {
   for (const auto& [old_bonus, new_bonus] : bonuses) {
    size_t pos = 0;
    while ((pos = text.bytes.find(old_bonus, pos)) != std::string::npos) {
     if (pos % 2 != 0) {
      ++pos;
      continue;
     }
     text.bytes.replace(pos, old_bonus.size(), new_bonus);
     pos += new_bonus.size();
    }
   }
  }
 }
 if (!has_annual_awards) return;
 struct Award { uint32_t key; const char16_t* text; };
 static constexpr Award career[] = {
   {0xA0A7B23F, u"LUTADOR DO ANO"}, {0xD6FED939, u"LUTADOR DO ANO"},
   {0xA1A7B392, u"LUTA DO ANO"}, {0xD3FED460, u"LUTA DO ANO"},
   {0xA2A7B505, u"NOCAUTE DO ANO"}, {0xD4FED5D3, u"NOCAUTE DO ANO"},
   {0x9BA7AA00, u"FINALIZAÇÃO DO ANO"}, {0xD5FED746, u"FINALIZAÇÃO DO ANO"},
   {0x720C5B98, u"Novo favorito ao prêmio de Nocaute do Ano"},
   {0x2631EEB9, u"<#N_PLYR_LSTN> nocauteou <#N_FIGTR1_LSTN> no <#OTY_EVENT> <#OTY_EVENT_NUM> e se tornou favorito ao prêmio de Nocaute do Ano."},
 };
 static constexpr Award generic[] = {
   {0xD1CBA4F6, u"LUTA DO ANO"}, {0xD1CBA4FB, u"LUTA DO ANO"},
   {0xD1CBA4F4, u"FINALIZAÇÃO DO ANO"},
 };
 const uint32_t group_id = is_career ? 0x6E772A59u : 0x050C5D78u;
 const auto group = std::find_if(groups.begin(), groups.end(), [group_id](const Group& value) {
  return value.id == group_id;
 });
 if (group == groups.end()) return;
 auto apply = [&](const auto& awards) {
  for (const auto& [key, text] : awards) {
   auto value = std::find_if(group->strings.begin(), group->strings.end(), [key](const Text& entry) {
    return entry.id == key;
   });
   if (value != group->strings.end()) value->bytes = Utf16(text);
  }
 };
 if (is_career) apply(career);
 else apply(generic);
 // The same award also appears in headlines and news templates. Keep their
 // surrounding text and dynamic fields intact when translating its name.
 struct AwardName { const char16_t* english; const char16_t* portuguese; bool feminine = false; };
 constexpr AwardName names[] = {
   {u"KNOCKOUT OF THE YEAR", u"NOCAUTE DO ANO"},
   {u"Knockout Of The Year", u"Nocaute do Ano"},
   {u"Knockout of the Year", u"Nocaute do Ano"},
   {u"KO OF THE YEAR", u"NOCAUTE DO ANO"},
   {u"KO Of The Year", u"Nocaute do Ano"},
   {u"KO of the Year", u"Nocaute do Ano"},
   {u"FIGHTER OF THE YEAR", u"LUTADOR DO ANO"},
   {u"Fighter Of The Year", u"Lutador do Ano"},
   {u"Fighter of the Year", u"Lutador do Ano"},
   {u"FIGHT OF THE YEAR", u"LUTA DO ANO", true},
   {u"Fight Of The Year", u"Luta do Ano", true},
   {u"Fight of the Year", u"Luta do Ano", true},
   {u"SUBMISSION OF THE YEAR", u"FINALIZAÇÃO DO ANO", true},
   {u"Submission Of The Year", u"Finalização do Ano", true},
   {u"Submission of the Year", u"Finalização do Ano", true},
   {u"SUB OF THE YEAR", u"FINALIZAÇÃO DO ANO", true},
   {u"Sub Of The Year", u"Finalização do Ano", true},
   {u"Sub of the Year", u"Finalização do Ano", true},
 };
 const std::pair<std::string, std::string> agreement[] = {
   {Utf16(u"Anunciado o "), Utf16(u"Anunciada a ")},
   {Utf16(u"Decidido o "), Utf16(u"Decidida a ")},
   {Utf16(u"para "), Utf16(u"para a ")},
   {Utf16(u"Do "), Utf16(u"Da ")},
   {Utf16(u"do "), Utf16(u"da ")},
   {Utf16(u"no "), Utf16(u"na ")},
   {Utf16(u"ao "), Utf16(u"à ")},
   {Utf16(u"o "), Utf16(u"a ")},
   {Utf16(u"O "), Utf16(u"A ")},
 };
 for (const auto& [english, portuguese, feminine] : names) {
  const auto from = Utf16(english);
  const auto to = Utf16(portuguese);
  for (auto& entry : group->strings) {
   size_t pos = 0;
   while ((pos = entry.bytes.find(from, pos)) != std::string::npos) {
    if (pos % 2 != 0) {
     ++pos;
     continue;
    }
    if (feminine) {
     for (const auto& [before, after] : agreement) {
      if (pos < before.size()) continue;
      const auto start = pos - before.size();
      if (entry.bytes.compare(start, before.size(), before) != 0 ||
          (start != 0 && entry.bytes[start - 1] != ' ')) continue;
      entry.bytes.replace(start, before.size(), after);
      pos = start + after.size();
      break;
     }
    }
    entry.bytes.replace(pos, from.size(), to);
    pos += to.size();
   }
  }
 }
}
inline bool IsRosterNameGroup(uint32_t id) {
 return id == 0x050C5D71u || id == 0x3A97A3A9u;
}
inline void CopyRosterNames(const std::vector<Group>& reborn, std::vector<Group>& localized) {
 for (const auto& source : reborn) {
  if (!IsRosterNameGroup(source.id)) continue;
  auto dest = std::find_if(localized.begin(), localized.end(),
                           [&](const Group& group) { return group.id == source.id; });
  // Keep name keys, spelling, and empty entries identical to Reborn's English roster.
  if (dest == localized.end()) localized.push_back(source);
  else *dest = source;
 }
}
inline std::string MergeNames(const std::string& english,const std::string& translated,int language=1){
 auto names=Parse(english),target=Parse(translated);
 auto group=[](auto& groups,uint32_t id){return std::find_if(groups.begin(),groups.end(),[&](const Group& g){return g.id==id;});};
 auto source=group(names,0x050c5d71),dest=group(target,0x050c5d71);
 if(source==names.end()||dest==target.end()||source->strings.size()<1000)throw std::runtime_error("Tabela do elenco Reborn nao encontrada.");
 CopyRosterNames(names,target);
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
  // BETA 1.8: the pre-fight tale of the tape labels the weight with key 0x335F914E of group 0x687720AC, which is
  // "STONE" in the English table; the official Portuguese table translated it literally as "PEDRA".
  if(language==1){
   put(0x687720AC,0x335F914E,Utf16(u"PESO"));
   put(0x687720AC,0x305F8C89,Utf16(u"PESO:"));
   ApplyPortugueseAwards(target,"generic.ytx");
   LocalizePortuguesePauseMenu(target);
  }
  put(0x69772238,0x00E3286E,Utf16(skip[i]));put(0x69772238,0x208BC8C7,Utf16(attire[i]));put(0x69772238,0x1A57EA5E,Utf16(id[i]));put(0x69772238,0x5AC63877,Utf16(invite[i]));put(0x5A770ADD,0xF17E0001,Utf16(fly[i]));
 }
 auto result=Serialize(target);if(Parse(result)!=target)throw std::runtime_error("Falha ao verificar os textos preservados.");return result;
}

}
// BETA 1.8.1: Reborn's own texts in the selected language. Reborn edited only its EN tables (fighter and CAF names,
// nicknames, arenas, Flyweight, renamed labels); the other languages keep the Original game's translation, so with
// the Reborn text kept the fighter select showed blank or mixed names (the Original roster names on Reborn fighters),
// the Original arena names and an empty Flyweight label. The table of the selected language gets every text Reborn
// added or changed (ufc_reborn_pt_overrides.h, built from Reborn EN vs Original EN): proper names copied from Reborn's
// EN table, labels translated (Portuguese); every other text keeps its translation.
inline std::string Utf8ToUtf16Be(const char* s){std::string out;const auto* u=reinterpret_cast<const unsigned char*>(s);
 while(*u){uint32_t c=*u++;if(c>=0xF0){c=(c&7)<<18|(u[0]&63)<<12|(u[1]&63)<<6|(u[2]&63);u+=3;}else if(c>=0xE0){c=(c&15)<<12|(u[0]&63)<<6|(u[1]&63);u+=2;}else if(c>=0xC0){c=(c&31)<<6|(u[0]&63);u+=1;}
  auto put=[&](uint32_t v){out+=char(v>>8);out+=char(v&255);};if(c>=0x10000){c-=0x10000;put(0xD800|(c>>10));put(0xDC00|(c&0x3FF));}else put(c);}
 return out;}
inline fs::path PrepareRebornLanguageTable(const fs::path& game,int language,const std::string& file){
 auto code=LanguageCodes[language];auto english=game/"DAT/ytx/EN"/file,translated=game/"DAT/ytx"/code/file;
 auto output=Storage(game)/"localization"/code/("reborn-"+file);
 auto fingerprints=Hash(english)+"\n"+Hash(translated)+"\nreborn-language=9\n";auto metadata=output;metadata+=".sources";
 if(file=="generic.ytx"&&language==1)fingerprints+="portuguese-pause-menu=1\n";
 if(language==1&&(file=="generic.ytx"||file=="career.ytx"))fingerprints+="portuguese-awards=5\n";
 const bool exhibition_arenas=file=="exhibition.ytx"&&language==1;
 const auto arena_names=game/"DAT/ytx/EN/generic.ytx";
 if(exhibition_arenas)fingerprints+="reborn-exhibition-arenas=1\n"+Hash(arena_names)+"\n";
 if(fs::is_regular_file(output)&&ReadText(metadata)==fingerprints)return output;
 auto en=ytx::Parse(ReadText(english));auto target=ytx::Parse(ReadText(translated));
 if(file=="generic.ytx")ytx::CopyRosterNames(en,target);
 auto find=[](std::vector<ytx::Group>& groups,uint32_t id){return std::find_if(groups.begin(),groups.end(),[&](const ytx::Group& g){return g.id==id;});};
 unsigned copied=0,translated_n=0;
 for(const auto& o:reborn_pt::kOverrides){
  if(file!=o.file||(o.pt_only&&language!=1))continue;
  if(file=="generic.ytx"&&ytx::IsRosterNameGroup(o.group))continue;
  std::string bytes;
  if(o.copy||language!=1){
   auto g=find(en,o.group);
   if(g==en.end())continue;
   auto t=std::find_if(g->strings.begin(),g->strings.end(),[&](const ytx::Text& v){return v.id==o.key;});
   if(t==g->strings.end())continue;
   bytes=t->bytes;
   ++copied;
  }
  else{bytes=Utf8ToUtf16Be(o.text);++translated_n;}
  auto g=find(target,o.group);if(g==target.end()){target.push_back({o.group,{}});g=target.end()-1;}
  auto t=std::find_if(g->strings.begin(),g->strings.end(),[&](const ytx::Text& v){return v.id==o.key;});
  if(t==g->strings.end())g->strings.push_back({o.key,bytes});else t->bytes=bytes;
 }
 if(language==1)ytx::ApplyPortugueseAwards(target,file);
 if(file=="generic.ytx"&&language==1)ytx::LocalizePortuguesePauseMenu(target);
 if(exhibition_arenas){
  // The arena selector uses exhibition.ytx. Reborn replaced the arenas and
  // updated their names in the English generic table, using different keys.
  struct Arena {uint32_t exhibition_key, generic_key;};
  constexpr Arena arenas[]={
    {0xE736ED70,0x267CAD82}, {0xE736ED71,0x267CAD83},
    {0xE736ED72,0x267CAD84}, {0xE736ED7D,0x267CAD87},
    {0xE836EEE7,0x267CAD88}, {0xE836EEE6,0x267CAD89},
  };
  auto canonical=ytx::Parse(ReadText(arena_names));
  auto source=find(canonical,0x5E771170);
  auto destination=find(target,0x6C772776);
  if(source==canonical.end()||destination==target.end())
   throw std::runtime_error("Grupo de nomes das arenas do Reborn ausente.");
  for(const auto& [exhibition_key,generic_key]:arenas){
   auto name=std::find_if(source->strings.begin(),source->strings.end(),[generic_key](const ytx::Text& text){return text.id==generic_key;});
   auto label=std::find_if(destination->strings.begin(),destination->strings.end(),[exhibition_key](const ytx::Text& text){return text.id==exhibition_key;});
   if(name==source->strings.end()||label==destination->strings.end())
    throw std::runtime_error("Nome de arena do Reborn ausente.");
   label->bytes=name->bytes;
  }
 }
 auto result=ytx::Serialize(target);if(ytx::Parse(result)!=target)throw std::runtime_error("Falha ao verificar os textos do Reborn.");
 AtomicText(output,result);AtomicText(metadata,fingerprints);return output;
}
// Preserve the native Portuguese tables and mount a corrected copy.
inline fs::path PreparePortugueseLabels(const fs::path& game,const std::string& file,
                                        const fs::path& translated){
 auto output=Storage(game)/"localization"/"PT"/("original-"+file);
 auto fingerprints=Hash(translated)+"\noriginal-portuguese-labels=4\n";
 fingerprints+="portuguese-awards=5\n";
 if(file=="generic.ytx")fingerprints+="portuguese-pause-menu=1\n";
 auto metadata=output;metadata+=".sources";
 if(fs::is_regular_file(output)&&ReadText(metadata)==fingerprints)return output;
 auto groups=ytx::Parse(ReadText(translated));
 struct Label {uint32_t group,key; const char16_t* text;};
 constexpr Label labels[]={
   {0x687720AC,0x335F914E,u"PESO"},
   {0x687720AC,0x305F8C89,u"PESO:"},
   {0x69772238,0x8C5D1B22,u"PAUSA"},
   {0x69772238,0x00E3286E,u"LOJA DO XBOX LIVE"},
 };
 if(file=="generic.ytx")for(const auto& [group,key,text]:labels){
  auto g=std::find_if(groups.begin(),groups.end(),[group](const ytx::Group& x){return x.id==group;});
  if(g==groups.end())throw std::runtime_error("Grupo da tradução PT-BR ausente.");
  auto t=std::find_if(g->strings.begin(),g->strings.end(),[key](const ytx::Text& v){return v.id==key;});
  if(t==g->strings.end())throw std::runtime_error("Rótulo da tradução PT-BR ausente.");
  t->bytes=ytx::Utf16(text);
 }
 ytx::ApplyPortugueseAwards(groups,file);
 if(file=="generic.ytx")ytx::LocalizePortuguesePauseMenu(groups);
 auto result=ytx::Serialize(groups);if(ytx::Parse(result)!=groups)throw std::runtime_error("Falha ao verificar os rótulos PT-BR.");
 AtomicText(output,result);AtomicText(metadata,fingerprints);return output;
}
inline fs::path PrepareLocalizedNames(const fs::path& game,int language){if(language<=0||language>=6)return {};auto code=LanguageCodes[language];auto english=game/"DAT/ytx/EN/generic.ytx",translated=game/"DAT/ytx"/code/"generic.ytx";auto output=Storage(game)/"localization"/code/"generic.ytx";
 auto fingerprints=Hash(english)+"\n"+Hash(translated)+"\nformat=9\n";if(language==1)fingerprints+="portuguese-pause-menu=1\nportuguese-awards=5\n";auto metadata=output;metadata+=".sources";if(fs::is_regular_file(output)&&ReadText(metadata)==fingerprints)return output;
 auto result=ytx::MergeNames(ReadText(english),ReadText(translated),language);AtomicText(output,result);AtomicText(metadata,fingerprints);return output;
}
}
