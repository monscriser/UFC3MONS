#!/usr/bin/env python3
"""UFC3MONS GPU: clear-rectangle cutout for EDRAM ownership transfers (port of Hydro Thunder opt. 4,
ht_clear_transfer_cutout, own code).

The game clears depth+stencil (and often color 0) with the D3D runtime's screen-space rectangle
(VS 1E6883FCCDE1F688, depthcontrol 0x8777: z ALWAYS + write, stencil ALWAYS/REPLACE, write mask FF).
Before each clear the engine copied the previous EDRAM owner into the whole target (depth + stencil
bit by bit = 9 full-target draws on GPUs without stencil export). ~51% of the transferred tiles in a
2x fight. Now the transfers skip the area the clear overwrites, with the engine's own transfer
cutout (the one resolve clears use). Stricter than Hydro (no stencil loss):
- depth slot only when the clear also writes the whole stencil (ALWAYS, zpass REPLACE, mask FF,
  both faces when back-face stencil is on);
- color slot only with mask 0xF and blend ONE/ZERO for color and alpha;
- rectangle edges must land on whole guest pixels after the half-pixel offset (no rasterization
  tie, the same coverage for every host pixel and MSAA sample at any resolution scale), w = 1 and
  0 <= z <= 1 on the three vertices, intersected with the draw scissor;
- no pixel kill / depth export / alpha test / alpha to coverage, all 4 AA samples, solid fill.
Cvars: ufc_clear_transfer_cutout (default true), test only: ufc_clear_cutout_sentinel (junk in the
skipped area before the clear: must never show), ufc_clear_cutout_sentinel_break (positive control:
junk over the cleared area AFTER the clear: must show). Stats [ufc-cutout] every 10 s with
ufc_clear_cutout_log=true (default false) or any of the test cvars.
Applied to vulkan_render_target_cache.cpp, .linux.cpp, the Vulkan RTC header and the command
processor. Backups in backup-originais/antes-xfer-0411/."""
import os, sys

W = os.path.dirname(os.path.abspath(__file__))


def patch(name, edits):
    p = os.path.join(W, name)
    s = open(p, "rb").read().decode()
    nl = "\r\n" if "\r\n" in s else "\n"
    s = s.replace("\r\n", "\n")
    for old, new in edits:
        n = s.count(old)
        if n != 1:
            sys.exit(f"{name}: expected 1 match, found {n}:\n{old[:300]}")
        s = s.replace(old, new)
    open(p, "wb").write(s.replace("\n", nl).encode())
    print("patched", name)


HDR = [("""  uint64_t ufc_native_resolves_ = 0;
};""", """  uint64_t ufc_native_resolves_ = 0;

  // UFC3MONS ufc_clear_transfer_cutout (state is file-static in the .cpp, one cache per process).
  bool UfcClearSlots(reg::RB_DEPTHCONTROL normalized_depth_control, uint32_t normalized_color_mask,
                     const Shader& vertex_shader, RenderTarget* const* rts, uint32_t& slots_out,
                     Transfer::Rectangle& rect_out);
  void UfcClearCutoutTransfers(RenderTarget* const* rts,
                               reg::RB_DEPTHCONTROL normalized_depth_control,
                               uint32_t normalized_color_mask, const Shader& vertex_shader);
  void UfcPerformCutTransfers(RenderTarget* const* rts, std::vector<Transfer>* transfers,
                              const Transfer::Rectangle& cut, const Transfer::Rectangle* fill);
  void UfcFlushPendingClear();
  bool ufc_pending_active_ = false;
  uint32_t ufc_pending_slots_ = 0;
  RenderTarget* ufc_pending_rts_[1 + xenos::kMaxColorRenderTargets] = {};
  std::vector<Transfer> ufc_pending_transfers_[1 + xenos::kMaxColorRenderTargets];
  Transfer::Rectangle ufc_pending_rect_ = {};
  // Positive control: area cleared by the last accepted clear, junk-filled before the next draw.
  bool ufc_break_active_ = false;
  RenderTarget* ufc_break_rts_[1 + xenos::kMaxColorRenderTargets] = {};
  Transfer::Rectangle ufc_break_rect_ = {};
};""")]

