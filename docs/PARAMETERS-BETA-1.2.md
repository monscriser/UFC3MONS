# BETA 1.2 — parameters of every change

Every change in BETA 1.2 can be switched on or off with a parameter (cvar), passed to the game as `--name=value`. The table also gives the default, what the change does, and how it was checked.

Normal players do not need any of this: the defaults are the tested setup. These switches exist for A/B tests and bug reports.

## GPU and video

| ID | Parameter | Default | What it does | Measured / proof |
|---|---|---|---|---|
| **A1** | `ufc_hoist_shared_memory_uploads` | `true` | Memory uploads requested in the middle of a render pass are moved before the pass, so the GPU does not stop drawing to copy. Launcher env `UFC_HOIST_UPLOADS=0` turns it off | No FPS limit: 169.7 → 183.4 FPS (+8%). The data copied is the same, only the moment changes |
| **U4** | `ufc_draw_resolution_scale_threshold` | `320` | Render targets whose pitch is at most this many pixels stay at native size (fixes blood sliding at 2× and above). `0` = off | 2×: +13% FPS, −36% video memory. Confirmed in game |
| **Cutout** | `ufc_clear_transfer_cutout` | `true` | Skips EDRAM copies into the area the game clears right after (depth+stencil or color clear rectangle, exact coverage only) | −6.8% GPU time (same build, on vs off) |
| | `ufc_clear_cutout_log` | `false` | Logs `[ufc-cutout]` counters every 10 s | Diagnostic |
| | `ufc_clear_cutout_sentinel` | `false` | Test only: fills the skipped area with junk before the clear (must never be visible) | Proof: >140,000 clears, clean image |
| | `ufc_clear_cutout_sentinel_break` | `false` | Test only (positive control): junk over the cleared area after the clear (must be visible) | Screen turns red |
| **A16** | `ufc_texture_waw_barrier` | `true` | Barrier between two uploads in a row to the same texture (old texture path) | Safety fix; 0 cases in a full fight |
| **B57** | `ufc_fetch_unchanged_skip` | `true` | Rewriting a texture/vertex fetch constant with the same value no longer invalidates bindings | −2% GPU command-thread work; 38% of the rewrites were identical |
| **B56** | `ufc_request_range_lockless` | `true` | Single-range valid-page check without the global lock | −10% command-thread work per texture lookup |
| | `ufc_request_range_fast_verify` | `false` | Test only: runs the original path too and counts differences | 8.1 M hits, 28 benign races |
| | `ufc_request_range_lockless_verify_break` | `false` | Test only (positive control) | 496,836 detections |
| **VRAM log** | `ufc_vram_log` | `true` | With logs on, writes `[ufc-vram]` (video memory use/budget) every 2 s | — |

## Original edition

| ID | Parameter | Default | What it does | Measured / proof |
|---|---|---|---|---|
| **U1** | env `UFC_ORIGINAL_READBACK=none` | not set | The Original edition now reads back GPU copies like Reborn (full readback + exact on-demand), so created fighters save their skin, shirt and hat correctly. Setting `none` restores the old behaviour | Positive control: old mode turned the fighter black again after saving; new mode fixed him. On-demand: 141 GB requested, 0.2 GB actually copied |

## BETA 1.3 (Original edition performance)

| ID | Parameter | Default | What it does | Measured / proof |
|---|---|---|---|---|
| **Skip scaled** | `ufc_readback_skip_scaled` (set by the Original; env `UFC_ORIGINAL_SKIP_SCALED=0` turns it off) | `true` in the Original, `false` in Reborn | Resolution-scaled copies are not read back (BETA 1.0 behaviour); CAF portrait captures still are | 3×: ~30 → 59 FPS in a fight; command thread 8.1 → 4.6 ms/frame |
| **Fast check** | env `UFC_ONDEMAND_FASTCHECK=0` turns it off | on | The on-demand readback checks a page map before walking its whole list of past copies | 1×, minutes 2–3 of a fight: ~146 → ~170 FPS. Proof: 274,996 skipped walks, 0 differences; positive control (`UFC_ONDEMAND_FASTCHECK_VERIFY=positive`) 186,074 |
| | env `UFC_ONDEMAND_FASTCHECK_VERIFY=1` | off | Test only: also runs the full walk and counts differences (`[ufc-ondemand-fastcheck]`) | — |

