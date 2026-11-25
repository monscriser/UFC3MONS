#pragma once
// 1.9 — Reborn category titles. The pre-fight card (DAT/hud/hud_tailoftape_ppv.pac, hud_tailoftape_fn.pac) and the
// in-fight scoreboard (exh_ppv.pac, exh_ufn.pac) draw the weight class from images. Reborn 1.1 has no flyweight
// image for the card: sub_82D3DE28 reads the class from guest byte 0x83574903 and its jump table (0x82D3DEB0) has no
// frame for flyweight (7), so the bantamweight frame stays. The titles are also English in every language.
// bancada/titulos/gerar_titulos.py redraws only the DXT5 blocks of the titles (ufc_titles_data.h):
//   card EN_FLY: the bantamweight images say FLYWEIGHT; card PT / PT_FLY: all titles in Portuguese;
//   scoreboard PT: all titles in Portuguese (the scoreboard already has a flyweight image).
// The card package is opened for every fight after the class is chosen; on that open the matching variant is mounted
// (flyweight → *_FLY, otherwise the base: original file in English, PT in Portuguese).
#include "ufc_titles_data.h"
#include "ufc_library.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

REXCVAR_DEFINE_BOOL(ufc_reborn_titles, true, "UFC", "Reborn: flyweight title on the pre-fight card and translated category titles");

namespace ufc_titles {
inline constexpr uint32_t kWeightClassAddress = 0x83574903;  // 0x83541FE0 + 0x32923, read by sub_82D3DE28
inline constexpr uint8_t kFlyweight = 7;

struct Card {
  const char* file = nullptr;
  rex::filesystem::HostPathEntry* entry = nullptr;
  std::filesystem::path base, fly;
  int current = 0;  // 0 = base mounted, 1 = flyweight variant mounted
};
struct State {
  std::mutex mutex;
  Card cards[2];
};
inline State& Get() { static State s; return s; }
inline std::atomic<bool> g_ready{false};

inline std::filesystem::path Prepare(const std::filesystem::path& game, const ufc_titles_data::Variant& v) {
  const auto source = game / "DAT/hud" / v.file;
  if (!std::filesystem::is_regular_file(source) || ufc::Hash(source) != v.source) {
    REXLOG_WARN("[ufc-titles] file={} variant={} applied=false reason=unrecognized_source preserving=true", v.file, v.name);
    return {};
  }
  const auto target = ufc::Storage(game) / "compatibility/titles-v1" / (std::string(v.target) + ".pac");
  if (!std::filesystem::exists(target)) {
    std::ifstream in(source, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() != v.file_size) return {};
    for (size_t i = 0; i < v.run_count; ++i) {
      const auto& r = v.runs[i];
      bytes.replace(r.offset, r.size, reinterpret_cast<const char*>(ufc_titles_data::Blob + r.blob), r.size);
    }
    ufc::AtomicText(target, bytes);
  }
  if (!std::filesystem::is_regular_file(target) || ufc::Hash(target) != v.target) {
    REXLOG_ERROR("[ufc-titles] file={} variant={} applied=false reason=signature_mismatch", v.file, v.name);
    return {};
  }
  return target;
}

inline const ufc_titles_data::Variant* Find(const char* file, const char* name) {
  for (const auto& v : ufc_titles_data::Variants)
    if (std::string_view(v.file) == file && std::string_view(v.name) == name) return &v;
  return nullptr;
}

inline rex::filesystem::HostPathEntry* Entry(rex::Runtime* runtime, const char* file) {
  return dynamic_cast<rex::filesystem::HostPathEntry*>(
      runtime->file_system()->ResolvePath(std::string("game:\\DAT\\hud\\") + file));
}

// language: index in ufc::LanguageCodes (1 = PT). Other languages keep the English art (with the flyweight card).
inline void Mount(rex::Runtime* runtime, const std::filesystem::path& game, int language) {
  if (!REXCVAR_GET(ufc_reborn_titles)) { REXLOG_INFO("[ufc-titles] enabled=false"); return; }
  const bool pt = language == 1;
  try {
    if (pt) {
      for (const char* file : {"exh_ppv.pac", "exh_ufn.pac"}) {
        const auto* v = Find(file, "PT"); auto* entry = Entry(runtime, file);
        const auto target = v ? Prepare(game, *v) : std::filesystem::path{};
        const bool ok = entry && !target.empty() && entry->OverrideReadOnlyFile(target);
        REXLOG_INFO("[ufc-titles] file={} variant=PT applied={}", file, ok);
      }
    }
    auto& s = Get();
    std::lock_guard lock(s.mutex);
    const char* cards[2] = {"hud_tailoftape_ppv.pac", "hud_tailoftape_fn.pac"};
    for (int i = 0; i < 2; ++i) {
      auto& c = s.cards[i];
      c.file = cards[i]; c.entry = Entry(runtime, cards[i]);
      if (!c.entry) { REXLOG_WARN("[ufc-titles] file={} applied=false reason=no_entry", cards[i]); continue; }
      const auto* fly = Find(cards[i], pt ? "PT_FLY" : "EN_FLY");
      c.fly = fly ? Prepare(game, *fly) : std::filesystem::path{};
      if (pt) {
        const auto* base = Find(cards[i], "PT");
        c.base = base ? Prepare(game, *base) : std::filesystem::path{};
        if (c.base.empty() || !c.entry->OverrideReadOnlyFile(c.base)) { c.entry = nullptr; continue; }
      } else {
        c.base = c.entry->host_path();
      }
      if (c.fly.empty()) { REXLOG_WARN("[ufc-titles] file={} flyweight=unavailable", cards[i]); }
      c.current = 0;
      REXLOG_INFO("[ufc-titles] file={} base={} flyweight={}", cards[i], pt ? "PT" : "original", !c.fly.empty());
    }
    g_ready.store(true);
  } catch (const std::exception& e) {
    REXLOG_ERROR("[ufc-titles] applied=false reason=exception error={}", e.what());
  }
}

// lower: lowercase guest path of a file being opened; base: guest memory base.
inline void OnOpen(const std::string& lower, const uint8_t* base) {
  if (!g_ready.load(std::memory_order_relaxed)) return;
  int index;
  if (lower.ends_with("\\hud\\hud_tailoftape_ppv.pac")) index = 0;
  else if (lower.ends_with("\\hud\\hud_tailoftape_fn.pac")) index = 1;
  else return;
  const uint8_t weight = base[kWeightClassAddress];
  auto& s = Get();
  std::lock_guard lock(s.mutex);
  auto& c = s.cards[index];
  if (!c.entry) return;
  const int want = (weight == kFlyweight && !c.fly.empty()) ? 1 : 0;
  bool ok = true;
  if (want != c.current) { ok = c.entry->OverrideReadOnlyFile(want ? c.fly : c.base); if (ok) c.current = want; }
  REXLOG_INFO("[ufc-titles] card={} weight_class={} flyweight={} switched={}", c.file, weight, want == 1, ok);
}
}  // namespace ufc_titles
