# UFC3MONS Linux runtime (librexruntime.so) relink from the existing sdk-linux objects.
#   python3 build_runtime84.py control -> unchanged objects, compare with the R7 runtime
#   python3 build_runtime84.py build   -> + WaitMultiple sleep fix in threading_posix.cpp
# The SDK build directory and sources are only read; outputs go to work/r8-input-lock/rt84-<mode>.
#
# Fix: PosixConditionBase::WaitMultiple polls with "sleep min(remaining, 1 ms)", but remaining is
# truncated to whole milliseconds. Alertable waits are sliced into 1 ms pieces, so every slice has
# remaining < 1 ms -> sleep_for(0) -> the thread spins the whole slice (AK::IOThread at 100% of a
# core on the Ally). Now the sleep keeps sub-millisecond precision (same 1 ms cap, same deadline).
from pathlib import Path
import subprocess, shlex, hashlib, json, shutil, sys
root = Path(__file__).resolve().parent
mode = sys.argv[1]
B = Path('/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux')
SRC = Path('/mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean')
out = root / ('rt84-' + mode)
if out.exists():
    dest = root / '_to_delete' / f'rt84-{mode}-{len(list((root / "_to_delete").glob("*")))}'
    dest.parent.mkdir(exist_ok=True); shutil.move(str(out), str(dest))
out.mkdir()

def ninja_cmd(target):
    text = subprocess.run(['ninja', '-C', str(B), '-t', 'commands', target], capture_output=True, text=True, check=True).stdout
    line = [l for l in text.splitlines() if l.strip()][-1]
    if line.startswith(': && '): line = line[5:]
    if ' && :' in line: line = line.split(' && :')[0]
    return shlex.split(line)

link = ninja_cmd('librexruntime.so')
link[link.index('-o') + 1] = str(out / 'librexruntime.so')

if mode == 'build':
    obj_rel = 'src/core/CMakeFiles/rexcore.dir/threading_posix.cpp.o'
    comp = ninja_cmd(obj_rel)
    src = out / 'threading_posix.cpp'
    t = (SRC / 'src/core/threading_posix.cpp').read_text()
    old = '''      if (timeout == std::chrono::milliseconds::max()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - now);
        auto sleep_time = std::min(remaining, std::chrono::milliseconds(1));
        std::this_thread::sleep_for(sleep_time);
      }'''
    new = '''      if (timeout == std::chrono::milliseconds::max()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else {
        // UFC3MONS: keep sub-millisecond precision. Truncating to whole milliseconds made
        // every 1 ms alertable slice sleep 0 and spin (a full core on Linux).
        auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - now);
        auto sleep_time = std::min<std::chrono::nanoseconds>(remaining, std::chrono::milliseconds(1));
        std::this_thread::sleep_for(sleep_time);
      }'''
    assert t.count(old) == 1
    src.write_text(t.replace(old, new))
    # Same embedded paths and include resolution as the original object.
    comp[comp.index('-c') + 1] = str(src)
    obj = out / 'threading_posix.cpp.o'
    comp[comp.index('-o') + 1] = str(obj)
    comp.insert(1, '-ffile-prefix-map=' + str(out) + '=' + str(SRC / 'src/core'))
    comp.insert(1, '-I' + str(SRC / 'src/core'))
    if '-MF' in comp:
        i = comp.index('-MF'); del comp[i:i + 2]
    if '-MD' in comp: comp.remove('-MD')
    if '-MT' in comp:
        i = comp.index('-MT'); del comp[i:i + 2]
    (out / 'compile.command.json').write_text(json.dumps(comp, indent=2))
    with (out / 'compile.log').open('w') as f:
        subprocess.run(comp, cwd=B, stdout=f, stderr=subprocess.STDOUT, check=True)
    assert link.count(obj_rel) == 1
    link[link.index(obj_rel)] = str(obj)

(out / 'link.command.json').write_text(json.dumps(link, indent=2))
with (out / 'link.log').open('w') as f:
    subprocess.run(link, cwd=B, stdout=f, stderr=subprocess.STDOUT, check=True)
# cmake install rewrites the build-tree RUNPATH placeholder in place; do the same.
so = out / 'librexruntime.so'
data = so.read_bytes()
placeholder = b'\x00' + b':' * 22 + b'\x00'
installed = b'\x00' + b'$ORIGIN:$ORIGIN/../lib' + b'\x00'
assert len(placeholder) == len(installed) and data.count(placeholder) == 1, data.count(placeholder)
so.write_bytes(data.replace(placeholder, installed))
digest = hashlib.sha256(so.read_bytes()).hexdigest()
r7 = '33bb9f22c0658bd3a0d94afe8c5aafbdacde2b43a451bf51f30fcc15834c8218'
print(json.dumps({'mode': mode, 'sha256': digest, 'matches_r7': digest == r7}))
(out / 'result.json').write_text(json.dumps({'mode': mode, 'sha256': digest, 'r7_sha256': r7}, indent=2))
