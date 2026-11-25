// BETA 1.8.1 (issue #24, Reborn 1.1 flyweight crashes: John Dodson at the O2 Arena on loading, Bridgestone Arena
// at the end of the fight). Reproduced on the PC with the player's save: the game crashes in memcpy (sub_82EFCAC0)
// writing to guest 0. Host backtrace of the bad copy:
//   sub_82C48270 (fighter accessory loader: walks the entries of one accessory archive, ssmd/sbmd/h_md/hhmd/htmd/
//   blta... and loads the models) -> sub_827A4C50 (model from an archive entry) -> sub_827A49E0 (chunk parser)
//   -> sub_8279DF38 alloc(garbage size) = 0 -> memcpy(0, ...).
// The entry handed to sub_827A4C50 was not a model: its bytes were the raw file bytes of 420_acce.mpc at 0x4B000,
// which is where the shirt mesh (ssmd.ymx) sits in the 9-entry accessory layout (416, 089, 098, 157, 248) but is
// the middle of another entry in Dodson's 11-entry layout (ssmd at 0x62800). The archive object the loader used
// carried one fighter's entry table and another fighter's data. It shows up with the varied shirts given to the
// fighters that had none (ufc_reborn_shirt_aliases); with them off the same fight loads.
//
// Guard: every model entry of an accessory archive is either compressed (the 4-byte signature sub_827A2018 tests,
// read from the game at 0x820EBFFC) or a chunk stream whose first tag is one of the 9 tags sub_827A4860 accepts
// (table at 0x83322428, 8 bytes per tag: DATA POF0 YOBJ YANM YATR BPE YMPC POFF DUMY). sub_827A4C50 already returns 0 when it cannot load a model; an entry
// that is neither is now refused the same way before it reaches the parser, so the accessory is simply not loaded
// instead of the game crashing. Valid entries are untouched (the check reads 4 bytes and changes nothing).
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <deque>
#include <fstream>
#include <mutex>
#include <vector>
#if defined(_WIN32) && defined(UFC_CS60_DIAG)
#include <windows.h>
#include <set>
#include <tuple>
#endif
#include <rex/system/kernel_state.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_entry.h>

REXCVAR_DEFINE_BOOL(ufc_model_guard_repair, true, "UFC", "Rebuild a mixed accessory archive from the wearer's own file (off: skip it)");
REXCVAR_DEFINE_BOOL(ufc_model_guard, true, "UFC",
                    "Refuse accessory model entries that are not models (fixes Reborn crashes with mismatched accessory data)");
REXCVAR_DEFINE_BOOL(ufc_match_crash_diagnostics, false, "Tests",
                    "Log Reborn match assets and the setup table involved in the consecutive-fight crash");

REXCVAR_DEFINE_BOOL(ufc_ppv_table_guard, true, "UFC",
                    "Reborn consecutive-fight crash: rebuild a missing presentation slot table before PPV_bg setup");

// 1.9 (Reborn, second flyweight fight in the same run): sub_82D382F0 (vtable 0x8214DB30 slot 73, the PPV_bg.pac
// presentation setup) writes table[i*84] for 16/13 slots through the pointer at +0x124 without testing it. That
// table (+0x124, count u16 at +0x128 = 15, set by the constructor sub_82D377F0) is allocated by the base-class
// method sub_82B50118 with count*84+4 bytes from the object's heap (+140); when that allocation fails it stores 0,
// and sub_82B52728 also clears it when the object is torn down. Every other reader of the class (sub_82B4C8C8,
// sub_82D38B28, sub_82B502D0...) already skips a null table. Repro: diag-crash-flyweight-190329 (new object
// AF540830, table NULL, write to guest 0). Guard: retry the class's own allocation once; if it is still null,
// skip this setup like the other methods do (the presentation background is left out instead of crashing).
extern "C" void __imp__sub_82D382F0(PPCContext& ctx, uint8_t* base);

