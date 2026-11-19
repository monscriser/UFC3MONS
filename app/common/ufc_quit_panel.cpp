// In-game quit panel (BETA 1.8). F4 on the keyboard, or Back + Start held for three seconds on the
// controller, opens a small UFC3MONS panel over the game with "Quit game" and "Back to the game".
// On the controller: A quits, B goes back. The touch screen or mouse can press the buttons too.
// Quitting asks the game window to close, the same path as closing the window by hand.
//
// Own implementation. The idea of a hotkey panel with Quit comes from UFC3ReX (F4 menu); no code
// from that project is used.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xtypes.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

REXCVAR_DEFINE_BOOL(ufc_quit_panel, true, "UFC",
                    "F4 or Back + Start (held 3 s) opens a panel to quit the game");

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint16_t kBack = rex::input::X_INPUT_GAMEPAD_BACK, kStart = rex::input::X_INPUT_GAMEPAD_START;
constexpr uint16_t kA = rex::input::X_INPUT_GAMEPAD_A, kB = rex::input::X_INPUT_GAMEPAD_B;
constexpr auto kComboHold = std::chrono::milliseconds(3000);  // 3 s: a quick bump of Back + Start must not open it

bool g_toggle_request = false;  // set by the F4 bind (UI thread, like OnDraw)

// Xbox 360 language ids as passed to --user_language: 5 = Spanish, 9 = Portuguese.
const char* L(const char* pt, const char* en, const char* es) {
  const std::string lang = rex::cvar::GetFlagByName("user_language");
  if (lang == "9") return pt;
  if (lang == "5") return es;
  return en;
}

// Controller buttons of user 0 as the game itself last read them (XamInputGetState).
// The panel must NOT call InputSystem::GetState from the UI thread: InputSystem is not thread-safe
// and the game polls it at the same time (that is why guest input calls go through
// UfcInputLockedCall). Measured on the ROG Ally (Linux, 18/11/2026): the UI thread (QuitPanel::OnDraw)
// and a game thread (XamInputGetState) both ended inside InputSystem::RefreshDevices, each at 100% CPU,
// so no frame was ever presented ("no image"). It is a race, so some runs start fine.
std::atomic<uint16_t> g_pad_buttons{0};
std::atomic<bool> g_pad_observed{false};  // true once the XamInputGetState wrapper reports buttons
}  // namespace
struct PPCContext;
void UfcInputLockedCall(void (*real)(PPCContext&, uint8_t*), PPCContext& ctx, uint8_t* base, const char* name);
namespace {
// Fallback while the wrapper does not report (an older ufc_test_input object): read the pad inside the
// same input lock the game's XamInput calls take (UfcInputLockedCall, ufc_r8_fixes.cpp), so the two
// threads never run InputSystem at the same time.
uint16_t g_locked_read = 0;
void LockedRead(PPCContext&, uint8_t*) {
  g_locked_read = 0;
  auto* runtime = rex::Runtime::instance();
  if (!runtime || !runtime->input_system()) return;
  auto* input = static_cast<rex::input::InputSystem*>(runtime->input_system());
  rex::input::X_INPUT_STATE state{};
  if (input->GetState(0, &state) == rex::X_RESULT(0)) g_locked_read = uint16_t(state.gamepad.buttons);
}
uint16_t PadButtons() {
  if (g_pad_observed.load(std::memory_order_relaxed)) return g_pad_buttons.load(std::memory_order_relaxed);
  alignas(64) static unsigned char dummy[64];
  UfcInputLockedCall(LockedRead, *reinterpret_cast<PPCContext*>(dummy), nullptr, "QuitPanel");
  return g_locked_read;
}

class QuitPanel final : public rex::ui::ImGuiDialog {
 public:
  QuitPanel(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) : ImGuiDialog(drawer), window_(window) {}

 private:
  void OnDraw(ImGuiIO& io) override {
    if (!REXCVAR_GET(ufc_quit_panel)) { open_ = false; g_toggle_request = false; return; }
    const uint16_t pad = PadButtons();
    const uint16_t pressed = pad & ~previous_;
    previous_ = pad;
    const auto now = Clock::now();

    // Back + Start held for three seconds toggles the panel; it fires once per hold.
    const bool combo = (pad & kBack) && (pad & kStart);
    if (!combo) { combo_since_.reset(); combo_fired_ = false; }
    else if (!combo_since_) combo_since_ = now;
    else if (!combo_fired_ && now - *combo_since_ >= kComboHold) { combo_fired_ = true; g_toggle_request = true; }

    if (g_toggle_request) {
      g_toggle_request = false;
      open_ = !open_;
      REXLOG_INFO("[ufc-quit-panel] {}", open_ ? "opened" : "closed");
      return;  // the button presses of this frame belong to the shortcut
    }
    if (!open_) return;

    bool quit = (pressed & kA) != 0;
    bool back = (pressed & kB) != 0 || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    const ImVec2 size(std::min(520.f, io.DisplaySize.x * 0.8f), 0);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(16, 17, 21, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(200, 30, 40, 255));
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##ufc_quit_panel", nullptr, flags)) {
      ImFont* big = io.Fonts->Fonts.Size > 2 ? io.Fonts->Fonts[2] : io.Fonts->Fonts[0];
      ImGui::PushFont(big, io.Fonts->Fonts.Size > 2 ? big->LegacySize : 28.f);
      ImGui::TextUnformatted("UFC3MONS");
      ImGui::PopFont();
      ImGui::Spacing();
      ImGui::TextWrapped("%s", L("Deseja sair do jogo? O progresso que não foi salvo será perdido.",
                                 "Quit the game? Progress that was not saved will be lost.",
                                 "¿Salir del juego? Se perderá el progreso no guardado."));
      ImGui::Spacing();
      ImGui::Spacing();
      const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 30, 40, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(225, 50, 60, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(170, 20, 30, 255));
      if (ImGui::Button(L("Sair do jogo  (A)", "Quit game  (A)", "Salir del juego  (A)"), ImVec2(width, 48)))
        quit = true;
      ImGui::PopStyleColor(3);
      ImGui::SameLine();
      if (ImGui::Button(L("Voltar ao jogo  (B)", "Back to game  (B)", "Volver al juego  (B)"), ImVec2(width, 48)))
        back = true;
      ImGui::Spacing();
      ImGui::TextDisabled("%s", L("F4 ou Back + Start (segurar 3 s) abre e fecha este painel.",
                                  "F4 or Back + Start (hold 3 s) opens and closes this panel.",
                                  "F4 o Back + Start (mantener 3 s) abre y cierra este panel."));
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    if (quit) {
      REXLOG_INFO("[ufc-quit-panel] quit requested");
      rex::FlushLogging();
      open_ = false;
      if (window_) window_->RequestClose();
    } else if (back) {
      open_ = false;
      REXLOG_INFO("[ufc-quit-panel] closed");
    }
  }

  rex::ui::Window* window_ = nullptr;
  bool open_ = false;
  uint16_t previous_ = 0;
  std::optional<Clock::time_point> combo_since_;
  bool combo_fired_ = false;
};
}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateUfcQuitPanel(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) {
  return std::make_unique<QuitPanel>(drawer, window);
}
void UfcToggleQuitPanel() { g_toggle_request = true; }
// Called by __wrap___imp__XamInputGetState (inside UfcInputLockedCall's lock) after a successful
// read for user index 0, with the host-order wButtons the wrapper already decodes.
void UfcQuitPanelObservePad(uint16_t buttons) {
  g_pad_buttons.store(buttons, std::memory_order_relaxed);
  g_pad_observed.store(true, std::memory_order_relaxed);
}
