// Test of src/ufc_mods.h (launcher side of the Mods tab) on a fake game folder.
// build: g++ -std=c++20 -I../src mods_test.cpp -o mods_test && ./mods_test <tmpdir>
#include "ufc_mods.h"
#include <cstdio>
#include <fstream>
using namespace ufc;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FALHOU linha %d: %s\n", __LINE__, #c); ++fails; } else std::printf("ok  %s\n", #c); } while (0)
static void Put(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
int main(int argc, char** argv) {
  const fs::path t = argc > 1 ? argv[1] : "/tmp/mods_test";
  fs::remove_all(t);
  const fs::path game = t / "game", in = t / "downloads";
  Put(game / "default.xex", "x");
  Put(game / "DAT/chara/029/029.pac", "ORIGINAL029");
  Put(game / "DAT/chara/029/029_acce.mpc", "ORIGACCE");
  Put(game / "DAT/stage/3000.pac", "STAGE");
  Put(game / "DAT/menu/a/dup.pac", "A"); Put(game / "DAT/menu/b/dup.pac", "B");
  Put(in / "029.pac", "MOD029-longer"); Put(in / "029_acce.mpc", "MODACCE"); Put(in / "3000.pac", "MODSTAGE");
  Put(in / "999.pac", "x"); Put(in / "dup.pac", "x"); Put(in / "empty.pac", "");
  CHECK(ModTarget(game, in / "029.pac") == fs::path("DAT/chara/029/029.pac"));
  CHECK(ModTarget(game, in / "029_acce.mpc") == fs::path("DAT/chara/029/029_acce.mpc"));
  CHECK(ModTarget(game, in / "3000.pac") == fs::path("DAT/stage/3000.pac"));
  bool threw = false; try { ModTarget(game, in / "999.pac"); } catch (...) { threw = true; } CHECK(threw);  // not in the game
  threw = false; try { ModTarget(game, in / "dup.pac"); } catch (...) { threw = true; } CHECK(threw);      // ambiguous
  threw = false; try { AddMod(game, in / "empty.pac"); } catch (...) { threw = true; } CHECK(threw);       // empty
  AddMod(game, in / "029.pac"); AddMod(game, in / "3000.pac");
  CHECK(ReadText(ModsDir(game) / "DAT/chara/029/029.pac") == "MOD029-longer");
  CHECK(ReadText(game / "DAT/chara/029/029.pac") == "ORIGINAL029");  // original untouched
  auto list = ListMods(game); CHECK(list.size() == 2 && list[0].enabled && list[1].enabled);
  SetModEnabled(game, "DAT/chara/029/029.pac", false);
  list = ListMods(game); CHECK(!list[0].enabled && ReadText(ModsDisabledFile(game)) == "DAT/chara/029/029.pac\n");
  AddMod(game, in / "029.pac");  // adding again re-enables
  list = ListMods(game); CHECK(list[0].enabled);
  RemoveMod(game, "DAT/chara/029/029.pac");
  CHECK(!fs::exists(ModsDir(game) / "DAT/chara/029"));  // empty folders cleaned
  CHECK(ReadText(game / "DAT/chara/029/029.pac") == "ORIGINAL029");
  CHECK(ListMods(game).size() == 1);
  threw = false; try { RemoveMod(game, "../game/DAT/chara/029/029.pac"); } catch (...) { threw = true; } CHECK(threw);
  CHECK(ReadText(game / "DAT/chara/029/029.pac") == "ORIGINAL029");
  std::printf(fails ? "\n%d FALHAS\n" : "\nTUDO OK\n", fails);
  return fails != 0;
}
