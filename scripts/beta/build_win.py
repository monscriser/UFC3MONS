#!/usr/bin/env python3
"""BETA1.1 candidate video DLL (Windows): UFC SDK + ht_texture_direct (+verify) from /root/ufc-port.
Recompiles texture_cache.cpp (patched) and the command processor (same source, new header: it calls the inline
VulkanTextureCache::Create) with /root/ufc-port/include first, then relinks with the production recipe.
Usage: build_win.py [--control]  (--control: same steps with the unpatched SDK header, must reproduce the objects' behaviour)"""
import json, subprocess, sys, hashlib, os, shlex
W = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-beta11"
PORT = "/root/ufc-port"
SDK = "/mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean"
CWD = "/mnt/e/UFC3-RexGlue-Lab/build/sdk-clean-win"
TD = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/optimization-p10-25-10/texture-descriptors"
SC = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/stale-check"
OUT = W + "/out-win"; os.makedirs(OUT, exist_ok=True)
# only the changed header: a full second include tree breaks #pragma once
os.makedirs(PORT + "/overlay/rex/graphics/vulkan", exist_ok=True)
subprocess.run(["cp", PORT + "/include/rex/graphics/vulkan/texture_cache.h", PORT + "/overlay/rex/graphics/vulkan/"], check=True)
def run(a, cwd, log):
    with open(log, "w") as f: r = subprocess.run(a, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode: print(open(log).read()[-4000:]); sys.exit("failed " + log)
def overlay(args):
    i = next(k for k, a in enumerate(args) if a.startswith("-I"))
    return args[:i] + ["-I" + PORT + "/overlay"] + args[i:]
# 1. texture_cache.cpp from compile_commands.json
cc = json.load(open(CWD + "/compile_commands.json"))
e = next(x for x in cc if x["file"].endswith("src/graphics/vulkan/texture_cache.cpp"))
args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
src = e["file"]; o = args.index("-o") + 1
args = [PORT + "/src/graphics/vulkan/texture_cache.cpp" if a == src else a for a in args]
args[o] = OUT + "/texture_cache.cpp.obj"
print("compile texture_cache"); run(overlay(args), e["directory"], OUT + "/tc.log")
# 2. command processor (unchanged source, new header)
comp = json.load(open(TD + "/win/compile.command.json")); o = comp.index("-o") + 1
vobj = comp[o]; comp[o] = OUT + "/vcp.obj"
print("compile vcp"); run(overlay(comp), CWD, OUT + "/vcp.log")
# 3. link
link = json.load(open(SC + "/link-control.command.json")); o = link.index("-o") + 1
rep = {"src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/texture_cache.cpp.obj": OUT + "/texture_cache.cpp.obj",
       TD + "/win/vcp.obj": OUT + "/vcp.obj"}
TRACE = "--trace" in sys.argv  # diagnostic: per-draw GPU tracer v2 (work/draw-trace), never shipped
if TRACE:
    rep["/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/ufc3mons-gpu-resource-analysis/work/v69-gpu/deferred_command_buffer.obj"] =         "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/draw-trace/out/deferred_command_buffer.obj"
for k in rep: assert k in link, k
DLL = OUT + ("/rexgpu-xenos-trace.dll" if TRACE else "/rexgpu-xenos.dll")
link = [rep.get(a, a) for a in link]; link[o] = DLL
link = [a.replace(SC + "/control/", OUT + "/") for a in link]
print("link"); run(link, CWD, OUT + "/link.log")
print(hashlib.sha256(open(DLL, "rb").read()).hexdigest())
