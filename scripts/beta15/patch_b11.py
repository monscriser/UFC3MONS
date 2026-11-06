#!/usr/bin/env python3
"""B11: memos exatos por desenho.
 (a) ufc_translation_memo  (vcp): mesmo shader + mesma modificacao do desenho anterior -> mesma traducao
     (pula GetOrCreateTranslation, que trava mutex e procura no mapa).
 (b) ufc_pipeline_desc_memo (pipeline_cache): mesmas entradas do GetCurrentStateDescription do desenho
     anterior -> mesma descricao (pula a montagem).
Uso: patch_b11.py <vcp_b4.cpp> <vcp_b11.cpp> <pipeline_cache.cpp> <pipeline_cache_b11.cpp>"""
import sys
vin, vout, pin, pout = sys.argv[1:5]


def apply(src, dst, reps):
    t = open(src, encoding="utf-8").read()
    for a, b in reps:
        assert t.count(a) == 1, (src, t.count(a), a[:80])
        t = t.replace(a, b)
    open(dst, "w", encoding="utf-8", newline="\n").write(t)


# ---------------- (a) vcp ----------------
VCP_HDR_OLD = "\nvoid VulkanCommandProcessor::ClearCaches() {\n  ufc_smp::S().valid = false;"
VCP_HDR_NEW = r'''
// UFC3MONS B11a: memo da traducao do shader do desenho anterior.
REXCVAR_DEFINE_BOOL(ufc_translation_memo, true, "GPU/Vulkan",
                    "B11: reuse the previous draw's shader translations when shader and modification match");
REXCVAR_DEFINE_BOOL(ufc_translation_memo_verify, false, "GPU/Vulkan",
                    "B11 test: look the translations up anyway and compare");
REXCVAR_DEFINE_BOOL(ufc_translation_memo_verify_break, false, "GPU/Vulkan",
                    "B11 positive control: perturb the reference of the verifier");
namespace ufc_xm {
struct Slot { const void* shader = nullptr; uint64_t modification = 0; void* translation = nullptr; };
struct State { Slot vs, ps; uint64_t hits = 0, misses = 0, compared = 0, mismatches = 0, n = 0; };
inline State& S() { static State s; return s; }
// Devolve a traducao: do memo se o shader e a modificacao forem os mesmos do desenho anterior.
inline void* Get(Slot& slot, Shader* shader, uint64_t modification) {
  auto& s = S();
  const bool on = REXCVAR_GET(ufc_translation_memo);
  const bool hit = on && slot.translation && slot.shader == shader && slot.modification == modification;
  if (hit && !REXCVAR_GET(ufc_translation_memo_verify)) { ++s.hits; return slot.translation; }
  void* t = shader->GetOrCreateTranslation(modification);
  if (hit) {
    void* ref = slot.translation;
    if (REXCVAR_GET(ufc_translation_memo_verify_break)) ref = reinterpret_cast<void*>(uintptr_t(ref) ^ 0x10);
    ++s.compared; if (ref != t) ++s.mismatches; ++s.hits;
  } else {
    ++s.misses;
  }
  if (on) { slot.shader = shader; slot.modification = modification; slot.translation = t; }
  if ((++s.n % 400000) == 0 && (REXCVAR_GET(ufc_translation_memo_verify) || std::getenv("UFC_MEASURE_DRAW")))
    REXGPU_WARN("[ufc-xlat-memo] verify={} hits={} misses={} compared={} mismatches={}",
                REXCVAR_GET(ufc_translation_memo_verify), s.hits, s.misses, s.compared, s.mismatches);
  return t;
}
}  // namespace ufc_xm

void VulkanCommandProcessor::ClearCaches() {
  ufc_smp::S().valid = false;
  ufc_xm::S().vs = ufc_xm::Slot();
  ufc_xm::S().ps = ufc_xm::Slot();'''
XL_OLD = '''    vertex_shader_translation = static_cast<VulkanShader::VulkanTranslation*>(
        vertex_shader->GetOrCreateTranslation(vertex_shader_modification.value));
    pixel_shader_translation =
        pixel_shader ? static_cast<VulkanShader::VulkanTranslation*>(
                           pixel_shader->GetOrCreateTranslation(pixel_shader_modification.value))
                     : nullptr;'''
XL_NEW = '''    vertex_shader_translation = static_cast<VulkanShader::VulkanTranslation*>(
        ufc_xm::Get(ufc_xm::S().vs, vertex_shader, vertex_shader_modification.value));
    pixel_shader_translation =
        pixel_shader ? static_cast<VulkanShader::VulkanTranslation*>(
                           ufc_xm::Get(ufc_xm::S().ps, pixel_shader, pixel_shader_modification.value))
                     : nullptr;'''
apply(vin, vout, [(VCP_HDR_OLD, VCP_HDR_NEW), (XL_OLD, XL_NEW)])

# ---------------- (b) pipeline cache ----------------
PC_DESC_OLD = '''  PipelineDescription description;
  if (!GetCurrentStateDescription(vertex_shader, pixel_shader, primitive_processing_result,
                                  normalized_depth_control, normalized_color_mask, render_pass_key,
                                  description)) {'''
