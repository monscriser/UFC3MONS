#!/usr/bin/env python3
"""Diagnostic only (cvar ufc_xfer_diag, default false): for every draw that needs EDRAM ownership
transfers, aggregate what the draw does (primitive, vertex count, depth/stencil/color state, VS hash)
and how many tiles are transferred per slot. Printed every 10 s as [ufc-xfer]."""
import sys
P = "vulkan_render_target_cache.cpp"
s = open(P, newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"
s = s.replace("\r\n", "\n")
def rep(a, b):
    global s
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)
rep('REXCVAR_DEFINE_STRING(render_target_path_vulkan, "", "GPU/Vulkan",',
    '#include <chrono>\n#include <map>\n#include <string>\n'
    'REXCVAR_DEFINE_BOOL(ufc_xfer_diag, false, "GPU", "Diagnostic: log draws that need EDRAM transfers");\n'
    'namespace {\nstd::map<std::string, std::pair<uint64_t, uint64_t>> ufc_xfer_agg;\n}\n'
    'REXCVAR_DEFINE_STRING(render_target_path_vulkan, "", "GPU/Vulkan",')
rep("""      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
""", """      if (REXCVAR_GET(ufc_xfer_diag)) {
        const std::vector<Transfer>* xt = last_update_transfers();
        uint64_t tiles[1 + xenos::kMaxColorRenderTargets] = {};
        bool any = false;
        for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
          for (const Transfer& t : xt[i]) tiles[i] += t.end_tiles - t.start_tiles;
          any |= tiles[i] != 0;
        }
        if (any) {
          const RegisterFile& regs = register_file();
          const uint32_t init = regs[XE_GPU_REG_VGT_DRAW_INITIATOR];
          const uint32_t dc = regs[XE_GPU_REG_RB_DEPTHCONTROL];
          const uint32_t srm = regs[XE_GPU_REG_RB_STENCILREFMASK];
          auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
          char key[384];
          std::snprintf(key, sizeof(key),
                        "prim=%u src=%u n=%u xyfmt=%u dc=%08X ndc=%08X srm=%06X cmask=%08X "
                        "blend0=%08X aamask=%X vs=%016llX slots=%c%c%c%c%c msaa=%u pitch=%u",
                        init & 0x3F, (init >> 6) & 3, init >> 16, uint32_t(vte.vtx_xy_fmt), dc,
                        normalized_depth_control.value, srm & 0xFFFFFF, normalized_color_mask,
                        regs[XE_GPU_REG_RB_BLENDCONTROL0], regs[XE_GPU_REG_PA_SC_AA_MASK] & 0xF,
                        (unsigned long long)vertex_shader.ucode_data_hash(),
                        tiles[0] ? 'D' : '-', tiles[1] ? '0' : '-', tiles[2] ? '1' : '-',
                        tiles[3] ? '2' : '-', tiles[4] ? '3' : '-',
                        uint32_t(depth_and_color_render_targets[0]
                                     ? uint32_t(depth_and_color_render_targets[0]->key().msaa_samples)
                                     : 9),
                        depth_and_color_render_targets[0]
                            ? depth_and_color_render_targets[0]->key().pitch_tiles_at_32bpp
                            : 0);
          auto& a = ufc_xfer_agg[key];
          ++a.first;
          for (uint64_t t : tiles) a.second += t;
        }
        static auto t0 = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - t0 >= std::chrono::seconds(10) && !ufc_xfer_agg.empty()) {
          t0 = now;
          for (const auto& kv : ufc_xfer_agg)
            REXGPU_WARN("[ufc-xfer] draws={} tiles={} {}", kv.second.first, kv.second.second, kv.first);
          REXGPU_WARN("[ufc-xfer] ---");
          ufc_xfer_agg.clear();
        }
      }
      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
""")
open(P, "w", newline="").write(s.replace("\n", nl))
print("ok")
