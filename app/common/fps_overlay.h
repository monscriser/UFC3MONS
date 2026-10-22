#pragma once
#include <memory>
#include <rex/ui/imgui_dialog.h>
std::unique_ptr<rex::ui::ImGuiDialog> CreateUfcFpsOverlay(rex::ui::ImGuiDrawer* drawer);

int GetUfcFrameLimit();
void CycleUfcFrameLimit();
