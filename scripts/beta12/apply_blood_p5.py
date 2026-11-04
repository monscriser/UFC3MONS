#!/usr/bin/env python3
"""UFC3MONS blood fix (U4), part 5: Vulkan command processor (gpu-port-a16 source). Per-draw resolution
scale for the viewport, scissor, point size and depth bias; readback knows whether a resolve was scaled.
Port of Xenia Canary 74db632 (BSD). Pristine copy in orig/, every replacement must match exactly once."""
import os, sys

W = os.path.dirname(os.path.abspath(__file__))


def patch(src, dst, edits):
    s = open(os.path.join(W, "orig", src), "rb").read().decode()
    nl = "\r\n" if "\r\n" in s else "\n"
    s = s.replace("\r\n", "\n")
    for old, new in edits:
        n = s.count(old)
        if n != 1:
            sys.exit(f"{src}: expected 1 match, found {n}:\n{old[:300]}")
        s = s.replace(old, new)
    out = os.path.join(W, dst)
    open(out, "wb").write(s.replace("\n", nl).encode())
    print("patched", dst)


patch("vulkan_command_processor.cpp", "vulkan_command_processor.cpp", [
    # IssueDraw: viewport with the scale of this draw.
    ("""  bool host_render_targets_used =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets;
  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();
""", """  bool host_render_targets_used =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kHostRenderTargets;
  // UFC3MONS U4: resolution scale of this draw (1x1 under ufc_draw_resolution_scale_threshold).
  uint32_t draw_resolution_scale_x = render_target_cache_->GetDrawScaleX();
  uint32_t draw_resolution_scale_y = render_target_cache_->GetDrawScaleY();
"""),
    # UpdateDynamicState: scissor and depth bias.
    ("""  const RegisterFile& regs = *register_file_;
  uint32_t draw_resolution_scale_x = texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
  uint32_t draw_resolution_scale_y = texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;
""", """  const RegisterFile& regs = *register_file_;
  // UFC3MONS U4: scale of this draw.
  uint32_t draw_resolution_scale_x = render_target_cache_ ? render_target_cache_->GetDrawScaleX() : 1;
  uint32_t draw_resolution_scale_y = render_target_cache_ ? render_target_cache_->GetDrawScaleY() : 1;
"""),
    # UpdateSystemConstantValues: point size, EDRAM tile size (FSI), polygon offset.
    ("""  bool edram_fragment_shader_interlock =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock;
  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();
""", """  bool edram_fragment_shader_interlock =
      render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock;
  // UFC3MONS U4: scale of this draw.
  uint32_t draw_resolution_scale_x = render_target_cache_->GetDrawScaleX();
  uint32_t draw_resolution_scale_y = render_target_cache_->GetDrawScaleY();
"""),
    # Readback: the resolve tells whether its data went to the scaled buffer.
    ("""  uint32_t written_address, written_length;
  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length)) {
    return false;
  }
""", """  uint32_t written_address, written_length;
  bool ufc_written_scaled = false;
  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length, &ufc_written_scaled)) {
    return false;
  }
"""),
    ("""  bool is_scaled = texture_cache_->IsDrawResolutionScaled();
""", """  // UFC3MONS U4: native resolves went to shared memory and are read back directly.
  bool is_scaled = ufc_written_scaled;
"""),
])
