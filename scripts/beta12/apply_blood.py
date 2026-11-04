#!/usr/bin/env python3
"""UFC3MONS blood fix (U4): port of Xenia Canary 74db632 "draw_resolution_scale_threshold for host
render targets" (BSD, Xenia project) onto the UFC3MONS GPU pieces. Surfaces whose tile-aligned pitch is
<= ufc_draw_resolution_scale_threshold render at 1x1 while everything else keeps the global scale.

Reads pristine copies from orig/ and writes the patched files next to this script (and inc/ for
headers). Every replacement must match exactly once, otherwise the script stops.
Not ported: the ZPD per-segment scale (UFC does not use occlusion queries: occlusion_n=0 in logs)."""
import os, sys

W = os.path.dirname(os.path.abspath(__file__))


def patch(src, dst, edits):
    s = open(os.path.join(W, "orig", src), "rb").read().decode()
    nl = "\r\n" if "\r\n" in s else "\n"
    s = s.replace("\r\n", "\n")
    for old, new in edits:
        n = s.count(old)
        if n != 1:
            sys.exit(f"{src}: expected 1 match, found {n}:\n{old[:200]}")
        s = s.replace(old, new)
    out = os.path.join(W, dst)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(s.replace("\n", nl).encode())
    print("patched", dst)


# ---------------------------------------------------------------- base render target cache header
patch("rt_cache.h", "inc/rex/graphics/pipeline/render_target/cache.h", [
    ("""  bool IsDrawResolutionScaled() const {
    return draw_resolution_scale_x() > 1 || draw_resolution_scale_y() > 1;
  }
""", """  bool IsDrawResolutionScaled() const {
    return draw_resolution_scale_x() > 1 || draw_resolution_scale_y() > 1;
  }

  // UFC3MONS U4 (Canary 74db632): whether surfaces with this pitch render native per the scale
  // threshold. Compares the tile-aligned width, so it's a pure function of key fields.
  bool IsScaleNativeForPitch(uint32_t pitch_tiles_at_32bpp, xenos::MsaaSamples msaa_samples) const;
  // Same for RB_SURFACE_INFO.
  bool IsDrawScaleNative() const;
  // Scale of the current draw, the global scale or 1x1 under the threshold. Everything per-draw
  // must use these so a draw never mixes scales.
  uint32_t GetDrawScaleX() const { return IsDrawScaleNative() ? 1 : draw_resolution_scale_x(); }
  uint32_t GetDrawScaleY() const { return IsDrawScaleNative() ? 1 : draw_resolution_scale_y(); }
"""),
    ("""      uint32_t resource_format : xenos::kRenderTargetFormatBits;  // 26
""", """      uint32_t resource_format : xenos::kRenderTargetFormatBits;  // 26
      // UFC3MONS U4: 1 if this render target is kept at native guest resolution because of
      // ufc_draw_resolution_scale_threshold. Native and scaled targets at the same base are
      // distinct objects and ownership can be transferred.
      uint32_t scale_native : 1;  // 27
"""),
    ("""      return fmt::format("RT @ {}t, <{}t>, {}xMSAA, {}", base_tiles, GetPitchTiles(),
                         uint32_t(1) << uint32_t(msaa_samples), GetFormatName());
    }
  };
""", """      return fmt::format("RT @ {}t, <{}t>, {}xMSAA, {}{}", base_tiles, GetPitchTiles(),
                         uint32_t(1) << uint32_t(msaa_samples), GetFormatName(),
                         scale_native ? ", native" : "");
    }
  };

  // UFC3MONS U4: the scale this render target is actually created and drawn at.
  uint32_t GetKeyScaleX(RenderTargetKey key) const {
    return key.scale_native ? 1 : draw_resolution_scale_x();
  }
  uint32_t GetKeyScaleY(RenderTargetKey key) const {
    return key.scale_native ? 1 : draw_resolution_scale_y();
  }
"""),
    ("""  void GetResolveCopyRectanglesToDump(uint32_t base, uint32_t row_length, uint32_t rows,
                                      uint32_t pitch,
                                      std::vector<ResolveCopyDumpRectangle>& rectangles_out) const;
""", """  void GetResolveCopyRectanglesToDump(uint32_t base, uint32_t row_length, uint32_t rows,
                                      uint32_t pitch,
                                      std::vector<ResolveCopyDumpRectangle>& rectangles_out) const;
  // UFC3MONS U4: true if everything owning the resolve source region is native scale, in which
  // case the resolve is done at 1x1 into shared memory instead of duplicating pixels into the
  // scaled layout. Decided from actual ownership since one range can span both classes.
  bool IsResolveSourceNativeOnly(uint32_t base, uint32_t row_length, uint32_t rows,
                                 uint32_t pitch) const;
"""),
])

