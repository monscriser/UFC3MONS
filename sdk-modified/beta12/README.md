# BETA 1.2 SDK changes

Replacement files for the ReXGlue SDK (v0.10.0) used by the BETA 1.2 builds. Patch, build and measurement scripts are in
`scripts/beta12`. These files supersede the same files in `sdk-modified/beta` and `sdk-modified/gpu`.

| Folder / file | Change |
|---|---|
| `gpu/rt_cache.cpp`, `gpu/vulkan_render_target_cache*.cpp`, `include/.../render_target/cache.h`, `include/.../vulkan/render_target_cache.h` | Blood fix: render targets up to `ufc_draw_resolution_scale_threshold` (320) px wide stay at native scale (idea from Xenia Canary 74db632, BSD). `ufc_clear_transfer_cutout`: no EDRAM transfers into the area a depth+stencil or color clear rectangle overwrites (exact-coverage rules, sentinel proof and positive-control cvars) |
| `gpu/spirv_translator.cpp`, `gpu/pipeline_cache.cpp`, `gpu/tex_cache.cpp`, `include/.../shader`, `include/.../texture` | Per-draw resolution scale for the blood fix |
| `gpu/vulkan_command_processor.cpp` | Per-draw viewport/scissor scale, upload hoisting (A1), skip of unchanged fetch constants (B57), video memory log `[ufc-vram]` |
| `gpu/shared_memory.cpp` | Range check without the global lock (B56) |
| `gpu/texture_cache.cpp` | Write-after-write barrier on the old texture path (A16); diagnostics and an optional mip path (off) |
| `gpu/vsm_r4.cpp` | Vulkan shared memory with the upload-hoisting hooks |
| `gpu/cache.cpp` | Optional texture-destroy lock (B60, not in the build) |
| `runtime/xma_context.cpp`, `runtime/xboxkrnl_audio_xma.cpp` | XMA loop end read from the hardware layout (A3) |
| `runtime/content_manager.cpp` | Long-path content folders (A5) |
| `runtime/xthread.cpp` | Resume of a running thread is a no-op (A15) |
| `runtime/threading_posix.cpp` | Linux: stop changing thread priority after the first EPERM (A11) |

Not published, because they contain game code or bytes from game files: recompiled code, `ufc_offline_memory.h`,
`ufc_gym_patch.h`, `ufc_button_art_data_p6.h`, `ufc_shirt_alias_data.h` and the shader seed lists.
The scripts use paths from the author's PC, so the tree does not build as it is.
