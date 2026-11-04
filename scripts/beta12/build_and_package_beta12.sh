#!/usr/bin/env bash
set -euo pipefail

W="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-blood"
A1="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-a1"
RT="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/a3-xma-loop"
P14="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-p14-28-10"
TESTE="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/BETA-1.2"
P13ZIP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/ranged-fallback-p13-27-10/release/entrega/UFC3MONS-P13-MEMORIA-ENTRADA-Windows.zip"
P12APP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/shirt-variety-p12-27-10/release/package-linux/UFC3MONS.AppDir"
FIX="/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/fault-measure"

echo "=== 1. GPU libraries already built in gpu-port-blood (out-win / out-linux) ==="
ls -l "$W/out-win/rexgpu-xenos.dll" "$W/out-linux/librexgpu-xenos.so" "$RT/all-win/librexruntime.dll" "$RT/all-linux/librexruntime.so"

echo "=== 2. Patch ufc_session.h and build all 5 targets via launcher build.py ==="
cp "$A1/backup-originais/ufc_session.h.bak" "$P14/src/ufc_session.h"
cp "$P14/src/ufc_build_identity.h" "$W/backup-originais/ufc_build_identity.h.beta11"
sed -i 's/UFC3MONS-BETA1.1-2026-10-29/UFC3MONS-BETA1.2-2026-11-04/' "$P14/src/ufc_build_identity.h"
grep -q "UFC3MONS-BETA1.2-2026-11-04" "$P14/src/ufc_build_identity.h"
python3 - "$P14/src/ufc_session.h" <<'PY'
import sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
old1 = 'const bool early_submit=qa_bool("UFC_QA_EARLY_SUBMIT",true);'
new1 = old1 + 'const bool hoist_uploads=qa_bool("UFC_HOIST_UPLOADS",true);'
old2 = 'args.push_back(std::string("--ufc_submit_before_wait=")+(early_submit?"true":"false"));'
new2 = old2 + 'args.push_back(std::string("--ufc_hoist_shared_memory_uploads=")+(hoist_uploads?"true":"false"));'
assert old1 in t and old2 in t
t = t.replace(old1, new1, 1).replace(old2, new2, 1)
open(p, "w", encoding="utf-8").write(t)
PY

cd "$P14"
python3 build.py frontdoor win-reborn win-original linux-reborn linux-original

mkdir -p "$TESTE" "$P14/_to_delete"
if [ -e "$TESTE/Windows" ]; then mv "$TESTE/Windows" "$P14/_to_delete/BETA12-Windows-$(date +%s)"; fi
if [ -e "$TESTE/Linux" ]; then mv "$TESTE/Linux" "$P14/_to_delete/BETA12-Linux-$(date +%s)"; fi
if [ -e "$TESTE/build" ]; then mv "$TESTE/build" "$P14/_to_delete/BETA12-build-$(date +%s)"; fi

cp -a "$P14/build" "$TESTE/build"

cp "$P14/src/ufc_session.h" "$W/backup-originais/ufc_session.h.beta12"
cp "$P14/src/ufc_build_identity.h" "$W/backup-originais/ufc_build_identity.h.beta12"
# Restore launcher-p14-28-10 ufc_session.h and build/ from backup so BETA 1.1 tree is untouched
cp "$A1/backup-originais/ufc_session.h.bak" "$P14/src/ufc_session.h"
cp "$W/backup-originais/ufc_build_identity.h.beta11" "$P14/src/ufc_build_identity.h"
rm -rf "$P14/build"
cp -a "$A1/backup-originais/build-beta11-bak" "$P14/build"

B="$TESTE/build"