REX_HOOK_RAW(sub_82D382F0) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  constexpr uint32_t table_offset = 0x124;
  constexpr uint32_t count_offset = 0x128;
  constexpr uint32_t package_offset = 0x134;
  constexpr size_t package_max = 32;
  const bool valid = object >= 0x40000000u && object <= 0xC0000000u - 0x900u;
  auto package_name = [&]() {
    std::string package;
    const auto* name = base + object + package_offset;
    for (size_t i = 0; i < package_max && name[i] >= 0x20 && name[i] <= 0x7E; ++i)
      package.push_back(static_cast<char>(name[i]));
    return package.empty() ? std::string("unknown") : package;
  };
  if (valid && REXCVAR_GET(ufc_ppv_table_guard) && REX_LOAD_U32(object + table_offset) == 0) {
    PPCContext call = ctx;
    call.r3.u64 = object;
    sub_82B50118(call, base);
    const uint32_t table = REX_LOAD_U32(object + table_offset);
    const uint32_t count = REX_LOAD_U16(object + count_offset);
    if (table) {
      REXLOG_WARN("[ufc-ppv-guard] caller={:08X} object={:08X} package={} count={} table_124 was NULL; rebuilt at {:08X}",
                  caller, object, package_name(), count, table);
    } else {
      REXLOG_ERROR("[ufc-ppv-guard] caller={:08X} object={:08X} package={} count={} table_124 still NULL; setup skipped",
                   caller, object, package_name(), count);
      return;
    }
  }
#if defined(_WIN32) && defined(UFC_CS60_DIAG)
  if (valid && REXCVAR_GET(ufc_match_crash_diagnostics)) {
    const uint32_t table = REX_LOAD_U32(object + table_offset);
    if (table)
      REXLOG_INFO("[ufc-match-setup] caller={:08X} object={:08X} table_124={:08X}", caller, object, table);
    else
      REXLOG_ERROR("[ufc-match-setup] caller={:08X} object={:08X} package={} table_124=NULL before indexed write",
                   caller, object, package_name());
  }
#endif
  __imp__sub_82D382F0(ctx, base);
}

extern "C" void __imp__sub_827A4C50(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kCompressedTag = 0x820EBFFC;  // sub_827A2018: lis r10,-32241; addi r10,r10,-16388
constexpr uint32_t kChunkTags = 0x83322428;      // sub_827A4860: lis r11,-31950; addi r29,r11,9256; 9 x 8 bytes
std::atomic<uint32_t> g_refused{0};

// Tags are accepted in both byte orders: the accessory models carry them as "JBOY" for "YOBJ" (PC-ordered
// chunk streams), which the game loads fine (seen on the first model entries of every session).
bool IsModelEntry(uint8_t* base, uint32_t at) {
  const uint32_t tag = REX_LOAD_U32(at);
  auto same = [tag](uint32_t ref) { return tag == ref || tag == __builtin_bswap32(ref); };
  if (same(REX_LOAD_U32(kCompressedTag))) return true;
  if (same(0x59324843u)) return true;  // "Y2HC": the other packed form (ssyh/sshk/ssmd entries of every Reborn archive)
  for (uint32_t i = 0; i < 9; ++i)
    if (same(REX_LOAD_U32(kChunkTags + 8 * i))) return true;
  return false;
}
}  // namespace

REX_HOOK_RAW(sub_827A4C50) {
  const uint32_t entry = ctx.r3.u32;
  if (REXCVAR_GET(ufc_model_guard) && entry >= 0x40000000u && entry < 0xC0000000u && !IsModelEntry(base, entry)) {
    const uint32_t n = ++g_refused;
    if (n <= 16 || (n & (n - 1)) == 0)
      REXLOG_WARN("[ufc-model-guard] refused accessory entry {:08X} tag={:08X} size={:08X} count={}", entry,
                  REX_LOAD_U32(entry), ctx.r5.u32, n);
    ctx.r3.u64 = 0;  // the function's own "not loaded" result
    return;
  }
  __imp__sub_827A4C50(ctx, base);
}

