#pragma once
// 1.9 — Reborn shirts by organization. figacce.mpc (shirt/accessory archive) is reopened for every fight, after the
// fight transition package: menu\mad\trnstn_ppv.pac (UFC) or menu\mad\trnstn_pride.pac (PRIDE), seen in
// bancada/logs/camisas-diag-ufc-183655 and camisas-diag-pride-184942. Two variants of the v3 alias archive are
// generated once (bancada/camisas-org/gerar_variantes.py -> ufc_shirt_org_data.h): UFC fights use UFC + neutral
// shirts, PRIDE fights use PRIDE + neutral shirts; the ONE Championship shirt is used in neither. The override of the
// figacce.mpc entry is switched when the transition package opens, before the fighters' accessories load.
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace ufc_shirt_org {
struct State {
  std::mutex mutex;
  rex::filesystem::HostPathEntry* entry = nullptr;
  std::filesystem::path ufc, pride;
  int current = 0;  // 0 = UFC variant mounted, 1 = PRIDE variant mounted
};
inline State& Get() { static State state; return state; }
inline std::atomic<bool> g_ready{false};

inline void Register(rex::filesystem::HostPathEntry* entry, const std::filesystem::path& ufc, const std::filesystem::path& pride) {
  auto& s = Get();
  std::lock_guard lock(s.mutex);
  s.entry = entry; s.ufc = ufc; s.pride = pride; s.current = 0;
  g_ready.store(true);
}

// lower: lowercase guest path of a file being opened.
inline void OnOpen(const std::string& lower) {
  if (!g_ready.load(std::memory_order_relaxed)) return;
  // PRIDE: the PRIDE transition plus the PRIDE effect/HUD packages that load after it and before figacce.mpc
  // (re-asserting PRIDE even if a common transition opens in between). UFC: any other fight/menu transition
  // (trnstn_ppv, trnstn_cmn...; the gym arena opens no trnstn_ppv).
  int want;
  if (lower.find("\\mad\\trnstn_pride") != std::string::npos || lower.ends_with("\\eff\\mad\\pride.pac") ||
      lower.find("\\hud\\exh_pride") != std::string::npos || lower.find("\\hud\\pmov_pride") != std::string::npos)
    want = 1;
  else if (lower.find("\\mad\\trnstn_") != std::string::npos)
    want = 0;
  else
    return;
  auto& s = Get();
  std::lock_guard lock(s.mutex);
  if (want == s.current || !s.entry) return;
  const bool ok = s.entry->OverrideReadOnlyFile(want ? s.pride : s.ufc);
  if (ok) s.current = want;
  REXLOG_INFO("[ufc-shirt-org] organization={} trigger={} switched={}", want ? "PRIDE" : "UFC", lower, ok);
}
}  // namespace ufc_shirt_org
