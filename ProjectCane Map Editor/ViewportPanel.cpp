#include "ViewportPanel.h"
#include "AssetDatabase.h"
#include "BrxDocument.h"
#include "EditorDocumentOperations.h"
#include "EditorCamera.h"
#include "EditorWorkspace.h"
#include "SceneRenderer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <glm/glm.hpp>

bool HandleViewportAssetDrop(BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                             const EditorCamera &camera, int &gizmoMode, std::string &status)
{
    if (!ImGui::BeginDragDropTarget())
        return false;
    bool changed = false;
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CANE_ASSET");
        payload && payload->DataSize == sizeof(EditorAssetDragPayload))
    {
        const auto dropped = *static_cast<const EditorAssetDragPayload *>(payload->Data);
        const auto kind = static_cast<EditorAssetKind>(dropped.kind);
        const float yaw = glm::radians(camera.Yaw());
        const float pitch = glm::radians(camera.Pitch());
        const glm::vec3 forward(std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch));
        const glm::vec3 placement = camera.Position() + forward * 500.0f;
        float placementTransform[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, placement.x, placement.y, placement.z, 1};
        if (kind == EditorAssetKind::Prefab)
        {
            const auto result = workspace.Prefabs().Instantiate(document, dropped.stableId, -1, placementTransform);
            status = result.message;
            if (result.succeeded)
            {
                renderer.SelectObject(result.primaryObjectIndex);
                gizmoMode = 0;
                changed = true;
            }
        }
        else if (const auto *asset = workspace.Assets().FindById(dropped.stableId))
        {
            if (kind == EditorAssetKind::Model && asset->ownerObjectIndex >= 0)
            {
                const auto result = EditorDocumentOperations::DuplicateHierarchy(document, asset->ownerObjectIndex);
                status = result.message;
                if (result.succeeded)
                {
                    const auto &root = document.Objects()[result.primaryObjectIndex];
                    const glm::vec3 delta = placement - glm::vec3(root.worldTransform[12], root.worldTransform[13],
                                                                  root.worldTransform[14]);
                    for (const auto index : result.affectedObjectIndices)
                    {
                        auto &transform = document.EditableObjects()[index].worldTransform;
                        transform[12] += delta.x;
                        transform[13] += delta.y;
                        transform[14] += delta.z;
                    }
                    renderer.SelectObject(result.primaryObjectIndex);
                    gizmoMode = 0;
                    changed = true;
                }
            }
            else if ((kind == EditorAssetKind::Material || kind == EditorAssetKind::Texture) &&
                     renderer.HasSubglobSelection())
            {
                auto glob = std::find_if(document.EditableGlobs().begin(), document.EditableGlobs().end(),
                                         [&](const BrxGlobRecord &candidate)
                                         { return candidate.renderGroup == renderer.SelectedRenderGroup(); });
                if (glob != document.EditableGlobs().end())
                {
                    const auto batch = std::find_if(
                        glob->derivedBatches.begin(), glob->derivedBatches.end(), [&](const BrxRenderBatch &candidate)
                        { return candidate.firstIndex == renderer.SelectedSubglobFirstIndex(); });
                    const auto subglobIndex =
                        batch == glob->derivedBatches.end()
                            ? glob->derivedBatches.size()
                            : static_cast<std::size_t>(std::distance(glob->derivedBatches.begin(), batch));
                    if (batch != glob->derivedBatches.end() && kind == EditorAssetKind::Material)
                    {
                        if (subglobIndex < glob->sourceSubglobs.size())
                        {
                            glob->sourceSubglobs[subglobIndex].shaderIndex =
                                static_cast<std::uint16_t>(asset->sourceIndex);
                            batch->shaderIndex = static_cast<std::uint16_t>(asset->sourceIndex);
                            glob->modified = true;
                            status = "Applied material to selected SUBGLOB.";
                            changed = true;
                        }
                        else
                            status = "Selected render packet has no authoritative SUBGLOB record.";
                    }
                    else if (batch != glob->derivedBatches.end() && batch->shaderIndex < document.Shaders().size())
                    {
                        auto &shader = document.EditableShaders()[batch->shaderIndex];
                        if (shader.textureStages.empty())
                            shader.textureStages.emplace_back();
                        auto &stage = shader.textureStages.front();
                        const std::size_t frameCount =
                            (std::max)({std::size_t{1}, stage.ambientBitmapIndices.size(),
                                        stage.diffuseBitmapIndices.size(), stage.saturateBitmapIndices.size()});
                        stage.ambientBitmapIndices.resize(frameCount, -1);
                        stage.diffuseBitmapIndices.resize(frameCount, -1);
                        stage.saturateBitmapIndices.resize(frameCount, -1);
                        stage.diffuseBitmapIndices.front() = asset->sourceIndex;
                        shader.ambientBitmapIndices = stage.ambientBitmapIndices;
                        shader.diffuseBitmapIndices = stage.diffuseBitmapIndices;
                        shader.saturateBitmapIndices = stage.saturateBitmapIndices;
                        shader.textureStageCount = static_cast<std::uint8_t>(shader.textureStages.size());
                        status = "Applied texture to selected SUBGLOB material.";
                        changed = true;
                    }
                }
            }
            else if (kind == EditorAssetKind::Material || kind == EditorAssetKind::Texture)
                status = "Select a SUBGLOB before applying a material or texture asset.";
            else
                status = "This asset is inspected through its owning object and cannot be placed independently.";
        }
        else
            status = "The dropped asset no longer exists in the active asset database.";
    }
    ImGui::EndDragDropTarget();
    if (changed)
    {
        document.RebuildRenderPacketsFromGlobs();
        workspace.Refresh(document);
        renderer.Upload(document);
    }
    return changed;
}
