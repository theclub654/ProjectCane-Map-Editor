#include "SceneWorldPanel.h"

#include "BrxDocument.h"
#include "EditorOptions.h"

#include <imgui.h>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
int WorldCategory(const std::int16_t id)
{
    if (id <= 20)
        return 0;
    if (id >= 526 && id <= 536)
        return 1;
    if (id == 524 || id == 525 || id == 538 || (id >= 549 && id <= 554) || id == 575 || id == 577)
        return 2;
    if (id == 537 || (id >= 542 && id <= 548) || id == 572 || (id >= 580 && id <= 583))
        return 3;
    if ((id >= 539 && id <= 541) || (id >= 568 && id <= 571) || id == 573 || id == 574 || id == 578 || id == 579)
        return 4;
    return 5;
}

BrxOptionRecord *FindWorldProperty(std::vector<BrxOptionRecord> &options, const std::int16_t eopid)
{
    const auto found = std::find_if(options.begin(), options.end(),
                                    [eopid](const BrxOptionRecord &option) { return option.eopid == eopid; });
    return found == options.end() ? nullptr : &*found;
}

BrxOptionRecord &EnsureWorldProperty(std::vector<BrxOptionRecord> &options, const std::int16_t eopid)
{
    if (auto *existing = FindWorldProperty(options, eopid))
        return *existing;
    const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
    BrxOptionRecord property;
    property.eopid = eopid;
    property.kind = definition.kind;
    property.flags = definition.flags;
    property.argumentCount = definition.argumentCount;
    property.modified = true;
    options.push_back(property);
    return options.back();
}

void ApplyPreview(BrxOptionRecord &property, BrxWorldHeader &world, const SceneWorldPreviewCallbacks &preview)
{
    switch (property.eopid)
    {
    case 526:
        world.worldMidtone = property.floatValue[0];
        break;
    case 527:
        world.worldShadow = property.floatValue[0];
        break;
    case 528:
        std::copy_n(property.floatValue, 4, world.skyColor);
        break;
    case 529:
        world.cameraFov = property.floatValue[0];
        break;
    case 530:
        world.cameraNearClip = property.floatValue[0];
        break;
    case 531:
        world.cameraFarClip = property.floatValue[0];
        break;
    case 532:
        world.fogNear = property.floatValue[0];
        break;
    case 533:
        world.fogFar = property.floatValue[0];
        break;
    case 534:
        world.fogMaximum = property.floatValue[0];
        break;
    case 535:
        std::copy_n(property.floatValue, 4, world.fogColor);
        world.fogColor[3] *= 2.0f;
        break;
    case 536:
        world.cameraMrdRatio = property.floatValue[0];
        break;
    default:
        break;
    }
    if (preview.updateWorldAppearance)
        preview.updateWorldAppearance(world.worldMidtone, world.worldShadow, world.skyColor);
    if (preview.updateFog)
        preview.updateFog(world.fogNear, world.fogFar, world.fogMaximum);
    if (preview.updateFogColor)
        preview.updateFogColor(world.fogColor);
    if (preview.updateProjection)
        preview.updateProjection(world.cameraFov, world.cameraNearClip, world.cameraFarClip);
    if (preview.updateMrdRatio)
        preview.updateMrdRatio(world.cameraMrdRatio);
}

