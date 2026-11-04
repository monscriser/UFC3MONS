#!/usr/bin/env python3
"""UFC3MONS blood fix (U4), part 4: Vulkan render target cache (v60 source) and its header.
Port of Xenia Canary 74db632 (BSD). Same rules as apply_blood.py: pristine copies from orig/,
every replacement must match exactly once."""
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
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(s.replace("\n", nl).encode())
    print("patched", dst)


patch("vulkan_render_target_cache.h", "inc/rex/graphics/vulkan/render_target_cache.h", [
    ("""  bool Resolve(const memory::Memory& memory, VulkanSharedMemory& shared_memory,
               VulkanTextureCache& texture_cache, uint32_t& written_address_out,
               uint32_t& written_length_out);
""", """  // UFC3MONS U4: written_scaled_out = whether the data went to the scaled resolve address space
  // rather than shared memory (native resolves under the scale threshold don't).
  bool Resolve(const memory::Memory& memory, VulkanSharedMemory& shared_memory,
               VulkanTextureCache& texture_cache, uint32_t& written_address_out,
               uint32_t& written_length_out, bool* written_scaled_out = nullptr);
"""),
    ("""      TransferMode mode : 4;
""", """      // UFC3MONS U4: scale classes of the two sides.
      uint32_t dest_scale_native : 1;
      uint32_t source_scale_native : 1;
      TransferMode mode : 4;
"""),
    ("""      uint32_t is_depth : 1;
    };

    DumpPipelineKey() : key(0) { static_assert_size(*this, sizeof(key)); }""",
     """      uint32_t is_depth : 1;
      // UFC3MONS U4: dumping this native render target into the scaled EDRAM layout duplicates
      // its guest pixels; native_layout addresses the EDRAM buffer with the plain 1x1 tile layout.
      uint32_t source_scale_native : 1;
      uint32_t native_layout : 1;
    };

    DumpPipelineKey() : key(0) { static_assert_size(*this, sizeof(key)); }"""),
    ("""  bool DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
                         uint32_t dump_pitch);
""", """  bool DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
                         uint32_t dump_pitch, bool native_layout = false);
"""),
    # New members at the END of the class: objects built against the old header keep their offsets.
    ("""  uint64_t ht_dres_stats_[4] = {};  // direct, verified, verified words, fallback

};
""", """  uint64_t ht_dres_stats_[4] = {};  // direct, verified, verified words, fallback

  // UFC3MONS U4: unscaled resolve copy variants for fully native resolves (only with resolution
  // scaling; the scaled set's push constants and destination assume the scaled resolve buffer).
  VkPipelineLayout resolve_copy_native_pipeline_layout_ = VK_NULL_HANDLE;
  std::array<VkPipeline, size_t(draw_util::ResolveCopyShaderIndex::kCount)>
      resolve_copy_native_pipelines_{};
  uint64_t ufc_native_resolves_ = 0;
};
"""),
])