# ---------------------------------------------------------------- base render target cache
patch("rt_cache.cpp", "rt_cache.cpp", [
    ("""REXCVAR_DEFINE_BOOL(direct_host_resolve, true, "GPU",""",
     """REXCVAR_DEFINE_UINT32(ufc_draw_resolution_scale_threshold, 320, "GPU",
                      "UFC3MONS: surface pitch in pixels at or below which render targets are not "
                      "upscaled by draw_resolution_scale (small effect buffers such as the blood "
                      "on the canvas break when upscaled). 0 disables it. Default 320 (U4: fixes the sliding blood at 2x). Host render targets only.");
REXCVAR_DEFINE_BOOL(direct_host_resolve, true, "GPU","""),
    ("""void RenderTargetCache::BeginFrame() {
  ResetAccumulatedRenderTargets();
}
""", """void RenderTargetCache::BeginFrame() {
  ResetAccumulatedRenderTargets();
}

bool RenderTargetCache::IsScaleNativeForPitch(uint32_t pitch_tiles_at_32bpp,
                                              xenos::MsaaSamples msaa_samples) const {
  uint32_t threshold = REXCVAR_GET(ufc_draw_resolution_scale_threshold);
  if (!threshold || !IsDrawResolutionScaled() || GetPath() != Path::kHostRenderTargets) {
    return false;
  }
  // Pitch is the only guest surface dimension that's reliably known, and it's shared by every
  // surface of a draw, so depth and color always land in the same class.
  uint32_t pitch_pixels_tile_aligned = RenderTargetKey::GetWidth(pitch_tiles_at_32bpp, msaa_samples);
  return pitch_pixels_tile_aligned != 0 && pitch_pixels_tile_aligned <= threshold;
}

bool RenderTargetCache::IsDrawScaleNative() const {
  if (!REXCVAR_GET(ufc_draw_resolution_scale_threshold)) {
    return false;
  }
  auto rb_surface_info = register_file().Get<reg::RB_SURFACE_INFO>();
  // Same pitch normalization as in Update.
  uint32_t msaa_samples_x_log2 = uint32_t(rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X);
  uint32_t pitch_tiles_at_32bpp =
      ((rb_surface_info.surface_pitch << msaa_samples_x_log2) + (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  return IsScaleNativeForPitch(pitch_tiles_at_32bpp, rb_surface_info.msaa_samples);
}

bool RenderTargetCache::IsResolveSourceNativeOnly(uint32_t base, uint32_t row_length,
                                                  uint32_t rows, uint32_t pitch) const {
  if (!REXCVAR_GET(ufc_draw_resolution_scale_threshold) || !IsDrawResolutionScaled() ||
      GetPath() != Path::kHostRenderTargets) {
    return false;
  }
  std::vector<ResolveCopyDumpRectangle> rectangles;
  GetResolveCopyRectanglesToDump(base, row_length, rows, pitch, rectangles);
  if (rectangles.empty()) {
    return false;
  }
  for (const ResolveCopyDumpRectangle& rectangle : rectangles) {
    if (!rectangle.render_target || !rectangle.render_target->key().scale_native) {
      return false;
    }
  }
  return true;
}
"""),
    ("""  uint32_t pitch_tiles_at_32bpp =
      ((pitch_pixels << msaa_samples_x_log2) + (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  if (!interlock_barrier_only) {
    uint32_t pitch_pixels_tile_aligned_scaled =
        pitch_tiles_at_32bpp * (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) *
        draw_resolution_scale_x();""",
     """  uint32_t pitch_tiles_at_32bpp =
      ((pitch_pixels << msaa_samples_x_log2) + (xenos::kEdramTileWidthSamples - 1)) /
      xenos::kEdramTileWidthSamples;
  // UFC3MONS U4: scale class of all the surfaces of this draw.
  bool scale_native = IsScaleNativeForPitch(pitch_tiles_at_32bpp, msaa_samples);
  if (!interlock_barrier_only) {
    uint32_t pitch_pixels_tile_aligned_scaled =
        pitch_tiles_at_32bpp * (xenos::kEdramTileWidthSamples >> msaa_samples_x_log2) *
        (scale_native ? 1 : draw_resolution_scale_x());"""),
    ("""    rt_key.resource_format = resource_formats[rt_bit_index];
""", """    rt_key.resource_format = resource_formats[rt_bit_index];
    rt_key.scale_native = uint32_t(scale_native);
"""),
    ("""  uint32_t pitch_pixels_scaled = pitch_pixels * draw_resolution_scale_x();
""", """  bool scale_native = IsScaleNativeForPitch(pitch_tiles_at_32bpp, msaa_samples);
  uint32_t pitch_pixels_scaled = pitch_pixels * (scale_native ? 1 : draw_resolution_scale_x());
"""),
    ("""    depth_render_target_key.resource_format = resolve_info.depth_edram_info.format;
""", """    depth_render_target_key.resource_format = resolve_info.depth_edram_info.format;
    depth_render_target_key.scale_native = uint32_t(scale_native);
"""),
    ("""    color_render_target_key.resource_format = uint32_t(GetColorResourceFormat(""",
     """    color_render_target_key.scale_native = uint32_t(scale_native);
    color_render_target_key.resource_format = uint32_t(GetColorResourceFormat("""),
    # Native render targets are kept out of host depth tracking entirely (both functions).
    ("""  bool host_depth_encoding_different = dest.is_depth && GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto would_require_transfers_in_extent""",
     """  bool host_depth_encoding_different = dest.is_depth && !dest.scale_native &&
                                       GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto would_require_transfers_in_extent"""),
    ("""  bool host_depth_encoding_different = dest.is_depth && GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto change_ownership_in_extent""",
     """  bool host_depth_encoding_different = dest.is_depth && !dest.scale_native &&
                                       GetPath() == Path::kHostRenderTargets &&
                                       IsHostDepthEncodingDifferent(dest.GetDepthFormat());
  auto change_ownership_in_extent"""),
])