bool EditProperty(const std::string &label, BrxOptionRecord &property)
{
    switch (property.kind)
    {
    case BrxPropertyKind::Bool:
        return ImGui::Checkbox(label.c_str(), &property.boolValue);
    case BrxPropertyKind::Float:
        return ImGui::DragFloat(label.c_str(), property.floatValue, 0.01f);
    case BrxPropertyKind::Vector3:
        return ImGui::DragFloat3(label.c_str(), property.floatValue, 0.01f);
    case BrxPropertyKind::Vector4:
        if (property.eopid == 528 || property.eopid == 535)
            return ImGui::ColorEdit4(label.c_str(), property.floatValue,
                                     ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
        return ImGui::DragFloat4(label.c_str(), property.floatValue, 0.01f);
    case BrxPropertyKind::Float2:
        return ImGui::DragFloat2(label.c_str(), property.floatValue, 0.01f);
    case BrxPropertyKind::UInt32:
        return ImGui::InputScalar(label.c_str(), ImGuiDataType_U32, &property.uintValue);
    case BrxPropertyKind::Int32:
    case BrxPropertyKind::Int16:
    {
        const bool changed = ImGui::InputInt(label.c_str(), &property.intValue);
        if (property.kind == BrxPropertyKind::Int16)
            property.intValue = std::clamp(property.intValue, -32768, 32767);
        return changed;
    }
    case BrxPropertyKind::TypedInt16:
        ImGui::Text("%s", label.c_str());
        return ImGui::InputInt("Value", &property.intValue);
    case BrxPropertyKind::Dynamic:
        ImGui::TextDisabled("%s: expression (%u arguments)", label.c_str(), property.argumentCount);
        return false;
    }
    return false;
}

void InitializeWorldDefault(BrxOptionRecord &property, const BrxWorldHeader &world)
{
    switch (property.eopid)
    {
    case 524:
        property.boolValue = true;
        break;
    case 525:
        property.floatValue[0] = 1.0f;
        break;
    case 527:
        property.floatValue[0] = 50.0f / 255.0f;
        break;
    case 528:
        property.floatValue[0] = property.floatValue[1] = 16.0f / 255.0f;
        property.floatValue[2] = 40.0f / 255.0f;
        property.floatValue[3] = 80.0f / 255.0f;
        break;
    case 529:
        property.floatValue[0] = world.cameraFov;
        break;
    case 530:
        property.floatValue[0] = world.cameraNearClip;
        break;
    case 531:
        property.floatValue[0] = world.cameraFarClip;
        break;
    case 532:
        property.floatValue[0] = world.fogNear;
        break;
    case 533:
        property.floatValue[0] = world.fogFar;
        break;
    case 534:
        property.floatValue[0] = 1.0f;
        break;
    case 535:
        std::copy_n(world.fogColor, 4, property.floatValue);
        property.floatValue[3] *= 0.5f;
        break;
    case 536:
        property.floatValue[0] = world.cameraMrdRatio;
        break;
    case 537:
        property.floatValue[0] = 15.0f;
        break;
    case 538:
        property.floatValue[0] = 180.0f;
        break;
    case 572:
        property.boolValue = true;
        break;
    default:
        break;
    }
}
}

