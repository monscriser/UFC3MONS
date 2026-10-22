# UFC3MONS Windows GPU plugin (rexgpu-xenos.dll) from the exact R5/R7 inputs.
#   python3 build_gpu_win.py control -> recompile vcp_r4.cpp + v59 shared_memory.cpp unchanged, relink
#   python3 build_gpu_win.py build   -> same sources with the Linux R8.5 patches (RequestRange
#                                       shortcut + async submit); every other object is the R7 one.
from pathlib import Path
import json, subprocess, shlex, hashlib, shutil, sys
root = Path(__file__).resolve().parent
mode = sys.argv[1]
C = Path('/mnt/c/Users/Monscriser/Documents/Codex')
r4 = C / '2026-10-20/de/work/r4-gpu'
v59 = C / '2026-10-20/continue-o-ufc3mons-usando-a-skill/work/v59-gpu'
release = C / '2026-10-21/files-mentioned-by-the-user-texto/work/release-intscale/build.sh'
out = root / ('gpuwin-' + mode)
if out.exists():
    dest = root / '_to_delete' / f'gpuwin-{mode}-{len(list((root / "_to_delete").glob("*")))}'
    dest.parent.mkdir(exist_ok=True); shutil.move(str(out), str(dest))
(out / 'vcp').mkdir(parents=True); (out / 'sm').mkdir()

def line_with(path, needle):
    ls = [l for l in path.read_text().splitlines() if needle in l]
    assert len(ls) == 1, (path, needle, len(ls))
    return shlex.split(ls[0])

vcp_cmd = line_with(r4 / 'build.sh', '-c ' + str(r4 / 'vcp_r4.cpp'))
sm_cmd = line_with(v59 / 'build.sh', '-c ' + str(v59 / 'shared_memory.cpp'))
link = [a for a in shlex.split([l for l in release.read_text().splitlines() if '-shared' in l][0])]

if mode == 'build':
    shutil.copy2(root / 'gpu84-async' / 'gpu' / 'vulkan_command_processor.cpp', out / 'vcp' / 'vcp_r4.cpp')
    shutil.copy2(root / 'gpu84-async' / 'gpu' / 'shared_memory.cpp', out / 'sm' / 'shared_memory.cpp')
    # The Linux tree had only these sources patched; the inputs were the same vcp_r4.cpp / v59 file.
    assert (r4 / 'vcp_r4.cpp').read_text().replace('\r\n', '\n') == (root.parent / 'parity-full/gpu/vulkan_command_processor.cpp').read_text().replace('\r\n', '\n')
    assert (v59 / 'shared_memory.cpp').read_text().replace('\r\n', '\n') == (root.parent / 'parity-full/gpu/shared_memory.cpp').read_text().replace('\r\n', '\n')
else:
    shutil.copy2(r4 / 'vcp_r4.cpp', out / 'vcp' / 'vcp_r4.cpp')
    shutil.copy2(v59 / 'shared_memory.cpp', out / 'sm' / 'shared_memory.cpp')

def compile(cmd, src_dir, original_dir, name, include_dir):
    cmd = list(cmd)
    i = next(k for k, a in enumerate(cmd) if a.endswith('.cpp') and (a.startswith('/') or a.startswith('.')) and cmd[k - 1] == '-c')
    cmd[i] = str(src_dir / name)
    obj = out / (name + '.obj'); cmd[cmd.index('-o') + 1] = str(obj)
    cmd.insert(1, '-I' + str(include_dir))
    cmd.insert(1, '-ffile-prefix-map=' + str(src_dir) + '=' + str(original_dir))
    for flag in ('-MD', '-MMD'):
        while flag in cmd: cmd.remove(flag)
    for flag in ('-MF', '-MT', '-MQ'):
        while flag in cmd:
            k = cmd.index(flag); del cmd[k:k + 2]
    (out / (name + '.command.json')).write_text(json.dumps(cmd, indent=2))
    with (out / (name + '.log')).open('w') as f:
        subprocess.run(cmd, cwd='/mnt/e/UFC3-RexGlue-Lab/build/sdk-clean-win', stdout=f, stderr=subprocess.STDOUT, check=True)
    print(name, 'OK', flush=True)
    return obj

vcp_obj = compile(vcp_cmd, out / 'vcp', r4, 'vcp_r4.cpp', r4)
sm_obj = compile(sm_cmd, out / 'sm', v59, 'shared_memory.cpp', v59)
old_vcp = str(r4 / 'vcp_r4.obj'); old_sm = str(v59 / 'shared_memory.cpp.obj')
assert link.count(old_vcp) == 1 and link.count(old_sm) == 1
link[link.index(old_vcp)] = str(vcp_obj); link[link.index(old_sm)] = str(sm_obj)
dll = out / 'rexgpu-xenos.dll'; link[link.index('-o') + 1] = str(dll)
(out / 'link.command.json').write_text(json.dumps(link, indent=2))
with (out / 'link.log').open('w') as f:
    subprocess.run(link, cwd='/mnt/e/UFC3-RexGlue-Lab/build/sdk-clean-win', stdout=f, stderr=subprocess.STDOUT, check=True)
r7 = C / '2026-10-21/files-mentioned-by-the-user-texto/work/release-intscale/rexgpu-xenos.dll'
a, b = r7.read_bytes(), dll.read_bytes()
diff = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
print(json.dumps({'mode': mode, 'sha256': hashlib.sha256(b).hexdigest(), 'bytes_diff_vs_r7': diff, 'size': len(b), 'r7_size': len(a)}))
