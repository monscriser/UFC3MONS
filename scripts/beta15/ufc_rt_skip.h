// UFC3MONS B6 (ufc_rt_update_skip): pular a preparacao de alvos quando nada mudou desde o desenho anterior.
#pragma once
#include <cstdint>
#include <cstring>
namespace ufc_rt_skip {
constexpr uint32_t kKeyWords = 20;
struct G {
  uint64_t gen = 1;  // muda a cada alteracao de dono da EDRAM, alvo criado, limpeza de cache
  bool allow = false, verify = false, verify_break = false;
  bool skipped = false, verify_pending = false;
  bool have = false, cand_ok = false;
  uint32_t key[kKeyWords] = {}, cand_key[kKeyWords] = {};
  uint64_t key_gen = 0;
  const void* snap_used[5] = {};
  const void* snap_acc[5] = {};
  const void* snap_fb = nullptr;
  const void* snap_rp = nullptr;
  uint64_t snap_rpkey = 0;
  uint64_t skips = 0, fulls = 0, compared = 0, mismatches = 0;
};
inline G& g() {
  static G s;
  return s;
}
inline void Bump() { ++g().gen; }
}  // namespace ufc_rt_skip
