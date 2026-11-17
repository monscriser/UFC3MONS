#!/usr/bin/env bash
# BETA 1.8: Reborn chunk 229 with the issue #24 fix (jump table entry 7 -> sub_83669280),
# built for Windows (.obj) and Linux (.o). Starts from the BETA 1.5 switch-fix source.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
history='/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-fix-switch'
validation='/mnt/e/PROJETOS ANTIGOS/Codex-22-10/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/beta17-validacao'
lab=/mnt/e/UFC3-RexGlue-Lab
generated="$lab/sources/ufc3-native-audited-r9/generated"
sdk="$lab/build/native-r9-sdk"
source="$here/ufc3_recomp.229.fix24.cpp"
python3 "$validation/patch_reborn_switch.py" "$history/ufc3_recomp.229.fix.cpp" "$source"
grep -q 'ctx.ctr.u32 == 0x83669280' "$source"

if [[ "${1:-}" != "linux" ]]; then
  CXX=/root/ht/llvm-mingw/bin/x86_64-w64-mingw32-clang++
  cp "$sdk/CMakeFiles/ufc3_recomp.dir/cmake_pch.hxx" "$here/win/recomp-prefix.hxx"
  echo "== windows"
  (cd "$sdk" && "$CXX" \
    -DNOMINMAX -DREX_HAS_VULKAN=1 -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL \
    -DUFC_EXTERNAL_JMPBUF=1 -DUFC_EXTERNAL_LOCK_CHECK=1 -DWIN32_LEAN_AND_MEAN \
    -I"$generated" -isystem "$lab/build/sdk-installed/include" \
    -march=x86-64-v2 -O3 -DNDEBUG -std=gnu++23 -fms-extensions -ffp-model=strict \
    -g0 -Xclang -fasync-exceptions -msse4.1 -include "$here/win/recomp-prefix.hxx" \
    -o "$here/win/fix24.obj" -c "$source")
fi

echo "== linux"
python3 - "$source" "$here/linux/fix24.o" <<'PY'
import subprocess, sys
sys.path.insert(0, "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/linux-update/pre")
import linux_pre as lp
source, output = sys.argv[1], sys.argv[2]
e = [p for p in lp.plans('reborn')[0] if p['label'] == 'reborn-ufc3_recomp.229'][0]
argv = [source if x == e['source'] else (output if x == e['output'] else x) for x in e['argv']]
subprocess.run(argv, check=True, cwd="/")
PY
sha256sum "$here"/win/fix24.obj "$here"/linux/fix24.o 2>/dev/null || true
