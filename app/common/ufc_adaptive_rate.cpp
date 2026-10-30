// Slow motion below 60 FPS (item 5 of the 28/10 roadmap; Ian 29/10: any rate between 60 and 30
// must keep normal speed, only less smooth).
//
// Guest code (same addresses in Reborn and Original, see optimization-p10-25-10/present-path):
//  0x833C80B8    rate block: +8 rate (60/50/30/25), +12 lock, +28 frames per second (float),
//                +32 ms per frame (float), +36 ms per frame (int, truncated), +56 step multiplier
//                (float, 1 at 60), +60 1/multiplier. Logged 29/10: 60 -> 60, 16.666666, 16, 1, 1;
//                30 -> 30, 33.333332, 33, 2, 0.5 (and the setters' 50/25 cases give 1.2/2.4).
//  sub_82748DC8 / sub_82748D38  full / half rate: rewrite the block. The game uses half rate for its
//                presentations, which is why they are smooth 30 FPS at normal speed.
//  sub_8273BBE0  once per frame: measures the frame and turns the rate into the present interval.
// The fight keeps the 60 block, so a PC that only renders 40 FPS steps 1/60 s per frame = 67% speed.
// ufc_adaptive_rate: at each frame of a full-rate (60) block, write the block for the real frame time T,
// clamped to the game's own range 16.67..33.33 ms (60..30 FPS): every value is linear in T, exactly
// like the game's 60/50/30 cases (at 30 FPS the values are exactly the ones the game uses in its cutscenes). Frames that keep up (T < 17.5 ms) get the untouched 60 values.
// The rate field (+8) stays 60, so the present interval stays 1 and the game's own logic is unchanged.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

REXCVAR_DEFINE_BOOL(ufc_adaptive_rate, true, "Display",
                    "Keep normal game speed between 60 and 30 FPS (step follows the real frame time); default ON");
REXCVAR_DEFINE_BOOL(ufc_rate_diagnostics, false, "Tests",
                    "Log the rate block, real FPS and simulated time every 2 s ([ufc-rate])");
REXCVAR_DEFINE_INT32(ufc_rate_test_delay_ms, 0, "Tests",
                     "Test only: extra wait per frame to imitate a slow PC (e.g. 9 = about 40 FPS); 0 = off");

extern "C" void __imp__sub_8273BBE0(PPCContext& ctx, uint8_t* base);

namespace {
constexpr uint32_t kBlock = 0x833C80B8;
constexpr double kFullMs = 1000.0 / 60.0, kHalfMs = 1000.0 / 30.0;
using Clock = std::chrono::steady_clock;

float LoadF32(uint8_t* base, uint32_t address) {
  const uint32_t bits = REX_LOAD_U32(address);
  float value;
  std::memcpy(&value, &bits, 4);
  return value;
}
void StoreF32(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  REX_STORE_U32(address, bits);
}

struct State {
  Clock::time_point last{}, diag_start{};
  double smooth_ms = kFullMs;
  bool adjusted = false;  // the block holds our values (restore the 60 ones when frames keep up)
  int diag_frames = 0;
  double diag_sim_ms = 0, diag_min = 1e9, diag_max = 0;
  uint32_t adjusted_frames = 0;
};
State g;

// The game's 60 block, written back exactly when frames keep up again.
void WriteBlock(uint8_t* base, double t) {
  if (t <= kFullMs) {
    StoreF32(base, kBlock + 28, 60.0f); StoreF32(base, kBlock + 32, 16.666666f);
    REX_STORE_U32(kBlock + 36, 16); StoreF32(base, kBlock + 56, 1.0f); StoreF32(base, kBlock + 60, 1.0f);
    return;
  }
  StoreF32(base, kBlock + 28, float(1000.0 / t)); StoreF32(base, kBlock + 32, float(t));
  REX_STORE_U32(kBlock + 36, uint32_t(t)); StoreF32(base, kBlock + 56, float(t / kFullMs)); StoreF32(base, kBlock + 60, float(kFullMs / t));
}

void Frame(uint8_t* base) {
  if (const int delay = REXCVAR_GET(ufc_rate_test_delay_ms)) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  const auto now = Clock::now();
  const double frame_ms = g.last.time_since_epoch().count() ? std::chrono::duration<double, std::milli>(now - g.last).count() : kFullMs;
  g.last = now;
  const uint32_t rate = REX_LOAD_U32(kBlock + 8);

  if (REXCVAR_GET(ufc_adaptive_rate) && rate == 60 && REX_LOAD_U32(kBlock + 12) == 0) {
    // Short average: follows a drop within a few frames, ignores single-frame jitter.
    g.smooth_ms += (std::clamp(frame_ms, kFullMs, kHalfMs) - g.smooth_ms) * 0.25;
    const double t = g.smooth_ms < 17.5 ? kFullMs : std::min(g.smooth_ms, kHalfMs);
    if (t > kFullMs || g.adjusted) { WriteBlock(base, t); g.adjusted = t > kFullMs; }
    if (g.adjusted) ++g.adjusted_frames;
  } else {
    g.smooth_ms = kFullMs;
    g.adjusted = false;  // the game's own setters rewrote the block (30 presentations)
  }

  if (REXCVAR_GET(ufc_rate_diagnostics)) {
    static uint32_t logged_rate = 0;
    if (rate != logged_rate) {
      logged_rate = rate;
      REXLOG_INFO("[ufc-rate] change rate={} lock={} ms={} f28={} f32={} f56={} f60={}", rate, REX_LOAD_U32(kBlock + 12),
                  REX_LOAD_U32(kBlock + 36), LoadF32(base, kBlock + 28), LoadF32(base, kBlock + 32), LoadF32(base, kBlock + 56),
                  LoadF32(base, kBlock + 60));
    }
    if (!g.diag_start.time_since_epoch().count()) g.diag_start = now;
    // Simulated time of this frame = the step the block gives it.
    ++g.diag_frames; g.diag_sim_ms += LoadF32(base, kBlock + 32);
    g.diag_min = std::min(g.diag_min, frame_ms); g.diag_max = std::max(g.diag_max, frame_ms);
    const double real_ms = std::chrono::duration<double, std::milli>(now - g.diag_start).count();
    if (real_ms >= 2000) {
      REXLOG_INFO("[ufc-rate] fps={:.1f} rate={} sim_per_real={:.3f} step_ms={:.2f} mult={:.3f} frame_ms={:.1f}..{:.1f} adjusted_frames={}",
                  g.diag_frames * 1000.0 / real_ms, rate, g.diag_sim_ms / real_ms, LoadF32(base, kBlock + 32), LoadF32(base, kBlock + 56),
                  g.diag_min, g.diag_max, g.adjusted_frames);
      g.diag_frames = 0; g.diag_sim_ms = 0; g.diag_min = 1e9; g.diag_max = 0; g.diag_start = now;
    }
  }
}
}  // namespace

REX_HOOK_RAW(sub_8273BBE0) {
  Frame(base);
  __imp__sub_8273BBE0(ctx, base);
}
