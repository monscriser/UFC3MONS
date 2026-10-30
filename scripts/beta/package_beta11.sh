#!/bin/bash
# BETA1.1 (direct texture loads, from the Hydro port) + BETA1.0 (direct resolve on by default) + P14.11 (texture filtering option) + P14.10 (pre-compile hidden + progress) + P14.9.1 (UFC3 front door rebuilt) + P14.9 (Mods tab) + P14.8 (shader list for the optional pre-compile + normal speed below 60 FPS). P14 packages: Windows zip = P13 zip with the new launcher/game exes and header art; Linux AppImage = P12 AppDir
# with the new exes, the P13 memory fix in librexruntime.so and the header art. Old LEIA-ME files go to _to_delete.
set -euo pipefail
P14="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-p14-28-10"
P13ZIP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/ranged-fallback-p13-27-10/release/entrega/UFC3MONS-P13-MEMORIA-ENTRADA-Windows.zip"
P12APP="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/shirt-variety-p12-27-10/release/package-linux/UFC3MONS.AppDir"
OUT="$P14/entrega"; B="$P14/build"; NAME=UFC3MONS-BETA1.1
mkdir -p "$OUT" "$P14/_to_delete"
for d in "$P14/package-windows" "$P14/package-linux"; do [ -e "$d" ] && mv "$d" "$P14/_to_delete/$(basename "$d")-$(date +%s)"; done
# Windows
W="$P14/package-windows/Windows"; mkdir -p "$W"
python3 -c "import zipfile,sys;zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$P13ZIP" "$W"
cp "$B/frontdoor/UFC3.exe" "$W/UFC3.exe"; cp "$B/frontdoor/UFC3.exe" "$W/Launcher-Config.exe"
cp "$B/win-reborn/ufc3-launcher-l1.exe" "$B/win-original/ufc3-original-v48.exe" "$W/data/"
cp "$P14/art/header-bg.png" "$P14/art/header-logo.png" "$W/data/ui/"
cp "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11/out-win/rexgpu-xenos.dll" "$W/data/rexgpu-xenos.dll"
mkdir -p "$W/data/shaders/windows"; cp "$P14/seed/windows/"* "$W/data/shaders/windows/"
cp /mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/fault-measure/win-fix/librexruntime.dll "$W/data/librexruntime.dll"
[ "$(ls "$W" | tr '\n' ' ')" = "Launcher-Config.exe UFC3.exe data " ] || { echo "raiz errada: $(ls "$W")"; exit 1; }
rm -f "$OUT/$NAME-Windows.zip"
python3 - "$W" "$OUT/$NAME-Windows.zip" <<'PY'
import os,sys,zipfile
root,dst=sys.argv[1],sys.argv[2]
with zipfile.ZipFile(dst,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for d,_,fs in os.walk(root):
        for f in sorted(fs):
            p=os.path.join(d,f); z.write(p,os.path.relpath(p,root).replace(os.sep,"/"))
PY
# Linux
L="$P14/package-linux/UFC3MONS.AppDir"; mkdir -p "$P14/package-linux"; cp -a "$P12APP" "$L"
mkdir -p "$P14/_to_delete/appdir-leia-me"; mv "$L"/LEIA-ME-*.md "$P14/_to_delete/appdir-leia-me/" 2>/dev/null || true
cp "$B/frontdoor/UFC3" "$L/usr/bin/UFC3"
cp "$B/linux-reborn/ufc3-launcher-l1" "$B/linux-original/ufc3-original-v48" "$L/usr/bin/data/"
cp /mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/fault-measure/linux-fix/librexruntime.so "$L/usr/bin/data/librexruntime.so"
cp "$P14/art/header-bg.png" "$P14/art/header-logo.png" "$L/usr/bin/data/ui/"
cp "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11/out-linux/librexgpu-xenos.so" "$L/usr/bin/data/librexgpu-xenos.so"
mkdir -p "$L/usr/bin/data/shaders/linux"; cp "$P14/seed/linux/"* "$L/usr/bin/data/shaders/linux/"
chmod +x "$L/usr/bin/UFC3" "$L/usr/bin/data/ufc3-launcher-l1" "$L/usr/bin/data/ufc3-original-v48" "$L/AppRun"
rm -f "$OUT/$NAME-Linux-x86_64.AppImage"
ARCH=x86_64 /root/ht/pkg/appimagetool --appimage-extract-and-run "$L" "$OUT/$NAME-Linux-x86_64.AppImage" > "$P14/package-linux/pack.log" 2>&1 || { tail -20 "$P14/package-linux/pack.log"; exit 1; }
cd "$OUT"; for f in "$NAME-Windows.zip" "$NAME-Linux-x86_64.AppImage"; do sha256sum "$f" | tee "$f.sha256"; done
