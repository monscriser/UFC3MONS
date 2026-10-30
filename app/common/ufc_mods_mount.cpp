// Mods tab (P14.9), game side: before the title starts, every enabled file under ufc_mods_dir/DAT replaces the
// game file at the same path (read-only redirect, the game folder is not changed). See ufc_mods.h.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/logging.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

REXCVAR_DEFINE_STRING(ufc_mods_dir, "", "Mods", "Launcher mods folder (<storage>/mods); empty = no mods");

namespace {
std::string U8(const std::filesystem::path& p) { auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
}

void MountUfcMods(rex::Runtime* runtime) {
  const std::string dir = REXCVAR_GET(ufc_mods_dir);
  if (dir.empty()) return;
  const auto root = std::filesystem::u8path(dir);
  std::error_code e;
  if (!std::filesystem::is_directory(root / "DAT", e)) return;
  std::set<std::string> disabled;
  {
    std::ifstream in(root / "disabled.txt");
    for (std::string line; std::getline(in, line);) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (!line.empty()) disabled.insert(line);
    }
  }
  unsigned loaded = 0, off = 0, failed = 0;
  for (std::filesystem::recursive_directory_iterator it(root / "DAT", e), end; !e && it != end; it.increment(e)) {
    if (!it->is_regular_file(e) || U8(it->path().filename()).find(".new") != std::string::npos) continue;
    std::string key = U8(it->path().lexically_relative(root));
    std::replace(key.begin(), key.end(), '\\', '/');
    if (disabled.count(key)) { ++off; continue; }
    std::string guest = "game:\\" + key;
    std::replace(guest.begin(), guest.end(), '/', '\\');
    auto* entry = dynamic_cast<rex::filesystem::HostPathEntry*>(runtime->file_system()->ResolvePath(guest));
    if (entry && entry->OverrideReadOnlyFile(it->path())) {
      ++loaded;
      REXLOG_INFO("[ufc-mods] {} -> {} ({} bytes)", key, U8(it->path()), entry->size());
    } else {
      ++failed;  // ISO (not a host folder) or a path the game does not have
      REXLOG_WARN("[ufc-mods] not applied: {} (the game has no host file at this path)", key);
    }
  }
  REXLOG_INFO("[ufc-mods] loaded={} disabled={} not_applied={}", loaded, off, failed);
}
