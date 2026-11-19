#!/usr/bin/env bash
# Linux Reborn chunk 229 with the issue #24 fix, using the archived linux_pre recipe.
# "control" rebuilds the BETA 1.5 fix source and must match beta15-fix-switch/linux/fix.o.
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
history='/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/beta15-fix-switch'
pre='/mnt/e/PROJETOS ANTIGOS/Codex-22-10/2026-10-20/de/work/linux-update/pre'
python3 - "$pre" "$history/ufc3_recomp.229.fix.cpp" "$here/ufc3_recomp.229.fix24.cpp" "$here/linux" <<'PY'
import subprocess, sys
pre, control_src, fix_src, out = sys.argv[1:5]
sys.path.insert(0, pre)
import linux_pre as lp
e = [p for p in lp.plans('reborn')[0] if p['label'] == 'reborn-ufc3_recomp.229'][0]
old = '/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20'
new = '/mnt/e/PROJETOS ANTIGOS/Codex-22-10/2026-10-20'
argv = [a.replace(old, new) for a in e['argv']]
src, obj = e['source'].replace(old, new), e['output'].replace(old, new)
print(" ".join(argv)[:600])
for s, o in ((control_src, out + "/control.o"), (fix_src, out + "/fix24.o")):
    a = [s if x == src else (o if x == obj else x) for x in argv]
    subprocess.run(a, check=True, cwd="/")
PY
sha256sum "$history/linux/fix.o" "$here/linux/control.o" "$here/linux/fix24.o"
cmp "$history/linux/fix.o" "$here/linux/control.o" && echo CONTROL-MATCH
