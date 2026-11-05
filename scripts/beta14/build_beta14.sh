# UFC3MONS BETA 1.4 = BETA 1.3 + Linux GPU library with the PROCMAP_QUERY fix (Original-edition freezes on Linux).
# Builds frontdoor + 4 game variants (only the build name changes), assembles Windows and Linux packages in BETA-1.4.
# launcher-p14-28-10 is restored exactly as it was at the end.
set -euo pipefail
E="/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho"
P14="$E/launcher-p14-28-10"; GB="$E/gpu-port-blood"; B13="$E/BETA-1.3"; D="$E/deck-stalls-05-11"
OUT="$E/BETA-1.4"; BK="$D/backup-p14-beta14"
ID13="UFC3MONS-BETA1.3-2026-11-04"; ID="UFC3MONS-BETA1.4-2026-11-05"
[ -e "$OUT" ] && { echo "BETA-1.4 já existe"; exit 1; }
mkdir -p "$OUT" "$BK"
cp "$P14/src/ufc_session.h" "$BK/ufc_session.h"; cp "$P14/src/ufc_build_identity.h" "$BK/ufc_build_identity.h"
rm -rf "$BK/build"; mv "$P14/build" "$BK/build"
restore() {
  cp "$BK/ufc_session.h" "$P14/src/ufc_session.h"; cp "$BK/ufc_build_identity.h" "$P14/src/ufc_build_identity.h"
  rm -rf "$P14/build"; mv "$BK/build" "$P14/build"
}
trap restore EXIT
cp -a "$B13/build" "$P14/build"
cp "$GB/backup-originais/ufc_session.h.beta13" "$P14/src/ufc_session.h"
cp "$GB/backup-originais/ufc_build_identity.h.beta13" "$P14/src/ufc_build_identity.h"
sed -i "s/$ID13/$ID/" "$P14/src/ufc_build_identity.h"
grep -q "$ID" "$P14/src/ufc_build_identity.h"
cp "$P14/src/ufc_build_identity.h" "$OUT/.ufc_build_identity.h.beta14"
echo "=== build.py frontdoor + 4 variants ==="
(cd "$P14" && python3 build.py frontdoor win-reborn win-original linux-reborn linux-original > "$D/beta14-build.log" 2>&1) || { tail -40 "$D/beta14-build.log"; exit 1; }
cp -a "$P14/build" "$OUT/build"
B="$OUT/build"
for f in frontdoor/UFC3 frontdoor/UFC3.exe win-reborn/ufc3-launcher-l1.exe win-original/ufc3-original-v48.exe linux-reborn/ufc3-launcher-l1 linux-original/ufc3-original-v48; do
  [ "$(grep -c "$ID" "$B/$f")" -ge 1 ] || { echo "sem nome BETA1.4: $f"; exit 1; }
done
echo "=== Windows ==="
WIN="$OUT/Windows"; cp -a "$B13/Windows" "$WIN"
cp "$B/frontdoor/UFC3.exe" "$WIN/UFC3.exe"; cp "$B/frontdoor/UFC3.exe" "$WIN/Launcher-Config.exe"
cp "$B/win-reborn/ufc3-launcher-l1.exe" "$B/win-original/ufc3-original-v48.exe" "$WIN/data/"
[ "$(ls "$WIN" | tr '\n' ' ')" = "Launcher-Config.exe UFC3.exe data " ] || { echo "raiz errada: $(ls "$WIN")"; exit 1; }
python3 - "$WIN" "$OUT/UFC3MONS-BETA1.4-Windows.zip" <<'PY'
import os,sys,zipfile
root,dst=sys.argv[1],sys.argv[2]
with zipfile.ZipFile(dst,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for d,_,fs in os.walk(root):
        for f in sorted(fs):
            p=os.path.join(d,f); z.write(p,os.path.relpath(p,root).replace(os.sep,"/"))
PY
echo "=== Linux ==="
LIN="$OUT/Linux/UFC3MONS.AppDir"; mkdir -p "$OUT/Linux"; cp -a "$B13/Linux/UFC3MONS.AppDir" "$LIN"
cp "$B/frontdoor/UFC3" "$LIN/usr/bin/UFC3"
cp "$B/linux-reborn/ufc3-launcher-l1" "$B/linux-original/ufc3-original-v48" "$LIN/usr/bin/data/"
cp "$D/out-gpu-fix/librexgpu-xenos.so" "$LIN/usr/bin/data/librexgpu-xenos.so"
chmod +x "$LIN/usr/bin/UFC3" "$LIN/usr/bin/data/ufc3-launcher-l1" "$LIN/usr/bin/data/ufc3-original-v48" "$LIN/AppRun"
ARCH=x86_64 /root/ht/pkg/appimagetool --appimage-extract-and-run "$LIN" "$OUT/UFC3MONS-BETA1.4-Linux-x86_64.AppImage" > "$OUT/Linux/pack.log" 2>&1 || { tail -20 "$OUT/Linux/pack.log"; exit 1; }
cd "$OUT"
for f in UFC3MONS-BETA1.4-Windows.zip UFC3MONS-BETA1.4-Linux-x86_64.AppImage; do sha256sum "$f" | tee "$f.sha256"; done
echo "=== BETA 1.3 intacta ==="
cat "$B13/UFC3MONS-BETA1.3-Windows.zip.sha256" "$B13/UFC3MONS-BETA1.3-Linux-x86_64.AppImage.sha256"
(cd "$B13" && sha256sum UFC3MONS-BETA1.3-Windows.zip UFC3MONS-BETA1.3-Linux-x86_64.AppImage)
