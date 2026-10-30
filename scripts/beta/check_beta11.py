#!/usr/bin/env python3
"""Checks the BETA1.1 packages against build/ (hashes), the zip root (3 items) and the fixed runtimes."""
import hashlib, os, subprocess, sys, zipfile, tempfile
P = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/launcher-p14-28-10"; B = P + "/build"; E = P + "/entrega"
FIX = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/fault-measure"
h = lambda b: hashlib.sha256(b).hexdigest()
hf = lambda p: h(open(p, "rb").read())
ok = True
def check(name, a, b):
    global ok; good = a == b; ok &= good; print(("OK  " if good else "ERR ") + name)
z = zipfile.ZipFile(E + "/UFC3MONS-BETA1.1-Windows.zip")
roots = sorted({n.split("/")[0] for n in z.namelist()})
check("zip raiz = Launcher-Config.exe, UFC3.exe, data " + str(roots), roots, ["Launcher-Config.exe", "UFC3.exe", "data"])
check("zip sem barra invertida", any("\\" in n for n in z.namelist()), False)
for inner, src in [("UFC3.exe", B + "/frontdoor/UFC3.exe"), ("Launcher-Config.exe", B + "/frontdoor/UFC3.exe"),
                   ("data/ufc3-launcher-l1.exe", B + "/win-reborn/ufc3-launcher-l1.exe"),
                   ("data/ufc3-original-v48.exe", B + "/win-original/ufc3-original-v48.exe"),
                   ("data/librexruntime.dll", FIX + "/win-fix/librexruntime.dll"),
                   ("data/rexgpu-xenos.dll", "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11/out-win/rexgpu-xenos.dll")]:
    check("zip " + inner, h(z.read(inner)), hf(src))
for inner, src in [("data/shaders/windows/5451087D.xsh", P + "/seed/windows/5451087D.xsh"), ("data/shaders/windows/5451087D.fbo.vk.xpso", P + "/seed/windows/5451087D.fbo.vk.xpso")]:
    check("zip " + inner, h(z.read(inner)), hf(src))
tested = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/dlc-test/teste-p148/data/ufc3-launcher-l1.exe"
#check("zip Reborn exe = exe testado", h(z.read("data/ufc3-launcher-l1.exe")), hf(tested))
d = tempfile.mkdtemp(dir="/root")
subprocess.run([E + "/UFC3MONS-BETA1.1-Linux-x86_64.AppImage", "--appimage-extract"], cwd=d, stdout=subprocess.DEVNULL, check=True)
r = d + "/squashfs-root/usr/bin"
for inner, src in [("UFC3", B + "/frontdoor/UFC3"), ("data/ufc3-launcher-l1", B + "/linux-reborn/ufc3-launcher-l1"),
                   ("data/ufc3-original-v48", B + "/linux-original/ufc3-original-v48"), ("data/librexruntime.so", FIX + "/linux-fix/librexruntime.so"), ("data/librexgpu-xenos.so", "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11/out-linux/librexgpu-xenos.so"), ("data/shaders/linux/5451087D.fbo.vk.xpso", P + "/seed/linux/5451087D.fbo.vk.xpso"), ("data/shaders/linux/5451087D.xsh", P + "/seed/linux/5451087D.xsh")]:
    check("AppImage " + inner, hf(r + "/" + inner), hf(src))
print("APPDIR", d)
sys.exit(0 if ok else 1)