PC_DESC_NEW = r'''  PipelineDescription description;
  // UFC3MONS B11b: chave com TODAS as entradas do GetCurrentStateDescription (valores, nao ponteiros).
  static struct UfcDescMemo {
    bool valid = false;
    uint32_t key[24] = {};
    PipelineDescription description;
    uint64_t hits = 0, misses = 0, compared = 0, mismatches = 0, n = 0;
  } ufc_dm_memo;
  uint32_t ufc_key[24];
  {
    const RegisterFile& r = register_file_;
    uint32_t k = 0;
    const uint64_t vh = vertex_shader->shader().ucode_data_hash();
    ufc_key[k++] = uint32_t(vh); ufc_key[k++] = uint32_t(vh >> 32);
    const uint64_t vm = vertex_shader->modification();
    ufc_key[k++] = uint32_t(vm); ufc_key[k++] = uint32_t(vm >> 32);
    const uint64_t ph = pixel_shader ? pixel_shader->shader().ucode_data_hash() : 0;
    ufc_key[k++] = uint32_t(ph); ufc_key[k++] = uint32_t(ph >> 32);
    const uint64_t pm = pixel_shader ? pixel_shader->modification() : 0;
    ufc_key[k++] = uint32_t(pm); ufc_key[k++] = uint32_t(pm >> 32);
    ufc_key[k++] = (pixel_shader ? 1u : 0u) | (pixel_shader && pixel_shader->shader().writes_depth() ? 2u : 0u) |
                   (primitive_processing_result.IsTessellated() ? 4u : 0u) |
                   (primitive_processing_result.host_primitive_reset_enabled ? 8u : 0u) |
                   (REXCVAR_GET(vulkan_tessellation_wireframe) ? 16u : 0u);
    ufc_key[k++] = uint32_t(primitive_processing_result.host_primitive_type) |
                   (uint32_t(primitive_processing_result.tessellation_mode) << 8) |
                   (uint32_t(primitive_processing_result.host_vertex_shader_type) << 16);
    ufc_key[k++] = normalized_depth_control.value;
    ufc_key[k++] = normalized_color_mask;
    ufc_key[k++] = render_pass_key.key;
    ufc_key[k++] = r[XE_GPU_REG_PA_SU_SC_MODE_CNTL];
    ufc_key[k++] = r[XE_GPU_REG_PA_CL_CLIP_CNTL];
    ufc_key[k++] = r[XE_GPU_REG_VGT_OUTPUT_PATH_CNTL];
    ufc_key[k++] = uint32_t(r.Get<reg::VGT_DRAW_INITIATOR>().prim_type);
    ufc_key[k++] = r[XE_GPU_REG_RB_MODECONTROL];
    ufc_key[k++] = r[XE_GPU_REG_SQ_PROGRAM_CNTL];
    ufc_key[k++] = r[XE_GPU_REG_RB_SURFACE_INFO];
    for (uint32_t i = 0; i < 4; ++i) ufc_key[k++] = r[reg::RB_BLENDCONTROL::rt_register_indices[i]];
    assert_true(k == 24);
  }
  const bool ufc_memo_on = REXCVAR_GET(ufc_pipeline_desc_memo);
  const bool ufc_memo_hit = ufc_memo_on && ufc_dm_memo.valid &&
                            !std::memcmp(ufc_key, ufc_dm_memo.key, sizeof(ufc_key));
  if ((++ufc_dm_memo.n % 400000) == 0 &&
      (REXCVAR_GET(ufc_pipeline_desc_memo_verify) || std::getenv("UFC_MEASURE_DRAW"))) {
    REXGPU_WARN("[ufc-desc-memo] verify={} hits={} misses={} compared={} mismatches={}",
                REXCVAR_GET(ufc_pipeline_desc_memo_verify), ufc_dm_memo.hits, ufc_dm_memo.misses,
                ufc_dm_memo.compared, ufc_dm_memo.mismatches);
  }
  if (ufc_memo_hit && !REXCVAR_GET(ufc_pipeline_desc_memo_verify)) {
    ++ufc_dm_memo.hits;
    description = ufc_dm_memo.description;
  } else {
  if (!GetCurrentStateDescription(vertex_shader, pixel_shader, primitive_processing_result,
                                  normalized_depth_control, normalized_color_mask, render_pass_key,
                                  description)) {
    ufc_dm_memo.valid = false;'''
PC_AFTER_OLD = '''  if (last_pipeline_ && last_pipeline_->first == description) {
    VkPipeline last_pipeline'''
PC_AFTER_NEW = r'''    if (ufc_memo_hit) {
      PipelineDescription ref = ufc_dm_memo.description;
      if (REXCVAR_GET(ufc_pipeline_desc_memo_verify_break)) {
        reinterpret_cast<uint8_t*>(&ref)[0] ^= 0x01;
      }
      ++ufc_dm_memo.compared;
      ++ufc_dm_memo.hits;
      if (!(ref == description)) ++ufc_dm_memo.mismatches;
    } else {
      ++ufc_dm_memo.misses;
    }
    if (ufc_memo_on) {
      ufc_dm_memo.valid = true;
      std::memcpy(ufc_dm_memo.key, ufc_key, sizeof(ufc_key));
      ufc_dm_memo.description = description;
    }
  }
  if (last_pipeline_ && last_pipeline_->first == description) {
    VkPipeline last_pipeline'''
CV_OLD = "bool VulkanPipelineCache::ConfigurePipeline("
CV_NEW = r'''REXCVAR_DEFINE_BOOL(ufc_pipeline_desc_memo, true, "GPU/Vulkan",
                    "B11: reuse the previous pipeline description when all its inputs match");
REXCVAR_DEFINE_BOOL(ufc_pipeline_desc_memo_verify, false, "GPU/Vulkan",
                    "B11 test: build the description anyway and compare");
REXCVAR_DEFINE_BOOL(ufc_pipeline_desc_memo_verify_break, false, "GPU/Vulkan",
                    "B11 positive control: perturb the reference of the verifier");

bool VulkanPipelineCache::ConfigurePipeline('''
apply(pin, pout, [(PC_DESC_OLD, PC_DESC_NEW), (PC_AFTER_OLD, PC_AFTER_NEW), (CV_OLD, CV_NEW)])
print("b11 ok")
