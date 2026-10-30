// Cheat "Full Training Rewards (Auto Mode)": Career automatic training gets the
// grade the best Manual Training result would give. Default OFF.
//
// Guest code (same addresses in Reborn and Original):
//  sub_822FD7A0  training result: field 8 != 0 -> automatic branch, calls
//                sub_822FCF38 (return address 0x822FD7CC); otherwise manual.
//  sub_822FCF38  automatic: random 0..99 against the activity's probability row
//                (career_training.bin), stores grade at +16 and the grade's score
//                threshold at +20.
//  sub_822FCEB8  manual: score from the minigame, grade 0 = score >= threshold[0].
// The reward is applied later by the game from grade +16, exactly like manual.
//
// Cheat "Double Career Creds": Creds earned from a Career fight count twice. Default OFF.
//  sub_822F5590  only caller sub_822F8918 (fight result): creds(+168) = clamp(old + earned),
//                old kept at +172; clamp limits come from a game table (+28 max, +32 min).
//                Found with a hardware write breakpoint on the Creds field (0 -> 3600 after a fight).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <cstdint>
#include <fstream>
#include <string>

REXCVAR_DEFINE_BOOL(ufc_full_training_rewards, false, "Cheats",
                    "Career automatic training gets the best Manual Training grade; default OFF");
REXCVAR_DEFINE_STRING(ufc_cheats_file, "", "Cheats",
                      "Launcher cheats.ini, re-read at each automatic training so the Cheats tab works while the game runs");

namespace {
// The launcher rewrites cheats.ini as soon as the box changes; a missing or unreadable
// file falls back to the command-line value.
// key=1 in cheats.ini; -1 when the file or the key is missing.
int CheatFromFile(const std::string& key) {
  const std::string path = REXCVAR_GET(ufc_cheats_file);
  if (path.empty()) return -1;
  std::ifstream in(path);
  std::string line;
  while (in && std::getline(in, line)) {
    if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
      return line.size() > key.size() + 1 && line[key.size() + 1] == '1' ? 1 : 0;
  }
  return -1;
}
bool FullTrainingRewardsEnabled() {
  const int file = CheatFromFile("full_training_rewards");
  return file >= 0 ? file == 1 : REXCVAR_GET(ufc_full_training_rewards);
}
}  // namespace
REXCVAR_DEFINE_BOOL(ufc_training_reward_diagnostics, false, "Tests",
                    "Log each automatic training result (grade/score before and after); default OFF");

extern "C" void __imp__sub_822FCF38(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_822FCF38) {
  const uint32_t object = ctx.r3.u32;
  const bool automatic_call = uint32_t(ctx.lr) == 0x822FD7CCu;
  // The original always runs: random number use and register side effects stay intact.
  __imp__sub_822FCF38(ctx, base);
  if (!automatic_call) return;
  const bool enabled = FullTrainingRewardsEnabled();
  const bool diag = REXCVAR_GET(ufc_training_reward_diagnostics);
  const int32_t activity = int32_t(REX_LOAD_U32(object + 4));
  const uint32_t repeat = REX_LOAD_U32(object + 12);
  const uint32_t grade = REX_LOAD_U32(object + 16);
  const uint32_t score = REX_LOAD_U32(object + 20);
  const char* result = "off";
  if (enabled) {
    result = "unchanged";
    const uint32_t row = object + uint32_t(activity) * 52;
    const int16_t threshold = int16_t(REX_LOAD_U16(row + 74));
    const int16_t next = int16_t(REX_LOAD_U16(row + 76));
    const int16_t last = int16_t(REX_LOAD_U16(row + 78));
    // Only the 14 regular activities with a sane descending threshold table.
    if (REX_LOAD_U32(object + 8) == 1 && activity >= 0 && activity < 14 && repeat <= 1 &&
        threshold >= next && next >= last && last >= 0) {
      REX_STORE_U32(object + 16, 0);
      REX_STORE_U32(object + 20, uint32_t(threshold));
      result = "best";
    }
  }
  if (diag) {
    static unsigned events = 0;
    if (events < 64) {
      ++events;
      REXLOG_INFO("[ufc-training-reward] activity={} repeat={} grade={} score={} cheat={} new_grade={} new_score={}",
                  activity, repeat, grade, score, result, REX_LOAD_U32(object + 16),
                  REX_LOAD_U32(object + 20));
    }
  }
}

REXCVAR_DEFINE_BOOL(ufc_double_career_creds, false, "Cheats",
                    "Creds earned from a Career fight count twice; default OFF");

extern "C" void __imp__sub_822F5590(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_822F5590) {
  const uint32_t career = ctx.r3.u32;
  const int32_t before = int32_t(REX_LOAD_U32(career + 168));
  __imp__sub_822F5590(ctx, base);
  const int file = CheatFromFile("double_career_creds");
  const bool enabled = file >= 0 ? file == 1 : REXCVAR_GET(ufc_double_career_creds);
  const int32_t after = int32_t(REX_LOAD_U32(career + 168));
  if (!enabled || after <= before) return;
  // Same table and maximum the original clamps to: *(*(0x8335D4A0) + 220) + 28.
  const uint32_t settings = REX_LOAD_U32(0x8335D4A0u);
  const uint32_t table = settings ? REX_LOAD_U32(settings + 220) : 0;
  if (!table) return;
  const int32_t maximum = int32_t(REX_LOAD_U32(table + 28));
  int64_t doubled = int64_t(after) + (int64_t(after) - before);
  if (doubled > maximum) doubled = maximum;
  if (doubled <= after) return;
  REX_STORE_U32(career + 168, uint32_t(int32_t(doubled)));
  if (REXCVAR_GET(ufc_training_reward_diagnostics))
    REXLOG_INFO("[ufc-creds-cheat] before={} game_result={} doubled={} max={}", before, after, int32_t(doubled), maximum);
}
