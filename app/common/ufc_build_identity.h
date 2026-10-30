#pragma once
#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#ifndef UFC3MONS_BUILD_ID
#define UFC3MONS_BUILD_ID "UFC3MONS-BETA1.1-2026-10-29"
#endif
namespace ufc {
inline constexpr const char* BuildId=UFC3MONS_BUILD_ID;
inline std::string NewLogSessionId(){
 static std::atomic<uint64_t> sequence{0};
 const auto now=std::chrono::system_clock::now();const auto time=std::chrono::system_clock::to_time_t(now);std::tm utc{};
#ifdef _WIN32
 gmtime_s(&utc,&time);
#else
 gmtime_r(&time,&utc);
#endif
 std::ostringstream out;out<<std::put_time(&utc,"%Y%m%dT%H%M%SZ")<<'-'<<std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()<<'-'<<sequence.fetch_add(1);return out.str();
}
}
