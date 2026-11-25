#!/usr/bin/env bash
# UFC3MONS 1.9 packages: Windows zip and Linux AppImage from the BETA 1.6 bases (same layout as BETA 1.8), with the 1.9 PGO
# executables, the 1.9 front door and, on Linux, the readback-index fix in librexgpu-xenos.so (deck-readback-pending).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
build="$here/build-tree/build"; kit=/mnt/c/UFC3MONS-Kit/saida
out="$here/../publicacao-1.9/pacotes"; mkdir -p "$out"
zip="$out/UFC3MONS-1.9-PGO-Windows.zip"; app="$out/UFC3MONS-1.9-PGO-Linux-x86_64.AppImage"
plugin="$here/deck-readback-pending/librexgpu-xenos.so"
rm -f "$zip" "$zip.sha256" "$app" "$app.sha256"
python3 - "$build" "$kit/UFC3MONS-BETA1.6-Tecnicas-Windows.zip" "$zip" <<'PY'
import hashlib, shutil, sys
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
build, base, package = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
rep = {"UFC3.exe": build / "frontdoor/UFC3.exe", "Launcher-Config.exe": build / "frontdoor/UFC3.exe",
       "data/ufc3-launcher-l1.exe": build / "win-reborn-1.9-pgo/ufc3-launcher-l1.exe",
       "data/ufc3-original-v48.exe": build / "win-original-1.9-pgo/ufc3-original-v48.exe"}
tmp = package.with_suffix(".tmp"); seen = set()
with ZipFile(base) as src, ZipFile(tmp, "w", ZIP_DEFLATED) as dst:
    for item in src.infolist():
        if item.filename in rep: dst.write(rep[item.filename], item.filename); seen.add(item.filename)
        else:
            with src.open(item) as a, dst.open(item, "w") as b: shutil.copyfileobj(a, b)
assert seen == set(rep), seen
tmp.replace(package)
digest = hashlib.sha256(package.read_bytes()).hexdigest()
package.with_suffix(".zip.sha256").write_text(f"{digest}  {package.name}\n")
print(digest, package.name)
PY
base="$kit/UFC3MONS-BETA1.6-Memexport-Candidata-Linux-x86_64.AppImage"; offset=944632
tmp=$(mktemp -d); trap 'rm -rf -- "$tmp"' EXIT
unsquashfs -no-progress -o "$offset" -d "$tmp/AppDir" "$base" >/dev/null
cp -- "$build/frontdoor/UFC3" "$tmp/AppDir/usr/bin/UFC3"
cp -- "$build/linux-reborn-1.9-pgo/ufc3-launcher-l1" "$tmp/AppDir/usr/bin/data/ufc3-launcher-l1"
cp -- "$build/linux-original-1.9-pgo/ufc3-original-v48" "$tmp/AppDir/usr/bin/data/ufc3-original-v48"
cp -- "$plugin" "$tmp/AppDir/usr/bin/data/librexgpu-xenos.so"
python3 - "$tmp/AppDir/ufc3mons.desktop" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1]); s = p.read_text(encoding="utf-8")
old = "Name=UFC3MONS BETA 1.6 Memexport (Candidata)\n"
if s.count(old) != 1: raise SystemExit("Unexpected AppImage desktop name")
p.write_text(s.replace(old, "Name=UFC3MONS 1.9\n", 1), encoding="utf-8")
PY
chmod +x "$tmp/AppDir/usr/bin/UFC3" "$tmp/AppDir/usr/bin/data/ufc3-launcher-l1" "$tmp/AppDir/usr/bin/data/ufc3-original-v48"
mksquashfs "$tmp/AppDir" "$tmp/package.squashfs" -noappend -all-root -comp zstd -processors 2 -no-progress >/dev/null
head -c "$offset" "$base" > "$tmp/candidate.AppImage"; cat "$tmp/package.squashfs" >> "$tmp/candidate.AppImage"
chmod +x "$tmp/candidate.AppImage"; mv -- "$tmp/candidate.AppImage" "$app"
for f in usr/bin/data/librexgpu-xenos.so usr/bin/data/ufc3-launcher-l1 usr/bin/data/ufc3-original-v48 usr/bin/UFC3; do
  printf '%s  %s\n' "$(unsquashfs -cat -o "$offset" "$app" "$f" | sha256sum | cut -d' ' -f1)" "$f"
done
(cd -- "$out" && sha256sum -- "$(basename -- "$app")") > "$app.sha256"
cat "$app.sha256"
