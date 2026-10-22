# UFC3MONS R9 packages (Windows zip + Linux AppImage) from the R8.1 packages, swapping only:
#   Windows: data/ufc3-launcher-l1.exe, data/ufc3-original-v48.exe (build9final), data/rexgpu-xenos.dll (gpuwin-build)
#   Linux:   usr/bin/data/{ufc3-launcher-l1, ufc3-original-v48} (build9final), librexgpu-xenos.so (gpu84-async),
#            librexruntime.so (rt84-build)
# Never overwrites existing outputs.
from pathlib import Path
import json, subprocess, hashlib, shutil, os, zipfile
root = Path(__file__).resolve().parent
out = root.parents[1] / 'outputs'
def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def tree(d): return {str(p.relative_to(d)): sha(p) for p in sorted(Path(d).rglob('*')) if p.is_file()}
pk = root / 'packages9'; pk.mkdir(exist_ok=True)
b = root / 'build9final'
arts = []

# Linux
base = root / 'packages81' / 'UFC3MONS-R8.1.AppDir'
app = pk / 'UFC3MONS-R9.AppDir'; assert not app.exists()
shutil.copytree(base, app, symlinks=True)
data = app / 'usr/bin/data'
for edition, binary in [('original', 'ufc3-original-v48'), ('reborn', 'ufc3-launcher-l1')]:
    shutil.copy2(b / 'linux' / edition / binary, data / binary); (data / binary).chmod(0o755)
shutil.copy2(root / 'gpu84-async' / 'librexgpu-xenos.so', data / 'librexgpu-xenos.so')
shutil.copy2(root / 'rt84-build' / 'librexruntime.so', data / 'librexruntime.so')
shutil.copy2(root / 'LEIA-ME-R9.md', app / 'LEIA-ME-R9.md')
env = dict(os.environ, LD_LIBRARY_PATH=str(data))
for x in ['data/ufc3-original-v48', 'data/ufc3-launcher-l1', 'data/librexgpu-xenos.so', 'data/librexruntime.so', 'UFC3']:
    r = subprocess.run(['ldd', '-r', str(app / 'usr/bin' / x)], env=env, capture_output=True, text=True)
    assert r.returncode == 0 and 'not found' not in r.stdout + r.stderr and 'undefined symbol' not in r.stdout + r.stderr, x
m_new, m_old = tree(app), tree(base)
changed = sorted(k for k in set(m_new) | set(m_old) if m_new.get(k) != m_old.get(k))
assert set(changed) == {'usr/bin/data/ufc3-original-v48', 'usr/bin/data/ufc3-launcher-l1', 'usr/bin/data/librexgpu-xenos.so',
                        'usr/bin/data/librexruntime.so', 'LEIA-ME-R9.md'}, changed
image = pk / 'UFC3MONS-Candidata-R9-Linux-x86_64.AppImage'; assert not image.exists()
with (root / 'appimage9-package.log').open('w') as f:
    subprocess.run(['/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/usan/work/appimagetool-x86_64.AppImage', '--no-appstream',
                    '--runtime-file', '/mnt/c/Users/Monscriser/Documents/Codex/2026-10-13/v/work/runtime-x86_64', str(app), str(image)],
                   env=dict(os.environ, ARCH='x86_64', APPIMAGE_EXTRACT_AND_RUN='1'), stdout=f, stderr=subprocess.STDOUT, check=True)
image.chmod(0o755)
ver = pk / 'verification'; ver.mkdir()
with (root / 'appimage9-extract.log').open('w') as f:
    subprocess.run([str(image), '--appimage-extract'], cwd=ver, stdout=f, stderr=subprocess.STDOUT, check=True)
for name, expected in m_new.items(): assert sha(ver / 'squashfs-root' / name) == expected, name

# Windows
wbase = root / 'packages81' / 'UFC3MONS-Candidata-R8.1-Correcoes-Windows'
win = pk / 'UFC3MONS-Candidata-R9-Windows'; assert not win.exists()
shutil.copytree(wbase, win)
for edition, binary in [('original', 'ufc3-original-v48.exe'), ('reborn', 'ufc3-launcher-l1.exe')]:
    shutil.copy2(b / 'windows' / edition / binary, win / 'data' / binary)
shutil.copy2(root / 'gpuwin-build' / 'rexgpu-xenos.dll', win / 'data' / 'rexgpu-xenos.dll')
shutil.copy2(root / 'LEIA-ME-R9.md', win / 'LEIA-ME-R9.md')
w_new, w_old = tree(win), tree(wbase)
wchanged = sorted(k for k in set(w_new) | set(w_old) if w_new.get(k) != w_old.get(k))
assert set(wchanged) == {'data/ufc3-original-v48.exe', 'data/ufc3-launcher-l1.exe', 'data/rexgpu-xenos.dll', 'LEIA-ME-R9.md'}, wchanged
zip_path = pk / 'UFC3MONS-Candidata-R9-Windows.zip'
with zipfile.ZipFile(zip_path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as a:
    for p in sorted(win.rglob('*')):
        if p.is_file(): a.write(p, str(p.relative_to(win)))
with zipfile.ZipFile(zip_path) as a: assert a.testzip() is None

for src in [image, zip_path]:
    dst = out / src.name; assert not dst.exists(), dst
    shutil.copy2(src, dst); assert sha(dst) == sha(src)
    (out / (src.name + '.sha256')).write_text(sha(dst) + '  ' + src.name + '\n')
    arts.append({'file': src.name, 'sha256': sha(dst), 'bytes': dst.stat().st_size})
folder = out / win.name; assert not folder.exists(); shutil.copytree(win, folder)
shutil.copy2(root / 'LEIA-ME-R9.md', out / 'UFC3MONS-R9-LEIA-ME.md')
report = {'artifacts': arts, 'linux_changed_vs_r81': changed, 'windows_changed_vs_r81': wchanged,
          'apps': json.loads((root / 'build9final-results.json').read_text()),
          'linux_plugin_sha256': sha(root / 'gpu84-async' / 'librexgpu-xenos.so'),
          'linux_runtime_sha256': sha(root / 'rt84-build' / 'librexruntime.so'),
          'windows_plugin_sha256': sha(root / 'gpuwin-build' / 'rexgpu-xenos.dll'),
          'windows_gameplay_tested': False, 'linux_r85_ally_tested': True, 'original_opponent_fixed': False}
(out / 'UFC3MONS-R9-VERIFICACAO.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(arts, indent=2))
