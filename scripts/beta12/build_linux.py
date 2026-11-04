#!/usr/bin/env python3
"""Item A1 isolated video library (Linux): BETA 1.1 base + ufc_hoist_shared_memory_uploads (default false) + GPU readback verifier."""
import json, subprocess, sys, hashlib, os, shlex

W = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-blood"
PORT = "/root/ufc-port"
CWD = "/mnt/e/UFC3-RexGlue-Lab/build/sdk-linux"
TD = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/optimization-p10-25-10/texture-descriptors/linux"
OUT = W + "/out-linux"
os.makedirs(OUT, exist_ok=True)

os.makedirs(PORT + "/overlay/rex/graphics/vulkan", exist_ok=True)
subprocess.run(["cp", W + "/texture_cache.cpp", PORT + "/src/graphics/vulkan/texture_cache_a16.cpp"], check=True)  # A16: same dir for relative shader includes
subprocess.run(["cp", PORT + "/include/rex/graphics/vulkan/texture_cache.h", PORT + "/overlay/rex/graphics/vulkan/"], check=True)

def run(a, cwd, log):
    with open(log, "w") as f:
        r = subprocess.run(a, cwd=cwd, stdout=f, stderr=subprocess.STDOUT)
    if r.returncode:
        print(open(log).read()[-4000:])
        sys.exit("failed " + log)

def overlay(args):
    i = next(k for k, a in enumerate(args) if a.startswith("-I"))
    return args[:i] + ["-I" + W + "/inc", "-I" + W, "-I" + PORT + "/overlay"] + args[i:]

# 1. texture_cache.cpp
tcomp = json.load(open(TD + "/compile.command.json"))
o = tcomp.index("-o") + 1
vsrc = next(x for x in tcomp if x.endswith("vulkan_command_processor.cpp"))
args = [PORT + "/src/graphics/vulkan/texture_cache_a16.cpp" if x == vsrc else x for x in tcomp]
args[o] = OUT + "/texture_cache.cpp.o"
print("compile texture_cache")
run(overlay(args), "/", OUT + "/tc.log")

# 2. vsm_r4.cpp -> vulkan_shared_memory.cpp.o
vsm_comp = json.load(open(TD + "/compile.command.json"))
o = vsm_comp.index("-o") + 1
vsm_args = [W + "/vsm_r4.cpp" if x == vsrc else x for x in vsm_comp]
vsm_args[o] = OUT + "/vulkan_shared_memory.cpp.o"
print("compile vulkan_shared_memory")
run(overlay(vsm_args), "/", OUT + "/vsm.log")

# 3. vulkan_command_processor.cpp -> vcp.o
comp = json.load(open(TD + "/compile.command.json"))
o = comp.index("-o") + 1
vobj = comp[o]
comp = [W + "/vulkan_command_processor.cpp" if x == vsrc else x for x in comp]
comp[o] = OUT + "/vcp.o"
print("compile vcp")
run(overlay(comp), "/", OUT + "/vcp.log")


B60 = "--b60" in sys.argv  # B60 kept out of the build until measured at 2x
# B60: pipeline/texture/cache.cpp
import re as _re
_t = open(CWD + "/build.ninja").read(); _o = "src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/texture/cache.cpp.o"
_b = _t[_t.index("build " + _o):]; _b = _b[:_b.index(chr(10) * 2)]
_v = {k: v for k, v in _re.findall(r"^\s+(\w+) = (.*)$", _b, _re.M)}
if B60:
  print("compile cache (B60)")
  run(["/usr/lib/llvm-19/bin/clang++"] + shlex.split(_v["DEFINES"]) + ["-I" + W, "-I" + PORT + "/overlay"] + shlex.split(_v["INCLUDES"]) + shlex.split(_v["FLAGS"]) + ["-o", OUT + "/cache.cpp.o", "-c", W + "/cache.cpp"], CWD, OUT + "/cache.log")


