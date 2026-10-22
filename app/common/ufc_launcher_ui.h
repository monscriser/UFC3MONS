#pragma once
#include <memory>
#include <filesystem>
#include <rex/ui/imgui_dialog.h>
namespace ufc {class Launcher;
std::shared_ptr<Launcher> CreateLauncher(rex::ui::ImGuiDrawer*,rex::ui::ImmediateDrawer*,const std::filesystem::path&);
bool LauncherRunning(const Launcher*);
void DestroyLauncher(std::shared_ptr<Launcher>&);
// UFC icon on the game/launcher window (native Windows icon / SDL Linux icon).
void ApplyWindowIcon(void* native_window);
}
