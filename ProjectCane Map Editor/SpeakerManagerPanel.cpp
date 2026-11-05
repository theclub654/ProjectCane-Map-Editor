#include "SpeakerManagerPanel.h"

#include "BrxDocument.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr float kRadiansToDegrees = 57.29577951308232f;
constexpr float kDegreesToRadians = 0.0174532925199433f;

void HelpMarker(const char *text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string SpeakerLabel(const BrxObjectRecord &speaker)
{
    if (!speaker.editorName.empty())
        return speaker.editorName + "  (OID " + std::to_string(speaker.oid) + ")";
    return "Speaker " + std::to_string(speaker.oid);
}

bool IsSpeakerProperty(const std::int16_t eopid)
{
    return eopid >= 988 && eopid <= 999;
}

void RestoreRetailSpeakerDefaults(BrxObjectRecord &speaker)
{
    speaker.properties.erase(std::remove_if(speaker.properties.begin(), speaker.properties.end(),
                                            [](const BrxPropertyRecord &property)
                                            { return IsSpeakerProperty(property.eopid); }),
                             speaker.properties.end());
}

BrxPropertyRecord *FindProperty(BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.rbegin(), object.properties.rend(),
                                    [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.rend() ? nullptr : &*found;
}

const BrxPropertyRecord *FindProperty(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.rbegin(), object.properties.rend(),
                                    [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.rend() ? nullptr : &*found;
}

void ApplySpeakerDefault(BrxPropertyRecord &property)
{
    switch (property.eopid)
    {
    case 988:
        property.floatValue[0] = property.floatValue[1] = property.floatValue[2] = property.floatValue[3] = 1.0f;
        break;
    case 989:
        property.floatValue[0] = 0.5f;
        break;
    case 990:
        property.floatValue[0] = 400.0f;
        break;
    case 991:
    case 992:
    case 996:
        property.floatValue[0] = 0.5f;
        break;
    case 995:
        property.floatValue[0] = 0.3f;
        break;
    case 993:
        property.floatValue[0] = 50.0f;
        break;
    case 994:
        property.floatValue[0] = 50.0f;
        property.floatValue[1] = 100.0f;
        property.floatValue[2] = 80.0f;
        break;
    case 997:
        property.intValue = -1;
        break;
    default:
        break;
    }
}

BrxPropertyRecord &EnsureProperty(BrxObjectRecord &object, const std::int16_t eopid)
{
    if (auto *existing = FindProperty(object, eopid))
        return *existing;
    const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
    BrxPropertyRecord property;
    property.eopid = eopid;
    property.kind = definition.kind;
    property.flags = definition.flags;
    property.argumentCount = definition.argumentCount;
    property.editable = true;
    property.modified = true;
    ResetPropertyValue(property);
    ApplySpeakerDefault(property);
    object.properties.push_back(std::move(property));
    return object.properties.back();
}

float Value(const BrxObjectRecord &object, const std::int16_t eopid, const float fallback)
{
    const auto *property = FindProperty(object, eopid);
    return property == nullptr ? fallback : property->floatValue[0];
}

bool IsDescendantOf(const BrxDocument &document, std::int32_t candidate, const std::int32_t ancestor)
{
    for (std::size_t guard = 0; candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                guard++ <= document.Objects().size();)
    {
        if (candidate == ancestor)
            return true;
        const auto &object = document.Objects()[static_cast<std::size_t>(candidate)];
        const auto parent = object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
        if (parent == candidate)
            break;
        candidate = parent;
    }
    return false;
}

std::int32_t FindObject(const BrxDocument &document, const std::int16_t oid, const std::int16_t requiredCid = -1,
                        const std::int32_t context = -1)
{
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &candidate = document.Objects()[index];
        if (candidate.oid != oid)
            continue;
        if (requiredCid >= 0 && candidate.cid != requiredCid && !CidDerivesFrom(candidate.cid, requiredCid))
            continue;
        if (context >= 0 && !IsDescendantOf(document, static_cast<std::int32_t>(index), context))
            continue;
        return static_cast<std::int32_t>(index);
    }
    return -1;
}

std::int32_t FindContextAnimation(const BrxDocument &document, const std::int32_t context, const std::int16_t oid)
{
    for (const auto &segment : document.AnimationSegments())
        if (segment.ownerObjectIndex >= 0 &&
            static_cast<std::size_t>(segment.ownerObjectIndex) < document.Objects().size() &&
            document.Objects()[static_cast<std::size_t>(segment.ownerObjectIndex)].oid == oid &&
            IsDescendantOf(document, segment.ownerObjectIndex, context))
            return segment.ownerObjectIndex;
    return -1;
}

struct SpeakerUse
{
    std::int32_t dialogIndex = -1;
    std::size_t eventIndex = 0;
    bool left = false;
    std::int16_t stagingOid = -1;
};

std::vector<SpeakerUse> FindUses(const BrxDocument &document, const std::int16_t speakerOid)
{
    std::vector<SpeakerUse> uses;
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        if (object.cid != 101 || !object.dialog.present)
            continue;
        for (std::size_t eventIndex = 0; eventIndex < object.dialog.events.size(); ++eventIndex)
        {
            const auto &event = object.dialog.events[eventIndex];
            if ((event.kind == BrxDialogEventKind::SpeakerLeft || event.kind == BrxDialogEventKind::SpeakerRight) &&
                event.primaryOid == speakerOid)
                uses.push_back({static_cast<std::int32_t>(objectIndex), eventIndex,
                                event.kind == BrxDialogEventKind::SpeakerLeft, event.replacementOid});
        }
    }
    return uses;
}

bool DrawFloatProperty(BrxObjectRecord &object, const std::int16_t eopid, const char *label, const float fallback,
                       const float speed, const float minimum, const float maximum)
{
    float value = Value(object, eopid, fallback);
    if (!ImGui::DragFloat(label, &value, speed, minimum, maximum))
        return false;
    auto &property = EnsureProperty(object, eopid);
    property.floatValue[0] = std::clamp(value, minimum, maximum);
    property.modified = true;
    return true;
}

bool DrawVectorProperty(BrxObjectRecord &object, const std::int16_t eopid, const char *label, const float x,
                        const float y, const float z)
{
    float value[3]{x, y, z};
    if (const auto *property = FindProperty(object, eopid))
        std::copy_n(property->floatValue, 3, value);
    if (!ImGui::DragFloat3(label, value, 1.0f))
        return false;
    auto &property = EnsureProperty(object, eopid);
    std::copy_n(value, 3, property.floatValue);
    property.modified = true;
    return true;
}

void DrawComposition(const BrxObjectRecord &speaker, const bool confrontation, const bool rightSide,
                     const char *subtitle)
{
    const float authoredX = Value(speaker, confrontation ? 995 : 991, confrontation ? 0.3f : 0.5f);
    const float authoredY = Value(speaker, confrontation ? 996 : 992, 0.5f);
    // uxTv is local to each TV rectangle; the runtime does not mirror it for
    // the right side. The side changes the destination pane, not the profile.
    const float x = std::clamp(authoredX, 0.0f, 1.0f);
    const float y = std::clamp(authoredY, 0.0f, 1.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size((std::max)(300.0f, ImGui::GetContentRegionAvail().x), 190.0f);
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(11, 17, 27, 255), 6.0f);
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(65, 82, 106, 255), 6.0f);
    draw->AddText(ImVec2(origin.x + 8.0f, origin.y + 7.0f), IM_COL32(150, 175, 205, 255),
                  rightSide ? "RIGHT TV" : "LEFT TV");
    const ImVec2 safeMin(origin.x + size.x * 0.08f, origin.y + size.y * 0.08f);
    const ImVec2 safeMax(origin.x + size.x * 0.92f, origin.y + size.y * 0.82f);
    draw->AddRect(safeMin, safeMax, IM_COL32(75, 125, 170, 150));
    const ImVec2 marker(origin.x + x * size.x, origin.y + y * size.y);
    draw->AddCircleFilled(marker, 22.0f, confrontation ? IM_COL32(245, 145, 75, 220) : IM_COL32(80, 185, 245, 220));
    draw->AddCircle(marker, 27.0f, IM_COL32(235, 245, 255, 255), 0, 2.0f);
    draw->AddLine(ImVec2(marker.x - 35.0f, marker.y), ImVec2(marker.x + 35.0f, marker.y), IM_COL32(235, 245, 255, 150));
    draw->AddLine(ImVec2(marker.x, marker.y - 35.0f), ImVec2(marker.x, marker.y + 35.0f), IM_COL32(235, 245, 255, 150));
    draw->AddRectFilled(ImVec2(origin.x, origin.y + size.y - 38.0f), ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(0, 0, 0, 210));
    float color[4]{1, 1, 1, 1};
    if (const auto *property = FindProperty(speaker, 988))
        std::copy_n(property->floatValue, 4, color);
    draw->AddText(ImVec2(origin.x + 12.0f, origin.y + size.y - 27.0f),
                  ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3])), subtitle);
    ImGui::Dummy(size);
}
}

void DrawSpeakerManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, std::string &status,
                             const SpeakerAnimationPreviewRequest &previewAnimation)
{
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(900.0f, 690.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Speaker Studio", &open))
    {
        ImGui::End();
        return;
    }

    static char search[96]{};
    ImGui::InputTextWithHint("##speakerSearch", "Search speaker name or OID", search, sizeof(search));
    std::vector<std::int32_t> speakers;
    const std::string needle(search);
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.cid != 102)
            continue;
        if (!needle.empty() && std::to_string(object.oid).find(needle) == std::string::npos &&
            object.editorName.find(needle) == std::string::npos)
            continue;
        speakers.push_back(static_cast<std::int32_t>(index));
    }

    ImGui::BeginChild("speakerList", ImVec2(220.0f, 0.0f), true);
    ImGui::TextDisabled("%zu speaker profile(s)", speakers.size());
    for (const auto index : speakers)
    {
        const auto &speaker = document.Objects()[static_cast<std::size_t>(index)];
        const auto uses = FindUses(document, speaker.oid);
        const std::string label = SpeakerLabel(speaker) + "\n" + std::to_string(uses.size()) + " dialog assignment(s)";
        if (ImGui::Selectable(label.c_str(), renderer.SelectedObjectIndex() == index, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, 40.0f)))
        {
            renderer.SelectObject(index);
            status = "Selected SPEAKER OID " + std::to_string(speaker.oid);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("speakerDetail", ImVec2(0.0f, 0.0f), true);

    const auto selected = renderer.SelectedObjectIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= document.Objects().size() ||
        document.Objects()[static_cast<std::size_t>(selected)].cid != 102)
    {
        ImGui::TextDisabled("Select a SPEAKER profile to edit its dialogue presentation.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    auto &speaker = document.EditableObjects()[static_cast<std::size_t>(selected)];
    const bool profileUsesProxyState = speaker.proxyClone || std::any_of(
        document.Proxies().begin(), document.Proxies().end(), [&](const BrxProxyRecord &proxy)
        {
            return std::any_of(proxy.overrides.begin(), proxy.overrides.end(),
                [&](const BrxProxyOverrideRecord &overrideRecord)
                {
                    return overrideRecord.targetObjectIndex == selected &&
                        (overrideRecord.inheritedPropertiesCaptured || std::any_of(
                            overrideRecord.properties.begin(), overrideRecord.properties.end(),
                            [](const BrxPropertyRecord &property) { return IsSpeakerProperty(property.eopid); }));
                });
        });
    const auto uses = FindUses(document, speaker.oid);
    const auto defaultLipsync = FindContextAnimation(document, selected, 1354);
    const bool needsDefaultLipsync =
        std::any_of(document.Objects().begin(), document.Objects().end(),
                    [&](const BrxObjectRecord &candidate)
                    {
                        if (candidate.cid != 101 || !candidate.dialog.present)
                            return false;
                        return std::any_of(candidate.dialog.events.begin(), candidate.dialog.events.end(),
                                           [&](const BrxDialogEvent &event)
                                           {
                                               return event.kind == BrxDialogEventKind::Talk &&
                                                      event.speakerOid == speaker.oid && event.lipsyncAnimationOid < 0;
                                           });
                    });
    static std::vector<BrxPropertyRecord> copiedProfile;
    static bool hasCopiedProfile = false;

    ImGui::TextUnformatted(SpeakerLabel(speaker).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("detached dialogue presentation profile");
    ImGui::TextWrapped(
        "ProjectCane removes this ALO from ordinary world rendering after load and temporarily inserts it into the TV dialogue renderer.");
    if (profileUsesProxyState)
        ImGui::TextWrapped(
            "This profile is controlled by a PROXY. Edit its properties through the Inspector's override controls, "
            "or make the linked hierarchy unique before editing it here.");

    if (ImGui::Button("Copy profile"))
    {
        copiedProfile.clear();
        std::copy_if(speaker.properties.begin(), speaker.properties.end(), std::back_inserter(copiedProfile),
                     [](const BrxPropertyRecord &property) { return IsSpeakerProperty(property.eopid); });
        hasCopiedProfile = true;
        status = "Copied presentation settings from " + SpeakerLabel(speaker);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasCopiedProfile || profileUsesProxyState);
    if (ImGui::Button("Paste profile"))
    {
        RestoreRetailSpeakerDefaults(speaker);
        speaker.properties.insert(speaker.properties.end(), copiedProfile.begin(), copiedProfile.end());
        for (auto &property : speaker.properties)
            if (IsSpeakerProperty(property.eopid))
                property.modified = true;
        status = "Pasted SPEAKER presentation settings";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(profileUsesProxyState);
    if (ImGui::Button("Restore retail defaults"))
    {
        RestoreRetailSpeakerDefaults(speaker);
        status = "Restored ProjectCane SPEAKER defaults";
    }
    ImGui::EndDisabled();
    HelpMarker(
        "Copy and paste transfers only SPEAKER presentation properties 988-999. Restore removes authored overrides so ProjectCane's native constructor defaults are used.");

    if (ImGui::CollapsingHeader("Usage and hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::size_t renderableCount = 0;
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
            if (IsDescendantOf(document, static_cast<std::int32_t>(index), selected) &&
                document.Objects()[index].renderGroupCount > 0)
                ++renderableCount;
        ImGui::Text("Renderable hierarchy nodes: %zu", renderableCount);
        ImGui::Text("Used by %zu DIALOG speaker assignment(s)", uses.size());
        for (const auto &use : uses)
        {
            const auto &dialog = document.Objects()[static_cast<std::size_t>(use.dialogIndex)];
            ImGui::PushID(static_cast<int>(use.dialogIndex * 257 + use.eventIndex));
            if (ImGui::SmallButton("Select"))
                renderer.SelectObject(use.dialogIndex);
            ImGui::SameLine();
            ImGui::Text("DIALOG %d, event %zu, %s TV", dialog.oid, use.eventIndex + 1u, use.left ? "left" : "right");
            if (use.stagingOid >= 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("staging ALO %d", use.stagingOid);
                const auto stagingIndex = FindObject(document, use.stagingOid, 2);
                if (stagingIndex >= 0)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Select staging"))
                        renderer.SelectObject(stagingIndex);
                }
            }
            ImGui::PopID();
        }
        if (uses.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f),
                               "This profile is not assigned by any decoded DIALOG event.");
    }

    ImGui::BeginDisabled(profileUsesProxyState);
    if (ImGui::CollapsingHeader("Subtitle style", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float color[4]{1, 1, 1, 1};
        if (const auto *property = FindProperty(speaker, 988))
            std::copy_n(property->floatValue, 4, color);
        if (ImGui::ColorEdit4("Subtitle color", color,
                              ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
        {
            auto &property = EnsureProperty(speaker, 988);
            std::copy_n(color, 4, property.floatValue);
            property.modified = true;
        }
        HelpMarker(
            "This is the color used by the dialogue subtitle blot while this speaker is talking. It does not recolor the character model.");
        ImGui::TextDisabled(
            "Editor values are normalized 0..1. The PCBRX conversion preserves the engine's GS RGB 0..128 and alpha 0..255 encoding.");
    }

    if (ImGui::CollapsingHeader("Character and animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto *idleProperty = FindProperty(speaker, 997);
        const int idleOid = idleProperty == nullptr ? -1 : idleProperty->intValue;
        const auto idleIndex = FindObject(document, static_cast<std::int16_t>(idleOid), 136, selected);
        const std::string preview =
            idleIndex >= 0 ? "SM OID " + std::to_string(idleOid)
                           : (idleOid < 0 ? "No idle state machine"
                                          : "Missing/incompatible contextual OID " + std::to_string(idleOid));
        if (ImGui::BeginCombo("Idle state machine", preview.c_str()))
        {
            if (ImGui::Selectable("None", idleOid < 0))
            {
                auto &idle = EnsureProperty(speaker, 997);
                idle.intValue = -1;
                idle.modified = true;
            }
            for (std::size_t index = 0; index < document.Objects().size(); ++index)
                if (document.Objects()[index].cid == 136 &&
                    IsDescendantOf(document, static_cast<std::int32_t>(index), selected))
                {
                    const auto oid = document.Objects()[index].oid;
                    const std::string label = "SM OID " + std::to_string(oid) + "##speakerSm" + std::to_string(index);
                    if (ImGui::Selectable(label.c_str(), idleOid == oid))
                    {
                        auto &idle = EnsureProperty(speaker, 997);
                        idle.intValue = oid;
                        idle.modified = true;
                    }
                }
            ImGui::EndCombo();
        }
        HelpMarker(
            "ProjectCane applies this state machine to the portrait character and requests goal OID 844 when the speaker enters a dialogue TV.");
        if (idleIndex >= 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select SM"))
                renderer.SelectObject(idleIndex);
            ImGui::TextDisabled(
                "Runtime applies this SM to the SPEAKER and requests idle goal OID 844 when assigned to a TV.");
        }
        if (defaultLipsync >= 0)
        {
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.55f, 1.0f),
                               "Default lipsync ASEG OID 1354 resolved in speaker context.");
            if (previewAnimation && ImGui::Button("Preview default lipsync"))
                previewAnimation(selected, -1, 1354, false);
        }
        else if (needsDefaultLipsync)
            ImGui::TextColored(
                ImVec4(1.0f, 0.7f, 0.25f, 1.0f),
                "No contextual ASEG OID 1354. Talk events must provide explicit lipsync or remain unanimated.");
        else
            ImGui::TextDisabled(
                "No contextual ASEG OID 1354 is required; current Talk events use explicit lipsync or do not use this SPEAKER.");
    }

    ImGui::EndDisabled();
    static bool confrontation = false;
    static bool rightSide = false;
    if (ImGui::CollapsingHeader("TV composition preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::RadioButton("Standard", !confrontation))
            confrontation = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Confrontation", confrontation))
            confrontation = true;
        ImGui::SameLine();
        ImGui::Checkbox("Right side", &rightSide);
        HelpMarker(
            "Left/right changes the destination TV pane. ProjectCane does not mirror the authored horizontal framing value.");
        DrawComposition(speaker, confrontation, rightSide, "Subtitle appearance preview");
    }

    ImGui::BeginDisabled(profileUsesProxyState);
    if (ImGui::CollapsingHeader("Standard dialogue framing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float fovDegrees = Value(speaker, 989, 0.5f) * kRadiansToDegrees;
        if (ImGui::SliderFloat("Field of view", &fovDegrees, 5.0f, 150.0f, "%.1f deg"))
        {
            auto &property = EnsureProperty(speaker, 989);
            property.floatValue[0] = fovDegrees * kDegreesToRadians;
            property.modified = true;
        }
        DrawFloatProperty(speaker, 990, "Portrait distance", 400.0f, 1.0f, 1.0f, 10000.0f);
        HelpMarker(
            "Distance from the dialogue camera to the speaker portrait. This is not the object's placement in the world.");
        DrawFloatProperty(speaker, 991, "Horizontal framing", 0.5f, 0.005f, 0.0f, 1.0f);
        DrawFloatProperty(speaker, 992, "Vertical framing", 0.5f, 0.005f, 0.0f, 1.0f);
        DrawVectorProperty(speaker, 993, "Portrait light offset", 50.0f, 0.0f, 0.0f);
        HelpMarker("Local offset of the standard-dialogue portrait light relative to the staged speaker.");
    }

    if (ImGui::CollapsingHeader("Confrontation framing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawFloatProperty(speaker, 995, "Horizontal composition", 0.3f, 0.005f, 0.0f, 1.0f);
        DrawFloatProperty(speaker, 996, "Vertical composition", 0.5f, 0.005f, 0.0f, 1.0f);
        DrawVectorProperty(speaker, 998, "Camera offset", 0.0f, 0.0f, 0.0f);
        HelpMarker(
            "Additional camera displacement used by confrontation composition after ProjectCane computes its base portrait camera position.");
        float rollDegrees = Value(speaker, 999, 0.0f) * kRadiansToDegrees;
        if (ImGui::SliderFloat("Camera roll", &rollDegrees, -180.0f, 180.0f, "%.1f deg"))
        {
            auto &property = EnsureProperty(speaker, 999);
            property.floatValue[0] = rollDegrees * kDegreesToRadians;
            property.modified = true;
        }
        DrawVectorProperty(speaker, 994, "Confrontation light offset", 50.0f, 100.0f, 80.0f);
        HelpMarker("Local portrait-light offset used by confrontation dialogue framing.");
        ImGui::TextDisabled(
            "The game positions the camera at portrait distance x 4/9, then adds Camera offset. A DIALOG replacement ALO supplies the world-space staging transform.");
    }

    ImGui::EndDisabled();
    if (ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int issueCount = 0;
        const float fov = Value(speaker, 989, 0.5f);
        const float distance = Value(speaker, 990, 400.0f);
        if (!std::isfinite(fov) || fov <= 0.0f || fov >= 3.1415927f)
        {
            ImGui::BulletText("ERROR: field of view must be between 0 and 180 degrees.");
            ++issueCount;
        }
        if (!std::isfinite(distance) || distance <= 0.0f)
        {
            ImGui::BulletText("ERROR: portrait distance must be positive.");
            ++issueCount;
        }
        const auto *idleProperty = FindProperty(speaker, 997);
        const int idleOid = idleProperty == nullptr ? -1 : idleProperty->intValue;
        const auto idleIndex = FindObject(document, static_cast<std::int16_t>(idleOid), 136, selected);
        if (idleOid >= 0 && idleIndex < 0)
        {
            ImGui::BulletText("ERROR: idle state-machine OID is missing or is not an SM.");
            ++issueCount;
        }
        if (needsDefaultLipsync && defaultLipsync < 0)
        {
            ImGui::BulletText("WARNING: contextual default lipsync ASEG 1354 is missing.");
            ++issueCount;
        }
        if (issueCount == 0)
            ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.55f, 1.0f), "No concrete SPEAKER profile issues found.");
        ImGui::Separator();
        ImGui::TextDisabled("Export readiness");
        ImGui::BulletText("%zu DIALOG assignment(s)", uses.size());
        ImGui::BulletText("Idle state machine: %s",
                          idleOid < 0 ? "not requested" : (idleIndex >= 0 ? "resolved" : "missing/incompatible"));
        ImGui::BulletText("Default lipsync: %s",
                          !needsDefaultLipsync ? "not required" : (defaultLipsync >= 0 ? "resolved" : "missing"));
    }

    ImGui::EndChild();
    ImGui::End();
}
