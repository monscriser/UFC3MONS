#!/usr/bin/env python3
"""SO MEDICAO (UFC_MEASURE_DRAW=1): cronometros por fase do IssueDraw + contadores de registradores.
Le gpu-port-blood/vulkan_command_processor.cpp e grava vcp_measure.cpp aqui. Nao muda o jogo."""
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src, encoding="utf-8").read()
def rep(old, new, count=1):
    global t
    n = t.count(old)
    assert n == count, (n, old[:80])
    t = t.replace(old, new)

HDR = r'''
namespace ufc_dm {
using clk = std::chrono::steady_clock;
static const bool kOn = std::getenv("UFC_MEASURE_DRAW") != nullptr;
enum { kAnalyze, kPrim, kModTrans, kSamplers, kTextures, kRt, kPipeline, kPreBind, kBindings, kTail,
       kType0, kFromMem, kCount };
static const char* const kNames[kCount] = {"analyze","prim_submit","mod_trans","samplers","textures",
  "rt_update","pipeline","pre_bind","bindings","tail","reg_type0","reg_frommem"};
struct State {
  uint64_t ns[kCount] = {};
  uint64_t draws = 0, wr = 0, wr_same = 0, wr_float = 0, wr_fetch = 0, t0_calls = 0, t0_regs = 0,
           fm_calls = 0, fm_regs = 0, fm_same = 0, same_rt = 0, same_smp = 0, same_pipe = 0, n_smp = 0;
  const void* prev_fb = nullptr; const void* prev_pipe = nullptr; uint64_t thr0 = 0;
  std::vector<uint32_t> prev_smp, cur_smp;
  clk::time_point last{}, win{};
};
inline State& S() { static State s; return s; }
inline void Lap(int p) {
  if (!kOn) return;
  auto& s = S(); auto n = clk::now();
  s.ns[p] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(n - s.last).count());
  s.last = n;
}
inline void Report() {
  auto& s = S(); auto n = clk::now();
  auto thr = [] { FILETIME a, b, k, u; GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u);
    return ((uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime)) * 100; };
  if (s.win.time_since_epoch().count() == 0) { s.win = n; s.thr0 = thr(); return; }
  double sec = std::chrono::duration<double>(n - s.win).count();
  if (sec < 5.0) return;
  std::string o = fmt::format("[ufc-dm] sec={:.2f} draws_per_s={:.0f}", sec, s.draws / sec);
  uint64_t tot = 0;
  for (int i = 0; i < kCount; ++i) {
    if (i < kType0) tot += s.ns[i];
    o += fmt::format(" {}={:.3f}", kNames[i], s.ns[i] / 1e6 / sec);  // ms por segundo
  }
  o += fmt::format(" draw_total={:.3f} us_per_draw={:.3f} wr_per_s={:.0f} wr_same={:.0f} wr_float={:.0f} "
                   "wr_fetch={:.0f} t0_calls={:.0f} t0_regs={:.0f} fm_calls={:.0f} fm_regs={:.0f} thread_cpu={:.3f} same_rt={:.3f} same_smp={:.3f} smp_per_draw={:.2f} same_pipe={:.3f}",
                   tot / 1e6 / sec, s.draws ? tot / 1e3 / s.draws : 0.0, s.wr / sec, s.wr_same / sec,
                   s.wr_float / sec, s.wr_fetch / sec, s.t0_calls / sec, s.t0_regs / sec,
                   s.fm_calls / sec, s.fm_regs / sec, (thr() - s.thr0) / 1e6 / sec,
                   double(s.same_rt) / std::max<uint64_t>(s.draws, 1), double(s.same_smp) / std::max<uint64_t>(s.draws, 1),
                   double(s.n_smp) / std::max<uint64_t>(s.draws, 1), double(s.same_pipe) / std::max<uint64_t>(s.draws, 1));
  REXGPU_WARN("{}", o);
  uint64_t t1 = thr();
  for (auto& v : s.ns) v = 0;
  s.draws = s.wr = s.wr_same = s.wr_float = s.wr_fetch = s.t0_calls = s.t0_regs = s.fm_calls = s.fm_regs = 0;
  s.same_rt = s.same_smp = s.same_pipe = s.n_smp = 0; s.win = n; s.thr0 = t1;
}
struct DrawScope {
  DrawScope() { if (kOn) S().last = clk::now(); }
  ~DrawScope() { if (kOn) { Lap(kTail); ++S().draws; Report(); } }
};
struct Timed {
  int p; clk::time_point b;
  explicit Timed(int p_) : p(p_) { if (kOn) b = clk::now(); }
  ~Timed() { if (kOn) S().ns[p] += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now() - b).count()); }
};
}  // namespace ufc_dm

void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {'''
rep("\nvoid VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {", HDR)
rep('''      index < RegisterFile::kRegisterCount ? register_file_->values[index] : ~value;
  CommandProcessor::WriteRegister(index, value);''',
'''      index < RegisterFile::kRegisterCount ? register_file_->values[index] : ~value;
  if (ufc_dm::kOn) {
    auto& s = ufc_dm::S(); ++s.wr; s.wr_same += (ufc_old_value == value);
    s.wr_float += (index >= XE_GPU_REG_SHADER_CONSTANT_000_X && index <= XE_GPU_REG_SHADER_CONSTANT_511_W);
    s.wr_fetch += (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 && index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5);
  }
  CommandProcessor::WriteRegister(index, value);''')
