# 🥊 UFC Undisputed 3: Recompiled — BETA 1.8

> **Created fighters look right in the arena again.** On the Steam Deck and on AMD graphics, created and generic fighters could turn black or into colorful static as soon as a Career fight or a training session loaded. BETA 1.8 fixes the cause and keeps every created-fighter speed-up from BETA 1.7 turned on. It also fixes a Reborn 1.1 crash when loading a flyweight Exhibition, now on Windows and Linux.

**Fixes GitHub issues #24, #25 and #26.**

---

## 👤 Created fighters in Career and training (#25, #26)

**What players saw:** fighters looked right in the menus and on the versus screen, then broke as soon as the fight started. Limbs and faces went solid black, and hair, shorts and skin looked like TV static, sometimes green. It happened in Career fights and manual training on the Steam Deck (Original and Reborn) and on a laptop with AMD Radeon graphics. It didn't happen in Exhibition, and it couldn't be reproduced on NVIDIA or the ROG Ally.

**What caused it:** the game builds each created-fighter texture in seven steps. Three of them do nothing but wait about six frames, which gives the GPU time to finish the layers before the last pass reads them. BETA 1.7 ran all the steps back to back to make created fighters appear sooner, and that skipped the wait. When the GPU kept up, nothing showed. When it fell behind, the last pass read unfinished layers. That happened under arena load on a Steam Deck running at about 42 FPS, or on an integrated AMD GPU.

| Area | What changed in BETA 1.8 |
|:--|:--|
| ⏱️ **GPU wait restored** | The steps are still chained, but the texture now waits for real frames to pass at the two points where the GPU has work to finish: 2 frames after the first layers and 6 after the rest, matching the game. Every other speed-up from BETA 1.7 stays on. |
| 🔑 **Texture cache key** | With two fighters loading, a queued texture could be saved to the cache under the other fighter's key. Thanks to Cfmpda for pointing this out. The cache now saves only textures whose owner is known for sure. |
| 🧹 **Old cache ignored** | Cached textures written by BETA 1.7 may be corrupted, so BETA 1.8 never reads them. **Nothing needs to be deleted by hand.** |
| 🛡️ **No more bad entries** | A texture made of a single repeated color block (like the solid black limbs) is never cached, so a broken result can't be confirmed and reused. |

The four created-fighter options stay **on by default**: `ufc_caf_fast_build`, `ufc_caf_compress_now`, `ufc_caf_prefetch_parts` and `ufc_caf_texture_cache`. The new `ufc_caf_settle_frames` option sets the wait after the layers (default `6`).

**Your save is fine.** The same fighter looked right on the versus screen and broke only in the fight, so the damage happened while the game was running, not in the save file. You don't need to recreate your fighter.

## 🔁 Reborn 1.1: flyweight Exhibition crash (#24)

Loading a flyweight Exhibition at the O2 Arena and some other arenas closed the game with `0xC000001D`. A jump table in the game code had an entry that Reborn 1.1 points to its own function, and the recompiled table didn't know that function. BETA 1.8 sends that entry to the right Reborn function.

| Platform | Status |
|:--|:--|
| **Windows** | Fixed and confirmed in game: the flyweight fight at the O2 Arena loads and plays normally. |
| **Linux** | Same fix, new in BETA 1.8. The Linux build of that code was rebuilt with the original recipe, checked by first rebuilding the previous version byte for byte. |

## ✅ How it was checked

- **#24** was confirmed in game on Windows (Reborn 1.1, O2 Arena, flyweight Exhibition). The fixed function was checked in both the Windows and Linux Reborn executables.
- **#25 / #26** were traced from the reporters' logs, photos and screenshots and from the game code. The problem doesn't appear on the PCs used for development, so the fix still needs confirmation on a Steam Deck and on AMD graphics. If you still see any static, please open an issue with logs turned on.
- **All four builds** (Windows and Linux, Original and Reborn) compile and link. The packaged executables match the build output.

## 📦 Build files

| File | Platform |
|:--|:--|
| `UFC3MONS-BETA1.8-Windows.zip` | Windows. Open `UFC3.exe` to launch or `Launcher-Config.exe` for settings. |
| `UFC3MONS-BETA1.8-Linux-x86_64.AppImage` | Linux x86-64, including the Steam Deck. |
| `*.sha256` | SHA-256 checksums for the packages. |

Both packages support **Original** and **Reborn**. You'll need your own copy of UFC Undisputed 3.

## 🙏 Credits

Thanks to Cueball771 for the #24 crash logs, to Vikbosz for the #25 logs, photos and saves, to gilliard23 for the #26 logs and screenshots, and to Cfmpda for isolating the created-fighter options and spotting the texture cache problem.

Thank you for supporting UFC3MONS.
