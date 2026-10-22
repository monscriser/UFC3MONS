# UFC UNDISPUTED 3 - UFC3MONS RECOMP | R9

**Our first public release brings together the work of several internal development revisions, with the main focus on the community-created Reborn mod.** Reborn is a community project featured in community YouTube videos; credit for the mod belongs to its creators. UFC3MONS RECOMP provides the native PC integration and the port-specific fixes and optimizations described below.

## ROG Ally Z1 Extreme: Reborn Career Mode

The project maintainer reports **stable Reborn Career Mode gameplay on both Windows and Linux**, running at **60 FPS**, with **no gameplay bugs or texture errors observed during those tested sessions**.

60 FPS is the current engine-timing target/limit used by this build. The objective is correct game speed and stable gameplay, not an uncapped counter. Console vsync remains enabled because disabling it accelerated menus, the match clock and other game behavior in development tests.

This is a hands-on validation report on the tested hardware and scenarios, not a claim that every career, fighter, configuration or device is free of defects.

## From the early measured baseline to R9

R9 is the first version being posted publicly, but the improvements were developed and checked across earlier internal builds.

![UFC3MONS development FPS and CPU comparison on ROG Ally Z1 Extreme](https://raw.githubusercontent.com/monscriser/UFC3MONS/main/media/performance-r9.png)

| Internal revision | Observed fight FPS | Approx. CPU usage | Main change |
| --- | --- | --- | --- |
| R8 | 30, with slow-motion gameplay | ~3.7 core equivalents | Input stability and accurate presentation baseline |
| R8.1 | No separate FPS range recorded here | Not separately quantified | Backoff in the game's GPU-fence spin wait |
| R8.3 | 33–36 | ~3.1 core equivalents | Faster register handling and grouped float-constant updates |
| R8.4 | 36–41 | ~3.1 core equivalents | Fast path for already-valid shared-memory ranges |
| R8.5, underlying R9 | 57–60 | ~2.1 core equivalents | Second GPU submission thread + Linux audio-wait fix |
| R9, latest maintainer test | Stable 60 in tested Reborn Career sessions | Not re-measured | Consolidated build, tested on Windows and Linux |

The recorded Linux fight ranges represent approximately **90–100% higher FPS** than the 30 FPS R8 baseline, with approximate CPU usage reduced from 3.7 to 2.1 core equivalents, or **about 43%**. These figures come from development sessions, not a controlled benchmark across identical runs. R8 is the earliest quantified baseline available for this chart; we do not have comparable measurements for the very first prototype. No power-consumption or battery-life gain is claimed.

## What was fixed and optimized

### 1. Controller vibration crash

Concurrent controller operations could refresh devices while another input call was active, corrupting the heap and closing the game during a fight. A shared mutex now serializes the four relevant input entry points: capabilities, state, vibration and keystrokes. An earlier Ally session recorded **124 contended input calls without a crash**, over approximately 20 minutes.

### 2. Accurate game-frame reporting

An open ImGui dialog could continuously repaint and inflate MangoHud/Afterburner readings. The overlay now uses a dedicated UI drawer and presents alongside frames delivered by the game. **F6** toggles the game's FPS overlay. Gamescope can still cause an external overlay to report compositor FPS instead of game FPS.

### 3. Less CPU spent waiting for the GPU

The game's fence-wait loop now uses a bounded pause/yield/sleep backoff instead of continuously spinning. This reduced unnecessary CPU activity; it was not presented as a standalone FPS gain.

### 4. Faster command processing

Fast register handling and grouped runs of floating-point constants reduce work on the graphics command-processing thread. These optimizations were already present in the project plugin but disabled; they were enabled and evaluated in the R8.3 development stage.

### 5. Shared-memory request shortcut

Single-range requests can skip merge/sort work when all requested pages are already valid. Invalid pages still take the original path. This adapts earlier HydroThunderPORT work to UFC's graphics workload.

### 6. A second GPU submission thread

Command submission moves to a second thread, reducing work on the translation thread. Swap submission stays synchronous. The feature disables itself for conflicting resolve-readback or GPU-measurement modes. This is **GPU command submission**, not asynchronous shader compilation.

### 7. Linux audio-wait fix

The POSIX runtime truncated remaining wait time to whole milliseconds, turning short waits into zero-duration sleeps. Preserving nanosecond precision stopped the audio thread from spinning at roughly one full CPU core. The Windows path uses native waits and did not have that particular problem.

### 8. Consolidation without changing game speed

R9 combines the accepted changes and removes optional CPU profiling samplers from the normal build. Console vsync stays on and shader compilation stays synchronous. An internal vsync-off experiment was rejected because higher reported FPS came with incorrect game speed.

## Launcher and integration features

- Separate Windows and Linux integration, with Original and Reborn configurations.
- Setup paths for supported extracted game content, XEX selections and ISO sources.
- Game-directory registration, save backups, log access and recoverable cache cleanup.
- Controller support, keyboard mapping and optional mouse-to-right-stick configuration.
- Portuguese/English launcher interface, game-language selection and resolution/fullscreen settings.
- Steam-shortcut export support and optional per-session diagnostic reports.

These features are present in the integration sources; their presence is not a claim that every path has been exhaustively tested on every system. FSR 1 and background shader compilation are not advertised as available features of this candidate.

## Validation performed during development

- **Real gameplay:** maintainer-confirmed Reborn Career Mode on ROG Ally Z1 Extreme, Windows and Linux, at 60 FPS, without observed texture errors or gameplay bugs in those sessions.
- **Stability investigation:** the Windows crash dump identified the vibration/input race; the serialized-input path was exercised in the logged Ally session.
- **Incremental Linux performance testing:** R8.3, R8.4 and R8.5 fight logs tracked FPS and approximate CPU-core usage.
- **Rebuild controls:** unchanged Linux app/plugin/runtime rebuild controls reproduced the earlier reference binaries byte for byte. Windows controls differed in PE timestamps and, for the plugin, embedded source-path separators.
- **Submission verification:** the recorded WSL command-stream hash test covered **1,422 submissions with zero differences**; an Ally run recorded **3,859 submissions without failure**. This is a command-stream check, not proof of identical pixels in every scene.
- **Audio waiting:** the WSL diagnostic changed from approximately 1,000 ms of CPU time per second to **99.5% of the observed time sleeping** in the investigated wait path.
- **Build and tool checks:** native helper compilation/execution, tool compilation/execution, core-format checks, GPU inventory checks and plugin-symbol checks passed in the earlier parity test suite. Those checks alone did not launch or validate gameplay.
- **Original graphics investigation:** compared draw submission, tested internal scaling and occlusion-query hypotheses, and preserved a RenderDoc capture for further analysis. The earlier pixel-history script had limitations and was not treated as a completed diagnosis.

Hardware coverage remains limited. The WSL tests used diagnostic/software-GPU paths where applicable and do not replace handheld gameplay validation.

## Known issues and next-build priorities

- **Original-game builds have graphics problems in Career Mode**, including the invisible-opponent issue under investigation. The current priority has been Reborn. Fixes for Original are planned for future builds; they are not claimed as resolved in R9 and no delivery date is promised.
- Short drops can still occur during loading and scene transitions.
- Synchronous shader compilation can still introduce stalls while new shaders are encountered.
- Wider hardware, fighter, save and career coverage is still needed.
- Some external overlay/compositor combinations can show a different FPS counter from the game overlay.
- Public integration sources still depend on excluded local materials and do not yet provide a clean, portable reproduction of the complete R9 executable.

## Sources, credits and reporting

Please include your edition, revision, OS, CPU/GPU, driver, game mode, reproduction steps and relevant logs when reporting a problem. Remove personal information; do not attach game files, generated game code or personal saves.

Thanks to the **Reborn mod creators and community**, and to **ReXGlue, Xenia, Dear ImGui, RenderDoc and HydroThunderPORT contributors** whose work supported the port and its investigation.

UFC3MONS RECOMP is unofficial and is not affiliated with the original game's developers, publishers or trademark owners. The public source repository excludes original game content and generated game code. Project code uses MIT where applicable; third-party code retains its own licenses.