rep('''void VulkanCommandProcessor::UfcWriteType0Range(memory::RingBuffer* ring, uint32_t base,
                                              uint32_t count) {''',
'''void VulkanCommandProcessor::UfcWriteType0Range(memory::RingBuffer* ring, uint32_t base,
                                              uint32_t count) {
  if (ufc_dm::kOn) { ++ufc_dm::S().t0_calls; ufc_dm::S().t0_regs += count; }''')
rep('''                                                   uint32_t num_registers) {
  if (!num_registers) {
    return;
  }''',
'''                                                   uint32_t num_registers) {
  if (!num_registers) {
    return;
  }
  if (ufc_dm::kOn) { auto& s = ufc_dm::S(); ++s.fm_calls; s.fm_regs += num_registers; }''')
rep('''  bool surface_pitch_is_zero = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch == 0;''',
'''  ufc_dm::DrawScope ufc_dm_scope;
  bool surface_pitch_is_zero = regs.Get<reg::RB_SURFACE_INFO>().surface_pitch == 0;''')
rep('''  for (uint32_t i = 0; i < 2; ++i) {
    if (!BeginSubmission(true)) {''', '''  ufc_dm::Lap(ufc_dm::kAnalyze);
  for (uint32_t i = 0; i < 2; ++i) {
    if (!BeginSubmission(true)) {''')
rep('''    // Shader modifications.
    vertex_shader_modification =''', '''    ufc_dm::Lap(ufc_dm::kPrim);
    // Shader modifications.
    vertex_shader_modification =''')
rep('''    // Obtain the samplers. Note''', '''    ufc_dm::Lap(ufc_dm::kModTrans);
    // Obtain the samplers. Note''')
rep('''    if (!samplers_overflowed_count) {
      break;''', '''    ufc_dm::Lap(ufc_dm::kSamplers);
    if (ufc_dm::kOn && !i) {
      auto& s = ufc_dm::S(); s.cur_smp.clear();
      for (auto& sp : current_samplers_vertex_) s.cur_smp.push_back(sp.first.value);
      s.cur_smp.push_back(0xFFFFFFFFu);
      for (auto& sp : current_samplers_pixel_) s.cur_smp.push_back(sp.first.value);
      s.n_smp += s.cur_smp.size() - 1; s.same_smp += (s.cur_smp == s.prev_smp); s.prev_smp.swap(s.cur_smp);
      s.last = ufc_dm::clk::now();
    }
    if (!samplers_overflowed_count) {
      break;''')
rep('''  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;
  // Set up the render targets''', '''  ufc_dm::Lap(ufc_dm::kTextures);
  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;
  // Set up the render targets''')
rep('''  const bool ufc_use_dynamic_rendering =
      REXCVAR_GET(vulkan_dynamic_rendering) &&
      GetVulkanDevice()->properties().dynamicRendering;
  const bool ufc_stays_in_current_pass =''', '''  ufc_dm::Lap(ufc_dm::kRt);
  if (ufc_dm::kOn) { auto& s = ufc_dm::S(); const void* fb = render_target_cache_->last_update_framebuffer();
    s.same_rt += (fb == s.prev_fb); s.prev_fb = fb; }
  const bool ufc_use_dynamic_rendering =
      REXCVAR_GET(vulkan_dynamic_rendering) &&
      GetVulkanDevice()->properties().dynamicRendering;
  const bool ufc_stays_in_current_pass =''')
rep('''  bool pipeline_is_placeholder = false;
  // Reload the current handle state''', '''  ufc_dm::Lap(ufc_dm::kPipeline);
  if (ufc_dm::kOn) { auto& s = ufc_dm::S(); s.same_pipe += (pipeline_handle == s.prev_pipe); s.prev_pipe = pipeline_handle; }
  bool pipeline_is_placeholder = false;
  // Reload the current handle state''')
rep('''  if (!UpdateBindings(vertex_shader, pixel_shader)) {
    return draw_fail("update_bindings");
  }''', '''  ufc_dm::Lap(ufc_dm::kPreBind);
  if (!UpdateBindings(vertex_shader, pixel_shader)) {
    return draw_fail("update_bindings");
  }
  ufc_dm::Lap(ufc_dm::kBindings);''')
open(dst, "w", encoding="utf-8").write(t)
print("ok")
