#pragma once
namespace ufc_gym_patch {
inline bool Decode(const unsigned char* data,size_t size,std::string& result,size_t expected){
 result.clear();result.reserve(expected);size_t pos=0;
 auto extend=[&](size_t& n){unsigned char x;do{if(pos>=size)return false;x=data[pos++];if(n>expected||x>expected-n)return false;n+=x;}while(x==255);return true;};
 while(pos<size){
  const unsigned token=data[pos++];size_t literal=token>>4;
  if(literal==15&&!extend(literal))return false;
  if(literal>size-pos||result.size()>expected||literal>expected-result.size())return false;
  result.append(reinterpret_cast<const char*>(data+pos),literal);pos+=literal;
  if(pos==size)return result.size()==expected;
  if(size-pos<2)return false;
  const size_t distance=size_t(data[pos])|(size_t(data[pos+1])<<8);pos+=2;
  if(!distance||distance>result.size())return false;
  size_t match=(token&15)+4;
  if((token&15)==15&&!extend(match))return false;
  if(result.size()>expected||match>expected-result.size())return false;
  for(size_t i=0;i<match;++i)result.push_back(result[result.size()-distance]);
 }
 return result.size()==expected;
}
inline bool Apply(std::string& bytes,bool positive=false){
 if(bytes.size()!=627464)return false;
 const unsigned char first=SourcePrefix[0]^(positive?1:0);
 if(static_cast<unsigned char>(bytes[0])!=first)return false;
 if(std::memcmp(bytes.data()+1,SourcePrefix+1,sizeof(SourcePrefix)-1))return false;
 if(std::memcmp(bytes.data()+77472,SourceDirectory,sizeof(SourceDirectory)))return false;
 std::string literals;if(!Decode(Compressed,sizeof(Compressed),literals,LiteralBytes))return false;
 std::string result;result.reserve(TargetBytes);
 for(const auto& op:Operations){
  const auto& input=op.literal?literals:bytes;
  if(op.offset>input.size()||op.length>input.size()-op.offset||result.size()>TargetBytes||op.length>TargetBytes-result.size())return false;
  result.append(input.data()+op.offset,op.length);
 }
 if(result.size()!=TargetBytes)return false;
 bytes=std::move(result);return true;
}
}
