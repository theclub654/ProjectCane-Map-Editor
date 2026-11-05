#include "WorldBuilderPanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorDocumentOperations.h"
#include "EditorOptions.h"
#include "EditorWorkspace.h"
#include "PlatformDialogs.h"
#include "SceneRenderer.h"
#include "SpliceRuntime.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct ImpactSummary
{
    std::size_t hierarchy = 0;
    std::size_t incoming = 0;
    std::size_t outgoing = 0;
    std::size_t globs = 0;
    std::size_t collision = 0;
    std::size_t effects = 0;
    std::size_t paths = 0;
    std::size_t animations = 0;
};

bool ContainsInsensitive(std::string value, std::string needle)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return needle.empty() || value.find(needle) != std::string::npos;
}

std::array<float, 16> CameraPlacement(const EditorCamera &camera)
{
    std::array<float, 16> result{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto position = camera.Position();
    result[12] = position.x;
    result[13] = position.y;
    result[14] = position.z;
    return result;
}

void FinishOperation(const EditorOperationResult &result, BrxDocument &document, EditorWorkspace &workspace,
                     SceneRenderer &renderer, std::string &status)
{
    status = result.message;
    if (!result.succeeded)
        return;
    workspace.Refresh(document);
    renderer.Upload(document);
    renderer.SelectObject(result.primaryObjectIndex);
}
}

void DrawWorldBuilderPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                           EditorCamera &camera, GLFWwindow *window, bool &showAssetBrowser, bool &showValidation,
                           bool &showSpliceEvents, bool &showPaths, bool &showReferenceLinks, bool &showPlacementTools,
                           const std::size_t undoCount, const std::size_t redoCount, const std::string &undoLabel,
                           const std::string &redoLabel, bool &requestUndo, bool &requestRedo, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Builder", &open))
    {
        ImGui::End();
        return;
    }

    const auto selected = renderer.SelectedObjectIndex();
    const auto validSelected = selected >= 0 && static_cast<std::size_t>(selected) < document.Objects().size();
    const auto placement = CameraPlacement(camera);
    static char recipeSearch[96]{};

    if (ImGui::BeginTabBar("WorldBuilderTabs"))
    {
        if (ImGui::BeginTabItem("1 Recipes"))
        {
            ImGui::InputTextWithHint("##recipeSearch", "Search gameplay object recipes...", recipeSearch,
                                     sizeof(recipeSearch));
            ImGui::TextDisabled(
                "Objects are placed at the editor camera and parented to the selection when compatible.");
            const std::string search(recipeSearch);
            const auto recipe = [&](const char *category, const char *name, const char *description, const auto &create)
            {
                if (!ContainsInsensitive(std::string(category) + " " + name + " " + description, search))
                    return;
                ImGui::PushID(name);
                if (ImGui::Button(name, ImVec2(220.0f, 0.0f)))
                    FinishOperation(create(), document, workspace, renderer, status);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", category);
                ImGui::TextWrapped("%s", description);
                ImGui::PopID();
            };
            const auto parent = validSelected ? selected : -1;
            recipe("Characters", "Patrol guard",
                   "Creates a STEP GUARD foundation ready for a model, patrol ASEG and WAYPOINT events.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 8, parent, placement.data()); });
            recipe("Characters", "Smart guard",
                   "Creates a SMARTGUARD foundation with ProjectCane's guard option schema.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 9, parent, placement.data()); });
            recipe("Characters", "Gomer guard",
                   "Creates a GOMER guard foundation for patrol and state-machine authoring.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 10, parent, placement.data()); });
            recipe("Collectibles", "Clue bottle",
                   "Creates a CLUE collectible; use its inspector to manage bounce frames and collection helpers.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 46, parent, placement.data()); });
            recipe("Collectibles", "Coin", "Creates a standard coin pickup.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 88, parent, placement.data()); });
            recipe("Collectibles", "Life token", "Creates a life-token pickup.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 87, parent, placement.data()); });
            recipe("Doors & barriers", "Barrier door",
                   "Creates a fixed collision barrier with a destination helper workflow.",
                   [&] { return EditorDocumentOperations::CreateBarrier(document, parent, placement.data()); });
            recipe("Doors & barriers", "Lock", "Creates an editable LOCK gameplay object for a door or gate hierarchy.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 92, parent, placement.data()); });
            recipe("Breakables", "Breakable object",
                   "Creates a BRK physical breakable ready for model, collision and break behavior.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 32, parent, placement.data()); });
            recipe("Breakables", "Fragile object", "Creates a FRAGILE object for damage-driven destruction.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 37, parent, placement.data()); });
            recipe("Hazards", "Mine", "Creates a MINE hazard with its exact CID option schema.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 63, parent, placement.data()); });
            recipe("Security", "Laser security system", "Creates LASEN with its authored starter beam.",
                   [&] { return EditorDocumentOperations::CreateLasen(document, parent, placement.data()); });
            recipe("Security", "Camera detector", "Creates CAMSEN with a detection sphere.",
                   [&] { return EditorDocumentOperations::CreateCamsen(document, parent, placement.data()); });
            recipe("Progression", "Checkpoint assembly", "Creates the checkpoint and required embedded WARP hierarchy.",
                   [&] { return EditorDocumentOperations::CreateCheckpoint(document, parent, placement.data()); });
            recipe("Progression", "Level exit", "Creates an EXIT with safe transition defaults.",
                   [&] { return EditorDocumentOperations::CreateExit(document, parent, placement.data()); });
            recipe("Progression", "Warp destination", "Creates a standalone WARP destination.",
                   [&] { return EditorDocumentOperations::CreateWarp(document, parent, placement.data()); });
            recipe("Interaction", "Push button", "Creates a BUTTON and its required helper hierarchy.",
                   [&]
                   {
                       return EditorDocumentOperations::CreateButton(
                           document, EditorDocumentOperations::ButtonPreset::Push, parent, placement.data());
                   });
            recipe("Interaction", "Lever button", "Creates a lever-style BUTTON preset.",
                   [&]
                   {
                       return EditorDocumentOperations::CreateButton(
                           document, EditorDocumentOperations::ButtonPreset::Lever, parent, placement.data());
                   });
            recipe("Interaction", "Contact button", "Creates a contact/pressure BUTTON preset.",
                   [&]
                   {
                       return EditorDocumentOperations::CreateButton(
                           document, EditorDocumentOperations::ButtonPreset::Contact, parent, placement.data());
                   });
            recipe("Lighting", "Directional light",
                   "Creates a LIGHT; choose Directional and tune it in the Lighting inspector.",
                   [&]
                   {
                       auto result = EditorDocumentOperations::CreateObject(document, 81, parent, placement.data());
                       if (!result.succeeded)
                           return result;
                       auto &light = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
                       const auto add =
                           [&](const std::int16_t eopid, const BrxPropertyKind kind, const std::array<float, 3> values)
                       {
                           const auto definition = EditorOptionDefinitionForCid(81, eopid);
                           BrxPropertyRecord property;
                           property.eopid = eopid;
                           property.kind = kind;
                           property.flags = definition.flags;
                           property.argumentCount = definition.argumentCount;
                           property.intValue = static_cast<std::int32_t>(values[0]);
                           property.floatValue[0] = values[0];
                           property.floatValue[1] = values[1];
                           property.floatValue[2] = values[2];
                           property.modified = true;
                           light.properties.push_back(std::move(property));
                       };
                       add(491, BrxPropertyKind::Int16, {0, 0, 0});
                       add(492, BrxPropertyKind::Vector3, {0, 0, 255});
                       add(493, BrxPropertyKind::Float, {255, 0, 0});
                       add(494, BrxPropertyKind::Float, {0, 0, 0});
                       add(495, BrxPropertyKind::Float, {180, 0, 0});
                       add(496, BrxPropertyKind::Float, {240, 0, 0});
                       add(497, BrxPropertyKind::Float, {180, 0, 0});
                       light.loaderName = "Editor-created directional LIGHT";
                       return result;
                   });
            recipe("Presentation", "Dialog controller", "Creates an editable DIALOG event container.",
                   [&] { return EditorDocumentOperations::CreateDialog(document, parent, placement.data()); });
            recipe("Effects", "Particle emitter", "Creates an EMITTER ready for BLIP or RIP authoring.",
                   [&] { return EditorDocumentOperations::CreateObject(document, 80, parent, placement.data()); });
            recipe("Traversal", "Spire perch", "Creates a ninja-spire traversal marker.",
                   [&] { return EditorDocumentOperations::CreateSpire(document, parent, placement.data()); });
            recipe("Traversal", "Climbable pipe", "Creates a linear PIPE with editable control points.",
                   [&] { return EditorDocumentOperations::CreatePipe(document, false, parent, placement.data()); });
            recipe("Traversal", "Traversal rail", "Creates a linear RAIL with editable control points.",
                   [&] { return EditorDocumentOperations::CreateRail(document, false, parent, placement.data()); });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("2 References"))
        {
            ImGui::Checkbox("Show selected object's links in viewport", &showReferenceLinks);
            if (!validSelected)
                ImGui::TextDisabled("Select an object to inspect its links and diagnostics.");
            else
            {
                const auto &object = document.Objects()[static_cast<std::size_t>(selected)];
                ImGui::Text("%s OID %d", BrxDocument::EditorClassName(object.cid).c_str(), object.oid);
                ImGui::SeparatorText("Object references");
                bool anyReference = false;
                for (std::size_t propertyIndex = 0; propertyIndex < object.properties.size(); ++propertyIndex)
                {
                    const auto &property = object.properties[propertyIndex];
                    if (!IsObjectReferenceOption(property.eopid))
                        continue;
                    anyReference = true;
                    const auto oid = static_cast<std::int16_t>(property.intValue);
                    const auto targetIndex = workspace.Scene().FindObjectFrom(oid, selected);
                    ImGui::PushID(static_cast<int>(propertyIndex));
                    ImGui::Text("EOPID %d -> OID %d", property.eopid, oid);
                    ImGui::SameLine();
                    if (targetIndex >= 0 && static_cast<std::size_t>(targetIndex) < document.Objects().size())
                    {
                        const auto &target = document.Objects()[static_cast<std::size_t>(targetIndex)];
                        if (ImGui::SmallButton(("Select " + BrxDocument::EditorClassName(target.cid)).c_str()))
                        {
                            renderer.SelectObject(targetIndex);
                            camera.FocusPoint(
                                {target.worldTransform[12], target.worldTransform[13], target.worldTransform[14]},
                                200.0f);
                        }
                    }
                    else
                        ImGui::TextColored(ImVec4(1, .35f, .25f, 1), "Missing target");
                    ImGui::PopID();
                }
                if (!anyReference)
                    ImGui::TextDisabled("No direct object-reference options.");

                const auto selectAndFrame = [&](const std::int32_t objectIndex)
                {
                    if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= document.Objects().size())
                        return;
                    const auto &target = document.Objects()[static_cast<std::size_t>(objectIndex)];
                    renderer.SelectObject(objectIndex);
                    camera.FocusPoint({target.worldTransform[12], target.worldTransform[13], target.worldTransform[14]},
                                      200.0f);
                };
                ImGui::SeparatorText("Resolved outgoing references");
                std::size_t outgoingCount = 0;
                for (const auto &reference : workspace.Scene().References())
                {
                    if (reference.sourceObjectIndex != selected)
                        continue;
                    ++outgoingCount;
                    ImGui::PushID(static_cast<int>(outgoingCount));
                    ImGui::Text("EOPID %d -> OID %d", reference.eopid, reference.targetOid);
                    ImGui::SameLine();
                    if (reference.targetObjectIndex >= 0)
                    {
                        if (ImGui::SmallButton(
                                ("Select " + document.ObjectEditorLabel(reference.targetObjectIndex)).c_str()))
                            selectAndFrame(reference.targetObjectIndex);
                        if (!reference.compatible)
                        {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(1, .4f, .25f, 1), "wrong CID");
                        }
                    }
                    else
                        ImGui::TextColored(ImVec4(1, .35f, .25f, 1), "Missing target");
                    ImGui::PopID();
                }
                if (!outgoingCount)
                    ImGui::TextDisabled("No resolved outgoing object references.");

                ImGui::SeparatorText("Incoming references");
                std::size_t incomingCount = 0;
                for (const auto &reference : workspace.Scene().References())
                {
                    if (reference.targetObjectIndex != selected)
                        continue;
                    ++incomingCount;
                    ImGui::PushID(10000 + static_cast<int>(incomingCount));
                    const auto source = reference.sourceObjectIndex;
                    ImGui::Text("%s via EOPID %d",
                                source >= 0 ? document.ObjectEditorLabel(source).c_str() : "Unknown source",
                                reference.eopid);
                    if (source >= 0)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Select source"))
                            selectAndFrame(source);
                    }
                    ImGui::PopID();
                }
                if (!incomingCount)
                    ImGui::TextDisabled("No decoded object references target this object.");

                ImGui::SeparatorText("Hierarchy");
                if (object.parentIndex >= 0)
                {
                    ImGui::Text("Parent: %s", document.ObjectEditorLabel(object.parentIndex).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Select parent"))
                        selectAndFrame(object.parentIndex);
                }
                else
                    ImGui::TextDisabled("World root object");
                const auto &children = workspace.Scene().Children(selected);
                ImGui::Text("Direct children: %zu", children.size());
                for (const auto child : children)
                {
                    ImGui::PushID(20000 + child);
                    if (ImGui::SmallButton(document.ObjectEditorLabel(child).c_str()))
                        selectAndFrame(child);
                    ImGui::PopID();
                    ImGui::SameLine();
                }
                if (!children.empty())
                    ImGui::NewLine();

                ImGui::SeparatorText("Owned models, materials, paths and effects");
                std::size_t ownedGlobs = 0, ownedSubglobs = 0, ownedEffects = 0, ownedPaths = 0, ownedAnimations = 0;
                std::vector<std::uint16_t> shaderIndices;
                for (const auto &glob : document.Globs())
                    if (glob.ownerObjectIndex == selected)
                    {
                        ++ownedGlobs;
                        ownedSubglobs += glob.sourceSubglobs.size();
                        for (const auto &subglob : glob.sourceSubglobs)
                            if (std::find(shaderIndices.begin(), shaderIndices.end(), subglob.shaderIndex) ==
                                shaderIndices.end())
                                shaderIndices.push_back(subglob.shaderIndex);
                    }
                ownedEffects = static_cast<std::size_t>(
                    std::count_if(document.Effects().begin(), document.Effects().end(),
                                  [&](const BrxEffectRecord &record) { return record.ownerObjectIndex == selected; }));
                ownedPaths = static_cast<std::size_t>(std::count_if(
                    document.ShapePaths().begin(), document.ShapePaths().end(),
                    [&](const BrxShapePathRecord &record) { return record.ownerObjectIndex == selected; }));
                ownedAnimations = static_cast<std::size_t>(
                    std::count_if(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                                  [&](const BrxAsegRecord &record) { return record.ownerObjectIndex == selected; }));
                ImGui::Text("%zu model group(s), %zu model part(s), %zu material(s)", ownedGlobs, ownedSubglobs,
                            shaderIndices.size());
                ImGui::Text("%zu path(s), %zu effect(s), %zu ASEG animation(s)", ownedPaths, ownedEffects,
                            ownedAnimations);
                for (const auto shaderIndex : shaderIndices)
                    if (shaderIndex < document.Shaders().size())
                        ImGui::BulletText("Material %u: %s", shaderIndex,
                                          document.Shaders()[shaderIndex].editorName.c_str());

                ImGui::SeparatorText("PROXY and Splice context");
                ImGui::Text("PROXY record %d | root %d | splice module %d", object.proxyRecordIndex,
                            object.proxyRootIndex, object.spliceIndex);
                if (object.spliceIndex >= 0 &&
                    static_cast<std::size_t>(object.spliceIndex) < document.SpliceModules().size())
                {
                    const auto analysis = AnalyzeSpliceModule(document, object.spliceIndex);
                    ImGui::Text("%zu event(s), %zu import(s), %zu built-in call(s)", analysis.eventIds.size(),
                                analysis.imports.size(), analysis.builtinCalls.size());
                    ImGui::Text("%zu property read(s), %zu property write(s), %zu diagnostic(s)",
                                analysis.optionReads.size(), analysis.optionWrites.size(), analysis.diagnostics.size());
                    if (ImGui::SmallButton("Open Splice graph"))
                        showSpliceEvents = true;
                }
                ImGui::SeparatorText("Validation for selection");
                bool anyIssue = false;
                for (const auto &issue : workspace.Validation().Issues())
                {
                    if (issue.objectIndex != selected)
                        continue;
                    anyIssue = true;
                    const ImVec4 color =
                        issue.severity == ValidationSeverity::Error ? ImVec4(1, .3f, .3f, 1) : ImVec4(1, .72f, .25f, 1);
                    ImGui::TextColored(color, "%s", issue.category.c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", issue.message.c_str());
                }
                if (!anyIssue)
                    ImGui::TextColored(ImVec4(.35f, 1, .45f, 1), "No validation issues for this object.");
                if (ImGui::Button("Open complete validation"))
                    showValidation = true;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("3 Prefabs"))
        {
            ImGui::TextWrapped(
                "Capture complete reusable hierarchies and place linked instances with safe OID remapping.");
            if (ImGui::Button("Open prefab and asset browser"))
                showAssetBrowser = true;
            ImGui::SameLine();
            ImGui::TextDisabled("%zu prefab(s)", workspace.Prefabs().Prefabs().size());
            if (validSelected)
            {
                const auto linked = workspace.Prefabs().LinkedPrefab(selected);
                ImGui::Text("Selected instance: %s", linked ? "linked" : "unique / not a prefab root");
                if (linked && ImGui::Button("Make selected instance unique"))
                {
                    workspace.Prefabs().MakeUnique(selected);
                    status = "Prefab instance is now unique.";
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("8 Safety"))
        {
            ImGui::TextWrapped(
                "Creation recipes call the same CID-aware operations used by the editor and refuse abstract, runtime-only, or structurally unsafe objects.");
            if (!validSelected)
                ImGui::TextDisabled("New recipes will be created at world root because no valid parent is selected.");
            else
            {
                const auto &object = document.Objects()[static_cast<std::size_t>(selected)];
                ImGui::Text("Prospective parent: %s OID %d", BrxDocument::EditorClassName(object.cid).c_str(),
                            object.oid);
                std::size_t errors = 0, warnings = 0;
                for (const auto &issue : workspace.Validation().Issues())
                {
                    if (issue.objectIndex != selected)
                        continue;
                    if (issue.severity == ValidationSeverity::Error)
                        ++errors;
                    else
                        ++warnings;
                }
                ImGui::TextColored(errors ? ImVec4(1, .3f, .3f, 1) : ImVec4(.35f, 1, .45f, 1),
                                   "%zu error(s), %zu warning(s) on selected parent", errors, warnings);
                if (errors != 0)
                    ImGui::TextWrapped("Resolve parent errors before using it as the root of a new gameplay assembly.");
            }
            if (ImGui::Button("Refresh creation validation"))
            {
                workspace.Refresh(document);
                status = "Creation validation refreshed.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Open full validation"))
                showValidation = true;
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("10 Paths"))
        {
            ImGui::Checkbox("Show paths and traversal guides", &showPaths);
            ImGui::TextDisabled(
                "New paths are placed at the editor camera; use the inspector for control-point editing.");
            const auto parent = validSelected ? selected : -1;
            const auto pathButton = [&](const char *label, const auto &create)
            {
                if (ImGui::Button(label, ImVec2(210.0f, 0.0f)))
                {
                    FinishOperation(create(), document, workspace, renderer, status);
                    showPaths = true;
                }
            };
            pathButton("Linear SHAPE", [&]
                       { return EditorDocumentOperations::CreateShape(document, false, parent, placement.data()); });
            ImGui::SameLine();
            pathButton("Cubic SHAPE",
                       [&] { return EditorDocumentOperations::CreateShape(document, true, parent, placement.data()); });
            pathButton("Linear HSHAPE", [&]
                       { return EditorDocumentOperations::CreateHshape(document, false, parent, placement.data()); });
            ImGui::SameLine();
            pathButton("Cubic HSHAPE", [&]
                       { return EditorDocumentOperations::CreateHshape(document, true, parent, placement.data()); });
            pathButton("Linear PIPE",
                       [&] { return EditorDocumentOperations::CreatePipe(document, false, parent, placement.data()); });
            ImGui::SameLine();
            pathButton("Cubic PIPE",
                       [&] { return EditorDocumentOperations::CreatePipe(document, true, parent, placement.data()); });
            pathButton("Linear RAIL",
                       [&] { return EditorDocumentOperations::CreateRail(document, false, parent, placement.data()); });
            ImGui::SameLine();
            pathButton("Cubic RAIL",
                       [&] { return EditorDocumentOperations::CreateRail(document, true, parent, placement.data()); });
            pathButton("Linear LANDING", [&]
                       { return EditorDocumentOperations::CreateLanding(document, false, parent, placement.data()); });
            ImGui::SameLine();
            pathButton("Cubic LANDING", [&]
                       { return EditorDocumentOperations::CreateLanding(document, true, parent, placement.data()); });

            ImGui::SeparatorText("Guard patrol (WAYPOINT events in ASEG)");
            ImGui::TextWrapped(
                "ProjectCane patrols are animation paths. A guard's patrol ASEG moves the guard and its frame events activate WAYPOINT objects. Direction and speed come from ASEG channel keys; waypoint pause time comes from the WAYPOINT itself.");
            static std::int32_t patrolAsegObject = -1;
            static std::int32_t patrolWaypointObject = -1;
            static int patrolFrame = 0;
            const auto isDescendantOf = [&](std::int32_t candidate, const std::int32_t root)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate = document.Objects()[static_cast<std::size_t>(candidate)].parentIndex)
                    if (candidate == root)
                        return true;
                return false;
            };
            std::vector<std::int32_t> patrolAsegs;
            std::vector<std::int32_t> patrolWaypoints;
            for (std::size_t index = 0; index < document.Objects().size(); ++index)
            {
                const auto &candidate = document.Objects()[index];
                if (candidate.cid == 131 && (!validSelected || candidate.parentIndex == selected ||
                                             isDescendantOf(static_cast<std::int32_t>(index), selected)))
                    patrolAsegs.push_back(static_cast<std::int32_t>(index));
                if (candidate.cid == 98 && (!validSelected || candidate.parentIndex == selected ||
                                            isDescendantOf(static_cast<std::int32_t>(index), selected)))
                    patrolWaypoints.push_back(static_cast<std::int32_t>(index));
            }
            if (validSelected && document.Objects()[static_cast<std::size_t>(selected)].cid == 131 &&
                std::find(patrolAsegs.begin(), patrolAsegs.end(), selected) == patrolAsegs.end())
                patrolAsegs.push_back(selected);
            if (validSelected && document.Objects()[static_cast<std::size_t>(selected)].cid == 98 &&
                std::find(patrolWaypoints.begin(), patrolWaypoints.end(), selected) == patrolWaypoints.end())
                patrolWaypoints.push_back(selected);
            if (patrolAsegs.empty())
                for (std::size_t index = 0; index < document.Objects().size(); ++index)
                    if (document.Objects()[index].cid == 131)
                        patrolAsegs.push_back(static_cast<std::int32_t>(index));
            if (patrolWaypoints.empty())
                for (std::size_t index = 0; index < document.Objects().size(); ++index)
                    if (document.Objects()[index].cid == 98)
                        patrolWaypoints.push_back(static_cast<std::int32_t>(index));
            if (std::find(patrolAsegs.begin(), patrolAsegs.end(), patrolAsegObject) == patrolAsegs.end())
                patrolAsegObject = patrolAsegs.empty() ? -1 : patrolAsegs.front();
            if (std::find(patrolWaypoints.begin(), patrolWaypoints.end(), patrolWaypointObject) ==
                patrolWaypoints.end())
                patrolWaypointObject = patrolWaypoints.empty() ? -1 : patrolWaypoints.front();
            const auto objectCombo =
                [&](const char *label, std::int32_t &current, const std::vector<std::int32_t> &choices)
            {
                const std::string preview = current >= 0 ? document.ObjectEditorLabel(current) : "(none)";
                if (ImGui::BeginCombo(label, preview.c_str()))
                {
                    for (const auto choice : choices)
                    {
                        const bool active = choice == current;
                        if (ImGui::Selectable(document.ObjectEditorLabel(choice).c_str(), active))
                            current = choice;
                        if (active)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            };
            objectCombo("Patrol ASEG", patrolAsegObject, patrolAsegs);
            objectCombo("Waypoint", patrolWaypointObject, patrolWaypoints);
            ImGui::InputInt("Waypoint event frame", &patrolFrame);
            patrolFrame = (std::max)(0, patrolFrame);
            if (patrolWaypointObject >= 0)
            {
                auto &waypoint = document.EditableObjects()[static_cast<std::size_t>(patrolWaypointObject)];
                auto pause = std::find_if(waypoint.properties.begin(), waypoint.properties.end(),
                                          [](const BrxPropertyRecord &property) { return property.eopid == 838; });
                float pauseTime = pause == waypoint.properties.end() ? 0.0f : pause->floatValue[0];
                if (ImGui::DragFloat("Waypoint pause", &pauseTime, 0.05f, 0.0f, 3600.0f, "%.2f s"))
                {
                    if (pause == waypoint.properties.end())
                    {
                        const auto definition = EditorOptionDefinitionForCid(waypoint.cid, 838);
                        BrxPropertyRecord property;
                        property.eopid = 838;
                        property.kind = definition.kind;
                        property.flags = definition.flags;
                        property.argumentCount = definition.argumentCount;
                        property.editable = true;
                        ResetPropertyValue(property);
                        waypoint.properties.push_back(std::move(property));
                        pause = std::prev(waypoint.properties.end());
                    }
                    pause->floatValue[0] = pauseTime;
                    pause->modified = true;
                    workspace.Refresh(document);
                    status = "Updated waypoint pause timing.";
                }
            }
            ImGui::BeginDisabled(patrolAsegObject < 0 || patrolWaypointObject < 0);
            if (ImGui::Button("Add waypoint event to patrol"))
            {
                auto segment = std::find_if(document.EditableAnimationSegments().begin(),
                                            document.EditableAnimationSegments().end(), [&](const BrxAsegRecord &record)
                                            { return record.ownerObjectIndex == patrolAsegObject; });
                if (segment == document.EditableAnimationSegments().end())
                    status = "The selected ASEG has no decoded animation payload.";
                else
                {
                    BrxAsegEventRecord event;
                    event.lane = BrxAsegEventLane::Frame;
                    event.type = 41;
                    event.frame = static_cast<std::int16_t>((std::min)(patrolFrame, 32767));
                    event.shorts[0] = document.Objects()[static_cast<std::size_t>(patrolWaypointObject)].oid;
                    segment->events.push_back(event);
                    segment->maximumFrame = (std::max)(segment->maximumFrame, event.frame);
                    workspace.Refresh(document);
                    showPaths = true;
                    status = "Added ProjectCane WAYPOINT event to the patrol ASEG.";
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Create waypoint at camera"))
            {
                const auto created = EditorDocumentOperations::CreateObject(document, 98, validSelected ? selected : -1,
                                                                            placement.data());
                FinishOperation(created, document, workspace, renderer, status);
                if (created.succeeded)
                    patrolWaypointObject = created.primaryObjectIndex;
                showPaths = true;
            }
            ImGui::TextDisabled(
                "Select the patrol ASEG to edit movement keys, transition timing and other animation events. Select a path object to insert/snap control points in its inspector.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("9 Particles"))
        {
            const bool emitterSelected =
                validSelected && CidDerivesFrom(document.Objects()[static_cast<std::size_t>(selected)].cid, 80);
            auto &preview = renderer.EmitterPreview();
            ImGui::Text("Preview: %s | %zu live particle(s)", preview.Playing() ? "playing" : "stopped",
                        preview.Particles().size());
            ImGui::BeginDisabled(!emitterSelected);
            if (ImGui::Button("Solo and restart selected"))
            {
                preview.SetScope(EmitterPreviewScope::Selected);
                preview.SetSelectedEmitter(selected);
                preview.SetActivation(selected, EmitterPreviewActivation::Enabled);
                preview.SetPlaying(true);
                preview.Restart();
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause"))
                preview.SetActivation(selected, EmitterPreviewActivation::Paused);
            ImGui::SameLine();
            if (ImGui::Button("Stop all previews"))
                preview.Stop();
            static float particleStep = 1.0f / 30.0f;
            ImGui::DragFloat("Step duration", &particleStep, 0.001f, 0.001f, 1.0f, "%.3f s");
            if (ImGui::Button("Step selected once"))
                preview.Step(document, selected, particleStep);
            ImGui::EndDisabled();
            if (!emitterSelected)
                ImGui::TextDisabled("Select an EMITTER to use solo controls.");
            if (emitterSelected)
            {
                auto effect = std::find_if(document.EditableEffects().begin(), document.EditableEffects().end(),
                                           [&](const BrxEffectRecord &candidate)
                                           { return candidate.ownerObjectIndex == selected; });
                if (effect != document.EditableEffects().end())
                {
                    ImGui::SeparatorText("Emission origin workflow");
                    if (ImGui::Button("Point origin"))
                    {
                        effect->originKind = 0;
                        effect->curveKind = -1;
                        workspace.Refresh(document);
                        preview.Restart(selected);
                        status = "Emitter now uses a point origin.";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Curve origin"))
                    {
                        effect->originKind = 1;
                        if (effect->curveKind < 0)
                            effect->curveKind = 0;
                        if (effect->curvePoints.empty())
                        {
                            BrxEffectCurvePoint a{}, b{};
                            b.u = 1.0f;
                            b.position[0] = 100.0f;
                            effect->curvePoints = {a, b};
                        }
                        workspace.Refresh(document);
                        preview.Restart(selected);
                        status = "Emitter now uses an editable curve origin.";
                    }
                    ImGui::Text("Origin payload: %zu curve point(s), %zu mesh triangle(s), %zu color key(s)",
                                effect->curvePoints.size(), effect->meshTriangles.size(), effect->colors.size());
                }
            }
            ImGui::TextWrapped(
                "Use the selected EMITTER inspector for BLIP/RIP groups, emission shape, lifetime, color, scale and motion curves.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("11 Splice"))
        {
            if (ImGui::Button("Open Splice Events graph and diagnostics"))
                showSpliceEvents = true;
            if (validSelected)
            {
                const auto &object = document.Objects()[static_cast<std::size_t>(selected)];
                if (object.spliceIndex >= 0 &&
                    static_cast<std::size_t>(object.spliceIndex) < document.SpliceModules().size())
                {
                    static std::uint64_t spliceRevision = (std::numeric_limits<std::uint64_t>::max)();
                    static std::int32_t analyzedModule = -1;
                    static SpliceModuleAnalysis analysis;
                    if (spliceRevision != workspace.Revision() || analyzedModule != object.spliceIndex)
                    {
                        analysis = AnalyzeSpliceModule(document, object.spliceIndex);
                        spliceRevision = workspace.Revision();
                        analyzedModule = object.spliceIndex;
                    }
                    ImGui::Text("Module %d | %u nodes | depth %u", object.spliceIndex, analysis.nodeCount,
                                analysis.maximumDepth);
                    ImGui::Text("%zu event(s), %zu import(s), %zu diagnostic(s)", analysis.eventIds.size(),
                                analysis.imports.size(), analysis.diagnostics.size());
                }
                else
                    ImGui::TextDisabled("Selected object has no splice module.");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("12 Impact"))
        {
            if (!validSelected)
                ImGui::TextDisabled("Select an object to inspect the impact of editing or deleting its hierarchy.");
            else
            {
                static std::uint64_t impactRevision = (std::numeric_limits<std::uint64_t>::max)();
                static std::int32_t impactObject = -1;
                static ImpactSummary impact;
                if (impactRevision != workspace.Revision() || impactObject != selected)
                {
                    impact = {};
                    std::vector<std::uint8_t> affected(document.Objects().size(), 0);
                    std::vector<std::vector<std::int32_t>> children(document.Objects().size());
                    for (std::size_t index = 0; index < document.Objects().size(); ++index)
                    {
                        const auto parent = document.Objects()[index].parentIndex;
                        if (parent >= 0 && static_cast<std::size_t>(parent) < children.size() &&
                            parent != static_cast<std::int32_t>(index))
                            children[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(index));
                    }
                    affected[static_cast<std::size_t>(selected)] = 1;
                    std::vector<std::int32_t> hierarchy{selected};
                    for (std::size_t cursor = 0; cursor < hierarchy.size(); ++cursor)
                        for (const auto child : children[static_cast<std::size_t>(hierarchy[cursor])])
                            if (!affected[static_cast<std::size_t>(child)])
                            {
                                affected[static_cast<std::size_t>(child)] = 1;
                                hierarchy.push_back(child);
                            }
                    impact.hierarchy = hierarchy.size();
                    for (const auto &reference : workspace.Scene().References())
                    {
                        const bool sourceAffected =
                            reference.sourceObjectIndex >= 0 &&
                            static_cast<std::size_t>(reference.sourceObjectIndex) < affected.size() &&
                            affected[static_cast<std::size_t>(reference.sourceObjectIndex)];
                        const bool targetAffected =
                            reference.targetObjectIndex >= 0 &&
                            static_cast<std::size_t>(reference.targetObjectIndex) < affected.size() &&
                            affected[static_cast<std::size_t>(reference.targetObjectIndex)];
                        if (sourceAffected && !targetAffected)
                            ++impact.outgoing;
                        if (!sourceAffected && targetAffected)
                            ++impact.incoming;
                    }
                    const auto countOwners = [&](const auto &records) -> std::size_t
                    {
                        return static_cast<std::size_t>(std::count_if(
                            records.begin(), records.end(),
                            [&](const auto &record)
                            {
                                return record.ownerObjectIndex >= 0 &&
                                       static_cast<std::size_t>(record.ownerObjectIndex) < affected.size() &&
                                       affected[static_cast<std::size_t>(record.ownerObjectIndex)];
                            }));
                    };
                    impact.globs = countOwners(document.Globs());
                    impact.collision = countOwners(document.CollisionTriangles());
                    impact.effects = countOwners(document.Effects());
                    impact.paths = countOwners(document.ShapePaths());
                    impact.animations = countOwners(document.AnimationSegments());
                    impactRevision = workspace.Revision();
                    impactObject = selected;
                }
                ImGui::Text("Hierarchy objects: %zu", impact.hierarchy);
                ImGui::Text("External references: %zu incoming / %zu outgoing", impact.incoming, impact.outgoing);
                ImGui::Text(
                    "Owned resources: %zu GLOBs | %zu collision faces | %zu effects | %zu paths | %zu animations",
                    impact.globs, impact.collision, impact.effects, impact.paths, impact.animations);
                if (impact.incoming)
                    ImGui::TextColored(
                        ImVec4(1, .55f, .2f, 1),
                        "Deleting this hierarchy would leave external object references that must be redirected or cleared.");
                ImGui::Checkbox("Show affected reference links", &showReferenceLinks);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("13 History"))
        {
            ImGui::Text("Undo entries: %zu | Redo entries: %zu", undoCount, redoCount);
            const std::string undoButtonLabel = undoLabel.empty() ? "Undo" : "Undo " + undoLabel;
            const std::string redoButtonLabel = redoLabel.empty() ? "Redo" : "Redo " + redoLabel;
            ImGui::BeginDisabled(undoCount == 0);
            if (ImGui::Button(undoButtonLabel.c_str()))
                requestUndo = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(redoCount == 0);
            if (ImGui::Button(redoButtonLabel.c_str()))
                requestRedo = true;
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "Transforms, properties, resources, scripts and structural edits are restored as complete editor-document transactions.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("14 Playtest"))
        {
            static std::filesystem::path testDestination;
            static std::filesystem::path projectCaneExecutable;
            static std::uint64_t analyzedRevision = (std::numeric_limits<std::uint64_t>::max)();
            static BrxCompilePlan cachedPlan;
            if (analyzedRevision != workspace.Revision())
            {
                cachedPlan = workspace.Compiler().Analyze(document, workspace.Validation());
                analyzedRevision = workspace.Revision();
            }
            const auto &plan = cachedPlan;
            ImGui::TextColored(plan.writerAvailable ? ImVec4(.35f, 1, .45f, 1) : ImVec4(1, .35f, .3f, 1), "%s",
                               plan.writerAvailable ? "Test build is export-ready" : "Test build is blocked");
            for (const auto &blocker : plan.blockers)
                ImGui::BulletText("%s", blocker.c_str());
            if (ImGui::Button("Run validation now"))
            {
                workspace.Refresh(document);
                status = "Validation refreshed.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Choose test output..."))
            {
                const auto selectedPath = ChooseBrxSaveFile(window, document.Path());
                if (!selectedPath.empty())
                    testDestination = selectedPath;
            }
            ImGui::TextWrapped("Output: %s",
                               testDestination.empty() ? "(not selected)" : testDestination.string().c_str());
            if (ImGui::Button("Choose ProjectCane executable..."))
            {
                const auto selectedPath = ChooseExecutableFile(window);
                if (!selectedPath.empty())
                    projectCaneExecutable = selectedPath;
            }
            ImGui::TextWrapped("Runtime: %s", projectCaneExecutable.empty() ? "(not selected)"
                                                                            : projectCaneExecutable.string().c_str());
            ImGui::BeginDisabled(!plan.writerAvailable || testDestination.empty());
            if (ImGui::Button("Export test build"))
            {
                std::string error;
                if (workspace.Compiler().WriteRebuiltPayload(document, workspace.Validation(), testDestination, error))
                    status = "Exported playtest build to " + testDestination.string() + ".";
                else
                    status = "Playtest export failed: " + error;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(projectCaneExecutable.empty());
            if (ImGui::Button("Launch ProjectCane"))
            {
                status = LaunchExecutable(projectCaneExecutable) ? "Launched ProjectCane."
                                                                 : "Could not launch ProjectCane executable.";
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped(
                "This uses the same from-scratch retail writer as normal export, without mutating or reopening the active editor document.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("15 Build Mode"))
        {
            ImGui::TextWrapped(
                "Open the high-frequency world-building tools together while keeping exact CID and BRX authoring underneath.");
            if (ImGui::Button("Open asset browser"))
                showAssetBrowser = true;
            ImGui::SameLine();
            if (ImGui::Button("Open placement tools"))
                showPlacementTools = true;
            ImGui::Checkbox("Show traversal paths", &showPaths);
            ImGui::Checkbox("Show selected references", &showReferenceLinks);
            ImGui::TextDisabled(
                "Recommended flow: choose/prefab an asset, place it, snap or distribute it, then validate and playtest.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
