// UFC3MONS R8 - two app-side fixes, no SDK/runtime change.
//
// 1) Input lock. rex::input::InputSystem has no lock: GetCapabilities,
//    GetState, SetState (rumble) and GetKeystroke all rebuild devices_ /
//    device_owners_ through RefreshDevices(). The game polls the pad on one
//    thread and sends rumble from another, so two RefreshDevices() could run
//    at once and corrupt the heap (Windows dump 21/10 19:31: guest
//    XamInputSetState -> InputSystem::SetState -> RefreshDevices -> ntdll heap
//    access violation). The four guest imports are wrapped with --wrap and
//    serialized here. Contended calls are counted to show the race exists.
//
// 2) Present on guest frame. Any ImGui dialog (the FPS overlay is one) makes
//    ImGuiDrawer request a UI repaint after every paint, so the presenter
//    re-presents the same guest image at the UI tick rate. External counters
//    (MangoHud, RTSS/Afterburner) count those presents, not game frames. In
//    game mode the ImGui drawer is detached from the presenter and drawn by a
//    proxy UI drawer: the presenter then paints only when the guest refreshes
//    its output (one present per game frame). Paints are counted per second.
#include "ufc3_init.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_drawer.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

REXCVAR_DEFINE_BOOL(ufc_input_lock, true, "Input",
                    "Serialize guest XamInput calls (prevents the rumble heap crash)");
REXCVAR_DEFINE_BOOL(ufc_present_on_guest_frame, true, "Display",
                    "Present only when the game delivers a frame (MangoHud/Afterburner show the game FPS)");

namespace {
std::mutex input_mutex;
std::atomic<uint64_t> input_calls{0}, input_contended{0};
std::atomic<int64_t> input_last_log_ns{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void NoteContention(const char* name) {
  const uint64_t contended = input_contended.fetch_add(1, std::memory_order_relaxed) + 1;
  const int64_t now = NowNs();
  int64_t last = input_last_log_ns.load(std::memory_order_relaxed);
  if (now - last >= 10'000'000'000LL && input_last_log_ns.compare_exchange_strong(last, now)) {
    REXLOG_INFO("[ufc-input-lock] contended={} calls={} last={}", contended,
                input_calls.load(std::memory_order_relaxed), name);
  }
}
}  // namespace

void UfcInputLockedCall(void (*real)(PPCContext&, uint8_t*), PPCContext& ctx, uint8_t* base,
                        const char* name) {
  if (!REXCVAR_GET(ufc_input_lock)) {
    real(ctx, base);
    return;
  }
  input_calls.fetch_add(1, std::memory_order_relaxed);
  std::unique_lock lock(input_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    NoteContention(name);
    lock.lock();
  }
  real(ctx, base);
}

// XamInputGetState is wrapped in ufc_test_input.cpp, which calls
// UfcInputLockedCall for the real import.
extern "C" void __real___imp__XamInputGetCapabilities(PPCContext&, uint8_t*);
extern "C" void __real___imp__XamInputSetState(PPCContext&, uint8_t*);
extern "C" void __real___imp__XamInputGetKeystrokeEx(PPCContext&, uint8_t*);
extern "C" void __wrap___imp__XamInputGetCapabilities(PPCContext& ctx, uint8_t* base) {
  UfcInputLockedCall(__real___imp__XamInputGetCapabilities, ctx, base, "GetCapabilities");
}
extern "C" void __wrap___imp__XamInputSetState(PPCContext& ctx, uint8_t* base) {
  UfcInputLockedCall(__real___imp__XamInputSetState, ctx, base, "SetState");
}
extern "C" void __wrap___imp__XamInputGetKeystrokeEx(PPCContext& ctx, uint8_t* base) {
  UfcInputLockedCall(__real___imp__XamInputGetKeystrokeEx, ctx, base, "GetKeystrokeEx");
}

namespace {
// Counts presenter UI paints; each one is one vkQueuePresent of the window.
class PaintCounter {
 public:
  void Tick(bool guest_paced) {
    const int64_t now = NowNs();
    ++paints_;
    if (!start_) start_ = now;
    if (now - start_ >= 1'000'000'000LL) {
      const double seconds = double(now - start_) / 1e9;
      REXLOG_INFO("[ufc-present] paints={} seconds={:.3f} paints_per_s={:.2f} guest_paced={}",
                  paints_, seconds, paints_ / seconds, guest_paced);
      paints_ = 0;
      start_ = now;
    }
  }
 private:
  uint64_t paints_ = 0;
  int64_t start_ = 0;
};

class GuestFrameUiDrawer final : public rex::ui::UIDrawer {
 public:
  GuestFrameUiDrawer(rex::ui::ImGuiDrawer* drawer, bool guest_paced)
      : drawer_(drawer), guest_paced_(guest_paced) {}
  void Draw(rex::ui::UIDrawContext& context) override {
    counter_.Tick(guest_paced_);
    if (guest_paced_) drawer_->Draw(context);
  }
 private:
  rex::ui::ImGuiDrawer* drawer_;
  bool guest_paced_;
  PaintCounter counter_;
};

rex::ui::Presenter* attached_presenter = nullptr;
std::unique_ptr<GuestFrameUiDrawer> ui_proxy;
}  // namespace

// Called from OnCreateDialogs (UI thread) in game mode, after the FPS overlay
// exists. With ufc_present_on_guest_frame=false the old continuous repaint is
// kept and only a counting drawer is added (control for the paint counter).
void UfcAttachOverlayToGuestFrames(rex::ui::ImGuiDrawer* drawer, rex::ui::Presenter* presenter) {
  if (!drawer || !presenter || ui_proxy) {
    REXLOG_WARN("[ufc-present] guest-frame presentation unavailable drawer={} presenter={}",
                drawer != nullptr, presenter != nullptr);
    return;
  }
  const bool guest_paced = REXCVAR_GET(ufc_present_on_guest_frame);
  // Detaching stops ImGuiDrawer::Draw from requesting a repaint after each
  // paint; input listening is unaffected.
  if (guest_paced) drawer->SetPresenter(nullptr);
  ui_proxy = std::make_unique<GuestFrameUiDrawer>(drawer, guest_paced);
  // Same Z order ReXApp gives the ImGui drawer.
  presenter->AddUIDrawerFromUIThread(ui_proxy.get(), 64);
  attached_presenter = presenter;
  REXLOG_INFO("[ufc-present] guest_paced={}", guest_paced);
}

// Called from OnShutdown, before ReXApp destroys the ImGui drawer.
void UfcDetachOverlayFromGuestFrames() {
  if (ui_proxy && attached_presenter) attached_presenter->RemoveUIDrawerFromUIThread(ui_proxy.get());
  ui_proxy.reset();
  attached_presenter = nullptr;
}
