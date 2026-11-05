#include "TextureManagerPanel.h"

#include "BrxDocument.h"
#include "PlatformDialogs.h"
#include "SceneRenderer.h"
#include "TextureImport.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

void DrawTextureManagerPanel(bool &showTextureManager, BrxDocument &document, SceneRenderer &sceneRenderer,
                             GLFWwindow *window, std::string &status)
{
    if (showTextureManager && document.IsOpen())
    {
        ImGui::SetNextWindowSize(ImVec2(860.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Textures & Materials", &showTextureManager))
        {
            static int selectedBitmap = 0;
            static int selectedShader = 0;
            static int pendingDeleteKind = 0; // 1 bitmap, 2 shader
            static int pendingDeleteIndex = -1;
            static int replacementIndex = -1;
            static int importFormat = 0;
            const auto uploadResources = [&]()
            {
                document.RebuildRenderPacketsFromGlobs();
                sceneRenderer.Upload(document);
            };
            if (ImGui::BeginTabBar("ResourceTabs"))
            {
                if (ImGui::BeginTabItem("Bitmaps"))
                {
                    std::vector<std::size_t> bitmapUseCounts(document.Bitmaps().size(), 0);
                    for (const auto &shader : document.Shaders())
                    {
                        const auto countChannel = [&](const std::vector<std::int32_t> &channel)
                        {
                            for (const auto bitmapIndex : channel)
                                if (bitmapIndex >= 0 && static_cast<std::size_t>(bitmapIndex) < bitmapUseCounts.size())
                                    ++bitmapUseCounts[static_cast<std::size_t>(bitmapIndex)];
                        };
                        if (shader.textureStages.empty())
                        {
                            countChannel(shader.ambientBitmapIndices);
                            countChannel(shader.diffuseBitmapIndices);
                            countChannel(shader.saturateBitmapIndices);
                        }
                        else
                            for (const auto &stage : shader.textureStages)
                            {
                                countChannel(stage.ambientBitmapIndices);
                                countChannel(stage.diffuseBitmapIndices);
                                countChannel(stage.saturateBitmapIndices);
                            }
                    }
                    for (const auto &font : document.Fonts())
                        if (font.bitmapIndex >= 0 &&
                            static_cast<std::size_t>(font.bitmapIndex) < bitmapUseCounts.size())
                            ++bitmapUseCounts[static_cast<std::size_t>(font.bitmapIndex)];
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::Combo("Import compression", &importFormat,
                                 "RGBA8 - uncompressed\0BC1 / DXT1 - opaque\0BC3 / DXT5 - alpha\0");
                    ImGui::TextDisabled("Applies to Import image and Replace image. BC compression is lossy.");
                    if (importFormat == 1)
                        ImGui::TextDisabled("BC1: about 1/8 of RGBA8 size; opaque images only.");
                    else if (importFormat == 2)
                        ImGui::TextDisabled("BC3: about 1/4 of RGBA8 size; preserves an alpha channel.");
                    if (ImGui::Button("Import image..."))
                    {
                        const auto imagePath = ChooseTextureFile(window);
                        if (!imagePath.empty())
                        {
                            BrxBitmapRecord bitmap;
                            std::string imageError;
                            if (LoadTextureImage(imagePath, static_cast<TextureImportFormat>(importFormat),
                                                 bitmap, imageError))
                            {
                                selectedBitmap = static_cast<int>(document.AddBitmap(std::move(bitmap)));
                                uploadResources();
                                status = "Imported texture " + imagePath.filename().string() + ".";
                            }
                            else
                                status = imageError;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Duplicate") && selectedBitmap >= 0 &&
                        static_cast<std::size_t>(selectedBitmap) < document.Bitmaps().size())
                    {
                        selectedBitmap =
                            static_cast<int>(document.DuplicateBitmap(static_cast<std::size_t>(selectedBitmap)));
                        uploadResources();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete...") && selectedBitmap >= 0 &&
                        static_cast<std::size_t>(selectedBitmap) < document.Bitmaps().size())
                    {
                        pendingDeleteKind = 1;
                        pendingDeleteIndex = selectedBitmap;
                        replacementIndex = -1;
                        ImGui::OpenPopup("Delete shared resource");
                    }
                    ImGui::Separator();
                    ImGui::BeginChild("BitmapList", ImVec2(270.0f, 0.0f), true);
                    for (std::size_t bitmapIndex = 0; bitmapIndex < document.Bitmaps().size(); ++bitmapIndex)
                    {
                        const auto &bitmap = document.Bitmaps()[bitmapIndex];
                        const std::string label = std::to_string(bitmapIndex) + "  " + bitmap.editorName + "  (" +
                                                  std::to_string(bitmapUseCounts[bitmapIndex]) + " uses)";
                        if (ImGui::Selectable(label.c_str(), selectedBitmap == static_cast<int>(bitmapIndex)))
                            selectedBitmap = static_cast<int>(bitmapIndex);
                    }
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild("BitmapDetails", ImVec2(0.0f, 0.0f), true);
                    if (selectedBitmap >= 0 &&
                        static_cast<std::size_t>(selectedBitmap) < document.EditableBitmaps().size())
                    {
                        auto &bitmap = document.EditableBitmaps()[static_cast<std::size_t>(selectedBitmap)];
                        char name[256]{};
                        std::copy_n(bitmap.editorName.c_str(), (std::min)(bitmap.editorName.size(), sizeof(name) - 1),
                                    name);
                        if (ImGui::InputText("Name", name, sizeof(name)))
                            bitmap.editorName = name;
                        ImGui::Text("Stable editor ID: %llu", static_cast<unsigned long long>(bitmap.editorResourceId));
                        ImGui::Text("%u x %u | mipmaps %u | format %u | %u bytes", bitmap.width, bitmap.height,
                                    bitmap.mipCount, bitmap.textureFormat, bitmap.pixelBytes);
                        int textureFlags = static_cast<int>(bitmap.textureFlags & 0x07u);
                        if (ImGui::InputInt("Retail texture flags", &textureFlags))
                            bitmap.textureFlags = static_cast<std::uint32_t>(std::clamp(textureFlags, 0, 7));
                        const auto editTextureFlag = [&](const char *label, const std::uint32_t mask)
                        {
                            bool enabled = (bitmap.textureFlags & mask) != 0;
                            if (ImGui::Checkbox(label, &enabled))
                                bitmap.textureFlags =
                                    enabled ? bitmap.textureFlags | mask : bitmap.textureFlags & ~mask;
                        };
                        editTextureFlag("Clamp U", 0x01u);
                        editTextureFlag("Clamp V", 0x02u);
                        editTextureFlag("Generate/use mipmaps", 0x04u);
                        if (ImGui::Button("Apply bitmap settings"))
                        {
                            uploadResources();
                            status = "Updated bitmap sampler settings in memory.";
                        }
                        const GLuint texture = sceneRenderer.TextureHandle(selectedBitmap);
                        if (texture != 0)
                            ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(256.0f, 256.0f), ImVec2(0, 1),
                                         ImVec2(1, 0));
                        if (ImGui::Button("Replace image..."))
                        {
                            const auto imagePath = ChooseTextureFile(window);
                            if (!imagePath.empty())
                            {
                                BrxBitmapRecord replacement;
                                std::string imageError;
                                if (LoadTextureImage(imagePath, static_cast<TextureImportFormat>(importFormat),
                                                     replacement, imageError))
                                {
                                    const auto stableId = bitmap.editorResourceId;
                                    const auto contentRevision = bitmap.editorContentRevision;
                                    const auto oldName = bitmap.editorName;
                                    const auto oldZoneFlags = bitmap.zoneFlags;
                                    const auto oldTextureFlags = bitmap.textureFlags;
                                    bitmap = std::move(replacement);
                                    bitmap.editorResourceId = stableId;
                                    bitmap.editorContentRevision = contentRevision + 1u;
                                    bitmap.editorName = oldName;
                                    bitmap.zoneFlags = oldZoneFlags;
                                    bitmap.textureFlags = oldTextureFlags;
                                    uploadResources();
                                    status = "Replaced bitmap image in memory.";
                                }
                                else
                                    status = imageError;
                            }
                        }
                        ImGui::SeparatorText("Used by materials");
                        for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
                        {
                            const auto &shader = document.Shaders()[shaderIndex];
                            const auto contains = [&](const std::vector<std::int32_t> &values)
                            { return std::find(values.begin(), values.end(), selectedBitmap) != values.end(); };
                            bool used = contains(shader.ambientBitmapIndices) ||
                                        contains(shader.diffuseBitmapIndices) || contains(shader.saturateBitmapIndices);
                            for (const auto &stage : shader.textureStages)
                                used = used || contains(stage.ambientBitmapIndices) ||
                                       contains(stage.diffuseBitmapIndices) || contains(stage.saturateBitmapIndices);
                            if (used)
                                ImGui::BulletText("Material %zu: %s", shaderIndex, shader.editorName.c_str());
                        }
                        for (std::size_t fontIndex = 0; fontIndex < document.Fonts().size(); ++fontIndex)
                            if (document.Fonts()[fontIndex].bitmapIndex == selectedBitmap)
                                ImGui::BulletText("Font %zu", fontIndex);
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Materials"))
                {
                    std::vector<std::size_t> shaderUseCounts(document.Shaders().size(), 0);
                    for (const auto &glob : document.Globs())
                        for (const auto &subglob : glob.sourceSubglobs)
                            if (subglob.shaderIndex < shaderUseCounts.size())
                                ++shaderUseCounts[subglob.shaderIndex];
                    if (ImGui::Button("New one-way"))
                    {
                        BrxShaderRecord shader;
                        shader.kind = 1;
                        shader.editorName = "One-way Material";
                        selectedShader = static_cast<int>(document.AddShader(std::move(shader)));
                        uploadResources();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("New three-way"))
                    {
                        BrxShaderRecord shader;
                        shader.kind = 0;
                        shader.editorName = "Three-way Material";
                        selectedShader = static_cast<int>(document.AddShader(std::move(shader)));
                        uploadResources();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Duplicate") && selectedShader >= 0 &&
                        static_cast<std::size_t>(selectedShader) < document.Shaders().size())
                    {
                        selectedShader = static_cast<int>(document.DuplicateShader(selectedShader));
                        uploadResources();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete...") && selectedShader >= 0 &&
                        static_cast<std::size_t>(selectedShader) < document.Shaders().size())
                    {
                        pendingDeleteKind = 2;
                        pendingDeleteIndex = selectedShader;
                        replacementIndex = -1;
                        ImGui::OpenPopup("Delete shared resource");
                    }
                    ImGui::Separator();
                    ImGui::BeginChild("MaterialList", ImVec2(270.0f, 0.0f), true);
                    for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
                    {
                        const auto &shader = document.Shaders()[shaderIndex];
                        const std::string label = std::to_string(shaderIndex) + "  " + shader.editorName + "  (" +
                                                  std::to_string(shaderUseCounts[shaderIndex]) + " uses)";
                        if (ImGui::Selectable(label.c_str(), selectedShader == static_cast<int>(shaderIndex)))
                            selectedShader = static_cast<int>(shaderIndex);
                    }
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild("MaterialDetails", ImVec2(0.0f, 0.0f), true);
                    if (selectedShader >= 0 &&
                        static_cast<std::size_t>(selectedShader) < document.EditableShaders().size())
                    {
                        auto &shader = document.EditableShaders()[static_cast<std::size_t>(selectedShader)];
                        char name[256]{};
                        std::copy_n(shader.editorName.c_str(), (std::min)(shader.editorName.size(), sizeof(name) - 1),
                                    name);
                        if (ImGui::InputText("Name", name, sizeof(name)))
                            shader.editorName = name;
                        ImGui::Text("Stable editor ID: %llu", static_cast<unsigned long long>(shader.editorResourceId));
                        ImGui::InputScalar("Shader OID", ImGuiDataType_S16, &shader.oid);
                        static constexpr const char *shaderKinds[] = {
                            "Three-way",        "One-way / prelit", "Shadow",     "Spotlight",
                            "Projected volume", "Create texture",   "Background", "Foreground",
                            "World map",        "Murk clear",       "Murk fill"};
                        int shaderKind = shader.kind;
                        if (ImGui::Combo("Type", &shaderKind, shaderKinds, IM_ARRAYSIZE(shaderKinds)))
                            shader.kind = static_cast<std::uint8_t>(shaderKind);
                        int shaderFlags = shader.flags;
                        if (ImGui::InputInt("Flags", &shaderFlags))
                            shader.flags = static_cast<std::uint8_t>(std::clamp(shaderFlags, 0, 255));
                        ImGui::ColorEdit4("Material RGBA", shader.rgba,
                                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        ImGui::ColorEdit4("Volume RGBA", shader.volumeRgba,
                                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        ImGui::InputScalar("Zone flags", ImGuiDataType_U32, &shader.zoneFlags, nullptr, nullptr, "%08X",
                                           ImGuiInputTextFlags_CharsHexadecimal);
                        ImGui::InputScalar("Alternate saturation OID", ImGuiDataType_S16,
                                           &shader.alternateSaturationOid);
                        int renderPhase = shader.renderPhase;
                        if (ImGui::InputInt("Render phase", &renderPhase))
                            shader.renderPhase = static_cast<std::uint8_t>(std::clamp(renderPhase, 0, 17));
                        const auto editChannel = [&](const char *label, std::vector<std::int32_t> &channel)
                        {
                            if (!ImGui::TreeNode(label))
                                return;
                            for (std::size_t frame = 0; frame < channel.size(); ++frame)
                            {
                                ImGui::PushID(static_cast<int>(frame));
                                int bitmapIndex = channel[frame];
                                ImGui::SetNextItemWidth(150.0f);
                                if (ImGui::InputInt("##Bitmap", &bitmapIndex))
                                    channel[frame] =
                                        std::clamp(bitmapIndex, -1, static_cast<int>(document.Bitmaps().size()) - 1);
                                ImGui::SameLine();
                                ImGui::Text("Frame %zu", frame);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Remove"))
                                {
                                    channel.erase(channel.begin() + static_cast<std::ptrdiff_t>(frame));
                                    --frame;
                                }
                                ImGui::PopID();
                            }
                            ImGui::BeginDisabled(channel.size() >= (std::numeric_limits<std::uint16_t>::max)());
                            if (ImGui::Button("Add frame"))
                                channel.push_back(document.Bitmaps().empty() ? -1 : 0);
                            ImGui::EndDisabled();
                            if (channel.size() >= (std::numeric_limits<std::uint16_t>::max)())
                                ImGui::TextDisabled("A texture stage is limited to 65535 animation frames.");
                            ImGui::TreePop();
                        };
                        editChannel("Ambient frames", shader.ambientBitmapIndices);
                        editChannel("Diffuse frames", shader.diffuseBitmapIndices);
                        editChannel("Saturate frames", shader.saturateBitmapIndices);
                        const std::size_t primaryFrameCount =
                            (std::max)({shader.ambientBitmapIndices.size(), shader.diffuseBitmapIndices.size(),
                                        shader.saturateBitmapIndices.size()});
                        shader.ambientBitmapIndices.resize(primaryFrameCount, -1);
                        shader.diffuseBitmapIndices.resize(primaryFrameCount, -1);
                        shader.saturateBitmapIndices.resize(primaryFrameCount, -1);
                        if (primaryFrameCount != 0 || !shader.textureStages.empty())
                        {
                            if (shader.textureStages.empty())
                                shader.textureStages.emplace_back();
                            auto &primaryStage = shader.textureStages.front();
                            primaryStage.ambientBitmapIndices = shader.ambientBitmapIndices;
                            primaryStage.diffuseBitmapIndices = shader.diffuseBitmapIndices;
                            primaryStage.saturateBitmapIndices = shader.saturateBitmapIndices;
                        }
                        shader.textureStageCount =
                            static_cast<std::uint8_t>((std::min)(shader.textureStages.size(), std::size_t{255}));
                        if (ImGui::CollapsingHeader("Serialized texture stages"))
                        {
                            int removeStage = -1;
                            for (std::size_t stageIndex = 0; stageIndex < shader.textureStages.size(); ++stageIndex)
                            {
                                auto &stage = shader.textureStages[stageIndex];
                                ImGui::PushID(static_cast<int>(stageIndex));
                                const std::string stageLabel = "Texture stage " + std::to_string(stageIndex);
                                if (ImGui::TreeNode(stageLabel.c_str()))
                                {
                                    ImGui::InputScalar("TEX OID", ImGuiDataType_S16, &stage.oid);
                                    ImGui::InputScalar("TEX flags", ImGuiDataType_S16, &stage.flags);
                                    if (stageIndex == 0)
                                        ImGui::TextDisabled(
                                            "Stage zero frame triplets are edited by the primary frame controls above.");
                                    else
                                    {
                                        editChannel("Ambient frames##stage", stage.ambientBitmapIndices);
                                        editChannel("Diffuse frames##stage", stage.diffuseBitmapIndices);
                                        editChannel("Saturate frames##stage", stage.saturateBitmapIndices);
                                        const std::size_t frameCount = (std::max)({stage.ambientBitmapIndices.size(),
                                                                                   stage.diffuseBitmapIndices.size(),
                                                                                   stage.saturateBitmapIndices.size()});
                                        stage.ambientBitmapIndices.resize(frameCount, -1);
                                        stage.diffuseBitmapIndices.resize(frameCount, -1);
                                        stage.saturateBitmapIndices.resize(frameCount, -1);
                                        if (ImGui::Button("Remove this stage"))
                                            removeStage = static_cast<int>(stageIndex);
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                            if (removeStage > 0)
                                shader.textureStages.erase(shader.textureStages.begin() + removeStage);
                            ImGui::BeginDisabled(shader.textureStages.size() >= 255u);
                            if (ImGui::Button("Add texture stage"))
                                shader.textureStages.emplace_back();
                            ImGui::EndDisabled();
                            if (shader.textureStages.size() >= 255u)
                                ImGui::TextDisabled("A material is limited to 255 serialized texture stages.");
                            shader.textureStageCount = static_cast<std::uint8_t>(shader.textureStages.size());
                        }
                        if (ImGui::CollapsingHeader("Shader animation", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            static constexpr const char *animationKinds[] = {"None",     "Loop",     "Ping-pong",
                                                                             "Shuffle",  "Hologram", "Eyes",
                                                                             "Scroller", "Circler",  "Looker"};
                            auto &animation = shader.animation;
                            int animationKind = static_cast<int>(animation.kind);
                            bool animationChanged = false;
                            if (ImGui::Combo("Animation type", &animationKind, animationKinds,
                                             IM_ARRAYSIZE(animationKinds)))
                            {
                                ResetShaderAnimationPayload(animation,
                                                            static_cast<BrxShaderAnimationKind>(animationKind));
                                if (animation.shaderOid < 0)
                                    animation.shaderOid = shader.oid;
                                animationChanged = true;
                            }
                            ImGui::TextDisabled("Animation targets this material unless an override OID is specified.");
                            animationChanged |=
                                ImGui::InputScalar("Target shader OID", ImGuiDataType_S16, &animation.shaderOid);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Use this material"))
                            {
                                animation.shaderOid = shader.oid;
                                animationChanged = true;
                            }
                            animationChanged |= ImGui::Checkbox("Instanced animation state", &animation.instanced);
                            switch (animation.kind)
                            {
                            case BrxShaderAnimationKind::Loop:
                            case BrxShaderAnimationKind::PingPong:
                                animationChanged |=
                                    ImGui::DragFloatRange2("Cycle duration range", &animation.values[0],
                                                           &animation.values[1], 0.01f, 0.001f, 3600.0f);
                                animationChanged |= ImGui::DragFloatRange2("Pause duration range", &animation.values[2],
                                                                           &animation.values[3], 0.01f, 0.0f, 3600.0f);
                                animationChanged |=
                                    ImGui::InputScalar("Initial frame", ImGuiDataType_S32, &animation.initialFrame);
                                break;
                            case BrxShaderAnimationKind::Shuffle:
                                animationChanged |= ImGui::DragFloatRange2("Shuffle pause range", &animation.values[0],
                                                                           &animation.values[1], 0.01f, 0.0f, 3600.0f);
                                break;
                            case BrxShaderAnimationKind::Hologram:
                                ImGui::TextDisabled(
                                    "ProjectCane currently stores this payload; its hologram render callback is empty.");
                                animationChanged |= ImGui::DragFloat("Angle adjustment", &animation.values[0], 0.01f);
                                animationChanged |=
                                    ImGui::InputScalar("Symmetry count", ImGuiDataType_U32, &animation.symmetryCount);
                                break;
                            case BrxShaderAnimationKind::Eyes:
                                animationChanged |=
                                    ImGui::DragFloat("Blink duration", &animation.values[0], 0.01f, 0.001f, 3600.0f);
                                animationChanged |= ImGui::DragFloatRange2("Open duration range", &animation.values[1],
                                                                           &animation.values[2], 0.01f, 0.0f, 3600.0f);
                                animationChanged |=
                                    ImGui::SliderFloat("Double-blink chance", &animation.values[3], 0.0f, 1.0f);
                                animationChanged |= ImGui::InputScalar("Other shader OID", ImGuiDataType_S16,
                                                                       &animation.otherShaderOid);
                                break;
                            case BrxShaderAnimationKind::Scroller:
                                animationChanged |= ImGui::DragFloat2("UV speed", &animation.values[0], 0.001f);
                                animationChanged |= ImGui::DragFloat2("UV modulus", &animation.values[2], 0.001f);
                                break;
                            case BrxShaderAnimationKind::Circler:
                                animationChanged |= ImGui::DragFloat("Angular speed", &animation.values[0], 0.001f);
                                animationChanged |= ImGui::DragFloat("Radius", &animation.values[1], 0.001f);
                                animationChanged |= ImGui::DragFloat2("UV center", &animation.values[2], 0.001f);
                                break;
                            case BrxShaderAnimationKind::Looker:
                                ImGui::TextDisabled(
                                    "Runtime preview needs the owning ALOX target/focus and rendered triangle context.");
                                animationChanged |= ImGui::DragFloat2("UV center", &animation.values[0], 0.001f);
                                animationChanged |= ImGui::DragFloatRange2("U limits", &animation.values[2],
                                                                           &animation.values[3], 0.001f);
                                animationChanged |= ImGui::DragFloatRange2("V limits", &animation.values[4],
                                                                           &animation.values[5], 0.001f);
                                break;
                            default:
                                break;
                            }
                            if (animationChanged)
                            {
                                shader.animationKind = static_cast<std::int16_t>(animation.kind);
                                animation.modified = true;
                                sceneRenderer.ReloadDerivedRenderPackets(document);
                                status = "Updated material shader animation preview.";
                            }
                        }
                        if (ImGui::Button("Apply material changes"))
                        {
                            sceneRenderer.ReloadDerivedRenderPackets(document);
                            status = "Updated material in memory.";
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Assign to selected SUBGLOB"))
                        {
                            bool assigned = false;
                            for (std::size_t globIndex = 0; globIndex < document.EditableGlobs().size() && !assigned;
                                 ++globIndex)
                            {
                                auto &glob = document.EditableGlobs()[globIndex];
                                if (glob.renderGroup != sceneRenderer.SelectedRenderGroup())
                                    continue;
                                for (std::size_t subglobIndex = 0; subglobIndex < glob.derivedBatches.size();
                                     ++subglobIndex)
                                {
                                    if (glob.derivedBatches[subglobIndex].firstIndex !=
                                        sceneRenderer.SelectedSubglobFirstIndex())
                                        continue;
                                    if (subglobIndex >= glob.sourceSubglobs.size())
                                    {
                                        status = "Selected derived SUBGLOB has no editable source record.";
                                        break;
                                    }
                                    glob.sourceSubglobs[subglobIndex].shaderIndex =
                                        static_cast<std::uint16_t>(selectedShader);
                                    document.CommitGlobEdit(globIndex);
                                    assigned = true;
                                    break;
                                }
                            }
                            if (assigned)
                            {
                                sceneRenderer.ReloadDerivedRenderPackets(document);
                                status = "Assigned material to selected SUBGLOB.";
                            }
                            else
                                status = "Select a SUBGLOB before assigning a material.";
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (ImGui::BeginPopupModal("Delete shared resource", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped(
                    "This resource may be shared. Choose a replacement, or -1 to remove its references.");
                ImGui::InputInt("Replacement index", &replacementIndex);
                const int resourceCount = pendingDeleteKind == 1 ? static_cast<int>(document.Bitmaps().size())
                                                                 : static_cast<int>(document.Shaders().size());
                replacementIndex = std::clamp(replacementIndex, -1, (std::max)(-1, resourceCount - 1));
                if (ImGui::Button("Delete and remap"))
                {
                    const bool deleted = pendingDeleteKind == 1
                                             ? document.DeleteBitmapAndRemap(
                                                   static_cast<std::size_t>(pendingDeleteIndex), replacementIndex)
                                             : document.DeleteShaderAndRemap(
                                                   static_cast<std::size_t>(pendingDeleteIndex), replacementIndex);
                    if (deleted)
                    {
                        selectedBitmap = std::clamp(selectedBitmap, 0,
                                                    (std::max)(0, static_cast<int>(document.Bitmaps().size()) - 1));
                        selectedShader = std::clamp(selectedShader, 0,
                                                    (std::max)(0, static_cast<int>(document.Shaders().size()) - 1));
                        uploadResources();
                        status = "Deleted shared resource and remapped references.";
                        ImGui::CloseCurrentPopup();
                    }
                    else
                        status = "Could not delete resource: choose a different replacement index.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }
}
