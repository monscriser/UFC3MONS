# 🥊 UFC Undisputed 3: Recompiled — BETA 1.8

> **Created fighters look right in the arena again, and the pre-fight presentation runs at a true 60 FPS.** BETA 1.8 fixes created and generic fighters turning black or into static in Career and training, keeps every created-fighter speed-up turned on, removes the trail that doubled the fighters during the presentations, and fixes the Reborn 1.1 flyweight Exhibition crash on Windows and Linux. Both packages are PGO builds (optimized with a profile taken from real play).

**Fixes GitHub issues #24, #25 and #26.** This release replaces the first BETA 1.8 build, which was withdrawn on November 17.

---

## 👤 Created fighters in Career and training (#25, #26)

**What players saw:** fighters looked right in the menus and on the versus screen, then broke as soon as the fight started. Limbs and faces went solid black, and hair, shorts and skin looked like TV static. It happened in Career fights and training on the Steam Deck, the ROG Ally and AMD graphics.

**What caused it:** the fast build from BETA 1.7 repeats the fighter-model update several times per frame so created fighters appear sooner. That loop also ran the model's final steps in the same frame. The game finishes each texture by queuing a compression job that only runs on the next frame, so the model was finalized while its textures were still empty.

| Area | What changed in BETA 1.8 |
|:--|:--|
| 🔁 **Safe fast build** | The loop now repeats only the two waiting steps (parts and textures) and stops as soon as the step changes. The final steps run on the next update, as in the game. Measured on the ROG Ally: the created fighter is correct in the cage, and its textures take about 38% less time than with the fast build off. |
| 🔑 **Texture cache key** | With two fighters loading, a queued texture could be saved under the other fighter's key. Thanks to Cfmpda for pointing this out. The cache now saves only textures whose owner is known for sure. |
| 🧹 **Old cache ignored** | Cached textures written by BETA 1.7 may be corrupted, so BETA 1.8 never reads them. **Nothing needs to be deleted by hand.** |
| 🛡️ **No more bad entries** | A texture made of a single repeated color block (like the solid black limbs) is never cached. |

The four created-fighter options stay **on by default**: `ufc_caf_fast_build`, `ufc_caf_compress_now`, `ufc_caf_prefetch_parts` and `ufc_caf_texture_cache`. The BETA 1.7 workaround (`--ufc_caf_fast_build=false`) is no longer needed.

**Your save is fine.** The damage happened while the game was running, not in the save file. You don't need to recreate your fighter.

## 🎬 Pre-fight presentation at a true 60 FPS

BETA 1.7 runs the scenes the game dropped to 30 FPS at 60 FPS. In the presentations, the fighters moved at 60 FPS but the camera of each shot still moved at 30 FPS, so the fighters left a trail, like badly applied frame generation. The first BETA 1.8 also showed red and yellow blotches on the Career spotlights and Bruce Buffer's mouth out of sync with his voice.

| Area | What changed in BETA 1.8 |
|:--|:--|
| 🎥 **Camera at 60 FPS** | Each shot's camera now moves on every frame. It keeps the game's own timing and is drawn between the game's steps, so the scene plays exactly as the game plays it. No trail on the fighters. |
| 💡 **Career spotlights** | The spotlight intro keeps clean lighting. Its camera needs the game's whole steps, and the new method keeps them. |
| 🗣️ **Bruce Buffer** | His mouth follows his voice again. |

Checked on PC with frame-by-frame captures, comparing consecutive frames: every Exhibition presentation shot and the Career spotlight intro move on every frame, and the spotlights stay clean.

## 🔁 Reborn 1.1: flyweight Exhibition crash (#24)

Loading a flyweight Exhibition at the O2 Arena and some other arenas closed the game with `0xC000001D`. A jump table in the game code had an entry that Reborn 1.1 points to its own function, and the recompiled table didn't know that function. BETA 1.8 sends that entry to the right Reborn function, on Windows and on Linux.

## 🧩 Other fixes

| Area | What changed |
|:--|:--|
| 🚪 **Quit panel** | F4, or Back + Start held for 3 seconds, opens a panel to quit the game. |
| 🇧🇷 **Portuguese texts** | Reborn's Career no longer flickers between two texts on menu changes, and the weight label reads PESO instead of PEDRA. |
| 🐧 **Linux memory** | The game's memory file in `/dev/shm` is released when the game closes, so RAM no longer piles up after several sessions. |

## ⚡ PGO builds

Both packages are built with profile-guided optimization. The game was played with an instrumented build (menus, a Career fight with the full presentation and an Exhibition fight), and the compiler used that profile to optimize the recompiled game code. The same profile is used for Windows and Linux, and for Original and Reborn.

## ✅ How it was checked

- **#25 / #26:** the cause was isolated on a ROG Ally (Linux, Reborn) by testing each part of the fast build alone, and the fix was confirmed there with a created fighter in a Career fight. If you still see black or static on a Steam Deck or AMD graphics, please open an issue with logs turned on.
- **Presentation:** Exhibition and Career, Reborn, on PC, with consecutive-frame captures (see above).
- **#24:** confirmed in game on Windows (Reborn 1.1, O2 Arena, flyweight). The Linux object was rebuilt with the original recipe, checked by first rebuilding the previous version byte for byte.
- **All four executables** (Windows and Linux, Original and Reborn) compile and link with PGO. The recompiled game objects were checked against the normal build by rebuilding them without PGO first (byte-identical).

## 📦 Build files

| File | Platform |
|:--|:--|
| `UFC3MONS-BETA1.8-PGO-Windows.zip` | Windows. Open `UFC3.exe` to launch or `Launcher-Config.exe` for settings. |
| `UFC3MONS-BETA1.8-PGO-Linux-x86_64.AppImage` | Linux x86-64, including the Steam Deck. |
| `*.sha256` | SHA-256 checksums for the packages. |

Both packages support **Original** and **Reborn**. You'll need your own copy of UFC Undisputed 3.

## 🙏 Credits

Thanks to Cueball771 for the #24 crash logs, to Vikbosz for the #25 logs, photos and saves, to gilliard23 for the #26 logs and screenshots, and to Cfmpda for isolating the created-fighter options and spotting the texture cache problem.

Thank you for supporting UFC3MONS.
