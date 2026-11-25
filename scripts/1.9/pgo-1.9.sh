#!/usr/bin/env bash
# UFC3MONS 1.9 — final PGO builds (release: no UFC_CS60_DIAG). Same training profiles and PGO recompiled chunks as
# BETA 1.8 (pgo-final3.sh / pgo-linux-link.sh); sources from beta181-lab (build-tree/src + source/app/common).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
export UFC3_BUILD_TREE="$here/build-tree" UFC3_CODEX_ARCHIVE='/mnt/e/PROJETOS ANTIGOS/Codex-22-10'
export UFC3_WINDOWS_CXX=/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++
export UFC3_ORIGINAL_WIN_RECIPES=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3/work/ufc3mons-beta18-build-tree/windows-original-recipes
B="$here/source/scripts/beta18/build.py"; R=/mnt/e/ufc3mons-pgo/use
common="-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"
export UFC_OUT_SUFFIX=-1.9-pgo
variants=${*:-win-reborn win-original linux-reborn linux-original frontdoor}
for v in $variants; do
  rm -rf "$UFC3_BUILD_TREE/build/$v$UFC_OUT_SUFFIX"
  case "$v" in
    win-reborn) UFC_EXTRA_CXXFLAGS="-fprofile-use=/mnt/c/pgo/original.profdata $common" UFC_RECOMP_OBJS_WIN_REBORN=$R/reborn UFC3_REBORN_SWITCH_FIX=$R/reborn python3 -u "$B" win-reborn ;;
    win-original) UFC_EXTRA_CXXFLAGS="-fprofile-use=/mnt/c/pgo/original.profdata $common" UFC_RECOMP_OBJS_WIN_ORIGINAL=$R/original python3 -u "$B" win-original ;;
    linux-reborn) UFC_EXTRA_CXXFLAGS="-fprofile-use=/mnt/e/ufc3mons-pgo/original-llvm19.profdata $common" UFC_RECOMP_OBJS_LINUX_REBORN=$R/linux-reborn UFC3_REBORN_SWITCH_FIX=$R/linux-reborn python3 -u "$B" linux-reborn ;;
    linux-original) UFC_EXTRA_CXXFLAGS="-fprofile-use=/mnt/e/ufc3mons-pgo/original-llvm19.profdata $common" UFC_RECOMP_OBJS_LINUX_ORIGINAL=$R/linux-original python3 -u "$B" linux-original ;;
    frontdoor) python3 -u "$B" frontdoor ;;
  esac
done