# B56: base SharedMemory (R8 source, identical to Windows) + lockless single-range valid check
SML = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/r8-input-lock/gpu84-async"
B56 = "--no-b56" not in sys.argv
if B56:
  smc = json.load(open(SML + "/logs/shared_memory.cpp.command.json"))
  smc = [W + "/shared_memory.cpp" if a == SML + "/gpu/shared_memory.cpp" else a for a in smc]
  smc[smc.index("-o") + 1] = OUT + "/shared_memory.cpp.o"
  print("compile shared_memory (B56)")
  run(smc, "/", OUT + "/sm.log")


# U4 (blood fix): base render target cache, base texture cache, SPIR-V translator, Vulkan pipeline cache.
import re as _re2
U4_OBJS = {}
_nin = open(CWD + "/build.ninja").read()
for obj_rel, local in (("src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/render_target/cache.cpp.o", "rt_cache.cpp"),
                       ("src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/texture/cache.cpp.o", "tex_cache.cpp"),
                       ("src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/shader/spirv_translator.cpp.o", "spirv_translator.cpp"),
                       ("src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/pipeline_cache.cpp.o", "pipeline_cache.cpp")):
  _b2 = _nin[_nin.index("build " + obj_rel):]; _b2 = _b2[:_b2.index(chr(10) * 2)]
  _v2 = {k: v for k, v in _re2.findall(r"^\s+(\w+) = (.*)$", _b2, _re2.M)}
  print("compile", local, "(U4)")
  run(["/usr/lib/llvm-19/bin/clang++"] + shlex.split(_v2["DEFINES"]) + ["-I" + W + "/inc", "-I" + W] + shlex.split(_v2["INCLUDES"]) + shlex.split(_v2["FLAGS"]) + ["-o", OUT + "/u4_" + local + ".o", "-c", W + "/" + local], CWD, OUT + "/u4_" + local + ".log")
  U4_OBJS[CWD + "/" + obj_rel] = OUT + "/u4_" + local + ".o"
# U4: Vulkan render target cache (Linux copy of the same source), compiled from its own directory.
RTL = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/r8-input-lock/gpu84-async"
subprocess.run(["cp", W + "/vulkan_render_target_cache.linux.cpp", RTL + "/gpu/render_target_cache_u4.cpp"], check=True)
rc = json.load(open(RTL + "/logs/render_target_cache.cpp.command.json"))
rc = [RTL + "/gpu/render_target_cache_u4.cpp" if a == RTL + "/gpu/render_target_cache.cpp" else a for a in rc]
rc[rc.index("-o") + 1] = OUT + "/u4_vrtc.o"
print("compile vulkan_render_target_cache (U4)")
run(overlay(rc), "/", OUT + "/u4_vrtc.log")
U4_OBJS[RTL + "/obj/render_target_cache.cpp.o"] = OUT + "/u4_vrtc.o"

# 4. link
link = json.load(open(TD + "/link.command.json"))
o = link.index("-o") + 1
rep = {
    CWD + "/src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/texture_cache.cpp.o": OUT + "/texture_cache.cpp.o",
    vobj: OUT + "/vcp.o",
    "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/r8-input-lock/gpu84-async/obj/vulkan_shared_memory.cpp.o": OUT + "/vulkan_shared_memory.cpp.o",
}
if B60: rep[CWD + "/src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/texture/cache.cpp.o"] = OUT + "/cache.cpp.o"
if B56: rep[SML + "/obj/shared_memory.cpp.o"] = OUT + "/shared_memory.cpp.o"
rep.update(U4_OBJS)
for k in rep:
    assert k in link, k
link = [rep.get(a, a) for a in link]
link[o] = OUT + "/librexgpu-xenos.so"
print("link")
run(link, "/", OUT + "/link.log")
print(hashlib.sha256(open(OUT + "/librexgpu-xenos.so", "rb").read()).hexdigest())
