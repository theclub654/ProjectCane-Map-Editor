#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

#ifndef IMGUI_HAS_DOCK
#error ProjectCane editor requires the bundled docking-enabled ImGui sources.
#endif

// Layout only: never reads or mutates level data. DockBuilder is deliberately
// paired with the pinned bundled ImGui version (v1.90.9-docking).
inline void DrawEditorDockLayout(const float menuHeight, const float statusHeight,
                                 const float preferredLeftWidth, const float preferredRightWidth,
                                 bool &resetLayout)
{
    const auto *viewport = ImGui::GetMainViewport();
    const ImVec2 position(viewport->Pos.x, viewport->Pos.y + menuHeight);
    const ImVec2 size((std::max)(viewport->Size.x, 1.0f),
                      (std::max)(viewport->Size.y - menuHeight - statusHeight, 1.0f));
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    const auto flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Editor dock host", nullptr, flags);
    ImGui::PopStyleVar(3);
    const auto dockId = ImGui::GetID("ProjectCaneMainDock");
    if (resetLayout || ImGui::DockBuilderGetNode(dockId) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, size);
        ImGuiID center = dockId, left = 0, right = 0;
        const float leftRatio = std::clamp(preferredLeftWidth / size.x, 0.10f, 0.35f);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftRatio, &left, &center);
        const float remainingWidth = (std::max)(size.x * (1.0f - leftRatio), 1.0f);
        const float rightRatio = std::clamp(preferredRightWidth / remainingWidth, 0.15f, 0.50f);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightRatio, &right, &center);
        ImGui::DockBuilderDockWindow("Scene", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dockId);
        resetLayout = false;
    }
    // Submit every frame, including when another window covers the host.
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f));
    ImGui::End();
}
