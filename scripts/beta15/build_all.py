#!/usr/bin/env python3
"""UFC3MONS video DLL (Windows) + B6 + B4/B10 + B11 (05/11).: BETA 1.1 base + A1 hoist + A16 + B57 + B56 + VRAM log + U4 blood fix
(ufc_draw_resolution_scale_threshold, port of Xenia Canary 74db632). Headers in inc/ take precedence."""
import json, subprocess, sys, hashlib, os, shlex

W = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/gpu-port-blood"
V60 = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/continue-o-ufc3mons-usando-a-skill/work"
PORT = "/root/ufc-port"
SDK = "/mnt/e/UFC3-RexGlue-Lab/sources/rexglue-sdk-clean"
CWD = "/mnt/e/UFC3-RexGlue-Lab/build/sdk-clean-win"
TD = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/optimization-p10-25-10/texture-descriptors"
SC = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/stale-check"
MEAS = "--measure" in sys.argv
OUT = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b11-pipeline-memo-05-11" + ("/out-win-measure" if MEAS else "/out-win")
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
    return args[:i] + ["-I" + W + "/od-win", "-I" + W + "/inc", "-I" + W, "-I" + PORT + "/overlay"] + args[i:]  # od-win: on-demand fast page check

# 1. texture_cache.cpp (same as BETA 1.1)
cc = json.load(open(CWD + "/compile_commands.json"))
e = next(x for x in cc if x["file"].endswith("src/graphics/vulkan/texture_cache.cpp"))
args = e["arguments"] if "arguments" in e else shlex.split(e["command"])
src = e["file"]
o = args.index("-o") + 1
args = [PORT + "/src/graphics/vulkan/texture_cache_a16.cpp" if a == src else a for a in args]
args[o] = OUT + "/texture_cache.cpp.obj"
print("compile texture_cache")
run(overlay(args), e["directory"], OUT + "/tc.log")


B60 = "--b60" in sys.argv  # B60 kept out of the build until measured at 2x
# B60: pipeline/texture/cache.cpp (texture destroy under the global lock)
if B60:
  e2 = next(x for x in cc if x["file"].endswith("src/graphics/pipeline/texture/cache.cpp"))
  a2 = e2["arguments"] if "arguments" in e2 else shlex.split(e2["command"])
  o2 = a2.index("-o") + 1
  a2 = [W + "/cache.cpp" if a == e2["file"] else a for a in a2]
  a2[o2] = OUT + "/cache.cpp.obj"
  print("compile cache (B60)")
  run(overlay(a2), e2["directory"], OUT + "/cache.log")


# B56: base SharedMemory (R8 source + lockless single-range valid check)
SMD = "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-21/continue-o-projeto-ufc3mons-port-nativo/work/r8-input-lock/gpuwin-build"
B56 = "--no-b56" not in sys.argv
if B56:
  smc = json.load(open(SMD + "/shared_memory.cpp.command.json"))
  smc = [W + "/shared_memory.cpp" if a == SMD + "/sm/shared_memory.cpp" else a for a in smc]
  smc[smc.index("-o") + 1] = OUT + "/shared_memory.cpp.obj"
  print("compile shared_memory (B56)")
  run(smc, CWD, OUT + "/sm.log")


# U4: base render target cache, base texture cache, SPIR-V translator, Vulkan pipeline cache.
U4_OBJS = {}
for src_rel, local in (("src/graphics/pipeline/render_target/cache.cpp", "rt_cache.cpp"),
                       ("src/graphics/pipeline/texture/cache.cpp", "tex_cache.cpp"),
                       ("src/graphics/pipeline/shader/spirv_translator.cpp", "spirv_translator.cpp"),
                       ("src/graphics/vulkan/pipeline_cache.cpp", "pipeline_cache.cpp")):
  e3 = next(x for x in cc if x["file"].endswith(src_rel))
  a3 = e3["arguments"] if "arguments" in e3 else shlex.split(e3["command"])
  o3 = a3.index("-o") + 1
  obj_name = a3[o3]
  a3 = [(("/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b6-rt-skip-05-11" + "/rt_cache_b6.cpp") if local == "rt_cache.cpp" else ("/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b11-pipeline-memo-05-11" + "/pipeline_cache_b11.cpp" if local == "pipeline_cache.cpp" else W + "/" + local)) if a == e3["file"] else a for a in a3]
  a3[o3] = OUT + "/u4_" + local + ".obj"
  print("compile", local, "(U4)")
  run(overlay(a3), e3["directory"], OUT + "/u4_" + local + ".log")
  U4_OBJS[obj_name] = OUT + "/u4_" + local + ".obj"
