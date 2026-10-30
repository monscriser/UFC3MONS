#pragma once
#include <array>
#include <string>
#include <string_view>
namespace ufc {
inline constexpr std::array<const char*,4> MeasurementVariables={"UFC_MEASURE","UFC_WAIT_DIAG","UFC_GPU_TIMING","UFC_RESOURCE_TIMING"};
inline bool IsMeasurementEnvironment(const std::string& value){for(auto key:MeasurementVariables)if(value.rfind(std::string(key)+"=",0)==0)return true;return false;}
inline const char* MeasurementValue(const char* key,bool on){return on&&std::string_view(key)!="UFC_RESOURCE_TIMING"?"1":"0";}
#ifdef _WIN32
inline std::vector<wchar_t> MeasurementEnvironmentBlock(bool on){
 std::vector<std::wstring> entries;auto* block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot read child environment");
 auto wide=[](const std::string&s){return std::wstring(s.begin(),s.end());};
 for(auto* p=block;*p;p+=wcslen(p)+1){bool omit=false;for(auto key:MeasurementVariables){auto name=wide(key);if(_wcsnicmp(p,name.c_str(),name.size())==0&&p[name.size()]==L'='){omit=true;break;}}if(!omit)entries.emplace_back(p);}
 FreeEnvironmentStringsW(block);
 for(auto key:MeasurementVariables)entries.push_back(wide(std::string(key)+"="+MeasurementValue(key,on)));
 std::sort(entries.begin(),entries.end(),[](const auto&a,const auto&b){return _wcsicmp(a.c_str(),b.c_str())<0;});
 std::vector<wchar_t> result;for(auto& e:entries){result.insert(result.end(),e.begin(),e.end());result.push_back(0);}result.push_back(0);return result;
}
#endif
}