void DrawSceneWorldPanel(bool &open, BrxDocument &document, std::string &status,
                         const SceneWorldPreviewCallbacks &preview)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene World", &open))
    {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.25f, 0.70f, 1.0f, 1.0f), "SW / CID 108 / OID WORLD");
    ImGui::TextWrapped("These options control the complete loaded scene world, not an individual object.");
    static ImGuiTextFilter optionFilter;
    static bool showAbsentOptions = false;

    auto &options = document.EditableWorldOptions();
    auto &world = document.EditableWorldHeader();
    const auto removeWorldProperty = [&](const std::int16_t eopid)
    {
        options.erase(std::remove_if(options.begin(), options.end(),
                                     [eopid](const BrxOptionRecord &option) { return option.eopid == eopid; }),
                      options.end());
        switch (eopid)
        {
        case 526:
            world.worldMidtone = 0.0f;
            break;
        case 527:
            world.worldShadow = 50.0f / 255.0f;
            break;
        case 528:
            world.skyColor[0] = world.skyColor[1] = 16.0f / 255.0f;
            world.skyColor[2] = 40.0f / 255.0f;
            world.skyColor[3] = 80.0f / 255.0f;
            break;
        case 529:
            world.cameraFov = 1.0f;
            break;
        case 530:
            world.cameraNearClip = 100.0f;
            break;
        case 531:
            world.cameraFarClip = 20000.0f;
            break;
        case 532:
            world.fogNear = 500.0f;
            break;
        case 533:
            world.fogFar = 20000.0f;
            break;
        case 534:
            world.fogMaximum = 0.5f;
            break;
        case 535:
            std::fill_n(world.fogColor, 4, 0.0f);
            break;
        case 536:
            world.cameraMrdRatio = 1.0f;
            break;
        default:
            break;
        }
        if (preview.updateWorldAppearance)
            preview.updateWorldAppearance(world.worldMidtone, world.worldShadow, world.skyColor);
        if (preview.updateFog)
            preview.updateFog(world.fogNear, world.fogFar, world.fogMaximum);
        if (preview.updateFogColor)
            preview.updateFogColor(world.fogColor);
        if (preview.updateProjection)
            preview.updateProjection(world.cameraFov, world.cameraNearClip, world.cameraFarClip);
        if (preview.updateMrdRatio)
            preview.updateMrdRatio(world.cameraMrdRatio);
    };
    const auto drawWorldOverride = [&](const std::int16_t eopid, const char *friendlyLabel)
    {
        auto *property = FindWorldProperty(options, eopid);
        bool enabled = property != nullptr;
        ImGui::PushID(700000 + eopid);
        if (ImGui::Checkbox("##override", &enabled))
        {
            if (enabled)
            {
                auto &added = EnsureWorldProperty(options, eopid);
                InitializeWorldDefault(added, world);
                property = &added;
                status = std::string("Enabled Scene World override: ") + friendlyLabel + ".";
            }
            else
            {
                removeWorldProperty(eopid);
                property = nullptr;
                status = std::string("Restored game default for ") + friendlyLabel + ".";
            }
        }
        ImGui::SameLine();
        if (!property)
            ImGui::TextDisabled("%s  (game default)", friendlyLabel);
        else
        {
            ImGui::SetNextItemWidth(-1.0f);
            if (EditProperty(friendlyLabel, *property))
            {
                property->modified = true;
                ApplyPreview(*property, world, preview);
                status = std::string("Updated ") + friendlyLabel + ".";
            }
        }
        ImGui::PopID();
    };
    if (ImGui::CollapsingHeader("World systems overview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::size_t rootCount = 0, staticObjectCount = 0, animatedObjectCount = 0;
        std::size_t checkpointCount = 0, warpCount = 0, dialogCount = 0;
        for (const auto &object : document.Objects())
        {
            if (object.parentIndex < 0)
                ++rootCount;
            if (object.cid == 3 || CidDerivesFrom(object.cid, 3))
                ++staticObjectCount;
            if (object.cid == 2 || CidDerivesFrom(object.cid, 2))
                ++animatedObjectCount;
            if (object.cid == 56)
                ++checkpointCount;
            if (object.cid == 116)
                ++warpCount;
            if (object.cid == 101)
                ++dialogCount;
        }
        if (ImGui::BeginTable("world_summary", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            const auto metric = [](const char *name, const std::size_t value)
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", name);
                ImGui::Text("%zu", value);
            };
            metric("Root objects", rootCount);
            metric("ALO/render objects", animatedObjectCount);
            metric("Static/physics objects", staticObjectCount);
            metric("Render GLOBs", document.Globs().size());
            metric("Lights", document.Lights().size());
            metric("PROXY instances", document.Proxies().size());
            metric("Checkpoints", checkpointCount);
            metric("Warps", warpCount);
            metric("Dialogs", dialogCount);
            metric("Collision triangles", document.CollisionTriangles().size());
            metric("VISMAP nodes", document.VisibilityNodes().size());
            metric("Visibility zones", world.visibilityZoneCount);
            ImGui::EndTable();
        }
        ImGui::TextDisabled(
            "SW owns these runtime lists; this summary catches missing world systems before gameplay testing.");
    }
    if (ImGui::CollapsingHeader("Camera & rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float fovDegrees = glm::degrees(world.cameraFov);
        bool projectionChanged = ImGui::DragFloat("Vertical FOV", &fovDegrees, 0.1f, 1.0f, 179.0f, "%.2f deg");
        projectionChanged |= ImGui::DragFloat("Near clip", &world.cameraNearClip, 1.0f, 0.001f, 0.0f, "%.3f");
        projectionChanged |= ImGui::DragFloat("Far clip", &world.cameraFarClip, 10.0f, 0.002f, 0.0f, "%.3f");
        const bool mrdChanged = ImGui::DragFloat("MRD ratio", &world.cameraMrdRatio, 0.01f, 0.75f, 3.0f, "%.4f");
        if (projectionChanged)
        {
            world.cameraFov = glm::radians(std::clamp(fovDegrees, 1.0f, 179.0f));
            for (const auto [id, value] : {std::pair<std::int16_t, float>{529, world.cameraFov},
                                           {530, world.cameraNearClip},
                                           {531, world.cameraFarClip}})
            {
                auto &option = EnsureWorldProperty(options, id);
                option.floatValue[0] = value;
                option.modified = true;
            }
            if (preview.updateProjection)
                preview.updateProjection(world.cameraFov, world.cameraNearClip, world.cameraFarClip);
        }
        if (mrdChanged)
        {
            world.cameraMrdRatio = std::clamp(world.cameraMrdRatio, 0.75f, 3.0f);
            auto &option = EnsureWorldProperty(options, 536);
            option.floatValue[0] = world.cameraMrdRatio;
            option.modified = true;
            if (preview.updateMrdRatio)
                preview.updateMrdRatio(world.cameraMrdRatio);
        }
        if (world.cameraNearClip <= 0.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Near clip must be greater than zero.");
        if (world.cameraFarClip <= world.cameraNearClip)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Far clip must be greater than near clip.");
        if (world.cameraMrdRatio <= 0.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.20f, 1.0f), "MRD ratio should be greater than zero.");
        if (ImGui::Button("Remove camera overrides"))
        {
            world.cameraFov = 1.0f;
            world.cameraNearClip = 100.0f;
            world.cameraFarClip = 20000.0f;
            world.cameraMrdRatio = 1.0f;
            for (const auto id : {std::int16_t{529}, std::int16_t{530}, std::int16_t{531}, std::int16_t{536}})
                removeWorldProperty(id);
            if (preview.updateProjection)
                preview.updateProjection(world.cameraFov, world.cameraNearClip, world.cameraFarClip);
            if (preview.updateMrdRatio)
                preview.updateMrdRatio(world.cameraMrdRatio);
            status = "Removed Scene World camera overrides; engine defaults now apply.";
        }
        for (const auto &object : document.Objects())
        {
            if (object.cid != 109)
                continue;
            const bool overrides = std::any_of(object.properties.begin(), object.properties.end(),
                                               [](const BrxPropertyRecord &property)
                                               {
                                                   return property.eopid == 586 || property.eopid == 589 ||
                                                          property.eopid == 590 || property.eopid == 594;
                                               });
            if (overrides)
                ImGui::TextColored(
                    ImVec4(1.0f, 0.45f, 0.25f, 1.0f),
                    "CM OID %d contains runtime-only camera EOPIDs. They are ignored for authored defaults and should be removed.",
                    object.oid);
        }
    }

    if (ImGui::CollapsingHeader("Lighting, sky & fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped("These values are applied directly to the viewport so the level can be graded in context.");
        drawWorldOverride(526, "World midtone brightness");
        drawWorldOverride(527, "World shadow brightness");
        drawWorldOverride(528, "Sky and clear color");
        ImGui::SeparatorText("Fog volume");
        drawWorldOverride(532, "Fog start distance");
        drawWorldOverride(533, "Fog full-strength distance");
        drawWorldOverride(534, "Maximum fog strength");
        drawWorldOverride(535, "Fog color");
        const auto *fogNear = FindWorldProperty(options, 532);
        const auto *fogFar = FindWorldProperty(options, 533);
        const auto *fogMax = FindWorldProperty(options, 534);
        const float nearValue = fogNear ? fogNear->floatValue[0] : world.fogNear;
        const float farValue = fogFar ? fogFar->floatValue[0] : world.fogFar;
        const float maximum = fogMax ? fogMax->floatValue[0] : world.fogMaximum;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = (std::max)(ImGui::GetContentRegionAvail().x, 160.0f);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + 28.0f), IM_COL32(25, 32, 44, 255), 4.0f);
        if (farValue > nearValue)
        {
            const float scale = width / (std::max)(farValue, 1.0f);
            const float x0 = origin.x + std::clamp(nearValue * scale, 0.0f, width);
            const float x1 = origin.x + std::clamp(farValue * scale, 0.0f, width);
            draw->AddRectFilledMultiColor(
                ImVec2(x0, origin.y), ImVec2(x1, origin.y + 28.0f), IM_COL32(90, 150, 210, 0),
                IM_COL32(90, 150, 210, static_cast<int>(255.0f * std::clamp(maximum, 0.0f, 1.0f))),
                IM_COL32(90, 150, 210, static_cast<int>(255.0f * std::clamp(maximum, 0.0f, 1.0f))),
                IM_COL32(90, 150, 210, 0));
        }
        ImGui::Dummy(ImVec2(width, 28.0f));
        ImGui::TextDisabled("Fog preview: %.1f -> %.1f, maximum %.2f", nearValue, farValue, maximum);
        if (nearValue < 0.0f || farValue <= nearValue)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
                               "Fog distances are invalid: end must be greater than start.");
        if (maximum < 0.0f || maximum > 1.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Maximum fog strength must be between 0 and 1.");
    }

    if (ImGui::CollapsingHeader("World audio & metadata"))
    {
        const auto editFixedAsset = [&](const char *label, std::string &value)
        {
            char buffer[32]{};
            const std::size_t length = (std::min)(value.size(), sizeof(buffer) - 1);
            std::memcpy(buffer, value.data(), length);
            if (ImGui::InputText(label, buffer, sizeof(buffer)))
            {
                value = buffer;
                status = std::string("Updated ") + label + " in memory.";
            }
        };
        editFixedAsset("Sound bank", world.soundBankFile);
        editFixedAsset("Music file", world.musicFile);
        editFixedAsset("Commentary audio", world.commentaryAudioFile);
        char speakers[512]{};
        const std::size_t length = (std::min)(world.commentarySpeakers.size(), sizeof(speakers) - 1);
        std::memcpy(speakers, world.commentarySpeakers.data(), length);
        if (ImGui::InputText("Commentary speakers", speakers, sizeof(speakers)))
            world.commentarySpeakers = speakers;
        ImGui::TextDisabled("Asset names are fixed 32-byte BRX fields (31 visible characters maximum).");
        ImGui::SeparatorText("Music and commentary");
        drawWorldOverride(524, "Enable background music");
        drawWorldOverride(525, "Music volume");
        drawWorldOverride(538, "Commentary / Master Thief Sprint duration");
        if (auto *volume = FindWorldProperty(options, 525);
            volume && (volume->floatValue[0] < 0.0f || volume->floatValue[0] > 1.0f))
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Music volume must be between 0 and 1.");
        if (auto *duration = FindWorldProperty(options, 538); duration && duration->floatValue[0] < 0.0f)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Commentary duration cannot be negative.");

        if (ImGui::TreeNode("Intermittent ambient sounds"))
        {
            ImGui::TextWrapped(
                "Each sound record starts with a sound effect; optional volume and repeat ranges configure the most recently added sound. The engine supports at most sixteen.");
            static float ambientPreviewTime = 0.0f;
            ImGui::SliderFloat("Schedule preview time", &ambientPreviewTime, 0.0f, 120.0f, "%.1f s");
            std::vector<std::size_t> soundStarts;
            for (std::size_t index = 0; index < options.size(); ++index)
                if (options[index].eopid == 549)
                    soundStarts.push_back(index);
            for (std::size_t soundIndex = 0; soundIndex < soundStarts.size(); ++soundIndex)
            {
                const std::size_t begin = soundStarts[soundIndex];
                const std::size_t end =
                    soundIndex + 1 < soundStarts.size() ? soundStarts[soundIndex + 1] : options.size();
                ImGui::PushID(810000 + static_cast<int>(soundIndex));
                const std::string heading = "Ambient sound " + std::to_string(soundIndex + 1);
                if (ImGui::TreeNode(heading.c_str()))
                {
                    auto &sound = options[begin];
                    if (EditProperty("Sound effect ID", sound))
                        sound.modified = true;
                    BrxOptionRecord *volume = nullptr;
                    BrxOptionRecord *repeat = nullptr;
                    for (std::size_t index = begin + 1; index < end; ++index)
                    {
                        if (options[index].eopid == 550 && !volume)
                            volume = &options[index];
                        if (options[index].eopid == 551 && !repeat)
                            repeat = &options[index];
                    }
                    if (volume)
                    {
                        if (ImGui::DragFloat2("Audible volume percent range", volume->floatValue, 0.5f, 0.0f, 100.0f))
                            volume->modified = true;
                        ImGui::TextDisabled("Converted by the game into the intermittent sound distance range.");
                    }
                    else
                        ImGui::TextDisabled("Volume range: engine default");
                    if (repeat)
                    {
                        if (ImGui::DragFloat2("Repeat delay range", repeat->floatValue, 0.1f, 0.0f, 3600.0f))
                            repeat->modified = true;
                        const float averageDelay =
                            (std::max)(0.01f, (repeat->floatValue[0] + repeat->floatValue[1]) * 0.5f);
                        const float phase = std::fmod(ambientPreviewTime, averageDelay);
                        ImGui::TextColored(phase < 0.5f ? ImVec4(0.3f, 0.95f, 0.55f, 1.0f)
                                                        : ImVec4(0.55f, 0.6f, 0.7f, 1.0f),
                                           phase < 0.5f ? "Preview: trigger" : "Preview: waiting (%.1fs)",
                                           (std::max)(0.0f, averageDelay - phase));
                    }
                    else
                        ImGui::TextDisabled("Repeat range: engine default");
                    if (ImGui::Button("Delete ambient sound"))
                    {
                        std::vector<std::size_t> eraseIndices{begin};
                        for (std::size_t index = begin + 1; index < end; ++index)
                            if (options[index].eopid == 550 || options[index].eopid == 551)
                                eraseIndices.push_back(index);
                        for (auto erase = eraseIndices.rbegin(); erase != eraseIndices.rend(); ++erase)
                            options.erase(options.begin() + static_cast<std::ptrdiff_t>(*erase));
                        status = "Removed intermittent ambient sound.";
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (soundStarts.size() < 16 && ImGui::Button("Add ambient sound"))
            {
                const auto makeOption = [&](const std::int16_t id)
                {
                    const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(id)];
                    BrxOptionRecord added;
                    added.eopid = id;
                    added.kind = definition.kind;
                    added.flags = definition.flags;
                    added.argumentCount = definition.argumentCount;
                    added.modified = true;
                    return added;
                };
                auto sound = makeOption(549);
                sound.intValue = -1;
                options.push_back(sound);
                auto volume = makeOption(550);
                volume.floatValue[0] = 0.0f;
                volume.floatValue[1] = 100.0f;
                options.push_back(volume);
                auto repeat = makeOption(551);
                repeat.floatValue[0] = 5.0f;
                repeat.floatValue[1] = 10.0f;
                options.push_back(repeat);
                status = "Added intermittent ambient sound.";
            }
            if (soundStarts.size() >= 16)
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                                   "The runtime limit of sixteen intermittent sounds has been reached.");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Default reverb"))
        {
            auto *reverb = FindWorldProperty(options, 552);
            bool enabled = reverb != nullptr;
            if (ImGui::Checkbox("Override world reverb", &enabled))
            {
                if (enabled)
                {
                    BrxOptionRecord added;
                    const auto &definition = EditorOptionDefinitions()[552];
                    added.eopid = 552;
                    added.kind = BrxPropertyKind::Dynamic;
                    added.flags = definition.flags;
                    added.argumentCount = 2;
                    added.arguments.resize(2);
                    added.modified = true;
                    added.arguments[0].type = 5;
                    added.arguments[1].type = 5;
                    options.push_back(std::move(added));
                    reverb = &options.back();
                }
                else
                {
                    removeWorldProperty(552);
                    reverb = nullptr;
                }
            }
            if (reverb && reverb->arguments.size() == 2)
            {
                static const char *reverbNames[]{"None", "Studio A", "Studio B", "Studio C", "Pipe", "Room"};
                int kind = std::clamp(reverb->arguments[0].intValue, 0, 5);
                if (ImGui::Combo("Reverb space", &kind, reverbNames, 6))
                {
                    reverb->arguments[0].intValue = kind;
                    reverb->modified = true;
                }
                if (ImGui::SliderInt("Reverb depth", &reverb->arguments[1].intValue, 0, 127))
                    reverb->modified = true;
            }
            else if (reverb)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f),
                                   "Malformed reverb record: expected type and depth arguments.");
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Gameplay economy & rewards", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawWorldOverride(537, "Delay before world gold becomes active");
        drawWorldOverride(572, "Enable dynamic prize spawning");
        ImGui::SeparatorText("Adaptive coin drops");
        drawWorldOverride(542, "Rich-player coin minimum");
        drawWorldOverride(543, "Rich-player coin maximum");
        drawWorldOverride(544, "Poor-player coin minimum");
        drawWorldOverride(545, "Poor-player coin maximum");
        const auto integer = [&](const std::int16_t id, const int fallback)
        {
            const auto *property = FindWorldProperty(options, id);
            return property ? property->intValue : fallback;
        };
        const int richMin = integer(542, 0), richMax = integer(543, richMin);
        const int poorMin = integer(544, 0), poorMax = integer(545, poorMin);
        static float playerDifficultyPreview = 0.5f;
        ImGui::SliderFloat("Player performance preview", &playerDifficultyPreview, 0.0f, 1.0f,
                           playerDifficultyPreview < 0.35f   ? "Struggling"
                           : playerDifficultyPreview > 0.65f ? "Doing well"
                                                             : "Average");
        const float expectedMin = poorMin + (richMin - poorMin) * playerDifficultyPreview;
        const float expectedMax = poorMax + (richMax - poorMax) * playerDifficultyPreview;
        ImGui::Text("Expected random coin range: %.1f to %.1f", expectedMin, expectedMax);
        if (richMin > richMax)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Rich-player minimum exceeds maximum.");
        if (poorMin > poorMax)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Poor-player minimum exceeds maximum.");
        if (richMin < 0 || richMax < 0 || poorMin < 0 || poorMax < 0)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Coin counts cannot be negative.");

        if (ImGui::TreeNode("Initial vault locks"))
        {
            ImGui::TextWrapped(
                "These SetUser commands establish per-vault lock state when the world loads. Vault indices wrap to 0-31 in the game.");
            for (std::size_t index = 0; index < options.size(); ++index)
            {
                auto &property = options[index];
                if (property.eopid != 580 && property.eopid != 581)
                    continue;
                ImGui::PushID(830000 + static_cast<int>(index));
                const bool locked = property.eopid == 580;
                if (property.arguments.size() == 1)
                {
                    int vault = std::clamp(property.arguments[0].intValue, 0, 31);
                    if (ImGui::InputInt(locked ? "Lock vault" : "Unlock vault", &vault))
                    {
                        property.arguments[0].intValue = std::clamp(vault, 0, 31);
                        property.modified = true;
                    }
                }
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Malformed vault-state command.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    options.erase(options.begin() + static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            const auto addVaultState = [&](const std::int16_t eopid)
            {
                const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
                BrxOptionRecord property;
                property.eopid = eopid;
                property.kind = BrxPropertyKind::Dynamic;
                property.flags = definition.flags;
                property.argumentCount = 1;
                property.arguments.resize(1);
                property.arguments[0].type = 5;
                property.arguments[0].intValue = 0;
                property.modified = true;
                options.push_back(std::move(property));
            };
            if (ImGui::Button("Add locked vault"))
                addVaultState(580);
            ImGui::SameLine();
            if (ImGui::Button("Add unlocked vault"))
                addVaultState(581);
            ImGui::TreePop();
        }
    }

    ImGui::SeparatorText("Advanced Scene World data");
    ImGui::TextDisabled("Raw EOPIDs are for diagnostics. Only LoadOptionsFromBrx SetUser records can be changed.");
    static bool showAdvancedWorldOptions = false;
    ImGui::Checkbox("Show advanced EOPID view", &showAdvancedWorldOptions);
    if (showAdvancedWorldOptions)
    {
        optionFilter.Draw("Search Scene World EOPIDs", -1.0f);
        ImGui::Checkbox("Show authorable options not serialized in this level", &showAbsentOptions);
        static constexpr const char *categoryNames[] = {"Inherited LO", "World rendering",
                                                        "Audio",        "Gameplay & difficulty",
                                                        "Presentation", "Progression & runtime"};
        std::size_t removeOption = std::numeric_limits<std::size_t>::max();
        for (int category = 0; category < IM_ARRAYSIZE(categoryNames); ++category)
        {
            if (!ImGui::CollapsingHeader(categoryNames[category], category == 1 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                continue;
            bool any = false;
            for (std::size_t index = 0; index < options.size(); ++index)
            {
                auto &property = options[index];
                if (WorldCategory(property.eopid) != category)
                    continue;
                const char *knownName = PropertyName(property.eopid);
                const std::string label = (knownName ? std::string(knownName) : "Unknown") + " (EOPID " +
                                          std::to_string(property.eopid) + ")";
                if (!optionFilter.PassFilter(label.c_str()))
                    continue;
                any = true;
                ImGui::PushID(500000 + static_cast<int>(index));
                const bool writable = property.editable && IsEditorWritableOption(property.eopid);
                ImGui::BeginDisabled(!writable);
                const bool changed = EditProperty(label, property);
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (!writable)
                    ImGui::TextDisabled("%s", OptionBehaviorName(OptionBehavior(property.eopid)));
                else if (ImGui::SmallButton("Use default"))
                    removeOption = index;
                if (changed)
                {
                    property.modified = true;
                    ApplyPreview(property, world, preview);
                    status = "Updated Scene World EOPID " + std::to_string(property.eopid) + ".";
                }
                ImGui::PopID();
            }
            if (showAbsentOptions)
            {
                for (const std::int16_t optionId : CidOptionIds()[108])
                {
                    if (WorldCategory(optionId) != category || FindWorldProperty(options, optionId))
                        continue;
                    if (!IsEditorWritableOption(optionId))
                        continue;
                    const char *knownName = PropertyName(optionId);
                    const std::string label =
                        (knownName ? std::string(knownName) : "Unknown") + " (EOPID " + std::to_string(optionId) + ")";
                    if (!optionFilter.PassFilter(label.c_str()))
                        continue;
                    any = true;
                    const bool writable = IsEditorWritableOption(optionId);
                    ImGui::PushID(600000 + optionId);
                    ImGui::BeginDisabled(!writable);
                    if (ImGui::SmallButton("Add"))
                    {
                        auto &property = EnsureWorldProperty(options, optionId);
                        InitializeWorldDefault(property, world);
                        status = "Added Scene World EOPID " + std::to_string(optionId) + " in memory.";
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::Text("%s", label.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", OptionBehaviorName(OptionBehavior(optionId)));
                    ImGui::PopID();
                }
            }
            if (!any)
                ImGui::TextDisabled("No matching Scene World options.");
        }
        if (removeOption != std::numeric_limits<std::size_t>::max())
        {
            const auto removedId = options[removeOption].eopid;
            if (removedId >= 526 && removedId <= 536)
                removeWorldProperty(removedId);
            else
                options.erase(options.begin() + static_cast<std::ptrdiff_t>(removeOption));
            status = "Removed Scene World EOPID " + std::to_string(removedId) + "; the game default will apply.";
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Changes remain in memory until map saving is implemented.");
    ImGui::End();
}
