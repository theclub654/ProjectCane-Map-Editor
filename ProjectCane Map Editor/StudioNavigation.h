#pragma once

#include <imgui.h>

// Navigation requests only. The application owns all document-changing actions.
enum class StudioWorkspace
{
    Scene,
    Assets,
    Collision,
    Gameplay,
    Animation,
    Splice,
    Visibility
};

enum class StudioAction
{
    None,
    Open,
    Save,
    Undo,
    Redo,
    Add,
    Export,
    Issues,
    Help
};

inline const char *StudioWorkspaceHint(const StudioWorkspace workspace)
{
    switch (workspace)
    {
    case StudioWorkspace::Scene:
        return "Select in Scene, move with W/E/R, and use F to frame the selection.";
    case StudioWorkspace::Assets:
        return "Browse models and materials. Model instances share their source geometry.";
    case StudioWorkspace::Collision:
        return "Inspect collision ownership before moving geometry. Model-only edits do not move collision.";
    case StudioWorkspace::Gameplay:
        return "Select a gameplay object to see its behavior, relationships, and object-specific controls.";
    case StudioWorkspace::Animation:
        return "Select an animated object to inspect its clips and preview controls.";
    case StudioWorkspace::Splice:
        return "Inspect event bindings and linked objects before changing a script.";
    case StudioWorkspace::Visibility:
        return "Inspect zones, adjacency, and camera visibility in Visibility & Zones Studio.";
    }
    return "";
}

inline StudioAction DrawStudioNavigation(const ImVec2 position, const ImVec2 size,
                                         StudioWorkspace &workspace, bool &workspaceChanged,
                                         const bool levelOpen, const bool canUndo, const bool canRedo,
                                         const bool dirty, bool &guidance)
{
    StudioAction action = StudioAction::None;
    workspaceChanged = false;
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("Studio navigation", nullptr, flags);
    // Horizontal scrolling keeps every command reachable on smaller windows.
    const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4.0f;
    ImGui::BeginChild("Commands", ImVec2(0.0f, rowHeight), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const auto button = [&](const char *label, const StudioAction request, const bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(label))
            action = request;
        ImGui::EndDisabled();
        ImGui::SameLine();
    };
    button("Open...", StudioAction::Open, true);
    button("Save project", StudioAction::Save, levelOpen);
    button("Undo", StudioAction::Undo, canUndo);
    button("Redo", StudioAction::Redo, canRedo);
    button("Add...", StudioAction::Add, levelOpen);
    button("Review & Export...", StudioAction::Export, levelOpen);
    button("Issues", StudioAction::Issues, levelOpen);
    button("Help", StudioAction::Help, true);
    ImGui::TextDisabled("%s", !levelOpen ? "No level open" : dirty ? "Unsaved project changes" : "Project saved");
    ImGui::EndChild();
    ImGui::BeginChild("Workspaces", ImVec2(0.0f, rowHeight), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    constexpr const char *labels[]{"Scene", "Models & Materials", "Collision", "Gameplay", "Animation", "Splice", "Visibility"};
    for (int index = 0; index < IM_ARRAYSIZE(labels); ++index)
    {
        if (index != 0)
            ImGui::SameLine();
        const auto candidate = static_cast<StudioWorkspace>(index);
        if (ImGui::Selectable(labels[index], workspace == candidate, 0,
                              ImVec2(ImGui::CalcTextSize(labels[index]).x + 22.0f, ImGui::GetFrameHeight())))
        {
            workspace = candidate;
            workspaceChanged = true;
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Guidance", &guidance);
    ImGui::EndChild();
    if (guidance)
        ImGui::TextWrapped("%s", StudioWorkspaceHint(workspace));
    ImGui::End();
    return action;
}
