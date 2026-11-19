#!/usr/bin/env python3
"""PGO for the recompiled game code of the Windows builds (BETA 1.8).

build.py relinks prebuilt objects of the recompiled game (ufc3_recomp.N, ufc3_init, ufc3_register). For PGO they are
compiled again from their generated sources, with the exact recipe each object came from:
  - CMakeFiles/ufc3_recomp.dir/...   the build.ninja of the variant's build dir (ninja -t commands -s);
  - .../master-update/obj/*.obj      (win-reborn, Reborn 1.1 chunks) master-update/logs/reborn-<stem>.command.json;
  - launcher-ui-p6 ufc3_recomp.76.obj (win-reborn, footer hand edits) launcher-ui-p6/.../footer-generated.command.json;
  - Reborn chunk 229 with the #24 fix (work/beta18/ufc3_recomp.229.fix24.cpp), same recipe as chunk 229.
The objects keep the base names build.py looks for. Precompiled headers from the old toolchain cannot be loaded by
another compiler, so "-include-pch X.pch" is replaced by a plain "-include X" of the header it was made from.

Usage (WSL, as root):
  pgo_recomp.py win-reborn gen <out_dir>                     -fprofile-generate -DUFC_PGO_GEN
  pgo_recomp.py win-reborn use <out_dir> <reborn.profdata>   -fprofile-use=...
Then build.py with UFC_RECOMP_OBJS_<VARIANT>=<out_dir> (and UFC3_REBORN_SWITCH_FIX=<out_dir> for win-reborn) and
UFC_EXTRA_CXXFLAGS / UFC_EXTRA_LDFLAGS for its own units; see pgo-windows.sh.
"""
import json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
variant, mode, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
profdata = sys.argv[4] if mode == "use" else None
jobs = int(os.environ.get("PGO_JOBS", os.cpu_count() or 8))
only = os.environ.get("PGO_ONLY")  # optional regex: compile only matching objects (tests)

src = open(HERE / "build.py", encoding="utf-8").read().split("\nargs = [a for a in sys.argv")[0]
g = {"__file__": str(HERE / "build.py"), "__name__": "pgo"}
exec(compile(src, str(HERE / "build.py"), "exec"), g)
V, load_recipe, WIN_CXX = g["V"], g["load_recipe"], g["WIN_CXX"]
LINUX = variant.startswith("linux")
assert LINUX or WIN_CXX, "UFC3_WINDOWS_CXX must point at the llvm-mingw clang++ used for the whole PGO build"
v = V[variant]
cwd = v["cwd"]
ARCHIVE = os.environ.get("UFC3_CODEX_ARCHIVE", "/mnt/e/PROJETOS ANTIGOS/Codex-22-10").rstrip("/") + "/"
ORIG = "/mnt/c/Users/Monscriser/Documents/Codex/"
MASTER = "2026-10-20/de/work/master-update"
P6_76 = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-ui-p6/apps/windows/reborn/footer-generated.command.json"
FIX24 = "/mnt/c/Users/Monscriser/Documents/Codex/2026-11-14/new-chat-3/work/beta18/ufc3_recomp.229.fix24.cpp"
RECOMP = re.compile(r"ufc3_(recomp\.\d+|init|register)(\.cpp)?\.(obj|o)$")
P6_76_LINUX = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-ui-p6/apps/linux/reborn/footer-generated.command.json"

def real(p):
    if os.path.exists(p) or not p.startswith(ORIG): return p
    alt = ARCHIVE + p[len(ORIG):]
    return alt if os.path.exists(alt) else p

def ninja_command(obj):
    r = subprocess.run(["ninja", "-C", cwd, "-t", "commands", "-s", obj], capture_output=True, text=True, check=True)
    line = r.stdout.strip().splitlines()[-1]
    if line.startswith("cd ") and "&&" in line: line = line.split("&&", 1)[1].strip()
    return shlex.split(line)

def recipe_for(obj):
    """argv of the command that produced obj (paths resolved to the archive when moved)."""
    if not obj.startswith("/"):
        return ninja_command(obj)
    if MASTER in obj:
        stem = Path(obj).name.replace(".cpp.obj", "").replace(".obj", "")
        logs = real(ORIG + MASTER + "/logs")
        return [real(a) for a in load_recipe(f"{logs}/reborn-{stem}.command.json")]
    if obj.endswith("launcher-ui-p6/apps/windows/reborn/ufc3_recomp.76.obj"):
        return [real(a) for a in load_recipe(P6_76)]
    if "/linux-update/pre/obj-" in obj:  # Linux chunks: linux_pre recipe of each object (pre/logs)
        edition = "reborn" if "/obj-reborn/" in obj else "original"
        stem = Path(obj).name[:-2]
        return [real(a) for a in load_recipe(str(Path(obj).parent.parent / "logs" / f"{edition}-{stem}.command.json"))]
    if obj.endswith("launcher-ui-p6/apps/linux/reborn/ufc3_recomp.76.o"):
        return [real(a) for a in load_recipe(P6_76_LINUX)]
    raise SystemExit(f"no recipe known for {obj}")

