#!/usr/bin/env bash
set -euo pipefail

kit=/mnt/c/UFC3MONS-Kit
build=/mnt/c/Users/Monscriser/Documents/Codex/2026-11-09/o-zip-inclui-o-c-digo/work/technique-cheat/build-tree/build
out=${1:-/mnt/c/Users/Monscriser/Documents/Codex/2026-11-09/o-zip-inclui-o-c-digo/outputs/UFC3MONS-BETA1.7-Linux-x86_64.AppImage}
base="$kit/saida/UFC3MONS-BETA1.6-Memexport-Candidata-Linux-x86_64.AppImage"
offset=944632

for tool in unsquashfs mksquashfs sha256sum; do
  command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
[[ ! -e "$out" ]] || { echo "Output already exists: $out" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
unsquashfs -no-progress -o "$offset" -d "$tmp/AppDir" "$base" >/dev/null
cp -- "$build/frontdoor/UFC3" "$tmp/AppDir/usr/bin/UFC3"
cp -- "$build/linux-reborn/ufc3-launcher-l1" "$tmp/AppDir/usr/bin/data/ufc3-launcher-l1"
cp -- "$build/linux-original/ufc3-original-v48" "$tmp/AppDir/usr/bin/data/ufc3-original-v48"
python3 - "$tmp/AppDir/ufc3mons.desktop" <<'PY'
import sys
from pathlib import Path

p = Path(sys.argv[1])
s = p.read_text(encoding="utf-8")
old = "Name=UFC3MONS BETA 1.6 Memexport (Candidata)\n"
new = "Name=UFC3MONS BETA 1.6\n"
if s.count(old) != 1:
    raise SystemExit("Unexpected AppImage desktop name")
p.write_text(s.replace(old, new, 1), encoding="utf-8")
PY
chmod +x "$tmp/AppDir/usr/bin/UFC3" "$tmp/AppDir/usr/bin/data/ufc3-launcher-l1" "$tmp/AppDir/usr/bin/data/ufc3-original-v48"
mksquashfs "$tmp/AppDir" "$tmp/package.squashfs" -noappend -all-root -comp zstd -processors 2 -no-progress >/dev/null
head -c "$offset" "$base" > "$tmp/candidate.AppImage"
cat "$tmp/package.squashfs" >> "$tmp/candidate.AppImage"
chmod +x "$tmp/candidate.AppImage"
mv -- "$tmp/candidate.AppImage" "$out"

plugin=$(sha256sum "$kit/saida/linux/librexgpu-xenos.so" | cut -d' ' -f1)
embedded=$(unsquashfs -cat -o "$offset" "$out" usr/bin/data/librexgpu-xenos.so | sha256sum | cut -d' ' -f1)
[[ "$plugin" == "$embedded" ]] || { echo "Embedded plugin mismatch" >&2; exit 1; }
(cd -- "$(dirname -- "$out")" && sha256sum -- "$(basename -- "$out")") > "$out.sha256"
printf 'AppImage: %s\nPlugin SHA-256: %s\n' "$out" "$plugin"
