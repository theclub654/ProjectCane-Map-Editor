#include "AssetBrowserPanel.h"
#include "AssetDatabase.h"
#include "BrxDocument.h"
#include "EditorDocumentOperations.h"
#include "EditorCamera.h"
#include "EditorOptions.h"
#include "EditorProject.h"
#include "EditorWorkspace.h"
#include "PrefabLibrary.h"
#include "SceneRenderer.h"
#include "ModelImport.h"
#include "PlatformDialogs.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <glm/glm.hpp>

namespace
{
const char *KindName(EditorAssetKind kind)
{
    switch (kind)
    {
    case EditorAssetKind::Model:
        return "Models";
    case EditorAssetKind::Texture:
        return "Textures";
    case EditorAssetKind::Material:
        return "Materials";
    case EditorAssetKind::Animation:
        return "Animations";
    case EditorAssetKind::Effect:
        return "Effects";
    case EditorAssetKind::Audio:
        return "Audio";
    case EditorAssetKind::Collision:
        return "Collision";
    case EditorAssetKind::Prefab:
        return "Prefabs";
    }
    return "Assets";
}
ImU32 KindColor(EditorAssetKind kind)
{
    static constexpr std::array<ImU32, 8> colors{IM_COL32(80, 145, 235, 255),  IM_COL32(86, 190, 140, 255),
                                                 IM_COL32(190, 120, 220, 255), IM_COL32(230, 170, 65, 255),
                                                 IM_COL32(235, 95, 105, 255),  IM_COL32(90, 195, 205, 255),
                                                 IM_COL32(210, 105, 70, 255),  IM_COL32(235, 125, 60, 255)};
    return colors[static_cast<std::size_t>(kind)];
}
std::string UniqueImportedObjectName(const BrxDocument &document, const std::filesystem::path &path)
{
    std::string base = path.stem().string();
    for (char &character : base)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_')
            character = '_';
    if (base.empty() || (!std::isalpha(static_cast<unsigned char>(base.front())) && base.front() != '_'))
        base = "Model_" + base;
    std::string candidate = base;
    for (std::size_t suffix = 2; document.FindObjectByEditorName(candidate) >= 0; ++suffix)
        candidate = base + "_" + std::to_string(suffix);
    return candidate;
}

bool SameBitmapPayload(const BrxBitmapRecord &left, const BrxBitmapRecord &right)
{
    return left.width == right.width && left.height == right.height && left.zoneFlags == right.zoneFlags &&
           left.mipCount == right.mipCount && (left.textureFlags & 0x07u) == (right.textureFlags & 0x07u) &&
           left.textureFormat == right.textureFormat && left.pixels == right.pixels;
}

bool SameTextureStage(const BrxShaderRecord::TextureStage &left, const BrxShaderRecord::TextureStage &right)
{
    return left.oid == right.oid && left.flags == right.flags &&
           left.ambientBitmapIndices == right.ambientBitmapIndices &&
           left.diffuseBitmapIndices == right.diffuseBitmapIndices &&
           left.saturateBitmapIndices == right.saturateBitmapIndices;
}

bool SameShaderPayload(const BrxShaderRecord &left, const BrxShaderRecord &right)
{
    if (left.kind != right.kind || left.flags != right.flags || left.oid != right.oid ||
        left.zoneFlags != right.zoneFlags || left.alternateSaturationOid != right.alternateSaturationOid ||
        left.renderPhase != right.renderPhase || left.textureStageCount != right.textureStageCount ||
        left.animationKind != right.animationKind ||
        !std::equal(std::begin(left.rgba), std::end(left.rgba), std::begin(right.rgba)) ||
        !std::equal(std::begin(left.volumeRgba), std::end(left.volumeRgba), std::begin(right.volumeRgba)) ||
        left.ambientBitmapIndices != right.ambientBitmapIndices ||
        left.diffuseBitmapIndices != right.diffuseBitmapIndices ||
        left.saturateBitmapIndices != right.saturateBitmapIndices ||
        left.textureStages.size() != right.textureStages.size())
        return false;
    const auto &a = left.animation;
    const auto &b = right.animation;
    if (a.kind != b.kind || a.shaderOid != b.shaderOid || a.instanced != b.instanced ||
        a.initialFrame != b.initialFrame || a.otherShaderOid != b.otherShaderOid ||
        a.symmetryCount != b.symmetryCount ||
        !std::equal(std::begin(a.values), std::end(a.values), std::begin(b.values)))
        return false;
    for (std::size_t index = 0; index < left.textureStages.size(); ++index)
        if (!SameTextureStage(left.textureStages[index], right.textureStages[index]))
            return false;
    return true;
}

std::vector<std::size_t> BitmapUseCounts(const BrxDocument &document)
{
    std::vector<std::size_t> counts(document.Bitmaps().size(), 0);
    const auto countChannel = [&](const std::vector<std::int32_t> &channel)
    {
        for (const auto index : channel)
            if (index >= 0 && static_cast<std::size_t>(index) < counts.size())
                ++counts[index];
    };
    for (const auto &shader : document.Shaders())
    {
        if (shader.textureStages.empty())
        {
            // Compatibility with an unsynchronized editor document.  Valid
            // BRX materials use textureStages as the serialized authority.
            countChannel(shader.ambientBitmapIndices);
            countChannel(shader.diffuseBitmapIndices);
            countChannel(shader.saturateBitmapIndices);
        }
        else
        {
            for (const auto &stage : shader.textureStages)
            {
                countChannel(stage.ambientBitmapIndices);
                countChannel(stage.diffuseBitmapIndices);
                countChannel(stage.saturateBitmapIndices);
            }
        }
    }
    for (const auto &font : document.Fonts())
        if (font.bitmapIndex >= 0 && static_cast<std::size_t>(font.bitmapIndex) < counts.size())
            ++counts[font.bitmapIndex];
    return counts;
}

std::vector<std::size_t> ShaderUseCounts(const BrxDocument &document)
{
    std::vector<std::size_t> counts(document.Shaders().size(), 0);
    for (const auto &glob : document.Globs())
    {
        if (!glob.sourceSubglobs.empty())
        {
            for (const auto &subglob : glob.sourceSubglobs)
                if (subglob.shaderIndex < counts.size())
                    ++counts[subglob.shaderIndex];
        }
        else
        {
            // Compatibility fallback for a partially decoded document.  A
            // valid editable GLOB uses sourceSubglobs as its BRX authority;
            // derived batches mirror those assignments and must not count as
            // a second material user.
            for (const auto &batch : glob.derivedBatches)
                if (batch.shaderIndex < counts.size())
                    ++counts[batch.shaderIndex];
        }
    }
    return counts;
}
}

void DrawAssetBrowserPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, AssetDatabase &assets,
                           PrefabLibrary &prefabs, EditorWorkspace &workspace, GLFWwindow *window, EditorCamera &camera,
                           int &gizmoMode, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Browser", &open))
    {
        ImGui::End();
        return;
    }
    renderer.BeginAssetThumbnailFrame();
    static ImGuiTextFilter filter;
    static std::uint64_t selectedAsset = 0;
    static bool selectedObjectOnly = false;
    static char prefabName[96] = "New Prefab";
    static ModelImportOptions modelImportOptions;
    static bool createPhysicalModel = false;
    static bool placeInFrontOfCamera = true;
    static bool importFbxAnimations = true;
    static float fbxSampleRate = 60.0f;
    const auto selectedObject = renderer.SelectedObjectIndex();
    const bool selectedCanOwnModel =
        selectedObject >= 0 && static_cast<std::size_t>(selectedObject) < document.Objects().size() &&
        CidDerivesFrom(document.Objects()[static_cast<std::size_t>(selectedObject)].cid, 2);
    const bool selectedIsLinkedProxyClone =
        selectedCanOwnModel && document.Objects()[static_cast<std::size_t>(selectedObject)].proxyClone;
    const bool selectedCanOwnCollision =
        selectedObject >= 0 && static_cast<std::size_t>(selectedObject) < document.Objects().size() &&
        CidDerivesFrom(document.Objects()[static_cast<std::size_t>(selectedObject)].cid, 3);
    if (ImGui::Button("Import as new model object..."))
    {
        const auto path = ChooseStaticModelFile(window);
        if (!path.empty())
        {
            float placement[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            if (placeInFrontOfCamera)
            {
                const float yaw = glm::radians(camera.Yaw());
                const float pitch = glm::radians(camera.Pitch());
                const glm::vec3 forward(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                                        std::sin(pitch));
                const glm::vec3 position = camera.Position() + forward * 500.0f;
                placement[12] = position.x;
                placement[13] = position.y;
                placement[14] = position.z;
            }
            const auto created =
                EditorDocumentOperations::CreateObject(document, createPhysicalModel ? 3 : 2, -1, placement);
            if (!created.succeeded)
                status = created.message;
            else
            {
                auto &owner = document.EditableObjects()[static_cast<std::size_t>(created.primaryObjectIndex)];
                owner.editorName = UniqueImportedObjectName(document, path);
                const auto result = ImportStaticModel(path, document, created.primaryObjectIndex, modelImportOptions);
                if (!result.succeeded)
                {
                    EditorDocumentOperations::DeleteHierarchy(document, created.primaryObjectIndex);
                    status = result.message;
                }
                else
                {
                    bool ready = true;
                    if (createPhysicalModel)
                    {
                        std::string collisionStatus;
                        if (!BuildModelCollision(document, created.primaryObjectIndex, false, collisionStatus))
                        {
                            EditorDocumentOperations::DeleteHierarchy(document, created.primaryObjectIndex);
                            status = "Model import was cancelled because safe SO collision could not be generated: " +
                                     collisionStatus;
                            workspace.Refresh(document);
                            renderer.Upload(document);
                            ready = false;
                        }
                    }
                    if (ready)
                    {
                        workspace.Refresh(document);
                        renderer.Upload(document);
                        renderer.SelectObject(created.primaryObjectIndex);
                        gizmoMode = 0;
                        renderer.FrameSelection(camera);
                        status = result.message + " Created model object " + owner.editorName + ".";
                    }
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import FBX character rig..."))
    {
        const auto path = ChooseStaticModelFile(window);
        if (!path.empty())
        {
            float placement[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            if (placeInFrontOfCamera)
            {
                const float yaw = glm::radians(camera.Yaw());
                const float pitch = glm::radians(camera.Pitch());
                const glm::vec3 forward(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
                                        std::sin(pitch));
                const glm::vec3 position = camera.Position() + forward * 500.0f;
                placement[12] = position.x;
                placement[13] = position.y;
                placement[14] = position.z;
            }
            const auto created = EditorDocumentOperations::CreateObject(document, 2, -1, placement);
            if (!created.succeeded)
                status = created.message;
            else
            {
                auto &owner = document.EditableObjects()[static_cast<std::size_t>(created.primaryObjectIndex)];
                owner.editorName = UniqueImportedObjectName(document, path);
                FbxCharacterImportOptions characterOptions;
                characterOptions.model = modelImportOptions;
                characterOptions.importAnimations = importFbxAnimations;
                characterOptions.sampleRate = fbxSampleRate;
                const auto imported = ImportFbxCharacter(path, document, created.primaryObjectIndex, characterOptions);
                if (!imported.succeeded)
                {
                    EditorDocumentOperations::DeleteHierarchy(document, created.primaryObjectIndex);
                    status = imported.message;
                }
                else
                {
                    workspace.Refresh(document);
                    renderer.Upload(document);
                    renderer.SelectObject(created.primaryObjectIndex);
                    gizmoMode = 0;
                    renderer.FrameSelection(camera);
                    status = imported.message;
                }
            }
        }
    }
    ImGui::Checkbox("Import FBX animation stacks", &importFbxAnimations);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("ASEG sample Hz", &fbxSampleRate, 1.0f, 1.0f, 240.0f, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("Creates an LBONE skin palette and ASEG clips; gameplay AI remains separate.");
    if (ImGui::Button("Refresh model thumbnails"))
    {
        renderer.ClearModelThumbnailCache();
        status = "Cleared the model thumbnail cache; visible cards will regenerate lazily.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!selectedCanOwnModel || selectedIsLinkedProxyClone);
    if (ImGui::Button("Attach model to selected object..."))
    {
        const auto path = ChooseStaticModelFile(window);
        if (!path.empty())
        {
            const auto result = ImportStaticModel(path, document, selectedObject, modelImportOptions);
            status = result.message;
            if (result.succeeded)
            {
                workspace.Refresh(document);
                renderer.Upload(document);
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selectedCanOwnModel || selectedIsLinkedProxyClone);
    if (ImGui::Button("Replace selected model..."))
    {
        const auto path = ChooseStaticModelFile(window);
        if (!path.empty())
        {
            const auto result = ReplaceStaticModel(path, document, selectedObject, modelImportOptions);
            status = result.message;
            if (result.succeeded)
            {
                workspace.Refresh(document);
                renderer.Upload(document);
            }
        }
    }
    ImGui::EndDisabled();
    if (selectedIsLinkedProxyClone)
    {
        ImGui::TextColored(
            ImVec4(1.0f, .68f, .2f, 1.0f),
            "This model is inherited from a linked PROXY source. Replace the reusable source to update every instance, or make this instance unique before attaching/replacing its model.");
        if (ImGui::Button("Make selected PROXY instance unique for model editing"))
        {
            const auto operation = EditorDocumentOperations::MakeProxyRootUnique(document, selectedObject);
            status = operation.message;
            if (operation.succeeded)
            {
                workspace.Refresh(document);
                renderer.Upload(document);
                renderer.SelectObject(operation.primaryObjectIndex >= 0 ? operation.primaryObjectIndex
                                                                        : selectedObject);
            }
        }
    }
    ImGui::SeparatorText("Import settings");
    ImGui::TextDisabled("Supported: FBX, OBJ, GLB, glTF, DAE, PLY and STL");
    ImGui::SetNextItemWidth(130.0f);
    ImGui::DragFloat("Game scale", &modelImportOptions.scale, 0.1f, 0.0001f, 10000.0f, "%.4g");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("100 converts a meter-authored model to ProjectCane game-space centimeters.");
    ImGui::SameLine();
    ImGui::Checkbox("Flip texture V", &modelImportOptions.flipV);
    ImGui::SameLine();
    ImGui::Checkbox("Import materials", &modelImportOptions.importMaterials);
    if (selectedCanOwnModel)
    {
        if (ImGui::Button("Align selected model to object origin"))
        {
            if (AlignImportedModelToOwner(document, selectedObject, status))
            {
                workspace.Refresh(document);
                renderer.Upload(document);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Repairs an imported model whose mesh was left at world origin while its ALO transform moved elsewhere.");
    }
    ImGui::Checkbox("New object has SO collision/physics", &createPhysicalModel);
    ImGui::SameLine();
    ImGui::Checkbox("Place new object in front of camera", &placeInFrontOfCamera);
    if (!selectedCanOwnModel)
        ImGui::TextDisabled("Attach requires an ALO-derived owner; import-as-new creates one automatically.");
    if (selectedCanOwnCollision && !selectedIsLinkedProxyClone)
    {
        if (ImGui::Button("Generate bounded gameplay collision from selected model"))
        {
            if (BuildModelCollision(document, selectedObject, false, status))
            {
                workspace.Refresh(document);
                // Collision authoring changes only the SO collision payload.
                // Rebuilding render packets here is both unnecessary and
                // destructive for imported/legacy placement state: it can
                // reconstruct the visible model from a different transform
                // layer and make position, rotation, or scale snap back.
                renderer.SelectObject(selectedObject);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Generate bounded camera collision"))
        {
            if (BuildModelCollision(document, selectedObject, true, status))
            {
                workspace.Refresh(document);
                // Camera collision is also independent of render geometry.
                // Preserve the live model placement exactly as authored.
                renderer.SelectObject(selectedObject);
            }
        }
        if (ImGui::Button("Import custom gameplay collision mesh..."))
        {
            const auto path = ChooseStaticModelFile(window);
            if (!path.empty() &&
                ImportModelCollision(path, document, selectedObject, modelImportOptions, false, status))
            {
                workspace.Refresh(document);
                renderer.SelectObject(selectedObject);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Replaces gameplay collision only. Import a simplified, closed, consistently wound convex hull. The visible model is preserved.");
        ImGui::SameLine();
        if (ImGui::Button("Import custom camera collision mesh..."))
        {
            const auto path = ChooseStaticModelFile(window);
            if (!path.empty() && ImportModelCollision(path, document, selectedObject, modelImportOptions, true, status))
            {
                workspace.Refresh(document);
                renderer.SelectObject(selectedObject);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Replaces camera collision only. The imported hull must be closed and convex; render geometry is not changed.");
        ImGui::TextDisabled(
            "Bounded generation makes a safe box. Custom import accepts exact closed convex hulls; split concave collision into multiple fixed SOs.");
    }
    else if (selectedCanOwnCollision && selectedIsLinkedProxyClone)
        ImGui::TextDisabled(
            "Collision is inherited with the linked PROXY model; make the instance unique before regenerating it.");
    filter.Draw("Search assets or object names", -1);
    ImGui::BeginDisabled(selectedObject < 0);
    ImGui::Checkbox("Assets used by selected object", &selectedObjectOnly);
    ImGui::EndDisabled();
    if (selectedObject < 0)
        selectedObjectOnly = false;
    if (selectedObject >= 0 && static_cast<std::size_t>(selectedObject) < document.Objects().size())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Selected: %s", document.ObjectEditorLabel(selectedObject).c_str());
    }
    ImGui::Separator();
    const auto displayNameFor = [&](const EditorAssetRecord &asset)
    {
        std::string name = asset.name;
        if (asset.ownerObjectIndex < 0 || static_cast<std::size_t>(asset.ownerObjectIndex) >= document.Objects().size())
            return name;
        const auto ownerName = document.ObjectEditorLabel(asset.ownerObjectIndex);
        if (asset.kind == EditorAssetKind::Model || asset.kind == EditorAssetKind::Effect)
            return ownerName;
        if (asset.kind == EditorAssetKind::Animation)
            return ownerName + " / " + asset.name;
        if (asset.kind == EditorAssetKind::Collision)
            return ownerName + " Collision";
        return name;
    };
    const auto browserBitmapUses = BitmapUseCounts(document);
    const auto browserShaderUses = ShaderUseCounts(document);
    if (ImGui::BeginTabBar("Asset categories"))
    {
        if (ImGui::BeginTabItem("Object Contents"))
        {
            if (selectedObject < 0)
                ImGui::TextDisabled("Select a world object to inspect all assets it uses.");
            else if (ImGui::BeginTable("SelectedObjectAssets", 4,
                                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Asset");
                ImGui::TableSetupColumn("Usage");
                ImGui::TableSetupColumn("Action");
                ImGui::TableHeadersRow();
                for (const auto &asset : assets.Assets())
                {
                    if (asset.kind == EditorAssetKind::Prefab)
                        continue;
                    const bool used = asset.ownerObjectIndex == selectedObject ||
                                      std::find(asset.objectUsers.begin(), asset.objectUsers.end(), selectedObject) !=
                                          asset.objectUsers.end();
                    if (!used)
                        continue;
                    const auto displayName = displayNameFor(asset);
                    if (!filter.PassFilter(displayName.c_str()))
                        continue;
                    ImGui::PushID(static_cast<int>(asset.stableId));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(KindName(asset.kind));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", displayName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%zu object%s", asset.objectUsers.size(), asset.objectUsers.size() == 1 ? "" : "s");
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::SmallButton("Details"))
                        selectedAsset = asset.stableId;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        const std::array kinds{EditorAssetKind::Model,     EditorAssetKind::Texture, EditorAssetKind::Material,
                               EditorAssetKind::Animation, EditorAssetKind::Effect,  EditorAssetKind::Audio,
                               EditorAssetKind::Collision};
        for (const auto kind : kinds)
        {
            if (!ImGui::BeginTabItem(KindName(kind)))
                continue;
            const auto records = assets.Find(kind);
            const int columns = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / 150.0f));
            if (ImGui::BeginTable("assetTiles", columns))
            {
                for (const auto *asset : records)
                {
                    const bool selectedUsesAsset = asset->ownerObjectIndex == selectedObject ||
                                                   std::find(asset->objectUsers.begin(), asset->objectUsers.end(),
                                                             selectedObject) != asset->objectUsers.end();
                    if (selectedObjectOnly && !selectedUsesAsset)
                        continue;
                    std::string displayName = displayNameFor(*asset);
                    std::string searchable = displayName;
                    for (const auto user : asset->objectUsers)
                        if (user >= 0 && static_cast<std::size_t>(user) < document.Objects().size())
                            searchable += " " + document.ObjectEditorLabel(user);
                    if (!filter.PassFilter(searchable.c_str()))
                        continue;
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(asset->stableId));
                    const ImVec2 origin = ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("thumbnail", ImVec2(132, 82));
                    auto *draw = ImGui::GetWindowDrawList();
                    draw->AddRectFilled(origin, ImVec2(origin.x + 132, origin.y + 82), KindColor(kind), 7);
                    if (kind == EditorAssetKind::Texture)
                    {
                        const auto texture = renderer.TextureHandle(asset->sourceIndex);
                        if (texture != 0)
                            draw->AddImage((ImTextureID)(intptr_t)texture, origin,
                                           ImVec2(origin.x + 132, origin.y + 82), ImVec2(0, 1), ImVec2(1, 0));
                    }
                    else if (kind == EditorAssetKind::Material && asset->sourceIndex >= 0 &&
                             static_cast<std::size_t>(asset->sourceIndex) < document.Shaders().size())
                    {
                        const auto &shader = document.Shaders()[static_cast<std::size_t>(asset->sourceIndex)];
                        const auto bitmap = std::find_if(
                            shader.diffuseBitmapIndices.begin(), shader.diffuseBitmapIndices.end(),
                            [&](const std::int32_t index)
                            { return index >= 0 && static_cast<std::size_t>(index) < document.Bitmaps().size(); });
                        if (bitmap != shader.diffuseBitmapIndices.end())
                        {
                            const auto texture = renderer.TextureHandle(*bitmap);
                            if (texture != 0)
                                draw->AddImage((ImTextureID)(intptr_t)texture, origin,
                                               ImVec2(origin.x + 132, origin.y + 82), ImVec2(0, 1), ImVec2(1, 0),
                                               ImGui::ColorConvertFloat4ToU32(ImVec4(shader.rgba[0], shader.rgba[1],
                                                                                     shader.rgba[2], shader.rgba[3])));
                        }
                        else
                            draw->AddRectFilled(
                                ImVec2(origin.x + 8, origin.y + 25), ImVec2(origin.x + 124, origin.y + 74),
                                ImColor(shader.rgba[0], shader.rgba[1], shader.rgba[2], shader.rgba[3]), 4.0f);
                    }
                    else if (kind == EditorAssetKind::Model)
                    {
                        const auto thumbnail =
                            ImGui::IsItemVisible() && asset->sourceIndex >= 0
                                ? renderer.ModelThumbnail(document, static_cast<std::size_t>(asset->sourceIndex),
                                                          asset->stableId, 160)
                                : 0;
                        if (thumbnail != 0)
                            draw->AddImage((ImTextureID)(intptr_t)thumbnail, origin,
                                           ImVec2(origin.x + 132, origin.y + 82), ImVec2(0, 1), ImVec2(1, 0));
                        else
                        {
                            const ImU32 ink = IM_COL32(225, 238, 255, 230);
                            const ImVec2 a(origin.x + 66, origin.y + 20), b(origin.x + 25, origin.y + 42);
                            const ImVec2 c(origin.x + 66, origin.y + 68), d(origin.x + 107, origin.y + 42);
                            draw->AddLine(a, b, ink, 2.0f);
                            draw->AddLine(b, c, ink, 2.0f);
                            draw->AddLine(c, d, ink, 2.0f);
                            draw->AddLine(d, a, ink, 2.0f);
                            draw->AddLine(b, d, ink, 1.0f);
                            draw->AddLine(a, c, ink, 1.0f);
                        }
                    }
                    draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32_WHITE, KindName(kind));
                    std::string detail;
                    if (asset->width)
                        detail = std::to_string(asset->width) + " x " + std::to_string(asset->height);
                    else if (asset->primitiveCount)
                        detail = std::to_string(asset->primitiveCount) +
                                 (kind == EditorAssetKind::Collision ? " surfaces" : " triangles");
                    if (!detail.empty())
                        draw->AddText(ImVec2(origin.x + 8, origin.y + 56), IM_COL32_WHITE, detail.c_str());
                    if (ImGui::IsItemClicked())
                    {
                        selectedAsset = asset->stableId;
                        if (asset->ownerObjectIndex >= 0)
                            renderer.SelectObject(asset->ownerObjectIndex);
                        else if (!asset->objectUsers.empty())
                            renderer.SelectObject(asset->objectUsers.front());
                    }
                    const bool canDrag = kind == EditorAssetKind::Model || kind == EditorAssetKind::Texture ||
                                         kind == EditorAssetKind::Material;
                    if (canDrag && ImGui::BeginDragDropSource())
                    {
                        const EditorAssetDragPayload payload{asset->stableId, static_cast<std::uint8_t>(kind)};
                        ImGui::SetDragDropPayload("CANE_ASSET", &payload, sizeof(payload));
                        ImGui::Text("Place/apply %s", displayName.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImGui::TextWrapped("%s", displayName.c_str());
                    ImGui::TextDisabled("ID %016llX", static_cast<unsigned long long>(asset->stableId));
                    if (!asset->objectUsers.empty())
                        ImGui::TextDisabled("%zu object user%s", asset->objectUsers.size(),
                                            asset->objectUsers.size() == 1 ? "" : "s");
                    else if ((kind == EditorAssetKind::Texture && asset->sourceIndex >= 0 &&
                              static_cast<std::size_t>(asset->sourceIndex) < browserBitmapUses.size() &&
                              browserBitmapUses[static_cast<std::size_t>(asset->sourceIndex)] == 0) ||
                             (kind == EditorAssetKind::Material && asset->sourceIndex >= 0 &&
                              static_cast<std::size_t>(asset->sourceIndex) < browserShaderUses.size() &&
                              browserShaderUses[static_cast<std::size_t>(asset->sourceIndex)] == 0))
                        ImGui::TextColored(ImVec4(1.0f, .68f, .2f, 1.0f), "Unused resource");
                    if (!canDrag)
                    {
                        if (asset->ownerObjectIndex >= 0 && ImGui::SmallButton("Inspect owner"))
                        {
                            renderer.SelectObject(asset->ownerObjectIndex);
                            status = std::string("Selected owner of ") + displayName + ".";
                        }
                        else if (kind == EditorAssetKind::Audio)
                            ImGui::TextDisabled("Named runtime resource");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dependencies & Cleanup"))
        {
            const auto bitmapUses = BitmapUseCounts(document);
            const auto shaderUses = ShaderUseCounts(document);
            std::vector<std::pair<std::size_t, std::size_t>> duplicateBitmaps;
            std::vector<std::pair<std::size_t, std::size_t>> duplicateShaders;
            for (std::size_t candidate = 0; candidate < document.Bitmaps().size(); ++candidate)
                for (std::size_t canonical = 0; canonical < candidate; ++canonical)
                    if (SameBitmapPayload(document.Bitmaps()[canonical], document.Bitmaps()[candidate]))
                    {
                        duplicateBitmaps.emplace_back(candidate, canonical);
                        break;
                    }
            for (std::size_t candidate = 0; candidate < document.Shaders().size(); ++candidate)
                for (std::size_t canonical = 0; canonical < candidate; ++canonical)
                    if (SameShaderPayload(document.Shaders()[canonical], document.Shaders()[candidate]))
                    {
                        duplicateShaders.emplace_back(candidate, canonical);
                        break;
                    }
            std::vector<std::size_t> unusedBitmaps, unusedShaders;
            for (std::size_t index = 0; index < bitmapUses.size(); ++index)
                if (bitmapUses[index] == 0)
                    unusedBitmaps.push_back(index);
            for (std::size_t index = 0; index < shaderUses.size(); ++index)
                if (shaderUses[index] == 0)
                    unusedShaders.push_back(index);

            ImGui::TextWrapped(
                "Dependencies include material root channels, exact texture-stage channels, font atlases and editable SUBGLOB material assignments. Cleanup uses document remapping, so surviving indices remain valid.");
            ImGui::Text("%zu exact duplicate texture(s) | %zu exact duplicate material(s)", duplicateBitmaps.size(),
                        duplicateShaders.size());
            ImGui::Text("%zu unused texture(s) | %zu unused material(s)", unusedBitmaps.size(), unusedShaders.size());
            bool resourcesChanged = false;
            if (ImGui::CollapsingHeader("Exact duplicates", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextDisabled(
                    "Only byte-identical textures and serialization-identical materials are offered for automatic merging.");
                for (const auto [duplicate, canonical] : duplicateBitmaps)
                {
                    ImGui::PushID(static_cast<int>(duplicate));
                    ImGui::Text("Texture %zu %s", duplicate, document.Bitmaps()[duplicate].editorName.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("matches %zu %s", canonical, document.Bitmaps()[canonical].editorName.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Merge into canonical"))
                    {
                        resourcesChanged =
                            document.DeleteBitmapAndRemap(duplicate, static_cast<std::int32_t>(canonical));
                        if (resourcesChanged)
                            status =
                                "Merged an exact duplicate texture and remapped every material stage and font reference.";
                    }
                    ImGui::PopID();
                    if (resourcesChanged)
                        break;
                }
                if (!resourcesChanged)
                    for (const auto [duplicate, canonical] : duplicateShaders)
                    {
                        ImGui::PushID(100000 + static_cast<int>(duplicate));
                        ImGui::Text("Material %zu %s", duplicate, document.Shaders()[duplicate].editorName.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("matches %zu %s", canonical,
                                            document.Shaders()[canonical].editorName.c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Merge into canonical"))
                        {
                            resourcesChanged =
                                document.DeleteShaderAndRemap(duplicate, static_cast<std::int32_t>(canonical));
                            if (resourcesChanged)
                                status = "Merged an exact duplicate material and remapped all SUBGLOBs.";
                        }
                        ImGui::PopID();
                        if (resourcesChanged)
                            break;
                    }
                if (duplicateBitmaps.empty() && duplicateShaders.empty())
                    ImGui::TextDisabled("No exact duplicate resources found.");
            }
            if (resourcesChanged)
            {
                assets.Rebuild(document);
                assets.SyncPrefabs(prefabs);
                workspace.Refresh(document);
                renderer.Upload(document);
                selectedAsset = 0;
            }

            if (!resourcesChanged && ImGui::CollapsingHeader("Unused resources", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const auto index : unusedBitmaps)
                    ImGui::BulletText("Texture %zu: %s", index, document.Bitmaps()[index].editorName.c_str());
                for (const auto index : unusedShaders)
                    ImGui::BulletText("Material %zu: %s", index, document.Shaders()[index].editorName.c_str());
                if (unusedBitmaps.empty() && unusedShaders.empty())
                    ImGui::TextDisabled("No unused textures or materials found.");
                ImGui::BeginDisabled(unusedBitmaps.empty() && unusedShaders.empty());
                if (ImGui::Button("Clean all unused resources..."))
                    ImGui::OpenPopup("Clean unused assets");
                ImGui::EndDisabled();
                if (ImGui::BeginPopupModal("Clean unused assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextWrapped(
                        "Permanently remove %zu texture(s) and %zu material(s) with no decoded references? This operation is index-safe but cannot be undone outside editor history/project backups.",
                        unusedBitmaps.size(), unusedShaders.size());
                    if (ImGui::Button("Remove unused"))
                    {
                        for (auto iterator = unusedShaders.rbegin(); iterator != unusedShaders.rend(); ++iterator)
                            document.DeleteShaderAndRemap(*iterator, -1);
                        // Removing an unused material can make its formerly
                        // referenced textures unused. Recompute after the
                        // material pass so one cleanup leaves no new orphans.
                        const auto remainingBitmapUses = BitmapUseCounts(document);
                        for (std::size_t index = remainingBitmapUses.size(); index-- > 0;)
                            if (remainingBitmapUses[index] == 0)
                                document.DeleteBitmapAndRemap(index, -1);
                        assets.Rebuild(document);
                        assets.SyncPrefabs(prefabs);
                        workspace.Refresh(document);
                        renderer.Upload(document);
                        selectedAsset = 0;
                        status = "Removed all decoded-unused textures and materials with complete index remapping.";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Characters & Animation"))
        {
            static std::filesystem::path inspectedPackagePath;
            static BrxDocument inspectedPackage;
            static std::string inspectedPackageError;
            static bool packageLoaded = false;
            static bool importUnderSelection = false;

            ImGui::TextWrapped(
                "ProjectCane characters are complete object hierarchies, not standalone meshes. Preview a hierarchy package here before importing its models, skeleton palette, ASEGs, state machines, paths, project volumes and effects together.");
            if (ImGui::Button("Inspect character/hierarchy package..."))
            {
                const auto path = ChooseHierarchyFile(window);
                if (!path.empty())
                {
                    BrxDocument candidate;
                    std::string error;
                    packageLoaded = EditorProject::Load(path, candidate, error);
                    inspectedPackagePath = path;
                    inspectedPackageError = packageLoaded ? std::string{} : error;
                    if (packageLoaded)
                        inspectedPackage = std::move(candidate);
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Import under selected object", &importUnderSelection);
            if (!inspectedPackagePath.empty())
                ImGui::TextWrapped("Package: %s", inspectedPackagePath.string().c_str());
            if (!inspectedPackageError.empty())
                ImGui::TextColored(ImVec4(1.0f, .3f, .25f, 1.0f), "%s", inspectedPackageError.c_str());
            if (packageLoaded)
            {
                std::size_t skinnedSets = 0, bones = 0, unresolvedBones = 0;
                for (const auto &globset : inspectedPackage.Globsets())
                {
                    if (!globset.boneOids.empty())
                        ++skinnedSets;
                    bones += globset.boneOids.size();
                    for (const auto oid : globset.boneOids)
                    {
                        const auto matches = static_cast<std::size_t>(
                            std::count_if(inspectedPackage.Objects().begin(), inspectedPackage.Objects().end(),
                                          [oid](const BrxObjectRecord &object) { return object.oid == oid; }));
                        if (matches != 1)
                            ++unresolvedBones;
                    }
                }
                std::size_t unresolvedChannels = 0;
                for (const auto &aseg : inspectedPackage.AnimationSegments())
                    for (const auto &channel : aseg.channels)
                        if (std::none_of(inspectedPackage.Objects().begin(), inspectedPackage.Objects().end(),
                                         [&](const BrxObjectRecord &object)
                                         { return object.oid == channel.targetOid; }))
                            ++unresolvedChannels;
                ImGui::SeparatorText("Dependency preview");
                ImGui::Text("%zu objects | %zu model groups | %zu collision faces", inspectedPackage.Objects().size(),
                            inspectedPackage.Globs().size(), inspectedPackage.CollisionTriangles().size());
                ImGui::Text("%zu skinned GLOBSETs | %zu bone slots | %zu ASEGs | %zu state machines", skinnedSets,
                            bones, inspectedPackage.AnimationSegments().size(),
                            inspectedPackage.StateMachines().size());
                ImGui::Text("%zu paths | %zu project volumes | %zu effects", inspectedPackage.ShapePaths().size(),
                            inspectedPackage.Volumes().size(), inspectedPackage.Effects().size());
                if (unresolvedBones || unresolvedChannels)
                    ImGui::TextColored(ImVec4(1.0f, .55f, .2f, 1.0f),
                                       "%zu ambiguous/missing bone slot(s), %zu unresolved animation channel(s)",
                                       unresolvedBones, unresolvedChannels);
                else
                    ImGui::TextColored(ImVec4(.35f, 1.0f, .45f, 1.0f),
                                       "Skeleton and ASEG targets resolve inside the package.");

                ImGui::BeginDisabled(importUnderSelection && selectedObject < 0);
                if (ImGui::Button("Import complete hierarchy"))
                {
                    std::int32_t importedRoot = -1;
                    std::string error;
                    if (EditorProject::LoadHierarchy(inspectedPackagePath, document,
                                                     importUnderSelection ? selectedObject : -1, importedRoot, error))
                    {
                        workspace.Refresh(document);
                        renderer.Upload(document);
                        renderer.SelectObject(importedRoot);
                        renderer.FrameSelection(camera);
                        status = "Imported complete character hierarchy and native dependencies.";
                    }
                    else
                        status = "Character hierarchy import failed: " + error;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(importUnderSelection && selectedObject < 0);
                if (ImGui::Button("Import portable PROXY character"))
                {
                    std::int32_t importedRoot = -1;
                    std::string error;
                    if (EditorProject::LoadPortableProxy(inspectedPackagePath, document,
                                                         importUnderSelection ? selectedObject : -1, importedRoot,
                                                         error))
                    {
                        workspace.Refresh(document);
                        renderer.Upload(document);
                        renderer.SelectObject(importedRoot);
                        renderer.FrameSelection(camera);
                        status = "Imported portable PROXY character with its reusable source.";
                    }
                    else
                        status = "Portable character import failed: " + error;
                }
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Selected character skeleton palette");
            if (selectedObject < 0)
                ImGui::TextDisabled("Select a character root or one of its descendants to inspect its bone palettes.");
            else
            {
                const auto isInSelectedHierarchy = [&](std::int32_t objectIndex)
                {
                    for (std::size_t guard = 0;
                         objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < document.Objects().size() &&
                         guard++ <= document.Objects().size();
                         objectIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].parentIndex)
                        if (objectIndex == selectedObject)
                            return true;
                    return false;
                };
                bool foundPalette = false;
                for (std::size_t setIndex = 0; setIndex < document.EditableGlobsets().size(); ++setIndex)
                {
                    auto &globset = document.EditableGlobsets()[setIndex];
                    if (!isInSelectedHierarchy(globset.ownerObjectIndex) || globset.boneOids.empty())
                        continue;
                    foundPalette = true;
                    ImGui::PushID(static_cast<int>(setIndex));
                    const std::string label = "GLOBSET " + std::to_string(setIndex) + " bone mapping";
                    if (ImGui::TreeNode(label.c_str()))
                    {
                        for (std::size_t bone = 0; bone < globset.boneOids.size(); ++bone)
                        {
                            ImGui::PushID(static_cast<int>(bone));
                            auto oid = globset.boneOids[bone];
                            ImGui::SetNextItemWidth(110.0f);
                            if (ImGui::InputScalar("Bone OID", ImGuiDataType_S16, &oid))
                            {
                                globset.boneOids[bone] = oid;
                                workspace.Refresh(document);
                                renderer.Upload(document);
                                status = "Retargeted skeleton palette bone " + std::to_string(bone) + " to OID " +
                                         std::to_string(oid) + ".";
                            }
                            ImGui::SameLine();
                            const auto matches = static_cast<std::size_t>(
                                std::count_if(document.Objects().begin(), document.Objects().end(),
                                              [oid](const BrxObjectRecord &object) { return object.oid == oid; }));
                            if (matches == 1)
                            {
                                const auto target = static_cast<std::int32_t>(std::distance(
                                    document.Objects().begin(),
                                    std::find_if(document.Objects().begin(), document.Objects().end(),
                                                 [oid](const BrxObjectRecord &object) { return object.oid == oid; })));
                                ImGui::TextUnformatted(document.ObjectEditorLabel(target).c_str());
                            }
                            else
                                ImGui::TextColored(ImVec4(1.0f, .45f, .2f, 1.0f),
                                                   matches == 0 ? "Missing target" : "Ambiguous duplicate OID");
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (!foundPalette)
                    ImGui::TextDisabled("No skinned GLOBSET is owned by the selected hierarchy.");

                ImGui::SeparatorText("Native animation assets");
                bool foundAnimation = false;
                for (std::size_t index = 0; index < document.AnimationSegments().size(); ++index)
                {
                    const auto &aseg = document.AnimationSegments()[index];
                    if (!isInSelectedHierarchy(aseg.ownerObjectIndex))
                        continue;
                    foundAnimation = true;
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::Text("ASEG %zu: %zu channels, %zu events, frame 0-%d", index, aseg.channels.size(),
                                aseg.events.size(), aseg.maximumFrame);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Select ASEG"))
                        renderer.SelectObject(aseg.ownerObjectIndex);
                    ImGui::PopID();
                }
                if (!foundAnimation)
                    ImGui::TextDisabled("No ASEG is owned by the selected hierarchy.");
                if (ImGui::Button("Import native ASEG hierarchy..."))
                {
                    const auto path = ChooseHierarchyFile(window);
                    if (!path.empty())
                    {
                        std::int32_t imported = -1;
                        std::string error;
                        if (EditorProject::LoadHierarchy(path, document, selectedObject, imported, error))
                        {
                            workspace.Refresh(document);
                            renderer.Upload(document);
                            renderer.SelectObject(imported);
                            status = "Imported native animation hierarchy under the selected character.";
                        }
                        else
                            status = "Animation hierarchy import failed: " + error;
                    }
                }
                ImGui::TextDisabled(
                    "Use the selected ASEG inspector for keys, events, playback, transition preview and exact ProjectCane timing.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Prefabs"))
        {
            ImGui::InputText("Prefab name", prefabName, sizeof(prefabName));
            ImGui::BeginDisabled(selectedObject < 0);
            if (ImGui::Button("Save selected hierarchy as prefab"))
            {
                std::string error;
                if (prefabs.Capture(document, selectedObject, prefabName, error))
                {
                    assets.SyncPrefabs(prefabs);
                    status = "Saved reusable prefab " + std::string(prefabName) + ".";
                }
                else
                    status = error;
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            const auto linkedPrefab = prefabs.LinkedPrefab(selectedObject);
            if (linkedPrefab != 0)
            {
                ImGui::TextDisabled("Selected root is linked to prefab %016llX",
                                    static_cast<unsigned long long>(linkedPrefab));
                if (ImGui::Button("Make selected prefab instance unique"))
                {
                    prefabs.MakeUnique(selectedObject);
                    status = "Prefab instance is now unique.";
                }
                ImGui::Separator();
            }
            for (const auto *prefabAsset : assets.Find(EditorAssetKind::Prefab))
            {
                const auto *prefab = prefabs.Find(prefabAsset->stableId);
                if (!prefab || !filter.PassFilter(prefabAsset->name.c_str()))
                    continue;
                ImGui::PushID(static_cast<int>(prefabAsset->stableId));
                if (ImGui::Selectable(prefabAsset->name.c_str(), selectedAsset == prefabAsset->stableId))
                    selectedAsset = prefabAsset->stableId;
                if (ImGui::BeginDragDropSource())
                {
                    const EditorAssetDragPayload payload{prefabAsset->stableId,
                                                         static_cast<std::uint8_t>(EditorAssetKind::Prefab)};
                    ImGui::SetDragDropPayload("CANE_ASSET", &payload, sizeof(payload));
                    ImGui::Text("Place prefab %s", prefabAsset->name.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%zu objects | %zu GLOBs | %zu collision faces", prefab->objects.size(),
                                    prefab->globs.size(), prefab->collision.size());
                ImGui::PopID();
            }
            const auto *selectedPrefabAsset = assets.FindById(selectedAsset);
            if (selectedPrefabAsset && selectedPrefabAsset->kind == EditorAssetKind::Prefab)
            {
                ImGui::SeparatorText("Selected prefab");
                static char renamePrefab[96]{};
                static std::uint64_t renameTarget = 0;
                if (renameTarget != selectedAsset)
                {
                    const auto copied = (std::min)(selectedPrefabAsset->name.size(), sizeof(renamePrefab) - 1u);
                    std::copy_n(selectedPrefabAsset->name.data(), copied, renamePrefab);
                    renamePrefab[copied] = '\0';
                    renameTarget = selectedAsset;
                }
                ImGui::InputText("Name", renamePrefab, sizeof(renamePrefab));
                if (ImGui::Button("Rename prefab"))
                {
                    if (prefabs.Rename(selectedAsset, renamePrefab))
                    {
                        assets.SyncPrefabs(prefabs);
                        status = "Renamed prefab.";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete prefab from library"))
                    ImGui::OpenPopup("Delete prefab?");
                if (ImGui::BeginPopupModal("Delete prefab?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextWrapped(
                        "Remove %s from the editor prefab library? Existing placed objects remain in the world and become unique.",
                        selectedPrefabAsset->name.c_str());
                    if (ImGui::Button("Delete"))
                    {
                        prefabs.Remove(selectedAsset);
                        assets.SyncPrefabs(prefabs);
                        selectedAsset = 0;
                        renameTarget = 0;
                        status = "Removed prefab from the library.";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (const auto *selected = assets.FindById(selectedAsset))
    {
        ImGui::SeparatorText("Asset usage");
        ImGui::Text("Used by %zu object(s)", selected->objectUsers.size());
        for (const auto index : selected->objectUsers)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= document.Objects().size())
                continue;
            const auto &object = document.Objects()[static_cast<std::size_t>(index)];
            const std::string label = document.ObjectEditorLabel(index) + "  (" +
                                      BrxDocument::EditorClassName(object.cid) + " OID " + std::to_string(object.oid) +
                                      ")";
            ImGui::PushID(index);
            if (ImGui::SmallButton(label.c_str()))
                renderer.SelectObject(index);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::SeparatorText("Dependency inspection");
        bool changedResource = false;
        if (selected->kind == EditorAssetKind::Texture && selected->sourceIndex >= 0 &&
            static_cast<std::size_t>(selected->sourceIndex) < document.Bitmaps().size())
        {
            const auto bitmapIndex = static_cast<std::size_t>(selected->sourceIndex);
            const auto &bitmap = document.Bitmaps()[bitmapIndex];
            ImGui::Text("Texture %zu | %u x %u | format %u | %zu decoded bytes", bitmapIndex, bitmap.width,
                        bitmap.height, bitmap.textureFormat, bitmap.pixels.size());
            const auto handle = renderer.TextureHandle(static_cast<std::int32_t>(bitmapIndex));
            if (handle != 0)
                ImGui::Image((ImTextureID)(intptr_t)handle, ImVec2(192, 192), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::TextUnformatted("Referenced by:");
            bool dependency = false;
            for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
            {
                const auto &shader = document.Shaders()[shaderIndex];
                const auto contains = [&](const std::vector<std::int32_t> &values)
                { return std::find(values.begin(), values.end(), selected->sourceIndex) != values.end(); };
                bool uses = contains(shader.ambientBitmapIndices) || contains(shader.diffuseBitmapIndices) ||
                            contains(shader.saturateBitmapIndices);
                for (const auto &stage : shader.textureStages)
                    uses = uses || contains(stage.ambientBitmapIndices) || contains(stage.diffuseBitmapIndices) ||
                           contains(stage.saturateBitmapIndices);
                if (uses)
                {
                    ImGui::BulletText("Material %zu: %s", shaderIndex, shader.editorName.c_str());
                    dependency = true;
                }
            }
            for (std::size_t fontIndex = 0; fontIndex < document.Fonts().size(); ++fontIndex)
                if (document.Fonts()[fontIndex].bitmapIndex == selected->sourceIndex)
                {
                    ImGui::BulletText("Font atlas %zu", fontIndex);
                    dependency = true;
                }
            if (!dependency)
                ImGui::TextDisabled("No decoded dependencies.");
            static int textureReplacement = -1;
            textureReplacement = std::clamp(textureReplacement, -1, static_cast<int>(document.Bitmaps().size()) - 1);
            ImGui::InputInt("Replacement texture index", &textureReplacement);
            ImGui::BeginDisabled(textureReplacement == selected->sourceIndex);
            if (ImGui::Button("Replace all references and remove this texture"))
            {
                changedResource = document.DeleteBitmapAndRemap(bitmapIndex, textureReplacement);
                if (changedResource)
                    status = "Replaced every material-stage/font reference and removed the old texture.";
            }
            ImGui::EndDisabled();
        }
        else if (selected->kind == EditorAssetKind::Material && selected->sourceIndex >= 0 &&
                 static_cast<std::size_t>(selected->sourceIndex) < document.Shaders().size())
        {
            const auto shaderIndex = static_cast<std::size_t>(selected->sourceIndex);
            const auto &shader = document.Shaders()[shaderIndex];
            ImGui::ColorButton("Material color", ImVec4(shader.rgba[0], shader.rgba[1], shader.rgba[2], shader.rgba[3]),
                               ImGuiColorEditFlags_AlphaPreview, ImVec2(96, 64));
            ImGui::SameLine();
            ImGui::Text("Material %zu\nType %d | phase %u\n%u serialized texture stage(s)", shaderIndex, shader.kind,
                        shader.renderPhase, shader.textureStageCount);
            std::unordered_set<std::int32_t> textureDependencies;
            const auto collect = [&](const std::vector<std::int32_t> &values)
            {
                for (const auto value : values)
                    if (value >= 0)
                        textureDependencies.insert(value);
            };
            collect(shader.ambientBitmapIndices);
            collect(shader.diffuseBitmapIndices);
            collect(shader.saturateBitmapIndices);
            for (const auto &stage : shader.textureStages)
            {
                collect(stage.ambientBitmapIndices);
                collect(stage.diffuseBitmapIndices);
                collect(stage.saturateBitmapIndices);
            }
            ImGui::Text("Depends on %zu texture(s):", textureDependencies.size());
            for (const auto texture : textureDependencies)
                if (static_cast<std::size_t>(texture) < document.Bitmaps().size())
                    ImGui::BulletText("Texture %d: %s", texture, document.Bitmaps()[texture].editorName.c_str());
            static int materialReplacement = -1;
            materialReplacement = std::clamp(materialReplacement, -1, static_cast<int>(document.Shaders().size()) - 1);
            ImGui::InputInt("Replacement material index", &materialReplacement);
            ImGui::BeginDisabled(materialReplacement == selected->sourceIndex);
            if (ImGui::Button("Replace all SUBGLOB references and remove this material"))
            {
                changedResource = document.DeleteShaderAndRemap(shaderIndex, materialReplacement);
                if (changedResource)
                    status = "Replaced every SUBGLOB reference and removed the old material.";
            }
            ImGui::EndDisabled();
        }
        else if (selected->kind == EditorAssetKind::Model && selected->sourceIndex >= 0 &&
                 static_cast<std::size_t>(selected->sourceIndex) < document.Globsets().size())
        {
            const auto &globset = document.Globsets()[static_cast<std::size_t>(selected->sourceIndex)];
            ImGui::Text("Model group %d | %zu GLOB record(s) | %zu bone palette slot(s)", selected->sourceIndex,
                        globset.globIndices.size(), globset.boneOids.size());
            for (const auto globIndex : globset.globIndices)
                if (globIndex < document.Globs().size())
                {
                    const auto &glob = document.Globs()[globIndex];
                    ImGui::BulletText("GLOB %zu: %zu model part(s), group %d, phase %u%s", globIndex,
                                      glob.sourceSubglobs.size(), glob.renderGroup, glob.renderPhase,
                                      glob.instanced ? " (linked instance)" : "");
                }
            if (selected->ownerObjectIndex >= 0 && ImGui::Button("Preview model in viewport"))
            {
                renderer.SelectObject(selected->ownerObjectIndex);
                renderer.FrameSelection(camera);
                status = "Framed the model owner in the viewport.";
            }
        }
        if (changedResource)
        {
            assets.Rebuild(document);
            assets.SyncPrefabs(prefabs);
            workspace.Refresh(document);
            renderer.Upload(document);
            selectedAsset = 0;
        }
    }
    ImGui::End();
}
