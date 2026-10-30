#!/usr/bin/env python3
"""BETA1.1 candidate video library (Linux): same as build_win.py with the Linux recipe
(texture-descriptors/linux = shipped librexgpu-xenos.so 4503a9a4...)."""
import json, subprocess, sys, hashlib, os, shlex
W = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11"
PORT = "/root/ufc-port"
CWD = "/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux"
TD = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/optimization-p10-25-10/texture-descriptors/linux"
OUT = W + "/out-linux"; os.makedirs(OUT, exist_ok=True)
os.makedirs(PORT + "/overlay/rex/graphics/vulkan", exist_ok=True)
subprocess.run(["cp", PORT + "/include/rex/graphics/vulkan/texture_cache.h", PORT + "/overlay/rex/graphics/vulkan/"], check=True)
def run(a, cwd, log):
    with open(log, "w") as f: r = subprocess.run(a, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode: print(open(log).read()[-4000:]); sys.exit("failed " + log)
def overlay(args):
    i = next(k for k, a in enumerate(args) if a.startswith("-I"))
    return args[:i] + ["-I" + PORT + "/overlay"] + args[i:]
# sdk-linux/compile_commands.json has no graphics entries: use the command processor recipe (same target, same flags)
tcomp = json.load(open(TD + "/compile.command.json")); o = tcomp.index("-o") + 1
vsrc = next(x for x in tcomp if x.endswith("vulkan_command_processor.cpp"))
args = [PORT + "/src/graphics/vulkan/texture_cache.cpp" if x == vsrc else x for x in tcomp]
args[o] = OUT + "/texture_cache.cpp.o"
e = {"directory": "/"}
print("compile texture_cache"); run(overlay(args), e["directory"], OUT + "/tc.log")
comp = json.load(open(TD + "/compile.command.json")); o = comp.index("-o") + 1
vobj = comp[o]; comp[o] = OUT + "/vcp.o"
print("compile vcp"); run(overlay(comp), "/", OUT + "/vcp.log")
link = json.load(open(TD + "/link.command.json")); o = link.index("-o") + 1
rep = {CWD + "/src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/texture_cache.cpp.o": OUT + "/texture_cache.cpp.o", vobj: OUT + "/vcp.o"}
for k in rep: assert k in link, k
link = [rep.get(a, a) for a in link]; link[o] = OUT + "/librexgpu-xenos.so"
print("link"); run(link, "/", OUT + "/link.log")
print(hashlib.sha256(open(OUT + "/librexgpu-xenos.so", "rb").read()).hexdigest())
