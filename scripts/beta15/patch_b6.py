#!/usr/bin/env python3
"""B6: gera rt_cache_b6.cpp e vulkan_render_target_cache_b6.cpp a partir de gpu-port-blood (BETA 1.4).
Cvar ufc_rt_update_skip (padrao false), prova ufc_rt_update_skip_verify, controle _verify_break."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
G = os.path.join(HERE, "..", "gpu-port-blood")
INC = '#include "/mnt/e/PROJETOS ANTIGOS/UFC3MONS-trabalho/b6-rt-skip-05-11/ufc_rt_skip.h"\n'


def go(src, dst, reps):
    t = open(os.path.join(G, src), encoding="utf-8").read()
    for a, b, n in reps:
        assert t.count(a) == n, (src, n, t.count(a), a[:70])
        t = t.replace(a, b)
    open(os.path.join(HERE, dst), "w", encoding="utf-8", newline="\n").write(INC + t)


KEY = r'''
  // UFC3MONS B6: chave de tudo que esta funcao le (registradores + argumentos).
  auto& ufc_us = ufc_rt_skip::g();
  ufc_us.skipped = false;
  uint32_t ufc_key[ufc_rt_skip::kKeyWords];
  {
    uint32_t k = 0;
    ufc_key[k++] = regs[XE_GPU_REG_RB_SURFACE_INFO];
    ufc_key[k++] = regs[XE_GPU_REG_RB_DEPTH_INFO];
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i)
      ufc_key[k++] = regs[reg::RB_COLOR_INFO::rt_register_indices[i]];
    ufc_key[k++] = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
    ufc_key[k++] = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
    ufc_key[k++] = regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
    ufc_key[k++] = regs[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];
    ufc_key[k++] = regs[XE_GPU_REG_PA_CL_CLIP_CNTL];
    ufc_key[k++] = regs[XE_GPU_REG_PA_CL_VTE_CNTL];
    ufc_key[k++] = uint32_t(regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable);
    ufc_key[k++] = uint32_t(regs.Get<reg::PA_SU_VTX_CNTL>().pix_center);
    ufc_key[k++] = regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET];
    ufc_key[k++] = regs[XE_GPU_REG_PA_CL_VPORT_YSCALE];
    ufc_key[k++] = uint32_t(is_rasterization_done);
    ufc_key[k++] = normalized_depth_control.value;
    ufc_key[k++] = normalized_color_mask;
    while (k < ufc_rt_skip::kKeyWords) ufc_key[k++] = 0;
  }
  // Com clip desligado o estimador de altura pode executar o shader de vertices na CPU (depende dos
  // vertices): nunca pular nesse caso.
  const bool ufc_key_complete =
      !interlock_barrier_only && !(regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable &&
                                   REXCVAR_GET(execute_unclipped_draw_vs_on_cpu));
  if (ufc_us.allow && ufc_key_complete && ufc_us.have && ufc_us.key_gen == ufc_us.gen &&
      are_accumulated_render_targets_valid_ &&
      !std::memcmp(ufc_key, ufc_us.key, sizeof(ufc_key))) {
    if (!ufc_us.verify) {
      ufc_us.skipped = true;
      ++ufc_us.skips;
      return true;
    }
    ufc_us.verify_pending = true;
    for (uint32_t i = 0; i < 5; ++i) {
      ufc_us.snap_used[i] = last_update_used_render_targets_[i];
      ufc_us.snap_acc[i] = last_update_accumulated_render_targets_[i];
    }
  }
  ufc_us.have = false;
  ufc_us.cand_ok = false;
  ++ufc_us.fulls;
'''

END_OLD = '''  if (!are_accumulated_render_targets_valid_) {
    std::memcpy(last_update_accumulated_render_targets_, last_update_used_render_targets_,
                sizeof(last_update_accumulated_render_targets_));
    are_accumulated_render_targets_valid_ = true;
  }

  return true;
}

uint32_t RenderTargetCache::GetLastUpdateBoundRenderTargets('''

END_NEW = r'''  if (!are_accumulated_render_targets_valid_) {
    std::memcpy(last_update_accumulated_render_targets_, last_update_used_render_targets_,
                sizeof(last_update_accumulated_render_targets_));
    are_accumulated_render_targets_valid_ = true;
  }

  // UFC3MONS B6: candidato a ser pulado no proximo desenho se nao houve transferencia.
  {
    bool no_transfers = true;
    for (size_t i = 0; i < rex::countof(last_update_transfers_); ++i)
      no_transfers &= last_update_transfers_[i].empty();
    ufc_us.cand_ok = ufc_key_complete && no_transfers;
    std::memcpy(ufc_us.cand_key, ufc_key, sizeof(ufc_key));
    if (ufc_us.verify_pending) {
      bool same = no_transfers;
      for (uint32_t i = 0; i < 5; ++i) {
        same &= ufc_us.snap_used[i] == last_update_used_render_targets_[i];
        same &= ufc_us.snap_acc[i] == last_update_accumulated_render_targets_[i];
      }
      if (!same) ++ufc_us.mismatches;
    }
  }

  return true;
}

uint32_t RenderTargetCache::GetLastUpdateBoundRenderTargets('''

go("rt_cache.cpp", "rt_cache_b6.cpp", [
    ("  bool interlock_barrier_only = GetPath() == Path::kPixelShaderInterlock;\n",
     "  bool interlock_barrier_only = GetPath() == Path::kPixelShaderInterlock;\n" + KEY, 1),
    (END_OLD, END_NEW, 1),
    ("      // Claim the current range.\n      it->second.render_target = dest;",
     "      // Claim the current range.\n      ufc_rt_skip::Bump();\n      it->second.render_target = dest;", 1),
    ("        // Different render target overlapping the range - split the head.\n        ownership_ranges_.emplace(extent_start, it_pre->second);",
     "        // Different render target overlapping the range - split the head.\n        ufc_rt_skip::Bump();\n        ownership_ranges_.emplace(extent_start, it_pre->second);", 1),
    ("    render_target = CreateRenderTarget(key);\n",
     "    render_target = CreateRenderTarget(key);\n    ufc_rt_skip::Bump();\n", 1),
    ("void RenderTargetCache::DestroyAllRenderTargets(bool shutting_down) {\n",
     "void RenderTargetCache::DestroyAllRenderTargets(bool shutting_down) {\n  ufc_rt_skip::Bump();\n", 1),
    ("void RenderTargetCache::ClearCache() {\n",
     "void RenderTargetCache::ClearCache() {\n  ufc_rt_skip::Bump();\n", 1),
    ("void RenderTargetCache::PixelShaderInterlockFullEdramBarrierPlaced() {\n",
     "void RenderTargetCache::PixelShaderInterlockFullEdramBarrierPlaced() {\n  ufc_rt_skip::Bump();\n", 1),
])

CVARS_OLD = 'REXCVAR_DEFINE_BOOL(ufc_xfer_diag, false, "GPU", "Diagnostic: log draws that need EDRAM transfers");'
CVARS_NEW = CVARS_OLD + r'''
REXCVAR_DEFINE_BOOL(ufc_rt_update_skip, true, "GPU",
                    "B6: skip render target preparation when nothing changed since the last draw");
REXCVAR_DEFINE_BOOL(ufc_rt_update_skip_verify, false, "GPU",
                    "B6 test: run the full preparation anyway and compare with the skip decision");
REXCVAR_DEFINE_BOOL(ufc_rt_update_skip_verify_break, false, "GPU",
                    "B6 positive control: perturb the reference of the verifier");
REXCVAR_DEFINE_BOOL(ufc_rt_update_skip_log, false, "GPU", "B6: log [ufc-rtskip] counters");'''

VK_OLD = '''                                     uint32_t normalized_color_mask, const Shader& vertex_shader) {
  if (!RenderTargetCache::Update(is_rasterization_done, normalized_depth_control,
                                 normalized_color_mask, vertex_shader)) {
    return false;
  }
'''
VK_NEW = r'''                                     uint32_t normalized_color_mask, const Shader& vertex_shader) {
  // UFC3MONS B6: a parte Vulkan so pode ser pulada sem recorte pendente nem diagnosticos ligados.
  auto& ufc_us = ufc_rt_skip::g();
  {
    static uint64_t ufc_log_n = 0;
    if (REXCVAR_GET(ufc_rt_update_skip_log) && (++ufc_log_n % 200000) == 0) {
      REXGPU_WARN("[ufc-rtskip] on={} verify={} break={} skips={} fulls={} compared={} mismatches={}",
                  REXCVAR_GET(ufc_rt_update_skip), ufc_us.verify, ufc_us.verify_break,
                  ufc_us.skips, ufc_us.fulls, ufc_us.compared, ufc_us.mismatches);
    }
  }
  ufc_us.verify = REXCVAR_GET(ufc_rt_update_skip_verify);
  ufc_us.verify_break = REXCVAR_GET(ufc_rt_update_skip_verify_break);
  ufc_us.verify_pending = false;
  ufc_us.allow = REXCVAR_GET(ufc_rt_update_skip) && GetPath() == Path::kHostRenderTargets &&
                 !REXCVAR_GET(ufc_xfer_diag) && !REXCVAR_GET(ufc_clear_cutout_log) &&
                 !REXCVAR_GET(ufc_clear_cutout_sentinel) &&
                 !REXCVAR_GET(ufc_clear_cutout_sentinel_break) && !ufc_pending_active_ &&
                 !ufc_break_active_ && last_update_framebuffer_ != nullptr &&
                 last_update_render_pass_ != VK_NULL_HANDLE;
  if (ufc_us.allow) {
    ufc_us.snap_fb = last_update_framebuffer_;
    ufc_us.snap_rp = reinterpret_cast<const void*>(last_update_render_pass_);
    ufc_us.snap_rpkey = uint64_t(last_update_render_pass_key_.key);
  }
  if (!RenderTargetCache::Update(is_rasterization_done, normalized_depth_control,
                                 normalized_color_mask, vertex_shader)) {
    ufc_us.have = false;
    return false;
  }
  if (ufc_us.skipped) {
    // Mesmo resultado do desenho anterior: so as transicoes de layout (resolves podem ter mudado).
    RenderTarget* const* rts = last_update_accumulated_render_targets();
    for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      RenderTarget* rt = rts[i];
      if (!rt) {
        continue;
      }
      auto& vulkan_rt = *static_cast<VulkanRenderTarget*>(rt);
      VkPipelineStageFlags rt_dst_stage_mask;
      VkAccessFlags rt_dst_access_mask;
      VkImageLayout rt_new_layout;
      VulkanRenderTarget::GetDrawUsage(i == 0, &rt_dst_stage_mask, &rt_dst_access_mask,
                                       &rt_new_layout);
      command_processor_.PushImageMemoryBarrier(
          vulkan_rt.image(),
          ui::vulkan::util::InitializeSubresourceRange(
              i ? VK_IMAGE_ASPECT_COLOR_BIT
                : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)),
          vulkan_rt.current_stage_mask(), rt_dst_stage_mask, vulkan_rt.current_access_mask(),
          rt_dst_access_mask, vulkan_rt.current_layout(), rt_new_layout);
      vulkan_rt.SetUsage(rt_dst_stage_mask, rt_dst_access_mask, rt_new_layout);
    }
    return true;
  }
  ufc_us.have = false;
'''

FB_OLD = "      last_update_framebuffer_ = framebuffer;\n"
FB_NEW = FB_OLD + r'''      // UFC3MONS B6: guardar para o proximo desenho; no modo de prova, comparar com o resultado completo.
      if (ufc_us.verify_pending) {
        const void* ref_fb = ufc_us.snap_fb;
        if (ufc_us.verify_break) ref_fb = reinterpret_cast<const void*>(uintptr_t(ref_fb) ^ 0x10);
        bool same = ref_fb == static_cast<const void*>(last_update_framebuffer_) &&
                    ufc_us.snap_rp == reinterpret_cast<const void*>(last_update_render_pass_) &&
                    ufc_us.snap_rpkey == uint64_t(last_update_render_pass_key_.key) &&
                    !ufc_pending_active_;
        for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i)
          same &= last_update_transfers()[i].empty();
        ++ufc_us.compared;
        if (!same) ++ufc_us.mismatches;
        ufc_us.verify_pending = false;
      }
      ufc_us.have = ufc_us.cand_ok;
      if (ufc_us.have) {
        std::memcpy(ufc_us.key, ufc_us.cand_key, sizeof(ufc_us.key));
        ufc_us.key_gen = ufc_us.gen;
      }
'''

go("vulkan_render_target_cache.cpp", "vulkan_render_target_cache_b6.cpp", [
    (CVARS_OLD, CVARS_NEW, 1),
    (VK_OLD, VK_NEW, 1),
    (FB_OLD, FB_NEW, 1),
    ("  last_update_framebuffer_ = VK_NULL_HANDLE;\n",
     "  last_update_framebuffer_ = VK_NULL_HANDLE;\n  ufc_rt_skip::Bump();\n", 3),
])
print("b6 ok")

# Linux: mesma mudanca na copia Linux do cache de alvos Vulkan (nao tem ufc_xfer_diag).
CV_LX_OLD = 'REXCVAR_DEFINE_BOOL(ufc_clear_cutout_log, false, "GPU", "Log [ufc-cutout] counters every 10 s");'
CV_LX_NEW = CV_LX_OLD + CVARS_NEW[len(CVARS_OLD):]
go("vulkan_render_target_cache.linux.cpp", "vulkan_render_target_cache_linux_b6.cpp", [
    (CV_LX_OLD, CV_LX_NEW, 1),
    (VK_OLD, VK_NEW.replace("                 !REXCVAR_GET(ufc_xfer_diag) && !REXCVAR_GET(ufc_clear_cutout_log) &&",
                            "                 !REXCVAR_GET(ufc_clear_cutout_log) &&"), 1),
    (FB_OLD, FB_NEW, 1),
    ("  last_update_framebuffer_ = VK_NULL_HANDLE;\n",
     "  last_update_framebuffer_ = VK_NULL_HANDLE;\n  ufc_rt_skip::Bump();\n", 3),
])
print("b6 linux ok")
