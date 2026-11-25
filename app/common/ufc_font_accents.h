#pragma once
// 1.9 — Reborn PT-BR (and other languages): accents missing in the menus. Reborn 1.1 adds two fonts, DAT/fonts/
// IndustryMenu.xpr (caps-only: lowercase maps to the uppercase glyphs) and BroadcastDIN.xpr, sharing one 1024x512
// 4:4:4:4 texture. Its uppercase accented glyphs (À..Ý, glyphs 128..159) were drawn WITHOUT the accent, so
// "EXIBIÇÃO" showed as "EXIBICAO" (only Ç has its cedilla); the lowercase accented glyphs are correct. The patch
// (bancada/fonte-acentos, ufc_font_accents_data.h) draws each accent taken from the font's own lowercase vowels over
// the uppercase glyph, slightly shortened to fit the 40 px cell. Only the glyph channel (0xF000) changes.
#include "ufc_font_accents_data.h"
#include "ufc_library.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
#include <fstream>
#include <iterator>

REXCVAR_DEFINE_BOOL(ufc_reborn_font_accents, true, "UFC", "Reborn: draw the accents missing from the menu font's uppercase letters");

inline void MountUfcFontAccents(rex::Runtime* runtime, const std::filesystem::path& game) {
  if (!REXCVAR_GET(ufc_reborn_font_accents)) { REXLOG_INFO("[ufc-font-accents] enabled=false"); return; }
  struct Font { const char* name; const char* source; const char* target; };
  const Font fonts[] = {{"IndustryMenu.xpr", ufc_font_accents::IndustrySource, ufc_font_accents::IndustryTarget},
                        {"BroadcastDIN.xpr", ufc_font_accents::BroadcastSource, ufc_font_accents::BroadcastTarget}};
  for (const auto& f : fonts) {
    try {
      const auto source = game / "DAT/fonts" / f.name;
      if (!std::filesystem::is_regular_file(source) || ufc::Hash(source) != f.source) {
        REXLOG_WARN("[ufc-font-accents] font={} applied=false reason=unrecognized_source preserving=true", f.name);
        continue;
      }
      const auto target = ufc::Storage(game) / "compatibility/font-accents-v1" / (std::string(f.target) + ".xpr");
      if (!std::filesystem::exists(target)) {
        std::ifstream in(source, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), {});
        if (bytes.size() != ufc_font_accents::FileSize) { REXLOG_WARN("[ufc-font-accents] font={} applied=false reason=size", f.name); continue; }
        for (const auto& r : ufc_font_accents::Runs) bytes.replace(r.offset, r.size, reinterpret_cast<const char*>(r.data), r.size);
        ufc::AtomicText(target, bytes);
      }
      if (!std::filesystem::is_regular_file(target) || ufc::Hash(target) != f.target) {
        REXLOG_ERROR("[ufc-font-accents] font={} applied=false reason=signature_mismatch preserving=true", f.name);
        continue;
      }
      auto* entry = dynamic_cast<rex::filesystem::HostPathEntry*>(
          runtime->file_system()->ResolvePath(std::string("game:\\DAT\\fonts\\") + f.name));
      const bool ok = entry && entry->OverrideReadOnlyFile(target);
      REXLOG_INFO("[ufc-font-accents] font={} applied={} runs={}", f.name, ok, std::size(ufc_font_accents::Runs));
    } catch (const std::exception& e) {
      REXLOG_ERROR("[ufc-font-accents] font={} applied=false reason=exception error={}", f.name, e.what());
    }
  }
}