## BETA 1.4 (Linux, Original edition freezes)

| ID | Parameter | Default | What it does | Measured / proof |
|---|---|---|---|---|
| **Procmap query** | env `UFC_PROCMAP_QUERY=0` turns it off | on (Linux 6.11+; older kernels fall back automatically) | `ufc_linux::Query` asks the kernel for one address with the `PROCMAP_QUERY` ioctl instead of reading the whole `/proc/self/maps`. The on-demand readback calls it for every 4 KB page it watches | One query 0.7–5.5 ms → 0.5 µs. Proof: 500,000 queries, 0 differences; positive control (`UFC_PROCMAP_VERIFY=positive`) 374,866 |
| | env `UFC_PROCMAP_VERIFY=1` | off | Test only: also runs the old parser on every query and counts differences (`[ufc-procmap-verify]` on stderr) | — |

## BETA 1.5 (CPU per draw, GPU-commands thread)

All on by default. Each one skips work only when every input is the same as in the previous draw. Proof: verifier runs the full path anyway and compares (fight, all four together: 4.96 M + 3.44 M + 9.70 M + 4.29 M comparisons, 0 differences); positive controls detect every perturbed case.

| Parameter | What it does | Measured (per draw, same scene) |
|---|---|---|
| `ufc_rt_update_skip` | B6: skip render-target setup when nothing changed (key of all inputs + EDRAM ownership generation) | setup −44% |
| `ufc_sampler_reuse` | B4/B10: reuse the previous draw's samplers (same shaders, fetch constants, submission) | samplers −19% |
| `ufc_translation_memo` | B11: reuse the shader translation (no mutex/map lookup) | small |
| `ufc_pipeline_desc_memo` | B11: reuse the pipeline description when all its inputs match | small |
| `*_verify` / `*_verify_break` | Test only: exact proof / positive control | — |

Together: −0.14 µs per draw, about −5% of the GPU-commands thread. Vertex buffers, texture requests and constant writes were measured too: that time is real new data every draw, nothing to skip.

## Audio and system

| ID | Parameter | Default | What it does | Measured / proof |
|---|---|---|---|---|
| **A3** | `ufc_xma_loop_end_hw_layout` | `true` | Reads the XMA loop end from the hardware layout (the bits the game actually writes) | 52+ loop ends per fight, 0 XMA errors |
| **A15** | `ufc_resume_running_noop` | `true` | Windows: resuming a thread that is already running is a no-op, as on the console | 4 cases per session fixed |
| **A5** | — (always on) | — | Long-path content folders (more than ~248 characters) | Crash mechanism reproduced in isolation |
| **A11** | — (always on, Linux) | — | Stops changing thread priority after the first permission refusal and logs it once | Control object byte-identical to the original |

## Diagnostics (off by default, not needed to play)

| Parameter | What it logs |
|---|---|
| `ufc_xfer_diag` | Draws that need EDRAM transfers (`[ufc-xfer]`) |
| `ufc_texload_diag` | Textures loaded through buffer-to-image copies (`[ufc-texload]`) |
| `ufc_texture_direct_mips` / `_verify_break` | Direct loading of 2-level textures (exact, but no gain measured, so it stays off) |

## Measured and not shipped

These were tested and left out because they showed no gain, or no case of the problem was found in UFC. The code or the notes stay in the repository.

| Item | Result |
|---|---|
| A13 (recompiler register options) | Hung the game; fixed in the code generator, then no CPU gain → not shipped |
| B21, B37, B32 remap, B2 | Small or no target in UFC |
| A2, A4, A6, A7, A8, B55, B60 | No symptom or no gain |
| Direct resolve on small targets, direct mip loading | Exact proof passed, no gain |
