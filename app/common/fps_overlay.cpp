#include "ufc3_init.h"
#include "fps_overlay.h"
#include "ufc_frame_limiter.h"
REXCVAR_DEFINE_INT32(ufc_fps_limit,60,"Display","Guest submission ceiling: 60, 120, 240, or 0 (Unlimited)");
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <imgui.h>

REXCVAR_DEFINE_BOOL(ufc_show_fps, true, "Display", "Show UFC guest swap FPS");
REXCVAR_DEFINE_BOOL(ufc_swap_cadence_diagnostics, false, "Tests",
                    "Log time outside and inside each guest swap in one-second windows");
namespace {
std::atomic<uint64_t> swaps{0};
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

struct SwapCadence {
  void Record(Clock::time_point start, Clock::time_point paced,
              Clock::time_point submitted, Clock::time_point finished) {
    if (previous_finish != Clock::time_point{}) {
      const auto gap = start - previous_finish;
      guest_gap += gap;
      if (gap > guest_gap_max) guest_gap_max = gap;
      limiter += paced - start;
      swap += submitted - paced;
      capture += finished - submitted;
      ++frames;
      const double real_ms = Milliseconds(finished - window_start).count();
      if (real_ms >= 1000.0) {
        REXLOG_INFO("[ufc-swap-cadence] frames={} fps={:.2f} outside_swap_ms={:.3f} "
                    "outside_swap_max_ms={:.3f} limiter_ms={:.3f} vd_swap_ms={:.3f} capture_ms={:.3f}",
                    frames, frames * 1000.0 / real_ms, Milliseconds(guest_gap).count() / frames,
                    Milliseconds(guest_gap_max).count(), Milliseconds(limiter).count() / frames,
                    Milliseconds(swap).count() / frames, Milliseconds(capture).count() / frames);
        window_start = finished;
        frames = 0;
        guest_gap = guest_gap_max = limiter = swap = capture = Clock::duration::zero();
      }
    } else {
      window_start = finished;
    }
    previous_finish = finished;
  }

  Clock::time_point previous_finish{}, window_start{};
  Clock::duration guest_gap{}, guest_gap_max{}, limiter{}, swap{}, capture{};
  unsigned frames = 0;
};

class FpsOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
 private:
  void OnDraw(ImGuiIO&) override {
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - last_time_).count();
    const auto count = swaps.load(std::memory_order_relaxed);
    if (seconds >= 1.0) {
      fps_ = static_cast<double>(count - last_count_) / seconds;
      measured_ = true;
      REXLOG_INFO("[ufc-fps] guest_swaps={} delta={} seconds={:.3f} fps={:.2f}",
                  count, count - last_count_, seconds, fps_);
      last_count_ = count;
      last_time_ = now;
    }
    if (rex::cvar::GetFlagByName("ufc_show_fps") != "true") return;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.72f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##ufc_fps", nullptr, flags)) {
      if (measured_) ImGui::Text("%.1f FPS | jogo", fps_);
      else ImGui::TextUnformatted("FPS | aguardando quadros");
      const int cap = GetUfcFrameLimit();
      // The game simulation advances one step per frame, so it stays at 60.
      if (cap) ImGui::Text("Limite: %d FPS", cap);
      else ImGui::TextUnformatted("Limite: desligado (teste)");
    }
    ImGui::End();
  }
  Clock::time_point last_time_ = Clock::now();
  uint64_t last_count_ = 0;
  double fps_ = 0;
  bool measured_ = false;
};
}
// Linker wrapping intercepts both the direct import and the function table.
// Forward the unchanged context once; count completed guest swap submissions.
// This does not count interpolated frames, host UI redraws or physical scanout.
extern void ShadowCaptureTick();
extern "C" void __real___imp__VdSwap(PPCContext& ctx, uint8_t* base);
extern "C" void __wrap___imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  static thread_local ufc::FrameLimiter limiter;
  static const bool trace_cadence = REXCVAR_GET(ufc_swap_cadence_diagnostics);
  const auto started = trace_cadence ? Clock::now() : Clock::time_point{};
  // Registry reads share the setter mutex; direct cvar storage is a plain int.
  const auto requested_text = rex::cvar::GetFlagByName("ufc_fps_limit");
  const int requested = std::atoi(requested_text.c_str());
  const int cap = ufc::NormalizeFrameCap(requested);
  static thread_local int previous_cap = -1;
  if (cap != previous_cap) {
    REXLOG_INFO("[ufc-fps-limit] requested={} effective={} unlimited={} cadence=absolute", requested, cap, cap == 0);
    previous_cap = cap;
  }
  limiter.Pace(cap);
  const auto paced = trace_cadence ? Clock::now() : Clock::time_point{};
  __real___imp__VdSwap(ctx, base);
  const auto submitted = trace_cadence ? Clock::now() : Clock::time_point{};
  swaps.fetch_add(1, std::memory_order_relaxed);
  ShadowCaptureTick();
  if (trace_cadence) {
    static thread_local SwapCadence cadence;
    cadence.Record(started, paced, submitted, Clock::now());
  }
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateUfcFpsOverlay(rex::ui::ImGuiDrawer* drawer) {
  return std::make_unique<FpsOverlay>(drawer);
}

int GetUfcFrameLimit() {
  const auto value = rex::cvar::GetFlagByName("ufc_fps_limit");
  return ufc::NormalizeFrameCap(std::atoi(value.c_str()));
}
void CycleUfcFrameLimit() {
  const int cap = ufc::NextFrameCap(GetUfcFrameLimit());
  rex::cvar::SetFlagByName("ufc_fps_limit", std::to_string(cap));
  REXLOG_INFO("[ufc-fps-limit-selection] selected={} unlimited={}", cap, cap == 0);
}
