# UFC3MONS R8 packages: R7 packages with the two R8 game executables swapped in.
# Never overwrites: refuses if a destination exists.
from pathlib import Path
import json, subprocess, hashlib, shutil, os, zipfile
root = Path(__file__).resolve().parent
r7 = root.parent / 'parity-full' / 'packages'
out = root.parents[1] / 'outputs'
build = root / 'build'
def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
pk = root / 'packages'; pk.mkdir(exist_ok=True)

# Linux
app = pk / 'UFC3MONS-R8.AppDir'; assert not app.exists()
shutil.copytree(r7 / 'UFC3MONS-R7-Paridade.AppDir', app, symlinks=True)
data = app / 'usr/bin/data'
for edition, binary in [('original', 'ufc3-original-v48'), ('reborn', 'ufc3-launcher-l1')]:
    shutil.copy2(build / 'linux' / edition / binary, data / binary); (data / binary).chmod(0o755)
shutil.copy2(root / 'LEIA-ME-R8.md', app / 'LEIA-ME-R8.md')
env = dict(os.environ, LD_LIBRARY_PATH=str(data))
for b in ['data/ufc3-original-v48', 'data/ufc3-launcher-l1']:
    r = subprocess.run(['ldd', '-r', str(app / 'usr/bin' / b)], env=env, capture_output=True, text=True)
    assert r.returncode == 0 and 'not found' not in r.stdout + r.stderr and 'undefined symbol' not in r.stdout + r.stderr, b
manifest = {str(p.relative_to(app)): sha(p) for p in sorted(app.rglob('*')) if p.is_file()}
r7_manifest = json.loads((r7 / 'AppDir.sha256.json').read_text())
changed = sorted(k for k in set(manifest) | set(r7_manifest) if manifest.get(k) != r7_manifest.get(k))
assert set(changed) == {'usr/bin/data/ufc3-original-v48', 'usr/bin/data/ufc3-launcher-l1', 'LEIA-ME-R8.md'}, changed
(pk / 'AppDir.sha256.json').write_text(json.dumps(manifest, indent=2))
tool = '/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage'
rt = '/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/v/work/runtime-x86_64'
image = pk / 'UFC3MONS-Candidata-R8-Correcoes-Linux-x86_64.AppImage'; assert not image.exists()
with (root / 'appimage-package.log').open('w') as f:
    subprocess.run([tool, '--no-appstream', '--runtime-file', rt, str(app), str(image)],
                   env=dict(os.environ, ARCH='x86_64', APPIMAGE_EXTRACT_AND_RUN='1'), stdout=f, stderr=subprocess.STDOUT, check=True)
image.chmod(0o755)
ver = pk / 'verification'; ver.mkdir()
with (root / 'appimage-extract.log').open('w') as f:
    subprocess.run([str(image), '--appimage-extract'], cwd=ver, stdout=f, stderr=subprocess.STDOUT, check=True)
for name, expected in manifest.items(): assert sha(ver / 'squashfs-root' / name) == expected, name

# Windows
win = pk / 'UFC3MONS-Candidata-R8-Correcoes-Windows'; assert not win.exists()
shutil.copytree(r7 / 'UFC3MONS-R7-Paridade-Windows', win)
for edition, binary in [('original', 'ufc3-original-v48.exe'), ('reborn', 'ufc3-launcher-l1.exe')]:
    shutil.copy2(build / 'windows' / edition / binary, win / 'data' / binary)
shutil.copy2(root / 'LEIA-ME-R8.md', win / 'LEIA-ME-R8.md')
assert sha(win / 'data/rexgpu-xenos.dll') == '34dec9d2326ade3cd1e479e45b2869afb43d863926fba431365407c8d8319afb'
zip_path = pk / 'UFC3MONS-Candidata-R8-Correcoes-Windows.zip'
with zipfile.ZipFile(zip_path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as a:
    for p in sorted(win.rglob('*')):
        if p.is_file(): a.write(p, str(p.relative_to(win)))
with zipfile.ZipFile(zip_path) as a: assert a.testzip() is None

# Deliver
arts = []
for src in [image, zip_path]:
    dst = out / src.name; assert not dst.exists(), dst
    shutil.copy2(src, dst); assert sha(dst) == sha(src)
    (out / (src.name + '.sha256')).write_text(sha(dst) + '  ' + src.name + '\n')
    arts.append({'file': src.name, 'sha256': sha(dst), 'bytes': dst.stat().st_size})
folder = out / win.name; assert not folder.exists(); shutil.copytree(win, folder)
shutil.copy2(root / 'LEIA-ME-R8.md', out / 'UFC3MONS-R8-LEIA-ME.md')
report = {'artifacts': arts, 'linux_changed_vs_r7': changed,
          'builds': json.loads((root / 'build-results.json').read_text()),
          'control_relink': json.loads((root / 'control-results.json').read_text()),
          'r8_gameplay_tested': False, 'original_opponent_fixed': False}
(out / 'UFC3MONS-R8-VERIFICACAO.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(arts, indent=2))
