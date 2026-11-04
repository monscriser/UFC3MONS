#!/usr/bin/env bash
# Copy the BETA 1.2 sources (SDK replacement files, patch and build scripts) into the public repository.
# Only SDK-derived files and own scripts: no recompiled code, no game bytes, no binaries.
set -euo pipefail
T="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho"
W="$T/gpu-port-blood"
G="$T/github-UFC3MONS"
P14="$T/launcher-p14-28-10"
B="$G/sdk-modified/beta12"
S="$G/scripts/beta12"
mkdir -p "$B/gpu" "$B/include/rex/graphics/pipeline/render_target" "$B/include/rex/graphics/pipeline/shader" \
         "$B/include/rex/graphics/pipeline/texture" "$B/include/rex/graphics/vulkan" "$B/runtime" "$S"
for f in vulkan_render_target_cache.cpp vulkan_render_target_cache.linux.cpp vulkan_command_processor.cpp \
         texture_cache.cpp rt_cache.cpp tex_cache.cpp spirv_translator.cpp pipeline_cache.cpp \
         shared_memory.cpp vsm_r4.cpp cache.cpp; do
  cp "$W/$f" "$B/gpu/$f"
done
cp "$W/inc/rex/graphics/pipeline/render_target/cache.h" "$B/include/rex/graphics/pipeline/render_target/"
cp "$W/inc/rex/graphics/pipeline/shader/spirv_translator.h" "$B/include/rex/graphics/pipeline/shader/"
cp "$W/inc/rex/graphics/pipeline/texture/cache.h" "$B/include/rex/graphics/pipeline/texture/"
cp "$W/inc/rex/graphics/vulkan/render_target_cache.h" "$B/include/rex/graphics/vulkan/"
cp "$T/a3-xma-loop/fix/xma_context.cpp" "$T/a3-xma-loop/fix/xboxkrnl_audio_xma.cpp" "$B/runtime/"
cp "$T/a5-longpath/fix/src/system/xam/content_manager.cpp" "$B/runtime/"
cp "$T/a15-resume/fix/xthread.cpp" "$B/runtime/"
cp "$T/a11-priority/fix/threading_posix.cpp" "$B/runtime/"
for f in apply_blood.py apply_blood_p4.py apply_blood_p5.py apply_clear_cutout.py apply_xfer_diag.py \
         build_win.py build_linux.py build_and_package_beta12.sh gpu_ab.ps1 run_cut.ps1 trace_groups.py \
         stage_github_beta12.sh; do
  cp "$W/$f" "$S/$f"
done
cp "$T/b21-copias/between_breakdown.py" "$S/"
cp "$T/a3-xma-loop/build_runtime_all.sh" "$S/" 2>/dev/null || true
cp "$W/THIRD_PARTY_NOTICES.md" "$G/THIRD_PARTY_NOTICES.md"
# Launcher sources changed for BETA 1.2: build name and the A1 upload-hoist switch.
cp "$W/backup-originais/ufc_session.h.beta12" "$G/app/common/ufc_session.h"
cp "$W/backup-originais/ufc_build_identity.h.beta12" "$G/app/common/ufc_build_identity.h"
# Safety: nothing that embeds game code or bytes.
if grep -rlE "ufc3_recomp|ufc_offline_memory|ufc_gym_patch|ufc_button_art_data_p6|ufc_shirt_alias_data" "$B" "$S" \
     --include=*.cpp --include=*.h | grep -v "^$S/build_" ; then
  echo "WARNING: names of private files referenced above (check they are only mentions)"
fi
find "$B" "$S" -type f \( -name '*.dll' -o -name '*.so' -o -name '*.exe' -o -name '*.obj' -o -name '*.o' \) -print -quit | grep -q . && { echo "binary found"; exit 1; }
echo staged
