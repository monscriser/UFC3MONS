#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
previous=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3/work/ufc3mons-beta18-build-tree
export UFC3_BUILD_TREE="$here/build-tree"
export UFC3_CODEX_ARCHIVE='/mnt/e/PROJETOS ANTIGOS/Codex-22-10'
export UFC3_WINDOWS_CXX=/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++
export UFC_OUT_SUFFIX="${UFC_OUT_SUFFIX:--diag2}"
variant=${1:-win-reborn}
case "$variant" in
  win-reborn)
    export UFC_EXTRA_CXXFLAGS=-DUFC_CS60_DIAG
    export UFC_REPEAT_GUARD_DIAG_OBJECTS="${UFC_REPEAT_GUARD_DIAG_OBJECTS:-$previous/build/win-reborn-diag}"
    ;;
  linux-reborn)
    export UFC_REPEAT_GUARD_DIAG_OBJECTS="${UFC_REPEAT_GUARD_DIAG_OBJECTS:-$previous/build/linux-reborn}"
    ;;
  win-original)
    export UFC3_ORIGINAL_WIN_RECIPES="$previous/windows-original-recipes"
    export UFC_REPEAT_GUARD_DIAG_OBJECTS="${UFC_REPEAT_GUARD_DIAG_OBJECTS:-$previous/build/win-original}"
    ;;
  linux-original)
    export UFC_REPEAT_GUARD_DIAG_OBJECTS="${UFC_REPEAT_GUARD_DIAG_OBJECTS:-$previous/build/linux-original}"
    ;;
  *)
    printf 'Unknown Reborn build variant: %s\n' "$variant" >&2
    exit 2
    ;;
esac
python3 -u "$here/source/scripts/beta18/build.py" "$variant" | tee "$here/build-guard${UFC_OUT_SUFFIX}.log"
