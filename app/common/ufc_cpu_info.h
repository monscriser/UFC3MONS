#pragma once
// 1.9 (AVX2.md, option A approved on 24/11/2026): read-only CPU detection. The 1.9 builds contain no AVX/AVX2
// code of their own (audit in AVX2.md: only SDL3's CPUID-dispatched blitters), so there is a single execution
// mode for every supported processor. This header only reads CPUID/XGETBV: it is compiled without -mavx and is
// safe in the front door (UFC3.exe / UFC3), which carries no SSE4 code and can refuse an unsupported CPU before
// the game executable (SSSE3 + SSE4.1, plus SSE4.2 in the Reborn Windows build) starts.
#include <cstdint>
#include <cstring>
#include <string>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <cpuid.h>
#define UFC_CPU_X86 1
#endif

namespace ufc {
struct CpuInfo {
  std::string brand;
  bool ssse3 = false, sse41 = false, sse42 = false, popcnt = false;
  bool avx = false, avx2 = false, fma = false;
  bool os_ymm = false;  // OSXSAVE and XCR0 has XMM+YMM state enabled
  bool avx2_usable() const { return avx2 && avx && os_ymm; }
};

inline CpuInfo DetectCpu() {
  CpuInfo info;
#ifdef UFC_CPU_X86
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (__get_cpuid(1, &a, &b, &c, &d)) {
    info.ssse3 = c & (1u << 9);
    info.fma = c & (1u << 12);
    info.sse41 = c & (1u << 19);
    info.sse42 = c & (1u << 20);
    info.popcnt = c & (1u << 23);
    const bool osxsave = c & (1u << 27);
    info.avx = c & (1u << 28);
    if (osxsave) {
      unsigned lo = 0, hi = 0;
      __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
      info.os_ymm = (lo & 6u) == 6u;
    }
  }
  if (__get_cpuid_max(0, nullptr) >= 7) {
    __cpuid_count(7, 0, a, b, c, d);
    info.avx2 = b & (1u << 5);
  }
  if (__get_cpuid_max(0x80000000u, nullptr) >= 0x80000004u) {
    char text[49] = {};
    for (unsigned leaf = 0; leaf < 3; ++leaf) {
      unsigned r[4] = {};
      __get_cpuid(0x80000002u + leaf, &r[0], &r[1], &r[2], &r[3]);
      std::memcpy(text + 16 * leaf, r, 16);
    }
    info.brand = text;
    const auto first = info.brand.find_first_not_of(' ');
    info.brand = first == std::string::npos ? std::string() : info.brand.substr(first);
    while (!info.brand.empty() && info.brand.back() == ' ') info.brand.pop_back();
  }
#endif
  if (info.brand.empty()) info.brand = "?";
  return info;
}

// Instruction-set extensions the given game executable needs (AVX2.md audit). Empty when the CPU meets them.
inline std::string MissingCpuRequirement(const CpuInfo& cpu, bool original) {
  std::string missing;
  auto need = [&](bool ok, const char* name) {
    if (!ok) missing += (missing.empty() ? "" : ", ") + std::string(name);
  };
  need(cpu.ssse3, "SSSE3");
  need(cpu.sse41, "SSE4.1");
#ifdef _WIN32
  if (!original) need(cpu.sse42, "SSE4.2");
#else
  (void)original;
#endif
  return missing;
}
}  // namespace ufc