PREFIX_DIR = out / "prefix"

def prefix_copy(header):
    """clang silently uses <header>.pch when it sits next to an -include'd header, so the header is included
    from a copy in the output dir (the old .pch was made by another toolchain build and cannot be loaded)."""
    import hashlib, shutil
    PREFIX_DIR.mkdir(parents=True, exist_ok=True)
    dst = PREFIX_DIR / (hashlib.sha1(header.encode()).hexdigest()[:10] + "-" + Path(header).name)
    if not dst.exists(): shutil.copyfile(header, dst)
    return str(dst)

def strip_pch(a):
    """Old-toolchain PCH -> plain include of a copy of its header; no dependency files in the lab dirs."""
    out_, i = [], 0
    while i < len(a):
        x = a[i]
        if x == "-Xclang" and i + 1 < len(a) and a[i + 1] == "-include-pch":
            i += 4; continue                         # -Xclang -include-pch -Xclang <file.pch>
        if x == "-include-pch": i += 2; continue
        if x == "-Xclang" and i + 3 < len(a) and a[i + 1] == "-include" and a[i + 2] == "-Xclang":
            out_ += ["-include", prefix_copy(a[i + 3])]; i += 4; continue
        if x == "-include" and i + 1 < len(a) and os.path.exists(a[i + 1] + ".pch"):
            out_ += ["-include", prefix_copy(a[i + 1])]; i += 2; continue
        if x in ("-MD", "-MMD", "-Winvalid-pch"): i += 1; continue
        if x in ("-MT", "-MF", "-MQ"): i += 2; continue
        out_.append(x); i += 1
    return out_

def flags():
    if mode == "plain": return []          # control: same recipe, no PGO
    if mode == "gen": return ["-fprofile-generate", "-DUFC_PGO_GEN"]
    return [f"-fprofile-use={profdata}", "-Wno-profile-instr-unprofiled", "-Wno-profile-instr-out-of-date",
            "-Wno-backend-plugin"]

link = load_recipe(v["link"])
objs = [a for a in link if RECOMP.search(a)]
tasks = []
for obj in objs:
    a = strip_pch(recipe_for(obj))
    if not LINUX: a[0] = WIN_CXX  # Linux keeps its clang 19 (profile converted with llvm-profdata 19)
    o = a.index("-o")
    name = Path(obj).name
    a[o + 1] = str(out / name)
    tasks.append((name, a + flags(), cwd if not obj.startswith("/") else "/"))
if variant == "win-reborn":
    # #24: chunk 229 with entry 7 -> sub_83669280, as build.py's REBORN_SWITCH_OBJECTS expects it
    base = next(t for t in tasks if t[0] == "ufc3_recomp.229.cpp.obj")
    a = list(base[1]); c = next(i for i, x in enumerate(a) if x.endswith("ufc3_recomp.229.cpp") and i != a.index("-o") + 1)
    a[c] = FIX24
    (out / "win").mkdir(parents=True, exist_ok=True)
    a[a.index("-o") + 1] = str(out / "win" / "fix24-validated.obj")
    tasks.append(("win/fix24-validated.obj", a, base[2]))
if variant == "linux-reborn":
    base = next(t for t in tasks if t[0] == "ufc3_recomp.229.o")
    a = list(base[1]); c = next(i for i, x in enumerate(a) if x.endswith("ufc3_recomp.229.cpp") and i != a.index("-o") + 1)
    a[c] = FIX24
    (out / "linux").mkdir(parents=True, exist_ok=True)
    a[a.index("-o") + 1] = str(out / "linux" / "fix24.o")
    tasks.append(("linux/fix24.o", a, base[2]))
if only: tasks = [t for t in tasks if re.search(only, t[0])]
out.mkdir(parents=True, exist_ok=True)
(out / "logs").mkdir(exist_ok=True)
json.dump([{"obj": n, "argv": a, "cwd": c} for n, a, c in tasks], open(out / "commands.json", "w"), indent=1)

def build(t):
    name, a, c = t
    log = out / "logs" / (name.replace("/", "_") + ".log")
    with open(log, "w") as f:
        r = subprocess.run(a, cwd=c, stdout=f, stderr=subprocess.STDOUT)
    return name, r.returncode

failed = []
with ThreadPoolExecutor(jobs) as pool:
    for n, (name, rc) in enumerate(pool.map(build, tasks), 1):
        if rc: failed.append(name)
        if n % 25 == 0 or rc: print(f"[{variant} {mode}] {n}/{len(tasks)} {name} rc={rc}", flush=True)
print(f"[{variant} {mode}] done: {len(tasks)} objects, {len(failed)} failed {failed[:10]}")
sys.exit(1 if failed else 0)