EDITS_CPP = [
    # Initialize: native resolve copy layout + pipelines.
    ("""  if (dfn.vkCreatePipelineLayout(device, &resolve_copy_pipeline_layout_create_info, nullptr,
                                 &resolve_copy_pipeline_layout_) != VK_SUCCESS) {
    REXGPU_ERROR(
        "VulkanRenderTargetCache: Failed to create the resolve copy pipeline "
        "layout");
    Shutdown();
    return false;
  }
""", """  if (dfn.vkCreatePipelineLayout(device, &resolve_copy_pipeline_layout_create_info, nullptr,
                                 &resolve_copy_pipeline_layout_) != VK_SUCCESS) {
    REXGPU_ERROR(
        "VulkanRenderTargetCache: Failed to create the resolve copy pipeline "
        "layout");
    Shutdown();
    return false;
  }
  if (draw_resolution_scaled) {
    // UFC3MONS U4: second layout for fully native resolve copies (full push constants).
    resolve_copy_push_constant_range.size = sizeof(draw_util::ResolveCopyShaderConstants);
    if (dfn.vkCreatePipelineLayout(device, &resolve_copy_pipeline_layout_create_info, nullptr,
                                   &resolve_copy_native_pipeline_layout_) != VK_SUCCESS) {
      REXGPU_ERROR("VulkanRenderTargetCache: Failed to create the native resolve copy pipeline layout");
      Shutdown();
      return false;
    }
  }
"""),
    ("""    vulkan_device->SetObjectName(VK_OBJECT_TYPE_PIPELINE, resolve_copy_pipeline,
                                 resolve_copy_shader_info.debug_name);
    resolve_copy_pipelines_[i] = resolve_copy_pipeline;
  }
""", """    vulkan_device->SetObjectName(VK_OBJECT_TYPE_PIPELINE, resolve_copy_pipeline,
                                 resolve_copy_shader_info.debug_name);
    resolve_copy_pipelines_[i] = resolve_copy_pipeline;
    if (draw_resolution_scaled) {
      // UFC3MONS U4: unscaled variant for fully native resolves.
      VkPipeline resolve_copy_native_pipeline = ui::vulkan::util::CreateComputePipeline(
          vulkan_device, resolve_copy_native_pipeline_layout_, resolve_copy_shader_code.unscaled,
          resolve_copy_shader_code.unscaled_size_bytes);
      if (resolve_copy_native_pipeline == VK_NULL_HANDLE) {
        REXGPU_ERROR("VulkanRenderTargetCache: Failed to create the native resolve copy pipeline {}",
                     resolve_copy_shader_info.debug_name);
        Shutdown();
        return false;
      }
      resolve_copy_native_pipelines_[i] = resolve_copy_native_pipeline;
    }
  }
"""),
    # Shutdown.
    ("""  for (VkPipeline& resolve_copy_pipeline : resolve_copy_pipelines_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, resolve_copy_pipeline);
  }
""", """  for (VkPipeline& resolve_copy_native_pipeline : resolve_copy_native_pipelines_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                           resolve_copy_native_pipeline);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         resolve_copy_native_pipeline_layout_);
  for (VkPipeline& resolve_copy_pipeline : resolve_copy_pipelines_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device, resolve_copy_pipeline);
  }
"""),
    # Resolve: signature.
    ("""                                      uint32_t& written_address_out, uint32_t& written_length_out) {
  written_address_out = 0;
  written_length_out = 0;
""", """                                      uint32_t& written_address_out, uint32_t& written_length_out,
                                      bool* written_scaled_out) {
  written_address_out = 0;
  written_length_out = 0;
  if (written_scaled_out) {
    *written_scaled_out = false;
  }
"""),
    # Resolve: decide native-only source before choosing the shader.
    ("""  // Copying.
  bool copied = false;
  if (resolve_info.copy_dest_extent_length) {
    draw_util::ResolveCopyShaderConstants copy_shader_constants;
    uint32_t copy_group_count_x, copy_group_count_y;
    draw_util::ResolveCopyShaderIndex copy_shader =
        resolve_info.GetCopyShader(draw_resolution_scale_x(), draw_resolution_scale_y(),
                                   copy_shader_constants, copy_group_count_x, copy_group_count_y);""",
     """  // Copying.
  bool copied = false;
  if (resolve_info.copy_dest_extent_length) {
    // UFC3MONS U4: if everything owning the source is native, copy at 1x1 into shared memory.
    bool copy_native = false;
    if (GetPath() == Path::kHostRenderTargets && draw_resolution_scaled) {
      uint32_t span_base, span_row_length_used, span_rows, span_pitch;
      resolve_info.GetCopyEdramTileSpan(span_base, span_row_length_used, span_rows, span_pitch);
      copy_native = IsResolveSourceNativeOnly(span_base, span_row_length_used, span_rows, span_pitch);
      if (copy_native) {
        // Redo the resolve info at 1x1 so the scale-dependent fields match the unscaled shaders.
        if (!draw_util::GetResolveInfo(register_file(), memory, 1, 1,
                                       IsFixedRG16TruncatedToMinus1To1(),
                                       IsFixedRGBA16TruncatedToMinus1To1(), resolve_info)) {
          return false;
        }
        ++ufc_native_resolves_;
        if ((ufc_native_resolves_ & (ufc_native_resolves_ - 1)) == 0) {
          REXGPU_INFO("[ufc-u4] native_resolves={}", ufc_native_resolves_);
        }
      }
    }
    // From here on, the destination is the scaled resolve buffer only if dest_scaled.
    const bool dest_scaled = draw_resolution_scaled && !copy_native;
    draw_util::ResolveCopyShaderConstants copy_shader_constants;
    uint32_t copy_group_count_x, copy_group_count_y;
    draw_util::ResolveCopyShaderIndex copy_shader =
        resolve_info.GetCopyShader(copy_native ? 1 : draw_resolution_scale_x(),
                                   copy_native ? 1 : draw_resolution_scale_y(),
                                   copy_shader_constants, copy_group_count_x, copy_group_count_y);"""),
    ("""      const bool ht_use_dres = GetPath() == Path::kHostRenderTargets &&
                               REXCVAR_GET(ht_direct_resolve) &&""",
     """      // UFC3MONS U4: both direct resolve shortcuts assume the global scale - native copies dump.
      const bool ht_use_dres = GetPath() == Path::kHostRenderTargets && !copy_native &&
                               REXCVAR_GET(ht_direct_resolve) &&"""),
    ("""        if (REXCVAR_GET(direct_host_resolve)) {
          direct_resolved =""",
     """        if (REXCVAR_GET(direct_host_resolve) && !copy_native) {
          direct_resolved ="""),
    ("""          if (!DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch)) {""",
     """          if (!DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch,
                                 copy_native)) {"""),
    ("""      uint64_t copy_dest_use_length = resolve_info.copy_dest_extent_length;
      if (draw_resolution_scaled) {""",
     """      uint64_t copy_dest_use_length = resolve_info.copy_dest_extent_length;
      if (dest_scaled) {"""),
    ("""      bool copy_dest_committed;
      if (draw_resolution_scaled) {""",
     """      bool copy_dest_committed;
      if (dest_scaled) {"""),
    ("""          write_descriptor_set_dest_buffer_info.buffer = draw_resolution_scaled
                                                             ? texture_cache.scaled_resolve_buffer()""",
     """          write_descriptor_set_dest_buffer_info.buffer = dest_scaled
                                                             ? texture_cache.scaled_resolve_buffer()"""),
    ("""          // Submit the resolve.
          if (draw_resolution_scaled) {""",
     """          // Submit the resolve.
          if (dest_scaled) {"""),
    ("""          draw_util::ResolveCopyShaderConstants ht_constants = copy_shader_constants;
          if (!draw_resolution_scaled) {""",
     """          draw_util::ResolveCopyShaderConstants ht_constants = copy_shader_constants;
          if (!dest_scaled) {"""),
    ("""          UseEdramBuffer(EdramBufferUsage::kComputeRead);
          command_processor_.BindExternalComputePipeline(
              resolve_copy_pipelines_[size_t(copy_shader)]);
          VkDescriptorSet descriptor_sets[kResolveCopyDescriptorSetCount] = {};
          descriptor_sets[kResolveCopyDescriptorSetEdram] = edram_storage_buffer_descriptor_set_;
          descriptor_sets[kResolveCopyDescriptorSetDest] = descriptor_set_dest;
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_COMPUTE, resolve_copy_pipeline_layout_, 0,
              uint32_t(rex::countof(descriptor_sets)), descriptor_sets, 0, nullptr);
          if (draw_resolution_scaled) {
            command_buffer.CmdVkPushConstants(
                resolve_copy_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,""",
     """          UseEdramBuffer(EdramBufferUsage::kComputeRead);
          // UFC3MONS U4: fully native resolves use the unscaled variant with full push constants.
          const VkPipelineLayout copy_pipeline_layout =
              copy_native ? resolve_copy_native_pipeline_layout_ : resolve_copy_pipeline_layout_;
          command_processor_.BindExternalComputePipeline(
              copy_native ? resolve_copy_native_pipelines_[size_t(copy_shader)]
                          : resolve_copy_pipelines_[size_t(copy_shader)]);
          VkDescriptorSet descriptor_sets[kResolveCopyDescriptorSetCount] = {};
          descriptor_sets[kResolveCopyDescriptorSetEdram] = edram_storage_buffer_descriptor_set_;
          descriptor_sets[kResolveCopyDescriptorSetDest] = descriptor_set_dest;
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_COMPUTE, copy_pipeline_layout, 0,
              uint32_t(rex::countof(descriptor_sets)), descriptor_sets, 0, nullptr);
          if (dest_scaled) {
            command_buffer.CmdVkPushConstants(
                copy_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,"""),
    ("""            command_buffer.CmdVkPushConstants(
                resolve_copy_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                sizeof(copy_shader_constants), &copy_shader_constants);""",
     """            command_buffer.CmdVkPushConstants(
                copy_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                sizeof(copy_shader_constants), &copy_shader_constants);"""),
    ("""          texture_cache.MarkRangeAsResolved(resolve_info.copy_dest_extent_start,
                                            resolve_info.copy_dest_extent_length);
          written_address_out = resolve_info.copy_dest_extent_start;
          written_length_out = resolve_info.copy_dest_extent_length;""",
     """          // UFC3MONS U4: mark the range as scaled only if that's where the data went.
          texture_cache.MarkRangeAsResolved(resolve_info.copy_dest_extent_start,
                                            resolve_info.copy_dest_extent_length, dest_scaled);
          written_address_out = resolve_info.copy_dest_extent_start;
          written_length_out = resolve_info.copy_dest_extent_length;
          if (written_scaled_out) {
            *written_scaled_out = dest_scaled;
          }"""),
    # CreateRenderTarget.
    ("""  image_create_info.extent.width = key.GetWidth() * draw_resolution_scale_x();
  image_create_info.extent.height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples) * draw_resolution_scale_y();""",
     """  image_create_info.extent.width = key.GetWidth() * GetKeyScaleX(key);
  image_create_info.extent.height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples) * GetKeyScaleY(key);"""),
    # Framebuffer.
    ("""  VkExtent2D host_extent;
  if (pitch_tiles_at_32bpp) {
    host_extent.width =
        RenderTargetKey::GetWidth(pitch_tiles_at_32bpp, render_pass_key.msaa_samples);
    host_extent.height = GetRenderTargetHeight(pitch_tiles_at_32bpp, render_pass_key.msaa_samples);
  } else {""",
     """  VkExtent2D host_extent;
  // UFC3MONS U4: the scale class is a function of the pitch and MSAA mode, so a framebuffer can
  // never mix classes.
  uint32_t framebuffer_scale_x = draw_resolution_scale_x();
  uint32_t framebuffer_scale_y = draw_resolution_scale_y();
  if (pitch_tiles_at_32bpp) {
    host_extent.width =
        RenderTargetKey::GetWidth(pitch_tiles_at_32bpp, render_pass_key.msaa_samples);
    host_extent.height = GetRenderTargetHeight(pitch_tiles_at_32bpp, render_pass_key.msaa_samples);
    if (IsScaleNativeForPitch(pitch_tiles_at_32bpp, render_pass_key.msaa_samples)) {
      framebuffer_scale_x = 1;
      framebuffer_scale_y = 1;
    }
  } else {"""),
    ("""  host_extent.width = std::min(host_extent.width * draw_resolution_scale_x(),
                               device_properties.maxFramebufferWidth);
  host_extent.height = std::min(host_extent.height * draw_resolution_scale_y(),
                                device_properties.maxFramebufferHeight);""",
     """  host_extent.width = std::min(host_extent.width * framebuffer_scale_x,
                               device_properties.maxFramebufferWidth);
  host_extent.height = std::min(host_extent.height * framebuffer_scale_y,
                                device_properties.maxFramebufferHeight);"""),
    # Transfer shader.
    ("""  uint32_t tile_width_samples = xenos::kEdramTileWidthSamples * draw_resolution_scale_x();
  uint32_t tile_height_samples = xenos::kEdramTileHeightSamples * draw_resolution_scale_y();
""", """  // UFC3MONS U4: the two sides of the transfer may be in different scale classes. The host depth
  // source always has the destination's scale since native render targets don't track host depth.
  uint32_t dest_scale_x = key.dest_scale_native ? 1 : draw_resolution_scale_x();
  uint32_t dest_scale_y = key.dest_scale_native ? 1 : draw_resolution_scale_y();
  uint32_t source_scale_x = key.source_scale_native ? 1 : draw_resolution_scale_x();
  uint32_t source_scale_y = key.source_scale_native ? 1 : draw_resolution_scale_y();
  uint32_t dest_tile_width_samples = xenos::kEdramTileWidthSamples * dest_scale_x;
  uint32_t dest_tile_height_samples = xenos::kEdramTileHeightSamples * dest_scale_y;
  uint32_t source_tile_width_samples = xenos::kEdramTileWidthSamples * source_scale_x;
  uint32_t source_tile_height_samples = xenos::kEdramTileHeightSamples * source_scale_y;
"""),
    ("""  spv::Id const_dest_tile_width_pixels = builder.makeUintConstant(
      tile_width_samples >>""",
     """  spv::Id const_dest_tile_width_pixels = builder.makeUintConstant(
      dest_tile_width_samples >>"""),
    ("""      tile_height_samples >> uint32_t(key.dest_msaa_samples >= xenos::MsaaSamples::k2X));""",
     """      dest_tile_height_samples >> uint32_t(key.dest_msaa_samples >= xenos::MsaaSamples::k2X));"""),
    ("""  spv::Id source_sample_id = dest_sample_id;
  spv::Id source_tile_pixel_x = dest_tile_pixel_x;
  spv::Id source_tile_pixel_y = dest_tile_pixel_y;""",
     """  // UFC3MONS U4: if the scale classes differ, convert the tile-local pixel coordinates to the
  // source scale space - the remappings below transform between two layouts of one scale. The
  // destination-space coordinates are kept for the host depth source. Sample indices don't change.
  spv::Id dest_space_tile_pixel_x = dest_tile_pixel_x;
  spv::Id dest_space_tile_pixel_y = dest_tile_pixel_y;
  if (key.source_scale_native != key.dest_scale_native) {
    if (key.dest_scale_native) {
      // Native destination reading a scaled source - take the center host pixel of each guest
      // pixel, like memexport and the resolve downscale do.
      dest_tile_pixel_x = builder.createBinOp(
          spv::OpIAdd, type_uint,
          builder.createBinOp(spv::OpIMul, type_uint, dest_tile_pixel_x,
                              builder.makeUintConstant(source_scale_x)),
          builder.makeUintConstant(source_scale_x >> 1));
      dest_tile_pixel_y = builder.createBinOp(
          spv::OpIAdd, type_uint,
          builder.createBinOp(spv::OpIMul, type_uint, dest_tile_pixel_y,
                              builder.makeUintConstant(source_scale_y)),
          builder.makeUintConstant(source_scale_y >> 1));
    } else {
      // Scaled destination reading a native source - duplicate guest pixels.
      dest_tile_pixel_x = builder.createBinOp(spv::OpUDiv, type_uint, dest_tile_pixel_x,
                                              builder.makeUintConstant(dest_scale_x));
      dest_tile_pixel_y = builder.createBinOp(spv::OpUDiv, type_uint, dest_tile_pixel_y,
                                              builder.makeUintConstant(dest_scale_y));
    }
  }

  spv::Id source_sample_id = dest_sample_id;
  spv::Id source_tile_pixel_x = dest_tile_pixel_x;
  spv::Id source_tile_pixel_y = dest_tile_pixel_y;"""),
    ("""  uint32_t source_pixel_width_dwords_log2 =
      uint32_t(key.source_msaa_samples >= xenos::MsaaSamples::k4X) + uint32_t(source_is_64bpp);
""", """  // UFC3MONS U4: source coordinates are derived - the host depth source path below needs the
  // destination-space coordinates back.
  dest_tile_pixel_x = dest_space_tile_pixel_x;
  dest_tile_pixel_y = dest_space_tile_pixel_y;

  uint32_t source_pixel_width_dwords_log2 =
      uint32_t(key.source_msaa_samples >= xenos::MsaaSamples::k4X) + uint32_t(source_is_64bpp);
"""),
    ("""        tile_width_samples >> (1 + source_pixel_width_dwords_log2);""",
     """        source_tile_width_samples >> (1 + source_pixel_width_dwords_log2);"""),
    ("""              builder.makeUintConstant(tile_width_samples >> source_pixel_width_dwords_log2),""",
     """              builder.makeUintConstant(source_tile_width_samples >> source_pixel_width_dwords_log2),"""),
    ("""              builder.makeUintConstant(tile_height_samples >> uint32_t(key.source_msaa_samples >=""",
     """              builder.makeUintConstant(source_tile_height_samples >> uint32_t(key.source_msaa_samples >="""),
    ("""                                        builder.makeUintConstant(tile_width_samples >>
                                                                 uint32_t(key.source_msaa_samples >=
                                                                          xenos::MsaaSamples::k4X)),""",
     """                                        builder.makeUintConstant(dest_tile_width_samples >>
                                                                 uint32_t(key.source_msaa_samples >=
                                                                          xenos::MsaaSamples::k4X)),"""),
    ("""                                        builder.makeUintConstant(tile_height_samples >>
                                                                 uint32_t(key.source_msaa_samples >=
                                                                          xenos::MsaaSamples::k2X)),""",
     """                                        builder.makeUintConstant(dest_tile_height_samples >>
                                                                 uint32_t(key.source_msaa_samples >=
                                                                          xenos::MsaaSamples::k2X)),"""),
    ("""                    builder.makeUintConstant(tile_width_samples * tile_height_samples),""",
     """                    builder.makeUintConstant(dest_tile_width_samples * dest_tile_height_samples),"""),
    ("""                                        builder.makeUintConstant(tile_width_samples),""",
     """                                        builder.makeUintConstant(dest_tile_width_samples),"""),
    # Resolve clears: all render targets of one resolve clear share the pitch and thus the class.
    ("""    // Assuming the rectangle is already clamped by the setup function from the
    // common render target cache.
    resolve_clear_rect.rect.offset.x =
        int32_t(resolve_clear_rectangle->x_pixels * draw_resolution_scale_x());
    resolve_clear_rect.rect.offset.y =
        int32_t(resolve_clear_rectangle->y_pixels * draw_resolution_scale_y());
    resolve_clear_rect.rect.extent.width =
        resolve_clear_rectangle->width_pixels * draw_resolution_scale_x();
    resolve_clear_rect.rect.extent.height =
        resolve_clear_rectangle->height_pixels * draw_resolution_scale_y();""",
     """    // UFC3MONS U4: all render targets of one resolve clear share the pitch and thus the scale
    // class - take the scale from whichever is there.
    uint32_t resolve_clear_scale_x = draw_resolution_scale_x();
    uint32_t resolve_clear_scale_y = draw_resolution_scale_y();
    for (uint32_t i = 0; i < render_target_count; ++i) {
      if (render_targets[i]) {
        resolve_clear_scale_x = GetKeyScaleX(render_targets[i]->key());
        resolve_clear_scale_y = GetKeyScaleY(render_targets[i]->key());
        break;
      }
    }
    // Assuming the rectangle is already clamped by the setup function from the
    // common render target cache.
    resolve_clear_rect.rect.offset.x =
        int32_t(resolve_clear_rectangle->x_pixels * resolve_clear_scale_x);
    resolve_clear_rect.rect.offset.y =
        int32_t(resolve_clear_rectangle->y_pixels * resolve_clear_scale_y);
    resolve_clear_rect.rect.extent.width = resolve_clear_rectangle->width_pixels * resolve_clear_scale_x;
    resolve_clear_rect.rect.extent.height =
        resolve_clear_rectangle->height_pixels * resolve_clear_scale_y;"""),
    ("""      new_transfer_shader_key.dest_resource_format = dest_rt_key.resource_format;
""", """      new_transfer_shader_key.dest_resource_format = dest_rt_key.resource_format;
      new_transfer_shader_key.dest_scale_native = dest_rt_key.scale_native;
"""),
    ("""          new_transfer_shader_key.source_resource_format = source_rt_key.resource_format;
""", """          new_transfer_shader_key.source_resource_format = source_rt_key.resource_format;
          new_transfer_shader_key.source_scale_native = source_rt_key.scale_native;
"""),
    ("""                int32_t(stencil_clear_rectangle.x_pixels * draw_resolution_scale_x());
            stencil_clear_rect_write_ptr->rect.offset.y =
                int32_t(stencil_clear_rectangle.y_pixels * draw_resolution_scale_y());
            stencil_clear_rect_write_ptr->rect.extent.width =
                stencil_clear_rectangle.width_pixels * draw_resolution_scale_x();
            stencil_clear_rect_write_ptr->rect.extent.height =
                stencil_clear_rectangle.height_pixels * draw_resolution_scale_y();""",
     """                int32_t(stencil_clear_rectangle.x_pixels * GetKeyScaleX(dest_rt_key));
            stencil_clear_rect_write_ptr->rect.offset.y =
                int32_t(stencil_clear_rectangle.y_pixels * GetKeyScaleY(dest_rt_key));
            stencil_clear_rect_write_ptr->rect.extent.width =
                stencil_clear_rectangle.width_pixels * GetKeyScaleX(dest_rt_key);
            stencil_clear_rect_write_ptr->rect.extent.height =
                stencil_clear_rectangle.height_pixels * GetKeyScaleY(dest_rt_key);"""),
    ("""      float pixels_to_ndc_x = (2.0f / transfer_viewport.width) * float(draw_resolution_scale_x());
      float pixels_to_ndc_y = (2.0f / transfer_viewport.height) * float(draw_resolution_scale_y());""",
     """      // UFC3MONS U4: guest pixels to the destination's host pixels.
      float pixels_to_ndc_x = (2.0f / transfer_viewport.width) * float(GetKeyScaleX(dest_rt_key));
      float pixels_to_ndc_y = (2.0f / transfer_viewport.height) * float(GetKeyScaleY(dest_rt_key));"""),
    # Dump pipeline.
    ("""  uint32_t tile_width =
      (xenos::kEdramTileWidthSamples >> uint32_t(format_is_64bpp)) * draw_resolution_scale_x();""",
     """  // UFC3MONS U4: dumps for fully native resolves address the EDRAM buffer with the 1x1 layout.
  uint32_t layout_scale_x = key.native_layout ? 1 : draw_resolution_scale_x();
  uint32_t layout_scale_y = key.native_layout ? 1 : draw_resolution_scale_y();
  uint32_t tile_width =
      (xenos::kEdramTileWidthSamples >> uint32_t(format_is_64bpp)) * layout_scale_x;"""),
    ("""  uint32_t tile_height = xenos::kEdramTileHeightSamples * draw_resolution_scale_y();
  spv::Id const_tile_height = builder.makeUintConstant(tile_height);""",
     """  uint32_t tile_height = xenos::kEdramTileHeightSamples * layout_scale_y;
  spv::Id const_tile_height = builder.makeUintConstant(tile_height);"""),
    ("""  // Load the source, and pack the value into one or two 32-bit integers.
  spv::Id packed[2] = {};""",
     """  if (key.source_scale_native && !key.native_layout && IsDrawResolutionScaled()) {
    // UFC3MONS U4: native source dumped to the scaled EDRAM layout - duplicate each pixel into all
    // the scaled sample slots covering it (after the sample index is extracted).
    source_pixel_x = builder.createBinOp(spv::OpUDiv, type_uint, source_pixel_x,
                                         builder.makeUintConstant(draw_resolution_scale_x()));
    source_pixel_y = builder.createBinOp(spv::OpUDiv, type_uint, source_pixel_y,
                                         builder.makeUintConstant(draw_resolution_scale_y()));
  }

  // Load the source, and pack the value into one or two 32-bit integers.
  spv::Id packed[2] = {};"""),
    # DumpRenderTargets.
    ("""bool VulkanRenderTargetCache::DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                                                uint32_t dump_rows, uint32_t dump_pitch) {""",
     """bool VulkanRenderTargetCache::DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,
                                                uint32_t dump_rows, uint32_t dump_pitch,
                                                bool native_layout) {"""),
    ("""    pipeline_key.is_depth = rt_key.is_depth;
    dump_invocations_.emplace_back(rectangle, pipeline_key);""",
     """    pipeline_key.is_depth = rt_key.is_depth;
    pipeline_key.source_scale_native = rt_key.scale_native;
    pipeline_key.native_layout = uint32_t(native_layout);
    dump_invocations_.emplace_back(rectangle, pipeline_key);"""),
    ("""          (draw_resolution_scale_x() *
               (xenos::kEdramTileWidthSamples >> uint32_t(rt_key.Is64bpp())) *
               dispatch.width_tiles +
           (kDumpSamplesPerGroupX - 1)) /
              kDumpSamplesPerGroupX,
          (draw_resolution_scale_y() * xenos::kEdramTileHeightSamples * dispatch.height_tiles +""",
     """          ((native_layout ? 1 : draw_resolution_scale_x()) *
               (xenos::kEdramTileWidthSamples >> uint32_t(rt_key.Is64bpp())) *
               dispatch.width_tiles +
           (kDumpSamplesPerGroupX - 1)) /
              kDumpSamplesPerGroupX,
          ((native_layout ? 1 : draw_resolution_scale_y()) * xenos::kEdramTileHeightSamples *
               dispatch.height_tiles +"""),
]

patch("vulkan_render_target_cache.cpp", "vulkan_render_target_cache.cpp", EDITS_CPP)
# Linux: same source except the shader include paths.
patch("vulkan_render_target_cache.linux.cpp", "vulkan_render_target_cache.linux.cpp", EDITS_CPP)