CODE = r'''
// ---------------------------------------------------------------------------------------------
// UFC3MONS ufc_clear_transfer_cutout (see apply_clear_cutout.py).
namespace {
// 0 joined pair, 1 strip alone, 2 single, 3 tiles skipped (approx.), 4 tiles in, 5.. rejections.
enum : uint32_t {
  kUfcCutJoined, kUfcCutStripAlone, kUfcCutSingle, kUfcCutTilesCut, kUfcCutTilesIn,
  kUfcRejNotRect, kUfcRejState, kUfcRejNoSlot, kUfcRejStencil, kUfcRejVs, kUfcRejZw,
  kUfcRejEdge, kUfcRejEmpty, kUfcCutCount
};
uint64_t ufc_cut_stats[kUfcCutCount] = {};

class UfcClearRectSink : public ShaderInterpreter::ExportSink {
 public:
  void Export(ucode::ExportRegister export_register, const float* value,
              uint32_t value_mask) override {
    if (export_register == ucode::ExportRegister::kVSPosition) {
      for (uint32_t i = 0; i < 4; ++i) {
        if (value_mask & (1u << i)) {
          position[i] = value[i];
          mask |= 1u << i;
        }
      }
    } else if (export_register == ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex) {
      if ((value_mask & 0b0100) && rex::memory::Reinterpret<uint32_t>(value[2])) {
        killed = true;
      }
    }
  }
  float position[4] = {};
  uint32_t mask = 0;
  bool killed = false;
};

bool UfcIsWhole(float v) { return std::isfinite(v) && v == std::floor(v); }
}  // namespace

// Set by the command processor before Update: the pixel shader neither kills pixels nor writes
// depth (no pixel shader also counts).
bool ufc_clear_ps_ok = false;

bool VulkanRenderTargetCache::UfcClearSlots(reg::RB_DEPTHCONTROL normalized_depth_control,
                                            uint32_t normalized_color_mask,
                                            const Shader& vertex_shader, RenderTarget* const* rts,
                                            uint32_t& slots_out, Transfer::Rectangle& rect_out) {
  const RegisterFile& regs = register_file();
  auto initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (initiator.prim_type != xenos::PrimitiveType::kRectangleList ||
      initiator.source_select != xenos::SourceSelect::kAutoIndex || initiator.num_indices != 3) {
    ++ufc_cut_stats[kUfcRejNotRect];
    return false;
  }
  auto vte = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto mode = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  if (!ufc_clear_ps_ok || !vte.vtx_xy_fmt || colorcontrol.alpha_to_mask_enable ||
      (colorcontrol.alpha_test_enable &&
       colorcontrol.alpha_func != xenos::CompareFunction::kAlways) ||
      mode.cull_front || mode.cull_back ||
      mode.poly_mode != xenos::PolygonModeEnable::kDisabled ||
      (regs[XE_GPU_REG_PA_SC_AA_MASK] & 0xF) != 0xF) {
    ++ufc_cut_stats[kUfcRejState];
    return false;
  }
  uint32_t slots = 0;
  bool depth_candidate = false;
  if (rts[0] && normalized_depth_control.z_enable && normalized_depth_control.z_write_enable &&
      normalized_depth_control.zfunc == xenos::CompareFunction::kAlways) {
    depth_candidate = true;
    // The whole stencil must be replaced too - otherwise the old stencil is still needed.
    auto srm = regs.Get<reg::RB_STENCILREFMASK>();
    auto srm_bf = regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);
    bool stencil_ok = normalized_depth_control.stencil_enable &&
                      normalized_depth_control.stencilfunc == xenos::CompareFunction::kAlways &&
                      normalized_depth_control.stencilzpass == xenos::StencilOp::kReplace &&
                      srm.stencilwritemask == 0xFF;
    if (stencil_ok && normalized_depth_control.backface_enable) {
      stencil_ok = normalized_depth_control.stencilfunc_bf == xenos::CompareFunction::kAlways &&
                   normalized_depth_control.stencilzpass_bf == xenos::StencilOp::kReplace &&
                   srm_bf.stencilwritemask == 0xFF;
    }
    if (stencil_ok) {
      slots |= 1;
    }
  }
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    const uint32_t blend = regs[reg::RB_BLENDCONTROL::rt_register_indices[i]];
    // All channels written with ONE * source + ZERO * destination.
    if (rts[1 + i] && ((normalized_color_mask >> (i * 4)) & 0xF) == 0xF &&
        (blend & 0x1FFF) == 0x0001 && ((blend >> 16) & 0x1FFF) == 0x0001) {
      slots |= 1u << (1 + i);
    }
  }
  if (!slots) {
    ++ufc_cut_stats[depth_candidate ? kUfcRejStencil : kUfcRejNoSlot];
    return false;
  }
  if (!ufc_rtc_memory || !vertex_shader.is_ucode_analyzed() ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    ++ufc_cut_stats[kUfcRejVs];
    return false;
  }
  static ShaderInterpreter* interpreter = new ShaderInterpreter(regs, *ufc_rtc_memory);
  const float scale[2] = {
      vte.vport_x_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XSCALE) : 1.0f,
      vte.vport_y_scale_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE) : 1.0f};
  float offset[2] = {
      vte.vport_x_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_XOFFSET) : 0.0f,
      vte.vport_y_offset_ena ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET) : 0.0f};
  if (mode.vtx_window_offset_enable) {
    auto window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    offset[0] += float(window_offset.window_x_offset);
    offset[1] += float(window_offset.window_y_offset);
  }
  if (REXCVAR_GET(half_pixel_offset) &&
      regs.Get<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) {
    offset[0] += 0.5f;
    offset[1] += 0.5f;
  }
  const uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  const uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  const uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  interpreter->SetShader(vertex_shader);
  float lo[2] = {FLT_MAX, FLT_MAX}, hi[2] = {-FLT_MAX, -FLT_MAX};
  for (uint32_t i = 0; i < 3; ++i) {
    UfcClearRectSink sink;
    interpreter->SetExportSink(&sink);
    interpreter->temp_registers()[0] =
        float(std::min(max_index, std::max(min_index, (i + index_offset) & 0xFFFFFF)));
    interpreter->Execute();
    interpreter->SetExportSink(nullptr);
    if (sink.killed || sink.mask != 0xF) {
      ++ufc_cut_stats[kUfcRejVs];
      return false;
    }
    // w = 1 and 0 <= z <= 1: identical for every z/w format and never clipped.
    if (sink.position[3] != 1.0f || !(sink.position[2] >= 0.0f && sink.position[2] <= 1.0f)) {
      ++ufc_cut_stats[kUfcRejZw];
      return false;
    }
    for (uint32_t a = 0; a < 2; ++a) {
      const float v = sink.position[a] * scale[a] + offset[a];
      lo[a] = std::min(lo[a], v);
      hi[a] = std::max(hi[a], v);
    }
  }
  // Whole guest pixel edges: no rasterization tie, the same coverage for all host pixels and
  // samples of a guest pixel at any resolution scale.
  if (!UfcIsWhole(lo[0]) || !UfcIsWhole(lo[1]) || !UfcIsWhole(hi[0]) || !UfcIsWhole(hi[1])) {
    ++ufc_cut_stats[kUfcRejEdge];
    return false;
  }
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  const double x0 = std::max(double(lo[0]), double(scissor.offset[0]));
  const double y0 = std::max(double(lo[1]), double(scissor.offset[1]));
  const double x1 = std::min(double(hi[0]), double(scissor.offset[0]) + scissor.extent[0]);
  const double y1 = std::min(double(hi[1]), double(scissor.offset[1]) + scissor.extent[1]);
  if (x1 <= x0 || y1 <= y0 || x0 < 0.0 || y0 < 0.0) {
    ++ufc_cut_stats[kUfcRejEmpty];
    return false;
  }
  rect_out.x_pixels = uint32_t(x0);
  rect_out.y_pixels = uint32_t(y0);
  rect_out.width_pixels = uint32_t(x1 - x0);
  rect_out.height_pixels = uint32_t(y1 - y0);
  slots_out = slots;
  return true;
}

void VulkanRenderTargetCache::UfcPerformCutTransfers(RenderTarget* const* rts,
                                                     std::vector<Transfer>* transfers,
                                                     const Transfer::Rectangle& cut,
                                                     const Transfer::Rectangle* fill) {
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    if (!rts[i]) {
      transfers[i].clear();
      continue;
    }
    const RenderTargetKey key = rts[i]->key();
    auto& list = transfers[i];
    for (const Transfer& t : list) ufc_cut_stats[kUfcCutTilesIn] += t.end_tiles - t.start_tiles;
    // Transfers fully inside the cut area need no draw at all.
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Transfer& t) {
                                Transfer::Rectangle rects[Transfer::kMaxRectanglesWithCutout];
                                return t.GetRectangles(key.base_tiles, key.pitch_tiles_at_32bpp,
                                                       key.msaa_samples, key.Is64bpp(), rects,
                                                       &cut) == 0;
                              }),
               list.end());
  }
  PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers, nullptr,
                                   &cut);
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) transfers[i].clear();
  if (fill && REXCVAR_GET(ufc_clear_cutout_sentinel)) {
    // Proof: junk in the part of the skipped area the game has not drawn yet. The game's clear
    // must overwrite all of it. Depth 0.5 (24-bit depth << 8 | stencil 0x5A), magenta color.
    uint64_t sentinel[1 + xenos::kMaxColorRenderTargets] = {UINT64_C(0x8000005A)};
    for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      if (rts[i]) {
        sentinel[i] = rts[i]->key().GetColorFormat() == xenos::ColorRenderTargetFormat::k_32_FLOAT
                          ? UINT64_C(0x7F7FFFFF)
                          : UINT64_C(0xFFFF00FFFFFF00FF);
      }
    }
    PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, rts, transfers, sentinel,
                                     fill);
  }
}

void VulkanRenderTargetCache::UfcFlushPendingClear() {
  if (!ufc_pending_active_) {
    return;
  }
  ufc_pending_active_ = false;
  ++ufc_cut_stats[kUfcCutStripAlone];
  // The strip is already drawn: nothing left to fill.
  UfcPerformCutTransfers(ufc_pending_rts_, ufc_pending_transfers_, ufc_pending_rect_, nullptr);
}

void VulkanRenderTargetCache::UfcClearCutoutTransfers(
    RenderTarget* const* rts, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, const Shader& vertex_shader) {
  const bool test_mode =
      REXCVAR_GET(ufc_clear_cutout_sentinel) || REXCVAR_GET(ufc_clear_cutout_sentinel_break);
  if (REXCVAR_GET(ufc_clear_cutout_log) || test_mode) {
    static auto t0 = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - t0 >= std::chrono::seconds(10)) {
      t0 = now;
      const uint64_t* c = ufc_cut_stats;
      REXGPU_WARN(
          "[ufc-cutout] joined={} strip_alone={} single={} tiles_in={} rej_not_rect={} "
          "rej_state={} rej_no_slot={} rej_stencil={} rej_vs={} rej_zw={} rej_edge={} "
          "rej_empty={} sentinel={} break={}",
          c[kUfcCutJoined], c[kUfcCutStripAlone], c[kUfcCutSingle], c[kUfcCutTilesIn],
          c[kUfcRejNotRect], c[kUfcRejState], c[kUfcRejNoSlot], c[kUfcRejStencil], c[kUfcRejVs],
          c[kUfcRejZw], c[kUfcRejEdge], c[kUfcRejEmpty], REXCVAR_GET(ufc_clear_cutout_sentinel),
          REXCVAR_GET(ufc_clear_cutout_sentinel_break));
    }
  }
  std::vector<Transfer>* transfers = const_cast<std::vector<Transfer>*>(last_update_transfers());
  if (ufc_break_active_) {
    // Positive control: the previous clear is drawn - overwrite what it cleared with junk.
    ufc_break_active_ = false;
    std::vector<Transfer> none[1 + xenos::kMaxColorRenderTargets];
    uint64_t junk[1 + xenos::kMaxColorRenderTargets] = {UINT64_C(0x8000005A)};
    for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      junk[i] = UINT64_C(0xFFFF00FFFFFF00FF);
    }
    PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets, ufc_break_rts_, none, junk,
                                     &ufc_break_rect_);
  }
  if (!REXCVAR_GET(ufc_clear_transfer_cutout)) {
    return;
  }
  uint32_t slots = 0;
  Transfer::Rectangle rect = {};
  bool any_transfer = false;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    any_transfer |= !transfers[i].empty();
  }
  // Only evaluate the vertex shader when there is something to save or a strip to join.
  const bool is_clear = (any_transfer || ufc_pending_active_) &&
                        UfcClearSlots(normalized_depth_control, normalized_color_mask,
                                      vertex_shader, rts, slots, rect);
  if (ufc_pending_active_) {
    // Second half of a split clear: same targets, same rows, touching columns.
    const Transfer::Rectangle& p = ufc_pending_rect_;
    bool same = is_clear && slots == ufc_pending_slots_;
    for (uint32_t i = 0; same && i < 1 + xenos::kMaxColorRenderTargets; ++i) {
      same = !(slots & (1u << i)) || rts[i] == ufc_pending_rts_[i];
    }
    if (same && rect.y_pixels == p.y_pixels && rect.height_pixels == p.height_pixels &&
        rect.x_pixels <= p.x_pixels + p.width_pixels &&
        rect.x_pixels + rect.width_pixels >= p.x_pixels) {
      const uint32_t x0 = std::min(rect.x_pixels, p.x_pixels);
      const uint32_t x1 =
          std::max(rect.x_pixels + rect.width_pixels, p.x_pixels + p.width_pixels);
      ufc_pending_rect_.x_pixels = x0;
      ufc_pending_rect_.width_pixels = x1 - x0;
      ufc_pending_active_ = false;
      ++ufc_cut_stats[kUfcCutJoined];
      // Transfers of this draw for the pending targets are newer ownership changes of the same
      // area - merge them into the cut instead of dropping them.
      for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        if (slots & (1u << i)) {
          ufc_pending_transfers_[i].insert(ufc_pending_transfers_[i].end(), transfers[i].begin(),
                                          transfers[i].end());
          transfers[i].clear();
        }
      }
      UfcPerformCutTransfers(ufc_pending_rts_, ufc_pending_transfers_, ufc_pending_rect_, &rect);
      if (REXCVAR_GET(ufc_clear_cutout_sentinel_break)) {
        ufc_break_active_ = true;
        std::copy(std::begin(ufc_pending_rts_), std::end(ufc_pending_rts_), ufc_break_rts_);
        ufc_break_rect_ = ufc_pending_rect_;
      }
      return;
    }
    UfcFlushPendingClear();
  }
  if (!is_clear) {
    return;
  }
  bool any = false;
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    any |= (slots & (1u << i)) && !transfers[i].empty();
  }
  if (!any) {
    return;
  }
  for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    ufc_pending_transfers_[i].clear();
    ufc_pending_rts_[i] = nullptr;
    if (slots & (1u << i)) {
      ufc_pending_rts_[i] = rts[i];
      ufc_pending_transfers_[i].swap(transfers[i]);
    }
  }
  ufc_pending_slots_ = slots;
  ufc_pending_rect_ = rect;
  if (rect.x_pixels == 0) {
    // Starts at the left edge: nothing to join, cut out now.
    ++ufc_cut_stats[kUfcCutSingle];
    UfcPerformCutTransfers(ufc_pending_rts_, ufc_pending_transfers_, ufc_pending_rect_, &rect);
    if (REXCVAR_GET(ufc_clear_cutout_sentinel_break)) {
      ufc_break_active_ = true;
      std::copy(std::begin(ufc_pending_rts_), std::end(ufc_pending_rts_), ufc_break_rts_);
      ufc_break_rect_ = rect;
    }
  } else {
    ufc_pending_active_ = true;
  }
}

'''

