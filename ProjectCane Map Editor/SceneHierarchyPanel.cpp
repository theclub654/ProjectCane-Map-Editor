#include "SceneHierarchyPanel.h"

#include "BrxDocument.h"
#include "EditorDocumentOperations.h"
#include "EditorOptions.h"
#include "ResolvedScene.h"
#include "SceneRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

void DrawSceneHierarchyPanel(const ImVec2 &position, const ImVec2 &size, const ImGuiWindowFlags panelFlags,
                             BrxDocument &document, const ResolvedScene &resolvedScene, SceneRenderer &sceneRenderer,
                             ImGuiTextFilter &objectFilter, std::int32_t &lastOutlinerObject, std::string &status,
                             SceneHierarchyAction &action, const bool dockable)
{
    action = {};
    if (!dockable)
    {
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
    }
    if (!ImGui::Begin("Scene", nullptr, panelFlags))
    {
        ImGui::End();
        return;
    }
    if (document.IsOpen())
    {
        ImGui::TextDisabled("OPEN LEVEL");
        ImGui::TextUnformatted(document.Path().filename().string().c_str());
        ImGui::TextDisabled("%zu objects", document.Objects().size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select an object to inspect it. Right-click for move, duplicate, replace, and hierarchy actions.");
        ImGui::Separator();
        const std::int32_t selectedObjectIndex = sceneRenderer.SelectedObjectIndex();
        const bool selectedObjectValid =
            selectedObjectIndex >= 0 && static_cast<std::size_t>(selectedObjectIndex) < document.Objects().size();
        // Follow selection automatically; clearing it restores the world tree.
        const bool focusedHierarchy = selectedObjectValid;
        static std::int32_t previouslyFocusedObject = -1;
        static std::unordered_set<std::int32_t> collapsedHierarchyObjects;
        // Large retail worlds contain several thousand objects and GLOBs. The
        // hierarchy topology and GLOB ownership only change when ResolvedScene
        // is rebuilt, so do not allocate and repopulate those indices on every
        // rendered frame. Collapse state is included in the display-order key
        // because it deliberately prunes descendants.
        static std::uint64_t cachedSceneRevision = 0;
        static std::uint64_t cachedCollapseFingerprint = 0;
        static std::int32_t cachedFocusedRoot = -2;
        static bool cachedFocusedHierarchy = false;
        static bool cachedFilterActive = false;
        static std::vector<std::pair<std::int32_t, int>> cachedDisplayOrder;
        static std::vector<std::vector<std::size_t>> cachedGlobIndicesByOwner;
        static std::unordered_set<std::int32_t> expandedGlobObjects;
        static bool hierarchyExpansionStateInitialized = false;
        static std::int32_t renameObjectIndex = -1;
        static std::array<char, 128> renameObjectBuffer{};
        static bool requestRenamePopup = false;
        static std::int32_t moveObjectIndex = -1;
        static ImGuiTextFilter parentFilter;
        static ModelImportOptions replacementModelOptions;
        bool openMovePopup = false;
        const auto acceptObjectDrop = [&](const std::int32_t parentIndex)
        {
            if (!ImGui::BeginDragDropTarget())
                return;
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CANE_HIERARCHY_OBJECT"))
            {
                if (payload->DataSize == sizeof(std::int32_t))
                {
                    std::int32_t source = -1;
                    std::memcpy(&source, payload->Data, sizeof(source));
                    action = {SceneHierarchyAction::Kind::MoveToParent, source, parentIndex};
                }
            }
            ImGui::EndDragDropTarget();
        };
        const bool revealSelectedObject = selectedObjectValid && selectedObjectIndex != previouslyFocusedObject;
        previouslyFocusedObject = selectedObjectValid ? selectedObjectIndex : -1;
        std::int32_t focusedRootIndex = focusedHierarchy ? selectedObjectIndex : -1;
        if (selectedObjectValid)
        {
            std::vector<bool> ancestorVisited(document.Objects().size(), false);
            while (focusedRootIndex >= 0 && static_cast<std::size_t>(focusedRootIndex) < document.Objects().size())
            {
                const auto rootOffset = static_cast<std::size_t>(focusedRootIndex);
                if (ancestorVisited[rootOffset])
                    break;
                ancestorVisited[rootOffset] = true;
                const std::int32_t parentIndex = document.Objects()[rootOffset].parentIndex;
                if (parentIndex < 0 || static_cast<std::size_t>(parentIndex) >= document.Objects().size())
                    break;
                focusedRootIndex = parentIndex;
            }
            if (revealSelectedObject)
            {
                std::int32_t ancestorIndex = selectedObjectIndex;
                std::vector<bool> revealVisited(document.Objects().size(), false);
                while (ancestorIndex >= 0 && static_cast<std::size_t>(ancestorIndex) < document.Objects().size())
                {
                    const auto ancestorOffset = static_cast<std::size_t>(ancestorIndex);
                    if (revealVisited[ancestorOffset])
                        break;
                    revealVisited[ancestorOffset] = true;
                    collapsedHierarchyObjects.erase(ancestorIndex);
                    ancestorIndex = document.Objects()[ancestorOffset].parentIndex;
                }
            }
        }
        if (focusedHierarchy)
        {
            const auto &selectedObject = document.Objects()[static_cast<std::size_t>(selectedObjectIndex)];
            const std::string selectedClassName = BrxDocument::EditorClassName(selectedObject.cid);
            ImGui::BeginChild("SelectedObjectCard", ImVec2(0.0f, 78.0f), true);
            if (ImGui::SmallButton("<  World"))
            {
                sceneRenderer.ClearSelection();
                lastOutlinerObject = -1;
                status = "Selection cleared.";
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Focused hierarchy");
            ImGui::Spacing();
            ImGui::TextUnformatted(document.ObjectEditorLabel(selectedObjectIndex).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("CID %d  |  OID %d", selectedObject.cid, selectedObject.oid);
            const auto hierarchyChildren = resolvedScene.Children(selectedObjectIndex).size();
            ImGui::TextDisabled("%zu direct child%s  |  Esc returns to the world", hierarchyChildren,
                                hierarchyChildren == 1 ? "" : "ren");
            ImGui::EndChild();
            ImGui::Separator();
        }

        ImGui::Button("World root (drop objects here)", ImVec2(-1.0f, 0.0f));
        acceptObjectDrop(-1);
        const bool worldOpen = focusedHierarchy || ImGui::TreeNodeEx("World", ImGuiTreeNodeFlags_DefaultOpen);
        if (worldOpen)
        {
            if (!focusedHierarchy)
            {
                ImGui::TextDisabled("%zu objects  |  %zu roots  |  %zu GLOBs", document.Objects().size(),
                                    resolvedScene.Roots().size(), document.Globs().size());
                objectFilter.Draw("Search", -1.0f);
                if (!objectFilter.IsActive())
                    ImGui::TextDisabled("Search names, types, or object IDs");
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("World contents", ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    for (const auto &section : document.Sections())
                    {
                        if (section.state == BrxSectionState::Parsed)
                            ImGui::BulletText("%s", section.name.c_str());
                        else
                            ImGui::TextDisabled("  %s (parser pending)", section.name.c_str());
                    }
                    if (!document.Bitmaps().empty())
                        ImGui::BulletText("%zu bitmap descriptors", document.Bitmaps().size());
                    if (!document.Shaders().empty())
                        ImGui::BulletText("%zu shader descriptors", document.Shaders().size());
                    ImGui::TreePop();
                }
                if (ImGui::TreeNodeEx("Object type summary", ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    std::array<std::size_t, 148> cidCounts{};
                    for (const auto &object : document.Objects())
                        if (object.cid >= 0 && object.cid < static_cast<std::int16_t>(cidCounts.size()))
                            ++cidCounts[static_cast<std::size_t>(object.cid)];
                    for (std::size_t cid = 0; cid < cidCounts.size(); ++cid)
                    {
                        if (cidCounts[cid] == 0)
                            continue;
                        const std::string className = BrxDocument::EditorClassName(static_cast<std::int16_t>(cid));
                        ImGui::BulletText("%s", className.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("CID %zu  |  %zu object%s", cid, cidCounts[cid],
                                            cidCounts[cid] == 1 ? "" : "s");
                    }
                    ImGui::TreePop();
                }
                ImGui::SeparatorText("Objects");
            }
            else
            {
                ImGui::SeparatorText("Hierarchy");
            }
            ImGui::TextDisabled("Drag to reparent. Right-click to move, replace or delete.");
            std::uint64_t collapseFingerprint = 1469598103934665603ull;
            for (const std::int32_t objectIndex : collapsedHierarchyObjects)
            {
                collapseFingerprint ^= static_cast<std::uint32_t>(objectIndex);
                collapseFingerprint *= 1099511628211ull;
            }
            const bool sceneCacheStale = cachedSceneRevision != resolvedScene.Revision() ||
                                         cachedGlobIndicesByOwner.size() != document.Objects().size();
            if (sceneCacheStale)
            {
                cachedGlobIndicesByOwner.assign(document.Objects().size(), {});
                for (std::size_t globIndex = 0; globIndex < document.Globs().size(); ++globIndex)
                {
                    const auto owner = document.Globs()[globIndex].ownerObjectIndex;
                    if (owner >= 0 && static_cast<std::size_t>(owner) < cachedGlobIndicesByOwner.size())
                        cachedGlobIndicesByOwner[static_cast<std::size_t>(owner)].push_back(globIndex);
                }
            }

            // Active filtering needs a full traversal because matching
            // descendants remain discoverable beneath collapsed parents. The
            // common unfiltered view can reuse its flattened order.
            const bool rebuildDisplayOrder =
                objectFilter.IsActive() || sceneCacheStale || cachedCollapseFingerprint != collapseFingerprint ||
                cachedFocusedRoot != focusedRootIndex || cachedFocusedHierarchy != focusedHierarchy ||
                cachedFilterActive != objectFilter.IsActive();
            if (rebuildDisplayOrder)
            {
                cachedDisplayOrder.clear();
                std::vector<bool> visited(document.Objects().size(), false);
                std::function<void(std::int32_t, int)> appendObject =
                    [&](const std::int32_t objectIndex, const int depth)
                {
                    if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= visited.size() ||
                        visited[static_cast<std::size_t>(objectIndex)])
                        return;
                    visited[static_cast<std::size_t>(objectIndex)] = true;
                    cachedDisplayOrder.emplace_back(objectIndex, depth);
                    if (!objectFilter.IsActive() && collapsedHierarchyObjects.contains(objectIndex))
                        return;
                    for (const auto child : resolvedScene.Children(objectIndex))
                        appendObject(child, depth + 1);
                };
                if (focusedHierarchy)
                    appendObject(focusedRootIndex, 0);
                else
                {
                    for (const auto root : resolvedScene.Roots())
                        appendObject(root, 0);
                    for (std::size_t index = 0; index < visited.size(); ++index)
                    {
                        if (visited[index])
                            continue;
                        bool beneathCollapsedBranch = false;
                        if (!objectFilter.IsActive())
                        {
                            std::int32_t ancestor = document.Objects()[index].parentIndex;
                            for (std::size_t guard = 0; ancestor >= 0 &&
                                                        static_cast<std::size_t>(ancestor) < visited.size() &&
                                                        guard++ <= visited.size();)
                            {
                                if (collapsedHierarchyObjects.contains(ancestor))
                                {
                                    beneathCollapsedBranch = true;
                                    break;
                                }
                                ancestor = document.Objects()[static_cast<std::size_t>(ancestor)].parentIndex;
                            }
                        }
                        if (!beneathCollapsedBranch)
                            appendObject(static_cast<std::int32_t>(index), 0);
                    }
                }
                cachedSceneRevision = resolvedScene.Revision();
                cachedCollapseFingerprint = collapseFingerprint;
                cachedFocusedRoot = focusedRootIndex;
                cachedFocusedHierarchy = focusedHierarchy;
                cachedFilterActive = objectFilter.IsActive();
            }
            const auto &displayOrder = cachedDisplayOrder;
            const auto &globIndicesByOwner = cachedGlobIndicesByOwner;

            // A collapsed object row has a fixed height. Once one complete pass
            // has learned which rows own expanded GLOB details, virtualize the
            // common flat hierarchy so ImGui only receives rows intersecting
            // the scroll viewport. Expanded GLOB/SUBGLOB trees and filtering
            // retain the complete path for exact variable-height behavior.
            const float hierarchyRowHeight = ImGui::GetFrameHeightWithSpacing();
            const float hierarchyListStartY = ImGui::GetCursorPosY();
            std::size_t displayBegin = 0;
            std::size_t displayEnd = displayOrder.size();
            const bool virtualizeHierarchy = hierarchyExpansionStateInitialized && expandedGlobObjects.empty() &&
                                             !objectFilter.IsActive() && !revealSelectedObject;
            if (virtualizeHierarchy && hierarchyRowHeight > 0.0f)
            {
                const float visibleTop = ImGui::GetScrollY();
                const float visibleBottom = visibleTop + ImGui::GetWindowHeight();
                const float overscan = hierarchyRowHeight * 3.0f;
                const double firstVisibleRow =
                    std::floor(static_cast<double>((visibleTop - hierarchyListStartY - overscan) / hierarchyRowHeight));
                const double lastVisibleRow = std::ceil(
                    static_cast<double>((visibleBottom - hierarchyListStartY + overscan) / hierarchyRowHeight));
                displayBegin = firstVisibleRow > 0.0 ? static_cast<std::size_t>(firstVisibleRow) : 0u;
                const std::size_t requestedEnd =
                    lastVisibleRow > 0.0 ? static_cast<std::size_t>(lastVisibleRow) + 1u : 1u;
                displayEnd = requestedEnd < displayOrder.size() ? requestedEnd : displayOrder.size();
                if (displayBegin > displayEnd)
                    displayBegin = displayEnd;
                ImGui::SetCursorPosY(hierarchyListStartY + static_cast<float>(displayBegin) * hierarchyRowHeight);
            }
            for (std::size_t displayIndex = displayBegin; displayIndex < displayEnd; ++displayIndex)
            {
                const auto [resolvedObjectIndex, depth] = displayOrder[displayIndex];
                const auto objectIndex = static_cast<std::size_t>(resolvedObjectIndex);
                const auto &object = document.Objects()[objectIndex];
                ImGui::Indent(static_cast<float>(depth) * 12.0f);
                const ImVec2 hierarchyRowStart = ImGui::GetCursorScreenPos();
                if (depth > 0)
                {
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    const ImU32 connectorColor = ImGui::GetColorU32(ImGuiCol_Border, 0.72f);
                    const float rowMiddleY = hierarchyRowStart.y + ImGui::GetFrameHeight() * 0.5f;
                    for (int ancestorDepth = 1; ancestorDepth <= depth; ++ancestorDepth)
                    {
                        const float connectorX =
                            hierarchyRowStart.x - static_cast<float>(depth - ancestorDepth) * 12.0f - 6.0f;
                        drawList->AddLine(
                            ImVec2(connectorX, hierarchyRowStart.y - ImGui::GetStyle().ItemSpacing.y * 0.5f),
                            ImVec2(connectorX, hierarchyRowStart.y + ImGui::GetFrameHeight() +
                                                   ImGui::GetStyle().ItemSpacing.y * 0.5f),
                            connectorColor, 1.0f);
                        if (ancestorDepth == depth)
                            drawList->AddLine(ImVec2(connectorX, rowMiddleY),
                                              ImVec2(hierarchyRowStart.x + 4.0f, rowMiddleY), connectorColor, 1.0f);
                    }
                }
                const bool selected = sceneRenderer.IsObjectSelected(static_cast<std::int32_t>(objectIndex));
                const std::string objectClassName = BrxDocument::EditorClassName(object.cid);
                const auto directChildCount = resolvedScene.Children(resolvedObjectIndex).size();
                const std::string displayName = document.ObjectEditorLabel(resolvedObjectIndex);
                const std::string label = displayName + "##object" + std::to_string(objectIndex);
                if (!focusedHierarchy && objectFilter.IsActive())
                {
                    std::string searchableLabel = displayName + " " + objectClassName +
                                                  " CID=" + std::to_string(object.cid) +
                                                  " OID=" + std::to_string(object.oid);
                    for (const std::size_t globIndex : globIndicesByOwner[objectIndex])
                    {
                        const auto &glob = document.Globs()[globIndex];
                        searchableLabel +=
                            " " + document.GlobEditorLabel(globIndex) + " GLOB=" + std::to_string(glob.renderGroup);
                        for (std::size_t subglobIndex = 0; subglobIndex < glob.derivedBatches.size(); ++subglobIndex)
                            searchableLabel += " " + document.SubglobEditorLabel(globIndex, subglobIndex) +
                                               " SUBGLOB=" + std::to_string(subglobIndex);
                    }
                    if (!objectFilter.PassFilter(searchableLabel.c_str()))
                    {
                        ImGui::Unindent(static_cast<float>(depth) * 12.0f);
                        continue;
                    }
                }
                ImGui::PushID(static_cast<int>(objectIndex));
                if (directChildCount != 0)
                {
                    const bool hierarchyCollapsed = collapsedHierarchyObjects.contains(resolvedObjectIndex);
                    if (ImGui::SmallButton(hierarchyCollapsed ? ">##hierarchy" : "v##hierarchy"))
                    {
                        if (hierarchyCollapsed)
                            collapsedHierarchyObjects.erase(resolvedObjectIndex);
                        else
                            collapsedHierarchyObjects.insert(resolvedObjectIndex);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(hierarchyCollapsed ? "Expand child objects" : "Collapse child objects");
                }
                else
                {
                    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                }
                ImGui::SameLine();
                const bool objectHidden = sceneRenderer.IsObjectEditorHidden(static_cast<std::int32_t>(objectIndex));
                const bool objectLocked = sceneRenderer.IsObjectLocked(static_cast<std::int32_t>(objectIndex));
                const bool hasGlobs = !globIndicesByOwner[objectIndex].empty();
                ImGuiTreeNodeFlags objectFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                                 ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (selected)
                    objectFlags |= ImGuiTreeNodeFlags_Selected;
                if (!hasGlobs)
                    objectFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                const bool objectOpen = ImGui::TreeNodeEx(label.c_str(), objectFlags);
                if (hasGlobs)
                {
                    if (objectOpen)
                        expandedGlobObjects.insert(resolvedObjectIndex);
                    else
                        expandedGlobObjects.erase(resolvedObjectIndex);
                }
                const bool objectRowClicked = ImGui::IsItemClicked();
                const bool objectOpenToggled = ImGui::IsItemToggledOpen();
                const bool objectRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                if (!objectLocked && ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("CANE_HIERARCHY_OBJECT", &resolvedObjectIndex,
                                             sizeof(resolvedObjectIndex));
                    ImGui::Text("Move %s", displayName.c_str());
                    ImGui::EndDragDropSource();
                }
                if (!objectLocked)
                    acceptObjectDrop(resolvedObjectIndex);
                if (objectRightClicked && !selected && !objectLocked)
                {
                    sceneRenderer.SelectObject(resolvedObjectIndex);
                    lastOutlinerObject = resolvedObjectIndex;
                }
                if (ImGui::BeginPopupContextItem("ObjectContextMenu"))
                {
                    ImGui::TextUnformatted(displayName.c_str());
                    if (!object.editorName.empty())
                        ImGui::TextDisabled("%s", objectClassName.c_str());
                    ImGui::TextDisabled("CID %d  |  OID %d", object.cid, object.oid);
                    ImGui::Separator();
                    ImGui::BeginDisabled(objectLocked);
                    if (ImGui::MenuItem("Move in viewport"))
                        action = {SceneHierarchyAction::Kind::MoveInViewport, resolvedObjectIndex};
                    if (ImGui::MenuItem("Select children for object..."))
                    {
                        moveObjectIndex = resolvedObjectIndex;
                        parentFilter.Clear();
                        openMovePopup = true;
                    }
                    if (ImGui::MenuItem("Move to world root", nullptr, false, object.parentIndex >= 0))
                        action = {SceneHierarchyAction::Kind::MoveToParent, resolvedObjectIndex, -1};
                    if (ImGui::BeginMenu("Replace"))
                    {
                        if (ImGui::MenuItem("Object hierarchy from package..."))
                            action = {SceneHierarchyAction::Kind::ReplaceHierarchy, resolvedObjectIndex};
                        if (object.proxyClone)
                            ImGui::TextDisabled("Make this linked instance unique to replace its model.");
                        if (ImGui::BeginMenu("Render model", CidDerivesFrom(object.cid, 2) && !object.proxyClone))
                        {
                            ImGui::TextDisabled("Keeps the object's identity and children.");
                            ImGui::DragFloat("Import scale", &replacementModelOptions.scale, 0.1f);
                            ImGui::Checkbox("Flip texture V", &replacementModelOptions.flipV);
                            ImGui::Checkbox("Import materials", &replacementModelOptions.importMaterials);
                            if (ImGui::MenuItem("Choose model file..."))
                                action = {SceneHierarchyAction::Kind::ReplaceModel, resolvedObjectIndex, -1,
                                          replacementModelOptions};
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    if (object.proxyRecordIndex >= 0 && ImGui::MenuItem("Make PROXY instance unique"))
                        action = {SceneHierarchyAction::Kind::MakeUnique, resolvedObjectIndex};
                    if (ImGui::MenuItem("Delete object and children..."))
                        action = {SceneHierarchyAction::Kind::DeleteHierarchy, resolvedObjectIndex};
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename object"))
                    {
                        renameObjectIndex = resolvedObjectIndex;
                        const auto copied = (std::min)(object.editorName.size(), renameObjectBuffer.size() - 1u);
                        std::copy_n(object.editorName.data(), copied, renameObjectBuffer.data());
                        renameObjectBuffer[copied] = '\0';
                        requestRenamePopup = true;
                    }
                    if (ImGui::MenuItem(objectHidden ? "Show object" : "Hide object"))
                        sceneRenderer.SetObjectEditorVisible(resolvedObjectIndex, objectHidden);
                    if (ImGui::MenuItem(objectLocked ? "Unlock selection" : "Lock selection"))
                        sceneRenderer.SetObjectLocked(resolvedObjectIndex, !objectLocked);
                    ImGui::Separator();
                    if (CidDerivesFrom(object.cid, 2) &&
                        ImGui::MenuItem("Add XFM transform marker", nullptr, false, !objectLocked))
                        action = {SceneHierarchyAction::Kind::CreateTransformMarker, resolvedObjectIndex};
                    if (ImGui::MenuItem("Select object", nullptr, false, !objectLocked))
                    {
                        sceneRenderer.SelectObject(resolvedObjectIndex);
                        lastOutlinerObject = resolvedObjectIndex;
                    }
                    if (directChildCount != 0 && ImGui::MenuItem(collapsedHierarchyObjects.contains(resolvedObjectIndex)
                                                                     ? "Expand child objects"
                                                                     : "Collapse child objects"))
                    {
                        if (collapsedHierarchyObjects.contains(resolvedObjectIndex))
                            collapsedHierarchyObjects.erase(resolvedObjectIndex);
                        else
                            collapsedHierarchyObjects.insert(resolvedObjectIndex);
                    }
                    ImGui::EndPopup();
                }
                if (requestRenamePopup && renameObjectIndex == resolvedObjectIndex)
                {
                    ImGui::OpenPopup("Rename object");
                    requestRenamePopup = false;
                }
                if (ImGui::BeginPopupModal("Rename object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextUnformatted("Editor display name");
                    ImGui::SetNextItemWidth(360.0f);
                    const bool submitted =
                        ImGui::InputText("##ObjectName", renameObjectBuffer.data(), renameObjectBuffer.size(),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::TextDisabled("Editor-only: CID, OID and exported runtime identity are unchanged.");
                    const std::string requestedName = renameObjectBuffer.data();
                    const bool validIdentifier =
                        requestedName.empty() ||
                        ((std::isalpha(static_cast<unsigned char>(requestedName.front())) ||
                          requestedName.front() == '_') &&
                         std::all_of(
                             requestedName.begin(), requestedName.end(), [](const char character)
                             { return std::isalnum(static_cast<unsigned char>(character)) || character == '_'; }));
                    const std::int32_t duplicate =
                        requestedName.empty() ? -1 : document.FindObjectByEditorName(requestedName);
                    const bool duplicateName = duplicate >= 0 && duplicate != renameObjectIndex;
                    if (!validIdentifier)
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
                                           "Use a letter or underscore first, then letters, numbers or underscores.");
                    if (duplicateName)
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
                                           "That name is already used by another object.");
                    const bool accept = (ImGui::Button("Rename") || submitted) && validIdentifier && !duplicateName;
                    if (accept && renameObjectIndex >= 0 &&
                        static_cast<std::size_t>(renameObjectIndex) < document.EditableObjects().size())
                    {
                        document.EditableObjects()[static_cast<std::size_t>(renameObjectIndex)].editorName =
                            requestedName;
                        status = requestedName.empty() ? "Cleared the object alias."
                                                       : "Renamed object to " + requestedName + ".";
                        renameObjectIndex = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Use default label"))
                    {
                        if (renameObjectIndex >= 0 &&
                            static_cast<std::size_t>(renameObjectIndex) < document.EditableObjects().size())
                            document.EditableObjects()[static_cast<std::size_t>(renameObjectIndex)].editorName.clear();
                        status = "Cleared the object alias.";
                        renameObjectIndex = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                    {
                        renameObjectIndex = -1;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                if (objectHidden)
                    ImGui::TextDisabled("hidden  |");
                if (objectHidden)
                    ImGui::SameLine();
                if (objectLocked)
                    ImGui::TextDisabled("locked  |");
                if (objectLocked)
                    ImGui::SameLine();
                ImGui::TextDisabled("CID %d  OID %d", object.cid, object.oid);
                if (directChildCount != 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("|  %zu child%s", directChildCount, directChildCount == 1 ? "" : "ren");
                }
                if (selected && revealSelectedObject)
                    ImGui::SetScrollHereY(0.45f);
                if (objectRowClicked && !objectOpenToggled && !objectLocked)
                {
                    const std::int32_t clickedObject = static_cast<std::int32_t>(objectIndex);
                    if (ImGui::GetIO().KeyShift && lastOutlinerObject >= 0)
                        sceneRenderer.SelectObjectRange(lastOutlinerObject, clickedObject);
                    else if (ImGui::GetIO().KeyCtrl)
                        sceneRenderer.ToggleObjectSelection(clickedObject);
                    else
                        sceneRenderer.SelectObject(clickedObject);
                    lastOutlinerObject = clickedObject;
                    status = std::to_string(sceneRenderer.SelectionCount()) + " object(s) selected.";
                }
                if (objectOpen && hasGlobs)
                {
                    for (const std::size_t globIndex : globIndicesByOwner[objectIndex])
                    {
                        const auto &glob = document.Globs()[globIndex];
                        ImGui::PushID(static_cast<int>(glob.renderGroup));
                        const bool globHidden = sceneRenderer.IsGlobEditorHidden(glob.renderGroup);
                        if (ImGui::SmallButton(globHidden ? "H##globVisibility" : "V##globVisibility"))
                            sceneRenderer.SetGlobEditorVisible(glob.renderGroup, globHidden);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(globHidden ? "Show GLOB" : "Hide GLOB");
                        ImGui::SameLine();
                        const bool globLocked = sceneRenderer.IsGlobLocked(glob.renderGroup);
                        if (ImGui::SmallButton(globLocked ? "L##globLock" : "U##globLock"))
                            sceneRenderer.SetGlobLocked(glob.renderGroup, !globLocked);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(globLocked ? "Unlock GLOB" : "Lock GLOB selection");
                        ImGui::SameLine();
                        ImGuiTreeNodeFlags globFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                                       ImGuiTreeNodeFlags_OpenOnArrow |
                                                       ImGuiTreeNodeFlags_OpenOnDoubleClick;
                        if (sceneRenderer.IsRenderGroupSelected(glob.renderGroup))
                            globFlags |= ImGuiTreeNodeFlags_Selected;
                        if (glob.derivedBatches.empty())
                            globFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        const std::string globLabel =
                            document.GlobEditorLabel(globIndex) +
                            (glob.editorName.empty() ? std::string{}
                                                     : "  [GLOB " + std::to_string(glob.renderGroup) + "]") +
                            " (" + std::to_string(glob.derivedBatches.size()) + " SUBGLOBs)";
                        const bool globOpen = ImGui::TreeNodeEx(globLabel.c_str(), globFlags);
                        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !globLocked)
                        {
                            if (ImGui::GetIO().KeyCtrl)
                                sceneRenderer.ToggleRenderGroupSelection(glob.renderGroup);
                            else
                                sceneRenderer.SelectRenderGroup(glob.renderGroup);
                            status = std::to_string(sceneRenderer.SelectionCount()) + " GLOB(s) selected.";
                        }
                        if (globOpen && !glob.derivedBatches.empty())
                        {
                            for (std::size_t subglobIndex = 0; subglobIndex < glob.derivedBatches.size();
                                 ++subglobIndex)
                            {
                                const auto &batch = glob.derivedBatches[subglobIndex];
                                ImGui::PushID(static_cast<int>(subglobIndex));
                                const bool subglobHidden =
                                    sceneRenderer.IsSubglobEditorHidden(glob.renderGroup, batch.firstIndex);
                                if (ImGui::SmallButton(subglobHidden ? "H##subVisibility" : "V##subVisibility"))
                                    sceneRenderer.SetSubglobEditorVisible(glob.renderGroup, batch.firstIndex,
                                                                          subglobHidden);
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(subglobHidden ? "Show SUBGLOB" : "Hide SUBGLOB");
                                ImGui::SameLine();
                                const bool subglobLocked =
                                    sceneRenderer.IsSubglobLocked(glob.renderGroup, batch.firstIndex);
                                if (ImGui::SmallButton(subglobLocked ? "L##subLock" : "U##subLock"))
                                    sceneRenderer.SetSubglobLocked(glob.renderGroup, batch.firstIndex, !subglobLocked);
                                ImGui::SameLine();
                                const bool customSubglobName = subglobIndex < glob.sourceSubglobs.size() &&
                                                               !glob.sourceSubglobs[subglobIndex].editorName.empty();
                                const std::string subglobLabel =
                                    document.SubglobEditorLabel(globIndex, subglobIndex) +
                                    (customSubglobName ? "  [SUBGLOB " + std::to_string(subglobIndex) + "]" : "") +
                                    "  tris=" + std::to_string(batch.indexCount / 3);
                                const bool subglobSelected =
                                    sceneRenderer.IsSubglobSelected(glob.renderGroup, batch.firstIndex);
                                if (ImGui::Selectable(subglobLabel.c_str(), subglobSelected) && !subglobLocked)
                                {
                                    if (ImGui::GetIO().KeyCtrl)
                                        sceneRenderer.ToggleSubglobSelection(glob.renderGroup, batch.firstIndex);
                                    else
                                        sceneRenderer.SelectSubglob(glob.renderGroup, batch.firstIndex);
                                    status = std::to_string(sceneRenderer.SelectionCount()) + " SUBGLOB(s) selected.";
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                ImGui::Unindent(static_cast<float>(depth) * 12.0f);
            }
            if (virtualizeHierarchy)
                ImGui::SetCursorPosY(hierarchyListStartY +
                                     static_cast<float>(displayOrder.size()) * hierarchyRowHeight);
            hierarchyExpansionStateInitialized = true;
            if (!focusedHierarchy)
                ImGui::TreePop();
        }
        if (openMovePopup)
            ImGui::OpenPopup("Select children for object");
        ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Select children for object", nullptr, ImGuiWindowFlags_NoCollapse))
        {
            const bool validSource = moveObjectIndex >= 0 &&
                                     static_cast<std::size_t>(moveObjectIndex) < document.Objects().size();
            if (validSource)
                ImGui::TextWrapped("Add children to: %s", document.ObjectEditorLabel(moveObjectIndex).c_str());
            ImGui::TextWrapped("Click an object to attach it here, keeping its world position. Its existing children move with it. Each change can be undone.");
            ImGui::TextWrapped("All parent classes are allowed here. Export validation reports relationships the game cannot serialize. Linked PROXY ownership rules still apply.");
            parentFilter.Draw("Find child by name, class or OID", -1.0f);
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::BeginDisabled(!validSource || sceneRenderer.IsObjectLocked(moveObjectIndex));
            ImGui::BeginChild("Child choices", ImVec2(0.0f, -40.0f), true);
            for (std::size_t index = 0; index < document.Objects().size(); ++index)
            {
                const auto candidate = static_cast<std::int32_t>(index);
                if (candidate == moveObjectIndex)
                    continue;
                // A parent or ancestor cannot become its own descendant.
                bool ancestor = false;
                auto cursor = moveObjectIndex;
                for (std::size_t steps = 0; validSource && cursor >= 0 &&
                     static_cast<std::size_t>(cursor) < document.Objects().size() &&
                     steps < document.Objects().size(); ++steps)
                {
                    if (cursor == candidate)
                    {
                        ancestor = true;
                        break;
                    }
                    cursor = document.Objects()[static_cast<std::size_t>(cursor)].parentIndex;
                }
                if (ancestor)
                    continue;
                const auto &child = document.Objects()[index];
                const bool alreadyChild = child.parentIndex == moveObjectIndex;
                const std::string label = document.ObjectEditorLabel(candidate) + " | OID " +
                                          std::to_string(child.oid) + (alreadyChild ? " (already a child)" : "");
                if (!parentFilter.PassFilter(label.c_str()))
                    continue;
                ImGui::PushID(candidate);
                ImGui::BeginDisabled(alreadyChild || sceneRenderer.IsObjectLocked(candidate));
                if (ImGui::Selectable(label.c_str(), alreadyChild, ImGuiSelectableFlags_DontClosePopups))
                {
                    action = {SceneHierarchyAction::Kind::MoveToParent, candidate, moveObjectIndex};
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndDisabled();
            if (ImGui::Button("Done"))
            {
                moveObjectIndex = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    else
    {
        ImGui::TextDisabled("No level loaded");
    }
    ImGui::End();
}