// Root guard (PC, 19/11, same repro): logging the archive objects sub_82C48270 walks showed the opponent (416)
// loading its shirt mesh group (sshk.hkx 0x1d000, ssmd.ymx 0xa000, ssyh.yhc 0x1000, every entry starting with the
// YZLI/Y2HC signature) and then Dodson (420) getting a new archive object with the SAME entry table while its
// data came from 420_acce.mpc at 0x2E000 (where that group starts in 416's layout): every entry was garbage. The
// group description of a varied shirt is reused between two fighters whose accessory archives have different
// layouts. Before the loader walks an archive, every entry is checked; a mixed archive is walked as empty (count 0
// for the call), which is exactly the state of a fighter without that accessory (Reborn's own state for the
// fighters that had no shirt), instead of loading garbage and crashing later (sub_824EB2D8 when only the mesh is
// refused). Textures and the other accessory groups are untouched.

// Repair (the varied shirts must stay on every fighter): the mixed archive's data buffer holds an exact slice of the
// wearer's own accessory file (read with the other fighter's group description). The file is found among the
// accessory archives opened last (its bytes at some offset equal the buffer), its own entries are read from its
// index, and when they fit in the space the game reserved (the sum of the borrowed sizes) the right bytes are copied
// into the buffer and its size table (in this fighter's own block) is rewritten. Otherwise the group is skipped.
namespace {
std::mutex g_open_mutex;
std::deque<std::string> g_recent_acce;  // guest paths of the accessory archives opened last
bool ReadHost(const std::string& guest, std::string& bytes) {
  auto* fs = REX_KERNEL_STATE()->emulator()->file_system();
  auto* entry = dynamic_cast<rex::filesystem::HostPathEntry*>(fs->ResolvePath(guest));
  if (!entry) return false;
  std::ifstream in(entry->host_path(), std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  if (size < 0x800 || size >= 64 * 1024 * 1024) return false;
  bytes.resize(static_cast<size_t>(size));
  in.seekg(0);
  return bool(in.read(bytes.data(), size));
}
uint32_t Be32(const std::string& b, size_t p) { return uint32_t(uint8_t(b[p])) << 24 | uint32_t(uint8_t(b[p+1])) << 16 | uint32_t(uint8_t(b[p+2])) << 8 | uint8_t(b[p+3]); }
bool RepairArchive(uint8_t* base, uint32_t ar) {
  if (!REXCVAR_GET(ufc_model_guard_repair)) return false;
  const uint32_t count = REX_LOAD_U32(ar) & 0xFFFF, data = REX_LOAD_U32(ar + 84), names = REX_LOAD_U32(ar + 88), sizes = REX_LOAD_U32(ar + 92);
  if (!count || count > 32 || names < 0x40000000u || names >= 0xC0000000u) return false;
  uint64_t capacity = 0;
  for (uint32_t k = 0; k < count; ++k) capacity += REX_LOAD_U32(sizes + k * 4);
  if (capacity < 64 || capacity >= 64u * 1024 * 1024) return false;
  const std::string probe(reinterpret_cast<const char*>(base + data), 64);
  std::vector<std::string> recent;
  { std::lock_guard<std::mutex> lock(g_open_mutex); recent.assign(g_recent_acce.rbegin(), g_recent_acce.rend()); }
  for (const auto& guest : recent) {
    std::string file;
    if (!ReadHost(guest, file) || file.find(probe) == std::string::npos) continue;
    const uint32_t align = Be32(file, 12), n = Be32(file, 0xBC);
    if (!align || n > 64 || 0xC0 + size_t(n) * 0x28 > file.size()) {
      if (REXCVAR_GET(ufc_match_crash_diagnostics))
        REXLOG_WARN("[ufc-match-archive] archive={:08X} path={} reason=invalid-source-index", ar, guest);
      return false;
    }
    std::vector<size_t> off(count);
    std::vector<uint32_t> size(count);
    uint64_t total = 0;
    for (uint32_t k = 0; k < count; ++k) {
      char nm[33] = {}; for (int c = 0; c < 32; ++c) nm[c] = char(base[names + k * 32 + c]);
      bool found = false;
      for (uint32_t i = 0; i < n && !found; ++i) {
        const size_t r = 0xC0 + size_t(i) * 0x28;
        std::string_view name(file.data() + r + 4, 32);
        name = name.substr(0, name.find('\0'));
        if (name != nm) continue;
        off[k] = size_t(Be32(file, r + 0x24)) + align;
        size[k] = (Be32(file, r) & 0xFFFF) * 32;
        found = true;
      }
      if (!found || size_t(off[k]) + size[k] > file.size()) {
        if (REXCVAR_GET(ufc_match_crash_diagnostics))
          REXLOG_WARN("[ufc-match-archive] archive={:08X} path={} entry={} reason={}",
                      ar, guest, nm, found ? "entry-outside-file" : "entry-missing");
        return false;
      }
      total += size[k];
    }
    if (total > capacity) {
      REXLOG_WARN("[ufc-model-guard] {} entries need {:x} bytes, the game reserved {:x}", guest, total, capacity);
      return false;
    }
    // The names/sizes table sits in this fighter's own block, right before the data (seen at data-0x20), so it is
    // rewritten in place; the archive keeps every pointer it had.
    uint32_t cum = 0;
    for (uint32_t k = 0; k < count; ++k) {
      std::memcpy(base + data + cum, file.data() + off[k], size[k]);
      REX_STORE_U32(sizes + k * 4, size[k]);
      cum += size[k];
    }
    REXLOG_INFO("[ufc-model-guard] accessory archive repaired from {} entries={} bytes={:x}", guest, count, total);
    return true;
  }
  if (REXCVAR_GET(ufc_match_crash_diagnostics))
    REXLOG_WARN("[ufc-match-archive] archive={:08X} data={:08X} reason=no-source-match recent_files={}",
                ar, data, recent.size());
  return false;
}
}  // namespace
#if defined(_WIN32) && defined(UFC_CS60_DIAG)
REXCVAR_DEFINE_BOOL(ufc_roster_name_diagnostics, false, "Tests",
                    "Trace relevant Reborn name fields and their guest callers");
REXCVAR_DEFINE_BOOL(ufc_flyweight_text_diagnostics, false, "Tests",
                   "Trace the renderer's flyweight labels and their callers");
extern "C" void __imp__sub_82B29CB0(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_82B29CB0) {
  const uint32_t key = ctx.r4.u32;
  const auto caller = static_cast<uint32_t>(ctx.lr);
  const bool roster_lookup = (caller == 0x82EDBD68 || caller == 0x82EDBDA8 ||
                              caller == 0x82EDBDE8) && REXCVAR_GET(ufc_roster_name_diagnostics);
  const uint16_t model = roster_lookup ? REX_LOAD_U16(ctx.r30.u32) : 0;
  const uint32_t table_object = ctx.r3.u32;
  __imp__sub_82B29CB0(ctx, base);
  if (REXCVAR_GET(ufc_flyweight_text_diagnostics) && ctx.r3.u32 >= 0x40000000u && ctx.r3.u32 < 0xC0000000u) {
    const auto* t = base + uint64_t(ctx.r3.u32);
    MEMORY_BASIC_INFORMATION reg{};
    if (VirtualQuery(t, &reg, sizeof(reg)) && reg.State == MEM_COMMIT && !(reg.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      const size_t room = reinterpret_cast<uintptr_t>(reg.BaseAddress) + reg.RegionSize - reinterpret_cast<uintptr_t>(t);
      std::string s;
      for (size_t i = 0; i + 1 < room && i < 160; i += 2) {
        if (t[i] == 0 && t[i + 1] == 0) break;
        s.push_back(t[i] == 0 && t[i + 1] >= 32 && t[i + 1] < 127 ? char(t[i + 1]) : '?');
      }
      if (s.find("WEIGHT") != std::string::npos || s.find("weight") != std::string::npos || s.find("BOUT") != std::string::npos ||
          s.find("CHAMPION") != std::string::npos || s.find("PESO") != std::string::npos || s.find("CAMPEONATO") != std::string::npos ||
          s.find("COMBATE") != std::string::npos || s.find("Weight>") != std::string::npos) {
        static std::mutex m; static std::set<std::tuple<uint32_t, uint32_t, std::string>> seen;
        std::lock_guard<std::mutex> lock(m);
        if (seen.size() < 512 && seen.emplace(caller, key, s).second)
          REXLOG_INFO("[ufc-text-lookup] caller={:08X} table={:08X} key={:08X} text={}", caller, table_object, key, s);
      }
    }
  }
  if (roster_lookup) {
    const uint32_t address = ctx.r3.u32;
    const auto* text = base + uint64_t(address);
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !VirtualQuery(text, &region, sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return;
    const size_t room = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize -
                        reinterpret_cast<uintptr_t>(text);
    std::string name;
    std::string utf16be;
    for (size_t i = 0; i + 1 < room && i < 126; i += 2) {
      if (text[i] == 0 && text[i + 1] == 0) break;
      name.push_back(text[i] == 0 && text[i + 1] >= 32 && text[i + 1] < 127
                         ? static_cast<char>(text[i + 1]) : '?');
      fmt::format_to(std::back_inserter(utf16be), "{:02X}{:02X}", text[i], text[i + 1]);
    }
    static std::mutex mutex;
    static std::set<std::tuple<uint16_t, uint32_t, uint32_t, std::string>> seen;
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.size() < 2048 && seen.emplace(model, caller, key, utf16be).second)
      REXLOG_INFO("[ufc-roster-lookup] model={:04X} caller={:08X} key={:08X} utf16be={} text={}",
                  model, caller, key, utf16be, name);
  }
}
extern "C" void __imp__sub_828F7BE0(PPCContext& ctx, uint8_t* base);
REX_HOOK_RAW(sub_828F7BE0) {
  const bool trace_names = REXCVAR_GET(ufc_roster_name_diagnostics);
  const bool trace_flyweight = REXCVAR_GET(ufc_flyweight_text_diagnostics);
  if (trace_names || trace_flyweight) {
    const uint32_t field = ctx.r4.u32;
    const uint32_t address = ctx.r5.u32;
    const auto* text = base + uint64_t(address);
    MEMORY_BASIC_INFORMATION region{};
    if (address && VirtualQuery(text, &region, sizeof(region)) && region.State == MEM_COMMIT &&
        !(region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
      const size_t room = end - reinterpret_cast<uintptr_t>(text);
      std::string name;
      for (size_t i = 0; i + 1 < room && i < 126; i += 2) {
        if (text[i] != 0 || text[i + 1] < 32 || text[i + 1] > 126) break;
        name.push_back(static_cast<char>(text[i + 1]));
      }
      const bool empty = address != 0x82004128 && room >= 2 && text[0] == 0 && text[1] == 0;
      const bool name_field = field == 4 || field == 8 || field == 161 || field == 165;
      if (trace_names && ((empty && name_field) || name.find("Hominick") != std::string::npos ||
          name.find("HOMINICK") != std::string::npos || name.find("McGregor") != std::string::npos ||
          name.find("MCGREGOR") != std::string::npos)) {
        static std::mutex mutex;
        static std::set<std::tuple<uint32_t, uint32_t, std::string>> seen;
        const auto caller = static_cast<uint32_t>(ctx.lr);
        std::lock_guard<std::mutex> lock(mutex);
        if (seen.size() < 128 && seen.emplace(caller, field, name).second)
          REXLOG_INFO("[ufc-roster-name] caller={:08X} field={} text={}", caller, field, name);
      }
      if (trace_flyweight && (name.find("FLYWEIGHT") != std::string::npos ||
                             name.find("Flyweight") != std::string::npos ||
                             name.find("PESO-MOSCA") != std::string::npos ||
                             name.find("BANTAMWEIGHT") != std::string::npos ||
                             name.find("Bantamweight") != std::string::npos ||
                             name.find("CHAMPIONSHIP") != std::string::npos ||
                             name.find("Championship") != std::string::npos ||
                             name.find("PESO-GALO") != std::string::npos)) {
        static std::mutex mutex;
        static std::set<std::tuple<uint32_t, uint32_t, std::string>> seen;
        const auto caller = static_cast<uint32_t>(ctx.lr);
        std::lock_guard<std::mutex> lock(mutex);
        if (seen.size() < 256 && seen.emplace(caller, field, name).second)
          REXLOG_INFO("[ufc-flyweight-text] caller={:08X} field={} address={:08X} text={}",
                      caller, field, address, name);
      }
    }
  }
  __imp__sub_828F7BE0(ctx, base);
}
#endif
void UfcModelGuardNoteOpen(const std::string& guest_path) {
  std::lock_guard<std::mutex> lock(g_open_mutex);
  g_recent_acce.push_back(guest_path);
  while (g_recent_acce.size() > 12) g_recent_acce.pop_front();
}
extern "C" void __imp__sub_82C48270(PPCContext& ctx, uint8_t* base);
namespace {
std::atomic<uint32_t> g_skipped{0};
uint32_t ArchiveFor(uint8_t* base, uint32_t mgr, uint32_t slot, uint32_t type) {
  const uint32_t cont = REX_LOAD_U32(mgr + slot);
  if (cont < 0x40000000u || cont >= 0xC0000000u) return 0;
  const uint32_t list = REX_LOAD_U32(cont + 4);
  if (list < 0x40000000u || list >= 0xC0000000u) return 0;
  const uint32_t ar = REX_LOAD_U32(list + type * 4);
  return (ar >= 0x40000000u && ar < 0xC0000000u) ? ar : 0;
}
bool ArchiveIsConsistent(uint8_t* base, uint32_t ar) {
  const uint32_t count = REX_LOAD_U32(ar) & 0xFFFF, data = REX_LOAD_U32(ar + 84), sizes = REX_LOAD_U32(ar + 92);
  if (!count) return true;
  if (data < 0x40000000u || data >= 0xC0000000u || sizes < 0x40000000u || sizes >= 0xC0000000u) return true;  // not a loaded data archive
  // Only mesh/cloth entries are checked: in every Reborn accessory archive (all chara/*/NNN_acce.mpc and figacce.mpc,
  // surveyed) .ymx/.hkx/.yhc entries start with YZLI, Y2HC or DUMY; .dat entries are raw and are not checked.
  const uint32_t names = REX_LOAD_U32(ar + 88);
  if (names < 0x40000000u || names >= 0xC0000000u) return true;
  uint32_t cum = 0;
  for (uint32_t k = 0; k < count && k < 64; ++k) {
    char nm[33] = {};
    for (int c = 0; c < 32; ++c) nm[c] = char(base[names + k * 32 + c]);
    const std::string name(nm);
    const bool mesh = name.size() > 4 && (name.ends_with(".ymx") || name.ends_with(".hkx") || name.ends_with(".yhc"));
    if (mesh && !IsModelEntry(base, data + cum)) return false;
    cum += REX_LOAD_U32(sizes + k * 4);
  }
  return true;
}
}  // namespace
REX_HOOK_RAW(sub_82C48270) {
  const uint32_t mgr = ctx.r3.u32, type = ctx.r4.u32;
  uint32_t hidden[2] = {0, 0}; uint16_t saved[2] = {0, 0};
  if (REXCVAR_GET(ufc_model_guard) && type <= 11 && mgr >= 0x40000000u && mgr < 0xC0000000u) {
    const uint32_t slots[2] = {432, 436};
    for (int i = 0; i < 2; ++i) {
      const uint32_t ar = ArchiveFor(base, mgr, slots[i], type);
      if (ar && !ArchiveIsConsistent(base, ar)) {
        if (RepairArchive(base, ar) && REXCVAR_GET(ufc_model_guard_repair) && ArchiveIsConsistent(base, ar)) continue;
        hidden[i] = ar;
        saved[i] = uint16_t(REX_LOAD_U32(ar) & 0xFFFF);
        REX_STORE_U16(ar + 2, 0);
        const uint32_t n = ++g_skipped;
        if (n <= 16 || (n & (n - 1)) == 0) {
          REXLOG_WARN("[ufc-model-guard] mixed accessory archive skipped mgr={:08X} type={} slot={} arch={:08X} entries={} count={}",
                      mgr, type, slots[i], ar, saved[i], n);
        }
      }
    }
  }
  __imp__sub_82C48270(ctx, base);
  for (int i = 0; i < 2; ++i)
    if (hidden[i]) REX_STORE_U16(hidden[i] + 2, saved[i]);
}
