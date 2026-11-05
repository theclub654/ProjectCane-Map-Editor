#include "CharacterStudioPanel.h"

#include "BrxDocument.h"
#include "CharacterExport.h"
#include "EditorCamera.h"
#include "EditorOptions.h"
#include "PlatformDialogs.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
bool IsCharacter(const BrxObjectRecord &object)
{
    return CidDerivesFrom(object.cid, 8);
}

std::int32_t RuntimeParent(const BrxObjectRecord &object)
{
    return object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
}

bool IsDescendantOf(const BrxDocument &document, std::int32_t candidate, const std::int32_t ancestor)
{
    for (std::size_t guard = 0; candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                guard++ <= document.Objects().size();)
    {
        if (candidate == ancestor)
            return true;
        const auto next = RuntimeParent(document.Objects()[static_cast<std::size_t>(candidate)]);
        if (next == candidate)
            break;
        candidate = next;
    }
    return false;
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string ObjectLabel(const BrxObjectRecord &object)
{
    const std::string identity = std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid);
    return object.editorName.empty() ? identity : object.editorName + "  (" + identity + ")";
}

const char *HierarchyRole(const BrxObjectRecord &object)
{
    if (CidDerivesFrom(object.cid, 8))
        return "Character root";
    if (object.cid == 136)
        return "State machine";
    if (object.cid == 131 || object.cid == 132)
        return "Animation segment";
    if (object.cid == 98 || object.cid == 110 || object.cid == 111 || object.cid == 114)
        return "Patrol/path helper";
    if (object.cid == 96)
        return "Project volume";
    if (CidDerivesFrom(object.cid, 80))
        return "Particle emitter";
    if (object.cid == 81)
        return "Light";
    if (object.cid == 115 || object.cid == 126 || object.cid == 127)
        return "Attachment/marker";
    if (object.renderGroupCount > 0)
        return "Renderable model";
    if (object.so.present)
        return "Collision/physics";
    return "Support object";
}

void ReadinessRow(const char *label, const bool ready, const char *readyText, const char *missingText,
                  const bool required)
{
    const ImVec4 color = ready ? ImVec4(0.32f, 0.88f, 0.52f, 1.0f)
                               : (required ? ImVec4(1.0f, 0.38f, 0.30f, 1.0f) : ImVec4(1.0f, 0.72f, 0.25f, 1.0f));
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(color, "%s", ready ? "READY" : (required ? "ERROR" : "CHECK"));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", ready ? readyText : missingText);
}
}

void DrawCharacterStudioPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                              std::string &status)
{
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(1040.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Character Studio", &open))
    {
        ImGui::End();
        return;
    }

    static char search[96]{};
    ImGui::InputTextWithHint("##character_search", "Search character name, class or OID", search, sizeof(search));
    ImGui::SameLine();
    ImGui::TextDisabled("Complete ProjectCane character packages, not isolated meshes");

    const std::string needle = Lower(search);
    std::vector<std::int32_t> characters;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (!IsCharacter(object))
            continue;
        const std::string haystack = Lower(ObjectLabel(object));
        if (!needle.empty() && haystack.find(needle) == std::string::npos)
            continue;
        characters.push_back(static_cast<std::int32_t>(index));
    }

    ImGui::BeginChild("character_list", ImVec2(275.0f, 0.0f), true);
    ImGui::TextDisabled("%zu character package(s)", characters.size());
    for (const auto index : characters)
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(index)];
        // Keep the browser O(N). Full hierarchy/animation counts are calculated only for
        // the selected package below; doing those walks for every character made large
        // retail worlds stall whenever Character Studio was open.
        const std::string label =
            ObjectLabel(object) + "\n" + std::to_string(object.properties.size()) + " authored option(s)";
        if (ImGui::Selectable(label.c_str(), renderer.SelectedObjectIndex() == index, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, 43.0f)))
        {
            renderer.SelectObject(index);
            status = "Selected " + ObjectLabel(object);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("character_detail", ImVec2(0.0f, 0.0f), true);

    const auto selected = renderer.SelectedObjectIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= document.Objects().size() ||
        !IsCharacter(document.Objects()[static_cast<std::size_t>(selected)]))
    {
        ImGui::TextDisabled("Select a guard or character package from the list.");
        ImGui::TextWrapped(
            "Character Studio treats the selected character and every object in its runtime hierarchy as one package. This keeps models, collision, animation, state machines, paths and effects together.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    const auto &character = document.Objects()[static_cast<std::size_t>(selected)];
    std::vector<std::int32_t> hierarchy;
    std::vector<std::int32_t> renderables;
    std::vector<std::int32_t> collisions;
    std::vector<std::int32_t> pathHelpers;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        if (!IsDescendantOf(document, static_cast<std::int32_t>(index), selected))
            continue;
        hierarchy.push_back(static_cast<std::int32_t>(index));
        const auto &object = document.Objects()[index];
        if (object.renderGroupCount > 0)
            renderables.push_back(static_cast<std::int32_t>(index));
        if (object.so.present && !object.so.collisionGeometry.surfaces.empty())
            collisions.push_back(static_cast<std::int32_t>(index));
        if (object.cid == 98 || object.cid == 110 || object.cid == 111 || object.cid == 114)
            pathHelpers.push_back(static_cast<std::int32_t>(index));
    }
    std::vector<const BrxAsegRecord *> animations;
    for (const auto &segment : document.AnimationSegments())
        if (IsDescendantOf(document, segment.ownerObjectIndex, selected))
            animations.push_back(&segment);
    std::vector<const BrxSmRecord *> machines;
    for (const auto &machine : document.StateMachines())
        if (IsDescendantOf(document, machine.ownerObjectIndex, selected))
            machines.push_back(&machine);

    ImGui::TextUnformatted(ObjectLabel(character).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame character"))
    {
        if (!renderer.FrameObject(camera, selected))
            status = "Character hierarchy has no frameable bounds.";
    }
    ImGui::TextDisabled("CID %d | %zu hierarchy objects | %s", character.cid, hierarchy.size(),
                        character.proxyRecordIndex >= 0 ? "reusable PROXY instance" : "inline hierarchy");
    if (ImGui::Button("Export rigged character GLB..."))
    {
        const std::string suggestedName =
            character.editorName.empty()
                ? std::string(BrxDocument::ClassName(character.cid)) + "_" + std::to_string(character.oid)
                : character.editorName;
        const auto path = ChooseCharacterGlbSaveFile(nullptr, suggestedName);
        if (!path.empty())
        {
            std::string exportError;
            status = ExportRiggedCharacterGlb(document, selected, path, exportError)
                         ? "Exported rigged character to " + path.string() + "."
                         : "Rigged character export failed: " + exportError;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("GLB preserves hierarchy, bind pose, joint palette and skin weights.");

    if (ImGui::CollapsingHeader("Package readiness", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("character_readiness", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("Details");
            ImGui::TableHeadersRow();
            ReadinessRow("Renderable hierarchy", !renderables.empty(),
                         "At least one character model node is available.",
                         "No renderable model exists below this character.", true);
            ReadinessRow(
                "Gameplay collision", !collisions.empty(), "Authored SO collision geometry is present.",
                "No authored collision surface was found; verify whether this character class supplies runtime collision.",
                false);
            ReadinessRow("Animation package", !animations.empty(), "Decoded ASEG animation data is present.",
                         "No ASEG animation was found in this hierarchy.", true);
            ReadinessRow("State machines", !machines.empty(), "Decoded SM state and transition data is present.",
                         "No SM payload was found; some simple classes may be driven entirely by class code or Splice.",
                         false);
            ReadinessRow("Patrol/path helpers", !pathHelpers.empty(),
                         "A path or waypoint helper exists in the package.",
                         "No path helper is attached; this is valid for stationary characters.", false);
            ReadinessRow("Portable package", character.proxyRecordIndex >= 0,
                         "The character belongs to a reusable PROXY package.",
                         "Inline character: export the complete hierarchy when moving it between levels.", false);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Hierarchy and models", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginTable("character_hierarchy", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, 210.0f)))
        {
            ImGui::TableSetupColumn("Object");
            ImGui::TableSetupColumn("Role");
            ImGui::TableSetupColumn("Models");
            ImGui::TableSetupColumn("Collision");
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();
            for (const auto index : hierarchy)
            {
                const auto &object = document.Objects()[static_cast<std::size_t>(index)];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(ObjectLabel(object).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(HierarchyRole(object));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", object.renderGroupCount);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(object.so.present && !object.so.collisionGeometry.surfaces.empty() ? "yes"
                                                                                                          : "-");
                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton("Select"))
                    renderer.SelectObject(index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Frame"))
                    renderer.FrameObject(camera, index);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Animation and behavior", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("%zu animation segment(s) | %zu state machine(s)", animations.size(), machines.size());
        for (std::size_t index = 0; index < animations.size(); ++index)
        {
            const auto &segment = *animations[index];
            if (segment.ownerObjectIndex < 0 ||
                static_cast<std::size_t>(segment.ownerObjectIndex) >= document.Objects().size())
                continue;
            const auto &owner = document.Objects()[static_cast<std::size_t>(segment.ownerObjectIndex)];
            ImGui::PushID(300000 + static_cast<int>(index));
            if (ImGui::SmallButton("Select ASEG"))
                renderer.SelectObject(segment.ownerObjectIndex);
            ImGui::SameLine();
            ImGui::Text("%s | %d frames | %zu channels | %zu events", ObjectLabel(owner).c_str(), segment.maximumFrame,
                        segment.channels.size(), segment.events.size());
            ImGui::PopID();
        }
        for (std::size_t index = 0; index < machines.size(); ++index)
        {
            const auto &machine = *machines[index];
            if (machine.ownerObjectIndex < 0 ||
                static_cast<std::size_t>(machine.ownerObjectIndex) >= document.Objects().size())
                continue;
            const auto &owner = document.Objects()[static_cast<std::size_t>(machine.ownerObjectIndex)];
            ImGui::PushID(400000 + static_cast<int>(index));
            if (ImGui::SmallButton("Edit SM"))
                renderer.SelectObject(machine.ownerObjectIndex);
            ImGui::SameLine();
            ImGui::Text("%s | %zu states | %zu transitions", ObjectLabel(owner).c_str(), machine.states.size(),
                        machine.transitions.size());
            ImGui::PopID();
        }
        if (animations.empty())
            ImGui::TextColored(
                ImVec4(1.0f, 0.42f, 0.30f, 1.0f),
                "No decoded animations. Import or transfer the full character hierarchy, not only its visible model.");
    }

    if (ImGui::CollapsingHeader("Patrol and navigation"))
    {
        if (pathHelpers.empty())
            ImGui::TextDisabled("No path helper is inside this package. Stationary guards do not require one.");
        for (const auto index : pathHelpers)
        {
            const auto &path = document.Objects()[static_cast<std::size_t>(index)];
            const auto shape =
                std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                             [&](const BrxShapePathRecord &candidate) { return candidate.ownerObjectIndex == index; });
            if (ImGui::SmallButton(("Select##path" + std::to_string(index)).c_str()))
                renderer.SelectObject(index);
            ImGui::SameLine();
            const std::string shapeSummary = shape == document.ShapePaths().end()
                                                 ? "no decoded curve"
                                                 : std::to_string(shape->points.size()) + " control points";
            ImGui::Text("%s | %s", ObjectLabel(path).c_str(), shapeSummary.c_str());
        }
        ImGui::TextWrapped(
            "Patrol motion is commonly driven by ASEG events, WAYPOINT/PATH objects and class-specific Splice. Character Studio reports the package pieces without inventing a generic patrol property that retail classes do not share.");
    }

    if (ImGui::CollapsingHeader("Authoring guidance"))
    {
        ImGui::BulletText("Move or export the complete character hierarchy, including its PROXY source when present.");
        ImGui::BulletText(
            "Do not replace a skinned guard with an unrelated static model and expect its bone bindings to remain valid.");
        ImGui::BulletText("Preserve fixed state, animation and helper OIDs referenced by class code or Splice.");
        ImGui::BulletText("Use the selected SM and ASEG specialized inspectors for exact state and timeline editing.");
        ImGui::BulletText("Run level validation before export after changing a character dependency.");
    }

    ImGui::EndChild();
    ImGui::End();
}
