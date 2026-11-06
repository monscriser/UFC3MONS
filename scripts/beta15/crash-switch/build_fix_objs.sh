# Compila ufc3_recomp.229 da Reborn (Windows .obj e Linux .o): controle (fonte original -> deve bater com o objeto
# usado nas builds) e corrigido (patch_switch.py).
set -euo pipefail
H="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-fix-switch"
G=/mnt/e/UFC3-RexGlue-Lab/sources/ufc3-native-audited-r9/generated
B=/mnt/e/UFC3-RexGlue-Lab/build/native-r9-sdk
CXX=/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/toolchain/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang++
cd "$H"
python3 patch_switch.py "$G/ufc3_recomp.229.cpp" "$H/ufc3_recomp.229.fix.cpp"
DEF="-DNOMINMAX -DREX_HAS_VULKAN=1 -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DUFC_EXTERNAL_JMPBUF=1 -DUFC_EXTERNAL_LOCK_CHECK=1 -DWIN32_LEAN_AND_MEAN"
FL="-march=x86-64-v2 -O3 -DNDEBUG -std=gnu++23 -fms-extensions -ffp-model=strict -g0 -Xclang -fasync-exceptions -msse4.1 -Winvalid-pch -Xclang -include-pch -Xclang $B/CMakeFiles/ufc3_recomp.dir/cmake_pch.hxx.pch -Xclang -include -Xclang $B/CMakeFiles/ufc3_recomp.dir/cmake_pch.hxx"
INC="-I$G -isystem /mnt/e/UFC3-RexGlue-Lab/build/sdk-installed/include"
mkdir -p win linux
echo "== windows controle"; (cd "$B" && $CXX $DEF $INC $FL -o "$H/win/control.obj" -c "$G/ufc3_recomp.229.cpp")
echo "== windows corrigido"; (cd "$B" && $CXX $DEF $INC $FL -o "$H/win/fix.obj" -c "$H/ufc3_recomp.229.fix.cpp")
sha256sum "$B/CMakeFiles/ufc3_recomp.dir/generated/ufc3_recomp.229.cpp.obj" win/control.obj win/fix.obj | cut -c1-16
cat > /root/lxobj.py <<'PY'
import sys, subprocess
sys.path.insert(0, "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/linux-update/pre")
import linux_pre as lp
H = sys.argv[1]
e = [p for p in lp.plans('reborn')[0] if p['label'] == 'reborn-ufc3_recomp.229'][0]
argv = e['argv']
for tag, src in (("control", e['source']), ("fix", H + "/ufc3_recomp.229.fix.cpp")):
    a = [src if x == e['source'] else (H + "/linux/" + tag + ".o" if x == e['output'] else x) for x in argv]
    print("== linux", tag); subprocess.run(a, check=True, cwd="/")
print(e['output'])
PY
python3 /root/lxobj.py "$H"
sha256sum /mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/linux-update/pre/obj-reborn/ufc3_recomp.229.o linux/control.o linux/fix.o | cut -c1-16
