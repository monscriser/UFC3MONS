#pragma once
#include "ufc_library.h"
#include <set>
#include <atomic>
#include <functional>
#include <cctype>
#include <algorithm>
#include <chrono>
namespace ufc {
inline constexpr const char* OriginalGuestSHA="39fb34cb7c42526469e85e2a1068a34eeddcb82abcd85e0937fc35b8b363d8e8";
struct DiscFile {fs::path path;uint64_t offset=0,size=0;};
struct DiscIndex {std::vector<DiscFile> files;uint64_t total=0;};
class DiscReader {
 std::ifstream in_;uint64_t size_=0,base_=0;std::set<uint64_t> dirs_;DiscIndex index_;
 static uint16_t U16(const char* p){return uint8_t(p[0])|(uint16_t(uint8_t(p[1]))<<8);}
 static uint32_t U32(const char* p){return U16(p)|(uint32_t(U16(p+2))<<16);}
 std::string Read(uint64_t off,uint64_t n){if(off>size_||n>size_-off||n>32*1024*1024)throw std::runtime_error("ISO: intervalo de leitura invalido.");std::string b(size_t(n),'\0');in_.clear();in_.seekg(std::streamoff(off));in_.read(b.data(),std::streamsize(n));if(uint64_t(in_.gcount())!=n)throw std::runtime_error("ISO: leitura incompleta.");return b;}
 static bool SafeName(const std::string& name){
  if(name.empty()||name=="."||name==".."||name.back()=='.'||name.back()==' ')return false;
  for(unsigned char c:name)if(c<32||c>=127||std::string("/\\:<>\"|?*").find(char(c))!=std::string::npos)return false;
  auto stem=name.substr(0,name.find('.'));for(auto& c:stem)c=char(std::toupper(uint8_t(c)));
  if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL")return false;
  if(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9')return false;
  return true;
 }
 std::set<std::string> paths_;
 void Directory(uint32_t sector,uint32_t length,const fs::path& parent,unsigned depth){
  if(depth>48||length<14||length>32*1024*1024||!dirs_.insert(sector).second)throw std::runtime_error("ISO: arvore de diretorios invalida.");
  auto table=Read(base_+uint64_t(sector)*2048,length);std::set<uint16_t> visited;
  std::function<void(uint16_t,unsigned)> node=[&](uint16_t ordinal,unsigned tree_depth){
   uint64_t off=uint64_t(ordinal)*4;if(tree_depth>128||off+14>table.size()||!visited.insert(ordinal).second)throw std::runtime_error("ISO: entrada de diretorio invalida.");
   const char* p=table.data()+off;auto left=U16(p),right=U16(p+2);auto s=U32(p+4),n=U32(p+8);auto attr=uint8_t(p[12]),len=uint8_t(p[13]);
   if(off+14+len>table.size())throw std::runtime_error("ISO: nome truncado.");
   std::string name(p+14,len);if(!SafeName(name))throw std::runtime_error("ISO: nome de arquivo inseguro.");
   if(left)node(left,tree_depth+1);
   auto path=parent/Path(name);std::string key=U8(path);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
   if(!paths_.insert(key).second||paths_.size()>100000)throw std::runtime_error("ISO: caminhos duplicados ou entradas demais.");
   if(attr&16){if(n)Directory(s,n,path,depth+1);}else{
    uint64_t start=base_+uint64_t(s)*2048;if(start>size_||n>size_-start)throw std::runtime_error("ISO: arquivo fora da imagem.");
    index_.files.push_back({path,start,n});index_.total+=n;
   }
   if(right)node(right,tree_depth+1);
  };node(0,0);
 }
 public:
 explicit DiscReader(const fs::path& path):in_(path,std::ios::binary){
  if(!in_)throw std::runtime_error("Nao foi possivel abrir a ISO.");size_=fs::file_size(path);
  bool found=false;for(uint64_t b:{0ull,0xfb20ull,0x20600ull,0x2080000ull,0xfd90000ull}){
   if(b+65536+2048>size_)continue;auto header=Read(b+65536,2048);
   if(header.compare(0,20,"MICROSOFT*XBOX*MEDIA")==0){base_=b;Directory(U32(header.data()+20),U32(header.data()+24),{},0);found=true;break;}
  }
  if(!found)throw std::runtime_error("A imagem nao e uma ISO Xbox 360 reconhecida.");
 }
 const DiscIndex& Index()const{return index_;}
 // Reads one file of the image in place, in blocks.
 template<class Sink> void Stream(const DiscFile& file,Sink&& sink){
  in_.clear();in_.seekg(std::streamoff(file.offset));std::vector<char> block(1024*1024);uint64_t left=file.size;
  while(left){auto n=size_t(std::min<uint64_t>(left,block.size()));in_.read(block.data(),n);if(size_t(in_.gcount())!=n)throw std::runtime_error("ISO: leitura incompleta.");sink(block.data(),n);left-=n;}
 }
 void Copy(const DiscFile& file,const fs::path& output){
  // Caller uses a freshly created private directory, never an existing game folder.
  if(fs::exists(output))throw std::runtime_error("Preparacao recusou substituir arquivo existente.");
  fs::create_directories(output.parent_path());std::ofstream dest(output,std::ios::binary);if(!dest)throw std::runtime_error("Nao foi possivel criar arquivo preparado.");
  in_.clear();in_.seekg(std::streamoff(file.offset));std::vector<char> block(1024*1024);uint64_t left=file.size;
  while(left){auto n=size_t(std::min<uint64_t>(left,block.size()));in_.read(block.data(),n);if(size_t(in_.gcount())!=n)throw std::runtime_error("ISO: leitura incompleta durante preparacao.");dest.write(block.data(),n);if(!dest)throw std::runtime_error("Sem espaco ou erro ao preparar arquivos.");left-=n;}
  dest.close();if(!dest)throw std::runtime_error("Falha ao concluir arquivo preparado.");
 }
};
// L2: SHA-256 of default.xex read directly from the image (nothing extracted).
inline std::string IsoGuestHash(const fs::path& iso){
 DiscReader disc(iso);const auto& index=disc.Index();
 auto executable=std::find_if(index.files.begin(),index.files.end(),[](const DiscFile& f){auto s=U8(f.path);std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s=="default.xex";});
 if(executable==index.files.end())throw std::runtime_error("ISO sem default.xex na raiz.");
#ifdef _WIN32
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return {};}
 bool good=true;
 try{disc.Stream(*executable,[&](const char* data,size_t n){if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(data)),ULONG(n),0)<0)good=false;});}catch(...){good=false;}
 std::array<unsigned char,32> value{};if(BCryptFinishHash(hash,value.data(),value.size(),0)<0)good=false;BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);if(!good)return {};
 std::string result;for(auto v:value){result+="0123456789abcdef"[v>>4];result+="0123456789abcdef"[v&15];}return result;