# ---------------------------------------------------------------- base texture cache
patch("tex_cache.h", "inc/rex/graphics/pipeline/texture/cache.h", [
    ("""  void MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled);
""", """  void MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled);
  // UFC3MONS U4: resolution_scaled is whether the data went to the scaled resolve address space,
  // or to shared memory (resolves done at native resolution under the scale threshold). The latter
  // clears the scaled state of the range so textures pick up the shared memory copy.
  void MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled,
                           bool resolution_scaled);
"""),
])
patch("tex_cache.cpp", "tex_cache.cpp", [
    ("""void TextureCache::MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled) {
  if (length_unscaled == 0) {""",
     """void TextureCache::MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled) {
  MarkRangeAsResolved(start_unscaled, length_unscaled, true);
}

void TextureCache::MarkRangeAsResolved(uint32_t start_unscaled, uint32_t length_unscaled,
                                       bool resolution_scaled) {
  if (length_unscaled == 0) {"""),
    ("""      scaled_resolve_pages_[i] |= add_bits;
      scaled_resolve_pages_l2_[i >> 6] |= UINT64_C(1) << (i & 63);
""", """      if (resolution_scaled) {
        scaled_resolve_pages_[i] |= add_bits;
        scaled_resolve_pages_l2_[i >> 6] |= UINT64_C(1) << (i & 63);
      } else {
        // Native resolve data is in shared memory - clear the same way the CPU write watch does.
        scaled_resolve_pages_[i] &= ~add_bits;
        if (!scaled_resolve_pages_[i]) {
          scaled_resolve_pages_l2_[i >> 6] &= ~(UINT64_C(1) << (i & 63));
        }
      }
"""),
])