# U4: Vulkan render target cache (v60 source), compiled from its own directory for relative includes.
subprocess.run(["cp", "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b6-rt-skip-05-11" + "/vulkan_render_target_cache_b6.cpp", V60 + "/v60-sdk/src/graphics/vulkan/render_target_cache_u4.cpp"], check=True)
v60_line = [l for l in open(V60 + "/build_v60_gpu.sh").read().splitlines() if "v60-render_target_cache.obj" in l and " -c " in l][0]
va = shlex.split(v60_line)
va = [V60 + "/v60-sdk/src/graphics/vulkan/render_target_cache_u4.cpp" if a.endswith("v60-sdk/src/graphics/vulkan/render_target_cache.cpp") else a for a in va]
va[va.index("-o") + 1] = OUT + "/u4_vulkan_render_target_cache.obj"
print("compile vulkan_render_target_cache (U4)")
run(overlay(va), CWD, OUT + "/u4_vrtc.log")
U4_OBJS[V60 + "/v60-render_target_cache.obj"] = OUT + "/u4_vulkan_render_target_cache.obj"

# 2. vsm_r4.cpp (VulkanSharedMemory with A1 upload hoisting hooks)
vsm_comp = json.load(open(TD + "/win/compile.command.json"))
o = vsm_comp.index("-o") + 1
c_idx = vsm_comp.index("-c") + 1
vsm_comp[c_idx] = W + "/vsm_r4.cpp"
vsm_comp[o] = OUT + "/vsm_r4.obj"
print("compile vsm_r4")
run(overlay(vsm_comp), CWD, OUT + "/vsm.log")

# 3. vulkan_command_processor.cpp (VulkanCommandProcessor with A1 hoist + GPU readback verifier)
comp = json.load(open(TD + "/win/compile.command.json"))
o = comp.index("-o") + 1
c_idx = comp.index("-c") + 1
vobj = comp[o]
comp[c_idx] = "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b11-pipeline-memo-05-11" + ("/vcp_b11_measure.cpp" if MEAS else "/vcp_b11.cpp")
comp[o] = OUT + "/vcp.obj"
print("compile vcp")
run(overlay(comp), CWD, OUT + "/vcp.log")

# 4. link
link = json.load(open(SC + "/link-control.command.json"))
o = link.index("-o") + 1
rep = {
    "src/graphics/CMakeFiles/rexgpu-xenos.dir/vulkan/texture_cache.cpp.obj": OUT + "/texture_cache.cpp.obj",
    TD + "/win/vcp.obj": OUT + "/vcp.obj",
    "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/de/work/r4-gpu/vsm_r4.obj": OUT + "/vsm_r4.obj",
}
TRACE = "--trace" in sys.argv
if TRACE:
    rep["/mnt/c/Users/Monscriser/Documents/Codex/2026-10-20/ufc3mons-gpu-resource-analysis/work/v69-gpu/deferred_command_buffer.obj"] = (
        "/mnt/c/Users/Monscriser/Documents/Codex/2026-10-27/continue-a-investiga-o-da-faixa/work/draw-trace/out/deferred_command_buffer.obj"
    )
if B60: rep["src/graphics/CMakeFiles/rexgpu-xenos.dir/pipeline/texture/cache.cpp.obj"] = OUT + "/cache.cpp.obj"
if B56: rep[SMD + "/shared_memory.cpp.obj"] = OUT + "/shared_memory.cpp.obj"
rep.update(U4_OBJS)
for k in rep:
    assert k in link, k
DLL = OUT + ("/rexgpu-xenos-trace.dll" if TRACE else "/rexgpu-xenos.dll")
link = [rep.get(a, a) for a in link]
link[o] = DLL
link = [a.replace(SC + "/control/", OUT + "/") for a in link]
print("link")
run(link, CWD, OUT + "/link.log")
print(hashlib.sha256(open(DLL, "rb").read()).hexdigest())
