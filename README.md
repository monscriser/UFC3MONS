# UFC UNDISPUTED 3 - UFC3MONS RECOMP

### Experimental native PC project for Windows & Linux

**UFC3MONS** is an experimental, unofficial native PC project for UFC Undisputed 3 on Windows and Linux. It uses static
recompilation with ReXGlue, a Vulkan graphics backend and Xenia-derived compatibility components.

The project combines a configuration launcher, controller and keyboard support, game integration and CPU/GPU improvements.
Original and Reborn configurations are tracked separately. Most work targets the **community-created Reborn mod**; credit
for the mod belongs to its creators.

> **Current build: P14.5.** On the **ROG Ally at 20 W**, a Reborn **Career fight runs at a locked 60 FPS**.
> Original game files are not included: you need your own copy of the game.

[Explore the source](https://github.com/monscriser/UFC3MONS/tree/main/app) · [Releases](https://github.com/monscriser/UFC3MONS/releases) · [Project website](https://monscriser.github.io/UFC3MONS/) · [Report an issue](https://github.com/monscriser/UFC3MONS/issues)

## UFC Undisputed 3 on PC: Quick Overview

| Feature | Status / details |
| --- | --- |
| Windows | Reborn Career and Exhibition playable; tested on PC (NVIDIA) and ROG Ally Z1 Extreme |
| Linux | Reborn Career fight at a locked 60 FPS on ROG Ally Z1 Extreme at 20 W (Steam Game Mode) |
| Graphics | Vulkan through the ReXGlue/Xenia-derived backend; optional AMD FSR 1 upscaling |
| Audio | Full commentary, including Bruce Buffer's ring announcements |
| Input | Controller and remappable keyboard |
| Editions | Reborn folder, Original (XEX or folder) or ISO, read directly without extracting |
| DLC | Installed from a folder in the launcher; DLC fighters appear on their own select page |
| Launcher | Portuguese (Brazil), English and Spanish; Add to Steam with artwork |
| Cheats | Full Training Rewards (Auto Mode) and Double Money (Career), both off by default |
| Game content | Required separately; not distributed here |

## What's new — P7 to P14.5

```
P7    ── constant-buffer cache · Career reported smooth on the Ally
P10   ── memory-protection batching · descriptor reuse · launcher cleanup
P11   ── shirts for all 342 Reborn models
P12   ── varied shirts (fixed per model)
P13   ── fighter entrance memory fix · 1 ms timer for Original
P14   ── new launcher (PT/EN/ES)
P14.1 ── Bruce Buffer fixed
P14.2 ── AMD FSR 1
P14.3 ── two-thread GPU submission
P14.4 ── Cheats tab: Full Training Rewards (Auto Mode)
P14.5 ── Linux page-fault fix (Ally 20 W: 60 FPS) · Double Money cheat
```

P8 and P9 were investigation builds. The full changelog with measurements is in the
[P14.5 release](https://github.com/monscriser/UFC3MONS/releases).

## Performance

### ROG Ally Z1 Extreme, Linux, 20 W — Reborn Career fight

```
FPS during the fight (higher is better, target 60)

P14.4  |████████████████████████████████████████▏           46–50 FPS
P14.5  |██████████████████████████████████████████████████  60 FPS (locked)
       0         10        20        30        40        50        60
```

| Metric (same fight, Ally @ 20 W) | P14.4 | P14.5 |
|---|---:|---:|
| Average FPS | 46–50 | **60** |
| Cost of one write-watch page fault | ~330 µs | **~59 µs** |
| Time spent in page faults | ~280 ms per second | **61 ms per second** |
| Game's command-building thread | 461–530 ms/s | **294 ms/s** |
| GPU clock | swinging 800–2000 MHz | **~1790 MHz, stable** |

**What was wrong:** the emulator protects the memory the game writes to, so it knows when to re-upload it to the GPU.
On Linux, every resulting page fault read and parsed the whole `/proc/self/maps` file. Windows answers the same question
with a fast system call, so only Linux was affected. The slow check now runs only in the rare case where it is needed.
The behavior stays the same: rebuilding without the change gives the previous runtime back, and the check-free path was
verified on PC and on the Ally.

### Other measured improvements

| Build | Change | Result |
|---|---|---|
| P14.3 | GPU submission on a second thread | 1.216 → 0.083 ms per frame to submit; 26,686 submissions checked, 0 differences |
| P14.2 | AMD FSR 1 (720p → output resolution) | GPU usage 37.7% → 20% in the same scene, with native-like sharpness |
| P7 | Constant-buffer descriptor cache | Constant-binding stage 4.3% cheaper; 66.9 million comparisons, 0 differences |

Performance depends on the scene, hardware, driver, settings and shader-cache state. Static recompilation alone does not
guarantee higher performance than an emulator.

![Development FPS and CPU progress (R8–R9 history)](media/performance-r9.png)

*Earlier R8–R9 history: Linux development observations on the ROG Ally before the P series.*

## Features

### Launcher
- Tabs: **Home, Video & language, Keyboard & controller, Saves & logs, Cheats**.
- Game source: **Reborn folder, Original XEX/folder or ISO** (the ISO is read directly).
- DLC install from a folder; **Add to Steam** creates a shortcut with artwork.
- Remappable keyboard; controller support.
- Save backup, shader-cache cleanup and log folders one click away. **Write logs** is off by default.
- Validated optimizations are always on; the old test switches are no longer on screen.

### Graphics, Timing and Performance
- Vulkan rendering with project-specific SDK modifications.
- Optional **AMD FSR 1** upscaling (off by default; not used on 16:10 screens or with black bars).
- Two-thread GPU submission, constant/descriptor caches, register and memory-protection batching.
- Linux page-fault fast path (P14.5).
- Synchronous shaders and a 60 FPS limit. Console vsync stays enabled: the game simulation advances once per frame.
- In-game FPS overlay toggled with **F6**.

### Cheats (off by default, work while the game is running)
- **Full Training Rewards (Auto Mode):** AUTO training in Career gives the best Manual Training grade (4 stars). Tested:
  same save and drill, 2 stars and +3 Speed off → 4 stars and +5 Speed on, saved to the fighter.
- **Double Money:** Creds earned from each Career fight count twice, within the game's own maximum. Tested: a fight
  worth 1,600 Creds paid 3,200, shown and saved.

### Reborn
- Shirts for all **342** fighter models (each model keeps the same shirt).
- Original game files are never modified; compatible copies are created on first start.

## Original and Reborn Compatibility

| Configuration | Current development status |
| --- | --- |
| Reborn | Career and Exhibition playable on Windows and Linux; Career fight at 60 FPS on the ROG Ally at 20 W |
| Original | Playable from XEX/folder or ISO. The invisible-opponent bug was fixed in R9.1; the 1 ms timer fix was added in P13 |
| Both | DLC supported. Wider gameplay, save, controller and hardware testing is still welcome |

## Getting Started

### For Players
1. Download the Windows zip or the Linux AppImage from [Releases](https://github.com/monscriser/UFC3MONS/releases).
   - The Windows zip contains only `Launcher-Config.exe`, `UFC3.exe` and `data/`. Each file has a `.sha256` to check it.
2. Open the launcher and point it to **your own** game: Reborn folder, Original XEX/folder or ISO.
3. Optional: install DLC from a folder, pick resolution/FSR 1 and language, or use **Add to Steam**.
4. Press **PLAY**.

**Requirements:** a Vulkan GPU, and a CPU with SSE4.2 (x86-64-v2). **AVX2 is not required.**

GitHub's **Download ZIP** button downloads source code, not a playable game. No game data, DLC or saves are included.

### For Developers
1. Read [dependencies and build limitations](docs/DEPENDENCIAS.md).
2. Inspect `app/variants/` for the platform and edition you are studying; these files take precedence over shared snapshots.
3. Review `sdk-modified/` and the incremental changes in `patches/`.
4. Treat `scripts/historical/` as records of the original environment, not portable build commands.

A clean build still requires a compatible SDK, code generated from your own game copy and other local dependencies.

## Repository Layout

| Path | Contents |
| --- | --- |
| `app/common/` | Shared integration and launcher snapshots |
| `app/variants/` | Platform- and edition-specific integration files |
| `app/fixes/` | Input/presentation fixes and optional diagnostic samplers |
| `sdk-modified/` | GPU/runtime modifications, support headers and the readback shader |
| `patches/` | Incremental changes against previous local snapshots |
| `scripts/historical/` | Original build and packaging recipes with local dependencies |
| `docs/` | Development state, dependencies and publication information |
| `LICENSES/` | Included third-party license |
| `index.html` | Standalone project page for GitHub Pages |

## Known Limitations

- The game simulation advances once per frame: if FPS drops below 60, the action slows down too (being investigated).
- Some menus, videos (Bink) and loading screens run at 30 FPS or drop briefly.
- The fighter entrance can still run at 30 FPS on the Ally.
- The Cheats were tested on Windows. The 20 W result was measured on the Ally (Linux).
- External overlays may report the compositor's frame rate; F6 reports the game's own frame counter.
- Historical recipes reuse local objects and precompiled headers. Portable, reproducible builds are pending.
- Generated game code and `ufc_offline_memory.h` are deliberately excluded from the sources.

## Reporting Issues and Contributing

Open an [issue](https://github.com/monscriser/UFC3MONS/issues) and include the build (for example P14.5), Original/Reborn
edition, operating system, CPU, GPU, driver, scene, game mode, reproduction steps, display settings and relevant logs.
To get logs, turn on **Write logs** in **Saves & logs**, play, then use **Open all logs**. Screenshots are useful when
private information is removed.

For comparison tests, use the same scene and settings. Do not upload game files, DLC, generated game code or personal saves
to public issues.

## Credits and Licensing

Thanks to the developers and contributors behind **ReXGlue**, **Xenia**, **Dear ImGui**, **RenderDoc**, **AMD FidelityFX
(FSR 1)** and the graphics and recompilation communities. Earlier HydroThunderPORT performance work informed optimizations
adapted for this project.

Own code uses [MIT](LICENSE). ReXGlue/Xenia-derived files retain BSD licensing; the RenderDoc header retains its MIT notice.
See [third-party notices](THIRD_PARTY_NOTICES.md) and [LICENSES](LICENSES/).

UFC3MONS is an unofficial community project, not affiliated with or endorsed by the original game's developers, publishers
or trademark owners. UFC Undisputed 3 names, artwork and content belong to their respective rights holders. This repository
grants no license to those materials.
