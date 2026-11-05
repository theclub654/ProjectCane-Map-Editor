#include "EditorTheme.h"

#include <imgui.h>
void DrawGrid(ImDrawList *drawList, const ImVec2 origin, const ImVec2 size)
{
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(17, 23, 31, 255));
    constexpr float spacing = 32.0f;
    for (float x = 0.0f; x < size.x; x += spacing)
        drawList->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + size.y),
                          IM_COL32(40, 51, 65, 255));
    for (float y = 0.0f; y < size.y; y += spacing)
        drawList->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + size.x, origin.y + y),
                          IM_COL32(40, 51, 65, 255));
    drawList->AddLine(ImVec2(origin.x, origin.y + size.y * 0.5f), ImVec2(origin.x + size.x, origin.y + size.y * 0.5f),
                      IM_COL32(95, 125, 155, 255), 2.0f);
    drawList->AddLine(ImVec2(origin.x + size.x * 0.5f, origin.y), ImVec2(origin.x + size.x * 0.5f, origin.y + size.y),
                      IM_COL32(95, 125, 155, 255), 2.0f);
}

void ApplyEditorStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(10.0f, 9.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(7.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.02f, 0.5f);

    const ImVec4 background(0.035f, 0.043f, 0.055f, 1.0f);
    const ImVec4 panel(0.052f, 0.063f, 0.080f, 1.0f);
    const ImVec4 raised(0.075f, 0.090f, 0.114f, 1.0f);
    const ImVec4 raisedHover(0.105f, 0.126f, 0.158f, 1.0f);
    const ImVec4 border(0.145f, 0.169f, 0.205f, 0.82f);
    const ImVec4 text(0.875f, 0.900f, 0.930f, 1.0f);
    const ImVec4 textMuted(0.475f, 0.525f, 0.590f, 1.0f);
    const ImVec4 accent(0.145f, 0.500f, 0.920f, 1.0f);
    const ImVec4 accentHover(0.205f, 0.590f, 1.000f, 1.0f);
    const ImVec4 accentActive(0.105f, 0.410f, 0.805f, 1.0f);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.054f, 0.069f, 0.985f);
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = raisedHover;
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.115f, 0.145f, 0.185f, 1.0f);
    colors[ImGuiCol_TitleBg] = panel;
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.065f, 0.085f, 0.115f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = panel;
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.040f, 0.049f, 0.063f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.025f, 0.031f, 0.041f, 0.80f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.180f, 0.210f, 0.255f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.290f, 0.350f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accentHover;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHover;
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.72f);
    colors[ImGuiCol_ButtonActive] = accentActive;
    colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accentHover.x, accentHover.y, accentHover.z, 0.45f);
    colors[ImGuiCol_HeaderActive] = ImVec4(accentActive.x, accentActive.y, accentActive.z, 0.72f);
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accentHover;
    colors[ImGuiCol_SeparatorActive] = accentActive;
    colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.18f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(accentHover.x, accentHover.y, accentHover.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive] = accentActive;
    colors[ImGuiCol_Tab] = ImVec4(0.060f, 0.073f, 0.094f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.72f);
    colors[ImGuiCol_TabActive] = ImVec4(0.105f, 0.240f, 0.430f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.045f, 0.055f, 0.071f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.075f, 0.145f, 0.255f, 1.0f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.070f, 0.085f, 0.108f, 1.0f);
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = ImVec4(border.x, border.y, border.z, 0.45f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_NavHighlight] = accentHover;
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.250f, 0.750f, 1.0f, 0.95f);
}