echo "=== 3. Assemble BETA-1.2/Windows ==="
WIN="$TESTE/Windows"
mkdir -p "$WIN"
python3 -c "import zipfile,sys;zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$P13ZIP" "$WIN"
cp "$B/frontdoor/UFC3.exe" "$WIN/UFC3.exe"
cp "$B/frontdoor/UFC3.exe" "$WIN/Launcher-Config.exe"
cp "$B/win-reborn/ufc3-launcher-l1.exe" "$B/win-original/ufc3-original-v48.exe" "$WIN/data/"
cp "$P14/art/header-bg.png" "$P14/art/header-logo.png" "$WIN/data/ui/"
cp "$W/out-win/rexgpu-xenos.dll" "$WIN/data/rexgpu-xenos.dll"
cp "$W/THIRD_PARTY_NOTICES.md" "$WIN/data/THIRD_PARTY_NOTICES.md"
mkdir -p "$WIN/data/shaders/windows"
cp "$P14/seed/windows/"* "$WIN/data/shaders/windows/"
cp "$RT/all-win/librexruntime.dll" "$WIN/data/librexruntime.dll"
[ "$(ls "$WIN" | tr '\n' ' ')" = "Launcher-Config.exe UFC3.exe data " ] || { echo "raiz errada: $(ls "$WIN")"; exit 1; }

rm -f "$TESTE/UFC3MONS-BETA1.2-Windows.zip"
python3 - "$WIN" "$TESTE/UFC3MONS-BETA1.2-Windows.zip" <<'PY'
import os,sys,zipfile
root,dst=sys.argv[1],sys.argv[2]
with zipfile.ZipFile(dst,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for d,_,fs in os.walk(root):
        for f in sorted(fs):
            p=os.path.join(d,f); z.write(p,os.path.relpath(p,root).replace(os.sep,"/"))
PY

echo "=== 4. Assemble BETA-1.2/Linux ==="
LIN="$TESTE/Linux/UFC3MONS.AppDir"
mkdir -p "$TESTE/Linux"
cp -a "$P12APP" "$LIN"
mkdir -p "$P14/_to_delete/appdir-leia-me"
mv "$LIN"/LEIA-ME-*.md "$P14/_to_delete/appdir-leia-me/" 2>/dev/null || true
cp "$B/frontdoor/UFC3" "$LIN/usr/bin/UFC3"
cp "$B/linux-reborn/ufc3-launcher-l1" "$B/linux-original/ufc3-original-v48" "$LIN/usr/bin/data/"
cp "$RT/all-linux/librexruntime.so" "$LIN/usr/bin/data/librexruntime.so"
cp "$P14/art/header-bg.png" "$P14/art/header-logo.png" "$LIN/usr/bin/data/ui/"
cp "$W/out-linux/librexgpu-xenos.so" "$LIN/usr/bin/data/librexgpu-xenos.so"
cp "$W/THIRD_PARTY_NOTICES.md" "$LIN/usr/bin/data/THIRD_PARTY_NOTICES.md"
mkdir -p "$LIN/usr/bin/data/shaders/linux"
cp "$P14/seed/linux/"* "$LIN/usr/bin/data/shaders/linux/"
chmod +x "$LIN/usr/bin/UFC3" "$LIN/usr/bin/data/ufc3-launcher-l1" "$LIN/usr/bin/data/ufc3-original-v48" "$LIN/AppRun"

rm -f "$TESTE/UFC3MONS-BETA1.2-Linux-x86_64.AppImage"
ARCH=x86_64 /root/ht/pkg/appimagetool --appimage-extract-and-run "$LIN" "$TESTE/UFC3MONS-BETA1.2-Linux-x86_64.AppImage" > "$TESTE/Linux/pack.log" 2>&1 || { tail -20 "$TESTE/Linux/pack.log"; exit 1; }

cd "$TESTE"
for f in "UFC3MONS-BETA1.2-Windows.zip" "UFC3MONS-BETA1.2-Linux-x86_64.AppImage"; do
    sha256sum "$f" | tee "$f.sha256"
done

echo "=== 5. Verify BETA 1.1 untouched ==="
sha256sum "$P14/entrega/UFC3MONS-BETA1.1-Windows.zip" "$P14/entrega/UFC3MONS-BETA1.1-Linux-x86_64.AppImage"
