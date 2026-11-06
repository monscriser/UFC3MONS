#!/usr/bin/env python3
"""B4/B10: reaproveitar os samplers do desenho anterior quando shaders, constantes de busca e submissao
sao os mesmos. Uso: patch_b4.py <vulkan_command_processor.cpp> <saida.cpp>
Cvars: ufc_sampler_reuse (padrao false), ufc_sampler_reuse_verify, ufc_sampler_reuse_verify_break."""
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, encoding="utf-8").read()


def rep(a, b, n=1):
    global t
    assert t.count(a) == n, (t.count(a), a[:80])
    t = t.replace(a, b)


HDR = r'''
// UFC3MONS B4/B10: reaproveitamento dos samplers do desenho anterior.
REXCVAR_DEFINE_BOOL(ufc_sampler_reuse, true, "GPU/Vulkan",
                    "B4: reuse the previous draw's samplers when shaders and fetch constants match");
REXCVAR_DEFINE_BOOL(ufc_sampler_reuse_verify, false, "GPU/Vulkan",
                    "B4 test: compute the samplers anyway and compare with the reused ones");
REXCVAR_DEFINE_BOOL(ufc_sampler_reuse_verify_break, false, "GPU/Vulkan",
                    "B4 positive control: perturb the reference of the verifier");
namespace ufc_smp {
struct State {
  bool valid = false;
  const void* vs = nullptr;
  const void* ps = nullptr;
  uint64_t submission = 0;
  int32_t aniso = 0;
  uint32_t words[2 * 32 * 6] = {};
  uint32_t word_count = 0;
  uint64_t hits = 0, misses = 0, compared = 0, mismatches = 0, n = 0;
};
inline State& S() {
  static State s;
  return s;
}
}  // namespace ufc_smp

void VulkanCommandProcessor::ClearCaches() {
  ufc_smp::S().valid = false;'''
rep("\nvoid VulkanCommandProcessor::ClearCaches() {", HDR)

LOOP_OLD = '''    uint32_t samplers_overflowed_count = 0;
    for (uint32_t j = 0; j < 2; ++j) {'''
LOOP_NEW = r'''    uint32_t samplers_overflowed_count = 0;
    // UFC3MONS B4: mesmos shaders, mesma submissao e mesmas constantes de busca do desenho anterior
    // -> os parametros (funcao pura das constantes) e os VkSampler (vivos ate o fim da submissao) sao os mesmos.
    auto& ufc_sm = ufc_smp::S();
    const bool ufc_sm_on = REXCVAR_GET(ufc_sampler_reuse) && !i;
    const bool ufc_sm_verify = ufc_sm_on && REXCVAR_GET(ufc_sampler_reuse_verify);
    uint32_t ufc_sm_words[2 * 32 * 6];
    uint32_t ufc_sm_wc = 0;
    bool ufc_sm_hit = false;
    if (ufc_sm_on) {
      for (uint32_t j = 0; j < 2; ++j) {
        const VulkanShader* shader = j ? pixel_shader : vertex_shader;
        if (!shader) continue;
        for (const VulkanShader::SamplerBinding& b : shader->GetSamplerBindingsAfterTranslation()) {
          if (ufc_sm_wc + 6 > rex::countof(ufc_sm_words)) { ufc_sm_wc = UINT32_MAX; break; }
          const uint32_t base = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + b.fetch_constant * 6;
          for (uint32_t w = 0; w < 6; ++w) ufc_sm_words[ufc_sm_wc++] = regs[base + w];
        }
        if (ufc_sm_wc == UINT32_MAX) break;
      }
      ufc_sm_hit = ufc_sm_wc != UINT32_MAX && ufc_sm.valid && ufc_sm.vs == vertex_shader &&
                   ufc_sm.ps == pixel_shader && ufc_sm.submission == GetCurrentSubmission() &&
                   ufc_sm.aniso == REXCVAR_GET(anisotropic_override) &&
                   ufc_sm.word_count == ufc_sm_wc &&
                   !std::memcmp(ufc_sm.words, ufc_sm_words, ufc_sm_wc * sizeof(uint32_t));
      ufc_sm_hit ? ++ufc_sm.hits : ++ufc_sm.misses;
      if ((++ufc_sm.n % 200000) == 0 && (ufc_sm_verify || std::getenv("UFC_MEASURE_DRAW"))) {
        REXGPU_WARN("[ufc-sampler-reuse] verify={} break={} hits={} misses={} compared={} mismatches={}",
                    ufc_sm_verify, REXCVAR_GET(ufc_sampler_reuse_verify_break), ufc_sm.hits,
                    ufc_sm.misses, ufc_sm.compared, ufc_sm.mismatches);
      }
    }
    std::vector<std::pair<VulkanTextureCache::SamplerParameters, VkSampler>> ufc_sm_ref[2];
    if (ufc_sm_hit && ufc_sm_verify) {
      ufc_sm_ref[0] = current_samplers_vertex_;
      ufc_sm_ref[1] = current_samplers_pixel_;
      if (REXCVAR_GET(ufc_sampler_reuse_verify_break)) {
        auto& r = !ufc_sm_ref[1].empty() ? ufc_sm_ref[1] : ufc_sm_ref[0];
        if (!r.empty()) r[0].second = reinterpret_cast<VkSampler>(uintptr_t(r[0].second) ^ 0x10);
      }
    }
    const bool ufc_sm_skip = ufc_sm_hit && !ufc_sm_verify;
    for (uint32_t j = 0; j < 2 && !ufc_sm_skip; ++j) {'''
rep(LOOP_OLD, LOOP_NEW)

AFTER_OLD = '''    if (!samplers_overflowed_count) {
      break;'''
AFTER_NEW = r'''    if (ufc_sm_on) {
      if (ufc_sm_hit && ufc_sm_verify) {
        bool same = ufc_sm_ref[0].size() == current_samplers_vertex_.size() &&
                    ufc_sm_ref[1].size() == current_samplers_pixel_.size();
        for (size_t k = 0; same && k < ufc_sm_ref[0].size(); ++k)
          same = ufc_sm_ref[0][k].first == current_samplers_vertex_[k].first &&
                 ufc_sm_ref[0][k].second == current_samplers_vertex_[k].second;
        for (size_t k = 0; same && k < ufc_sm_ref[1].size(); ++k)
          same = ufc_sm_ref[1][k].first == current_samplers_pixel_[k].first &&
                 ufc_sm_ref[1][k].second == current_samplers_pixel_[k].second;
        ++ufc_sm.compared;
        if (!same) ++ufc_sm.mismatches;
      }
      if (samplers_overflowed_count || ufc_sm_wc == UINT32_MAX) {
        ufc_sm.valid = false;
      } else if (!ufc_sm_skip) {
        ufc_sm.valid = true;
        ufc_sm.vs = vertex_shader;
        ufc_sm.ps = pixel_shader;
        ufc_sm.submission = GetCurrentSubmission();
        ufc_sm.aniso = REXCVAR_GET(anisotropic_override);
        ufc_sm.word_count = ufc_sm_wc;
        std::memcpy(ufc_sm.words, ufc_sm_words, ufc_sm_wc * sizeof(uint32_t));
      }
    } else if (!i) {
      ufc_sm.valid = false;
    }
    if (!samplers_overflowed_count) {
      break;'''
rep(AFTER_OLD, AFTER_NEW)
open(dst, "w", encoding="utf-8", newline="\n").write(t)
print("b4 ok")