# ---------------------------------------------------------------- SPIR-V translator (shader modification bit)
patch("spirv_translator.h", "inc/rex/graphics/pipeline/shader/spirv_translator.h", [
    ("""      DepthStencilMode depth_stencil_mode : 3;
""", """      DepthStencilMode depth_stencil_mode : 3;
      // UFC3MONS U4: PsParamGen and the memexport dedup must act like there's no resolution
      // scaling - set only when the draw is native because of the scale threshold. Zero for every
      // existing (scaled) shader, so stored pipelines stay valid (kVersion unchanged).
      uint32_t resolution_scale_native : 1;
"""),
    ("""  uint32_t draw_resolution_scale_y_;
""", """  uint32_t draw_resolution_scale_y_;

  // UFC3MONS U4: scale of the draw being translated; position-dependent paths use these.
  uint32_t GetCurrentDrawResolutionScaleX() const {
    return is_pixel_shader() && GetSpirvShaderModification().pixel.resolution_scale_native
               ? 1
               : draw_resolution_scale_x_;
  }
  uint32_t GetCurrentDrawResolutionScaleY() const {
    return is_pixel_shader() && GetSpirvShaderModification().pixel.resolution_scale_native
               ? 1
               : draw_resolution_scale_y_;
  }
"""),
])
patch("spirv_translator.cpp", "spirv_translator.cpp", [
    ("""  bool memexport_resolution_scaled = current_shader().memexport_eM_written() &&
                                     (draw_resolution_scale_x_ > 1 || draw_resolution_scale_y_ > 1);""",
     """  bool memexport_resolution_scaled =
      current_shader().memexport_eM_written() &&
      (GetCurrentDrawResolutionScaleX() > 1 || GetCurrentDrawResolutionScaleY() > 1);"""),
    ("""        uint32_t(draw_resolution_scale_x_ > 1) | (uint32_t(draw_resolution_scale_y_ > 1) << 1);""",
     """        uint32_t(GetCurrentDrawResolutionScaleX() > 1) |
        (uint32_t(GetCurrentDrawResolutionScaleY() > 1) << 1);"""),
    ("""            builder_->makeUintConstant(i ? draw_resolution_scale_y_ : draw_resolution_scale_x_));""",
     """            builder_->makeUintConstant(i ? GetCurrentDrawResolutionScaleY()
                                         : GetCurrentDrawResolutionScaleX()));"""),
    ("""                                  builder_->makeUintConstant(i ? (draw_resolution_scale_y_ >> 1)
                                                               : (draw_resolution_scale_x_ >> 1)));""",
     """                                  builder_->makeUintConstant(
                                      i ? (GetCurrentDrawResolutionScaleY() >> 1)
                                        : (GetCurrentDrawResolutionScaleX() >> 1)));"""),
    ("""    if (draw_resolution_scale_x_ > 1) {
      param_gen_x = builder_->createNoContractionBinOp(
          spv::OpFMul, type_float_, param_gen_x,
          builder_->makeFloatConstant(1.0f / float(draw_resolution_scale_x_)));""",
     """    if (GetCurrentDrawResolutionScaleX() > 1) {
      param_gen_x = builder_->createNoContractionBinOp(
          spv::OpFMul, type_float_, param_gen_x,
          builder_->makeFloatConstant(1.0f / float(GetCurrentDrawResolutionScaleX())));"""),
    ("""    if (draw_resolution_scale_y_ > 1) {
      param_gen_y = builder_->createNoContractionBinOp(
          spv::OpFMul, type_float_, param_gen_y,
          builder_->makeFloatConstant(1.0f / float(draw_resolution_scale_y_)));""",
     """    if (GetCurrentDrawResolutionScaleY() > 1) {
      param_gen_y = builder_->createNoContractionBinOp(
          spv::OpFMul, type_float_, param_gen_y,
          builder_->makeFloatConstant(1.0f / float(GetCurrentDrawResolutionScaleY())));"""),
])
patch("pipeline_cache.cpp", "pipeline_cache.cpp", [
    ("""  if (render_target_cache_.GetPath() == RenderTargetCache::Path::kHostRenderTargets) {
    using DepthStencilMode = SpirvShaderTranslator::Modification::DepthStencilMode;""",
     """  if (render_target_cache_.GetPath() == RenderTargetCache::Path::kHostRenderTargets) {
    // UFC3MONS U4: whether this draw is native resolution because of the scale threshold.
    modification.pixel.resolution_scale_native = uint32_t(render_target_cache_.IsDrawScaleNative());
    using DepthStencilMode = SpirvShaderTranslator::Modification::DepthStencilMode;"""),
])