CPP = [
    ('REXCVAR_DEFINE_STRING(render_target_path_vulkan, "", "GPU/Vulkan",',
     '#include <chrono>\n#include <cfloat>\n#include <cmath>\n'
     '#include <rex/graphics/pipeline/shader/interpreter.h>\n'
     'REXCVAR_DEFINE_BOOL(ufc_clear_transfer_cutout, true, "GPU",\n'
     '                    "Do not copy old EDRAM data into the area a clear rectangle overwrites");\n'
     'REXCVAR_DEFINE_BOOL(ufc_clear_cutout_log, false, "GPU", "Log [ufc-cutout] counters every 10 s");\n'
     'REXCVAR_DEFINE_BOOL(ufc_clear_cutout_sentinel, false, "GPU",\n'
     '                    "Test only: fill the skipped area with junk (must never be visible)");\n'
     'REXCVAR_DEFINE_BOOL(ufc_clear_cutout_sentinel_break, false, "GPU",\n'
     '                    "Test only (positive control): junk over the cleared area after the clear");\n'
     'namespace { const rex::memory::Memory* ufc_rtc_memory = nullptr; }\n'
     'REXCVAR_DEFINE_STRING(render_target_path_vulkan, "", "GPU/Vulkan",'),
    ("""    : RenderTargetCache(register_file, memory, draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor) {}
""", """    : RenderTargetCache(register_file, memory, draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor) {
  ufc_rtc_memory = &memory;
}
"""),
    ("""      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
""", """      UfcClearCutoutTransfers(depth_and_color_render_targets, normalized_depth_control,
                              normalized_color_mask, vertex_shader);
      PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                       depth_and_color_render_targets, last_update_transfers());
"""),
    ("""  written_address_out = 0;
  written_length_out = 0;
  if (written_scaled_out) {
    *written_scaled_out = false;
  }
""", """  written_address_out = 0;
  written_length_out = 0;
  if (written_scaled_out) {
    *written_scaled_out = false;
  }

  UfcFlushPendingClear();
"""),
    ("void VulkanRenderTargetCache::ClearCache() {\n",
     CODE + "void VulkanRenderTargetCache::ClearCache() {\n"
     "  ufc_pending_active_ = false;\n  ufc_break_active_ = false;\n"
     "  for (auto& transfers : ufc_pending_transfers_) transfers.clear();\n"),
]

CP = [("""    if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                      normalized_color_mask, *vertex_shader)) {""",
       """    // UFC3MONS ufc_clear_transfer_cutout: the pixel shader cannot leave covered pixels unwritten.
    ufc_clear_ps_ok =
        !pixel_shader || (!pixel_shader->kills_pixels() && !pixel_shader->writes_depth());
    if (!render_target_cache_->Update(is_rasterization_done, normalized_depth_control,
                                      normalized_color_mask, *vertex_shader)) {"""),
      ("namespace rex::graphics::vulkan {\n",
       "namespace rex::graphics::vulkan {\nextern bool ufc_clear_ps_ok;  // vulkan_render_target_cache.cpp\n")]

if __name__ == "__main__":
    patch("inc/rex/graphics/vulkan/render_target_cache.h", HDR)
    patch("vulkan_render_target_cache.cpp", CPP)
    patch("vulkan_render_target_cache.linux.cpp", CPP)
    patch("vulkan_command_processor.cpp", CP)
