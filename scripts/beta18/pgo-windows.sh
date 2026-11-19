#!/usr/bin/env bash
# BETA 1.8 PGO for the two Windows game executables (Reborn ufc3-launcher-l1.exe, Original ufc3-original-v48.exe).
# Run in WSL as root.
#   pgo-windows.sh gen                  instrumented build + training ZIP (outputs/beta18-pgo/...-TREINO-Windows.zip)
#   pgo-windows.sh use <reborn.profdata> <original.profdata>   optimized build + final ZIP
#   pgo-windows.sh merge <dir>          merges <dir>/reborn-*.profraw and <dir>/original-*.profraw (same llvm version)
# The recompiled game chunks are rebuilt by pgo_recomp.py with the archived Codex folders mounted on their
# original paths (pgo-ns.sh), so the objects are the ones the normal build links, plus the PGO flags.
set -euo pipefail
WS=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3
REPO=$WS/work/UFC3MONS-1.0.12-UFC3MONS
HERE=$REPO/scripts/beta18
NS=$WS/work/beta18/pgo-ns.sh
export UFC3_BUILD_TREE=$WS/work/ufc3mons-beta18-build-tree
export UFC3_CODEX_ARCHIVE='/mnt/e/PROJETOS ANTIGOS/Codex-22-10'
export UFC3_WINDOWS_CXX=/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++
export UFC3_ORIGINAL_WIN_RECIPES=$UFC3_BUILD_TREE/windows-original-recipes
PROFDATA=/root/ht/llvm-mingw/bin/llvm-profdata
ROOT=/mnt/e/ufc3mons-pgo
OUT=$WS/outputs/beta18-pgo; mkdir -p "$OUT"

package() {  # <build suffix> <zip name>
  python3 - "$UFC3_BUILD_TREE/build" "$1" "$OUT/$2" <<'PY'
import hashlib, shutil, sys
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
build, suffix, package = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
base = Path("/mnt/c/UFC3MONS-Kit/saida/UFC3MONS-BETA1.6-Tecnicas-Windows.zip")
rep = {"UFC3.exe": build / "frontdoor/UFC3.exe", "Launcher-Config.exe": build / "frontdoor/UFC3.exe",
       "data/ufc3-launcher-l1.exe": build / f"win-reborn{suffix}/ufc3-launcher-l1.exe",
       "data/ufc3-original-v48.exe": build / f"win-original{suffix}/ufc3-original-v48.exe"}
tmp = package.with_suffix(".tmp")
with ZipFile(base) as src, ZipFile(tmp, "w", ZIP_DEFLATED) as dst:
    for item in src.infolist():
        if item.filename in rep: dst.write(rep[item.filename], item.filename)
        else:
            with src.open(item) as a, dst.open(item, "w") as b: shutil.copyfileobj(a, b)
tmp.replace(package)
digest = hashlib.sha256(package.read_bytes()).hexdigest()
package.with_suffix(".zip.sha256").write_text(f"{digest}  {package.name}\n")
for name, path in rep.items():
    if name.startswith("data/"): print(hashlib.sha256(path.read_bytes()).hexdigest(), name)
print(digest, package)
PY
}

case "${1:-}" in
gen)
  for v in reborn original; do
    bash "$NS" python3 "$HERE/pgo_recomp.py" "win-$v" gen "$ROOT/gen/$v"
  done
  UFC_OUT_SUFFIX=-pgogen UFC_PGO_GEN=1 \
  UFC_EXTRA_CXXFLAGS="-fprofile-generate -DUFC_PGO_GEN" UFC_EXTRA_LDFLAGS="-fprofile-generate" \
  UFC_RECOMP_OBJS_WIN_REBORN=$ROOT/gen/reborn UFC_RECOMP_OBJS_WIN_ORIGINAL=$ROOT/gen/original \
  UFC3_REBORN_SWITCH_FIX=$ROOT/gen/reborn \
    python3 -u "$HERE/build.py" win-reborn win-original
  package -pgogen UFC3MONS-BETA1.8-PGO-TREINO-Windows.zip
  ;;
merge)
  d=${2:?profraw dir}
  "$PROFDATA" merge -o "$d/reborn.profdata" "$d"/reborn-*.profraw
  "$PROFDATA" merge -o "$d/original.profdata" "$d"/original-*.profraw
  ls -la "$d"/*.profdata
  ;;
use)
  rp=${2:?reborn.profdata}; op=${3:?original.profdata}
  bash "$NS" python3 "$HERE/pgo_recomp.py" win-reborn use "$ROOT/use/reborn" "$rp"
  bash "$NS" python3 "$HERE/pgo_recomp.py" win-original use "$ROOT/use/original" "$op"
  common="-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"
  UFC_OUT_SUFFIX=-pgo UFC_EXTRA_CXXFLAGS="-fprofile-use=$rp $common" \
  UFC_RECOMP_OBJS_WIN_REBORN=$ROOT/use/reborn UFC3_REBORN_SWITCH_FIX=$ROOT/use/reborn \
    python3 -u "$HERE/build.py" win-reborn
  UFC_OUT_SUFFIX=-pgo UFC_EXTRA_CXXFLAGS="-fprofile-use=$op $common" \
  UFC_RECOMP_OBJS_WIN_ORIGINAL=$ROOT/use/original \
    python3 -u "$HERE/build.py" win-original
  package -pgo UFC3MONS-BETA1.8-PGO-Windows.zip
  ;;
*) echo "usage: $0 gen | merge <dir> | use <reborn.profdata> <original.profdata>"; exit 2;;
esac