#else
 auto temp=fs::temp_directory_path()/("ufc3mons-iso-xex-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 disc.Copy(*executable,temp);auto result=Hash(temp);std::error_code error;fs::remove(temp,error);return result;
#endif
}
// Selects an ISO to be played in place: validates it and creates only its data folder.
inline Installation IsoInstallation(const fs::path& iso){
 if(!fs::is_regular_file(iso))throw std::runtime_error("Escolha o arquivo ISO.");
 if(IsoGuestHash(iso)!=OriginalGuestSHA)throw std::runtime_error("Esta ISO nao corresponde ao UFC3 Original suportado. Nada foi alterado.");
 auto shell=IsoShell(iso);fs::create_directories(shell);return {shell,{},iso};
}
inline fs::path PrepareIso(const fs::path& iso,std::atomic<uint64_t>& done,std::atomic<uint64_t>& total){
 DiscReader disc(iso);const auto& index=disc.Index();total=index.total;done=0;
 auto executable=std::find_if(index.files.begin(),index.files.end(),[](const DiscFile& f){auto s=U8(f.path);std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s=="default.xex";});
 if(executable==index.files.end())throw std::runtime_error("ISO sem default.xex na raiz.");
 auto identity=std::to_string(fs::file_size(iso))+"-"+std::to_string(static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(iso).time_since_epoch()).count()));
 auto parent=iso.parent_path()/"UFC3MONS-preparado";fs::create_directories(parent);
 auto target=parent/(iso.stem().wstring()+L"-original");
 if(fs::exists(target)){
  if(ReadText(target/".ufc3mons-iso-source")==U8(fs::weakly_canonical(iso))+"\n"+identity+"\n"&&Hash(target/"default.xex")==OriginalGuestSHA){
   for(const auto& file:index.files)if(!fs::is_regular_file(target/file.path)||fs::file_size(target/file.path)!=file.size)throw std::runtime_error("Arquivos preparados incompletos. Mova a pasta preparada para a Lixeira antes de preparar de novo.");
   done=total.load();return target;
  }
  throw std::runtime_error("A pasta preparada ja existe e nao corresponde a esta ISO. Nenhum arquivo foi substituido.");
 }
 if(fs::space(parent).available<index.total+64*1024*1024)throw std::runtime_error("Espaco insuficiente para preparar a ISO (aproximadamente7GB).");
 auto staging=parent/(iso.stem().wstring()+L"-preparando-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
 if(!fs::create_directory(staging))throw std::runtime_error("Nao foi possivel criar a pasta de preparacao.");
 // Validate the executable BEFORE extracting the full game. The ISO remains read-only.
 disc.Copy(*executable,staging/"default.xex");if(Hash(staging/"default.xex")!=OriginalGuestSHA)throw std::runtime_error("Esta ISO nao corresponde ao UFC3 Original suportado. Pasta parcial preservada.");done+=executable->size;
 for(const auto& file:index.files){if(&file==&*executable)continue;disc.Copy(file,staging/file.path);done+=file.size;}
 AtomicText(staging/".ufc3mons-iso-source",U8(fs::weakly_canonical(iso))+"\n"+identity+"\n");
 fs::rename(staging,target);return target;
}
}
