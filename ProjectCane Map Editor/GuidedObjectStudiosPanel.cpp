#include "GuidedObjectStudiosPanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
enum class StudioKind : std::uint8_t
{
    Breakable,
    Security,
    Mechanism,
    Pickup,
    Weapon,
    Water,
    Shadow
};

struct StudioInfo
{
    const char *title;
    const char *subtitle;
    const char *emptyText;
};

StudioInfo Info(const StudioKind kind)
{
    switch (kind)
    {
    case StudioKind::Breakable:
        return {"Breakable Studio", "BRK / BREAK / ALBRK / SWP / FRAGILE / ZAPBREAK / BRKP",
                "Select a breakable object or one of its controller/piece objects."};
    case StudioKind::Security:
        return {"Security Network", "ALARM / SENSOR / LASEN / CAMSEN / PRSEN / BARRIER / TZP / VOLZP",
                "Select a security detector, alarm, barrier or zap volume."};
    case StudioKind::Mechanism:
        return {"Moving Platform & Mechanism Studio", "STEP / BUTTON / VOLBTN / BARRIER / CNVO / HBSK",
                "Select a moving platform, switch, barrier or conveyor mechanism."};
    case StudioKind::Pickup:
        return {"Pickup & Progression Studio",
                "SPRIZE / SCPRIZE / CLUE / CHKPNT / LIFE / COIN / KEY / GOLD / BOOST / LOCK / LOCKG / WARP / EXIT",
                "Select a pickup, lock, checkpoint, warp or level exit."};
    case StudioKind::Weapon:
        return {"Weapon & Hazard Studio",
                "DARTGUN / BOMB / MISSILE family / MINE / DART / JLO / PUFFT / DECOY / EXPLOSION / RWM",
                "Select a weapon, projectile, mine, launcher, decoy or explosion."};
    case StudioKind::Water:
        return {"Water Surface Studio", "WATER rendering, collision and world placement", "Select a WATER object."};
    case StudioKind::Shadow:
        return {"Shadow Studio", "Fast/contact, static projected, and DYSH dynamic shadows",
                "Select a shadow caster or DYSH helper."};
    }
    return {"Object Studio", "", "Select an object."};
}

bool Fits(const StudioKind kind, const std::int16_t cid)
{
    switch (kind)
    {
    // DARTGUN (35) is semantically a weapon, but ProjectCane loads its
    // additional payload through LoadBrkFromBrx.  Keep it in both studios.
    case StudioKind::Breakable:
        return cid >= 32 && cid <= 39;
    case StudioKind::Security:
        return (cid >= 47 && cid <= 52) || cid == 54 || cid == 55;
    case StudioKind::Mechanism:
        return cid == 6 || cid == 40 || cid == 41 || cid == 52 || cid == 57 || cid == 58;
    case StudioKind::Pickup:
        return (cid >= 44 && cid <= 46) || cid == 56 || (cid >= 87 && cid <= 93) || cid == 116 || cid == 125;
    case StudioKind::Weapon:
        return cid == 35 || (cid >= 59 && cid <= 63) || cid == 70 || cid == 71 || cid == 72 || cid == 76 ||
               cid == 119 || cid == 140;
    case StudioKind::Water:
        return cid == 31;
    case StudioKind::Shadow:
        return cid == 97;
    }
    return false;
}

bool IsShadowObject(const BrxObjectRecord &object)
{
    if (object.cid == 97)
        return true;
    return std::any_of(object.properties.begin(), object.properties.end(),
                       [](const BrxPropertyRecord &property) { return property.eopid >= 41 && property.eopid <= 51; });
}

bool FitsObject(const StudioKind kind, const BrxObjectRecord &object)
{
    return kind == StudioKind::Shadow ? IsShadowObject(object) : Fits(kind, object.cid);
}

BrxPropertyRecord *FindProperty(BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.end() ? nullptr : &*found;
}

const BrxPropertyRecord *FindProperty(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.end() ? nullptr : &*found;
}

BrxPropertyRecord &EnsureProperty(BrxObjectRecord &object, const std::int16_t eopid)
{
    if (auto *existing = FindProperty(object, eopid))
        return *existing;
    BrxPropertyRecord property;
    property.eopid = eopid;
    property.kind = EditorOptionDefinitionForCid(object.cid, eopid).kind;
    property.editable = true;
    property.modified = true;
    ResetPropertyValue(property);
    object.properties.push_back(std::move(property));
    return object.properties.back();
}

void RefreshShadows(BrxDocument &document, SceneRenderer &renderer, std::string &status, const char *message)
{
    renderer.RefreshShadowPreviews(document);
    status = message;
}

std::int32_t RuntimeParent(const BrxObjectRecord &object)
{
    return object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
}

bool IsDescendant(const BrxDocument &document, std::int32_t candidate, const std::int32_t root)
{
    for (std::size_t guard = 0; candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                guard++ <= document.Objects().size();)
    {
        if (candidate == root)
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

std::string Label(const BrxObjectRecord &object)
{
    const std::string runtime = std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid);
    return object.editorName.empty() ? runtime : object.editorName + "  (" + runtime + ")";
}

std::string PropertyLabel(const std::int16_t cid, const std::int16_t eopid)
{
    const char *name = PropertyNameForCid(cid, eopid);
    return name != nullptr && name[0] != '\0' ? name : "EOPID " + std::to_string(eopid);
}

bool RequiresModel(const StudioKind kind, const std::int16_t cid)
{
    switch (kind)
    {
    case StudioKind::Breakable:
        return cid != 33; // BREAK is a controller and may own no geometry.
    case StudioKind::Security:
        return false;
    case StudioKind::Mechanism:
        return cid != 41 && cid != 52; // VOLBTN/BARRIER may be volume/controller-only.
    case StudioKind::Pickup:
        return cid == 44 || cid == 45 || cid == 46 || (cid >= 87 && cid <= 91) || cid == 94;
    case StudioKind::Weapon:
        return cid != 119 && cid != 140; // EXPL and RWM are effect/manager objects.
    case StudioKind::Water:
        return true;
    case StudioKind::Shadow:
        return false;
    }
    return false;
}

bool RequiresCollision(const StudioKind kind, const std::int16_t cid)
{
    switch (kind)
    {
    case StudioKind::Breakable:
        return cid != 33;
    case StudioKind::Mechanism:
        return cid == 6 || cid == 40 || cid == 41 || cid == 57 || cid == 58;
    case StudioKind::Water:
        return true;
    default:
        return false;
    }
}

std::string ValueSummary(const BrxPropertyRecord &property)
{
    switch (property.kind)
    {
    case BrxPropertyKind::Bool:
        return property.boolValue ? "On" : "Off";
    case BrxPropertyKind::Float:
        return std::to_string(property.floatValue[0]);
    case BrxPropertyKind::Vector3:
        return "(" + std::to_string(property.floatValue[0]) + ", " + std::to_string(property.floatValue[1]) + ", " +
               std::to_string(property.floatValue[2]) + ")";
    case BrxPropertyKind::Vector4:
        return "4-component value";
    case BrxPropertyKind::Float2:
        return "(" + std::to_string(property.floatValue[0]) + ", " + std::to_string(property.floatValue[1]) + ")";
    case BrxPropertyKind::UInt32:
        return std::to_string(property.uintValue);
    case BrxPropertyKind::Dynamic:
        return std::to_string(property.arguments.size()) + " argument(s)";
    default:
        return std::to_string(property.intValue);
    }
}

std::int32_t ResolveReference(const BrxDocument &document, const BrxObjectRecord &source, const std::int16_t option,
                              const std::int32_t oid)
{
    std::int32_t best = -1;
    int bestScore = -1;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &candidate = document.Objects()[index];
        if (candidate.oid != oid || !ObjectReferenceAccepts(option, candidate.cid))
            continue;
        int score = 0;
        if (source.proxyRecordIndex >= 0 && candidate.proxyRecordIndex == source.proxyRecordIndex)
        {
            score = 2;
            if (source.proxyRootIndex >= 0 && candidate.proxyRootIndex == source.proxyRootIndex)
                score = 3;
        }
        else if (source.proxyRecordIndex < 0 && candidate.proxyRecordIndex < 0)
            score = 2;
        if (source.spliceIndex == candidate.spliceIndex)
            ++score;
        if (score > bestScore)
        {
            best = static_cast<std::int32_t>(index);
            bestScore = score;
        }
    }
    return best;
}

void StateRow(const char *name, const bool ready, const bool required, const char *readyText, const char *missingText)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(ready ? ImVec4(0.30f, 0.88f, 0.50f, 1.0f)
                             : (required ? ImVec4(1.0f, 0.37f, 0.28f, 1.0f) : ImVec4(1.0f, 0.72f, 0.25f, 1.0f)),
                       "%s", ready ? "READY" : (required ? "ERROR" : "CHECK"));
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(name);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextWrapped("%s", ready ? readyText : missingText);
}

const char *Role(const BrxObjectRecord &object)
{
    if (object.cid == 33)
        return "Break controller";
    if (object.cid == 39)
        return "Break piece";
    if (object.cid == 47)
        return "Alarm coordinator";
    if (object.cid >= 48 && object.cid <= 51)
        return "Detector";
    if (object.cid == 52)
        return "Barrier";
    if (object.cid == 54 || object.cid == 55)
        return "Zap hazard";
    if (object.cid == 57)
        return "Conveyor";
    if (object.cid >= 59 && object.cid <= 63)
        return "Projectile/hazard";
    if (object.cid == 70 || object.cid == 72)
        return "Projectile/hazard";
    if (object.cid == 71)
        return "Launcher encounter";
    if (object.cid == 76)
        return "Decoy";
    if (object.cid == 80)
        return "Emitter";
    if (object.cid == 81)
        return "Light";
    if (object.cid == 116)
        return "Warp destination";
    if (object.cid == 119)
        return "Explosion";
    if (object.cid == 131 || object.cid == 132)
        return "Animation";
    if (object.cid == 136)
        return "State machine";
    if (object.renderGroupCount > 0)
        return "Renderable model";
    if (object.so.present)
        return "Collision/physics";
    return "Support object";
}

void Guidance(const StudioKind kind)
{
    switch (kind)
    {
    case StudioKind::Breakable:
        ImGui::BulletText("Keep intact geometry, break controllers, pieces, effects and sounds in one hierarchy.");
        ImGui::BulletText(
            "BRKP pieces need usable model/collision data and must remain connected to their owning break setup.");
        ImGui::BulletText("Use the specialized inspector to configure class-specific damage and persistence options.");
        break;
    case StudioKind::Security:
        ImGui::BulletText("Trace detector references through ALARM and BARRIER before changing OIDs.");
        ImGui::BulletText("Use viewport previews for LASEN beams, camera sweeps and trigger/zap volumes.");
        ImGui::BulletText("Test enable, disable and reset paths in Splice as one security network.");
        break;
    case StudioKind::Mechanism:
        ImGui::BulletText("A visible mechanism normally needs render geometry plus fixed gameplay collision.");
        ImGui::BulletText("Movement may be driven by ASEG, SM, Splice or class-specific velocity options.");
        ImGui::BulletText("Verify passenger/camera collision after changing a platform path or transform.");
        break;
    case StudioKind::Pickup:
        ImGui::BulletText("Preserve collection, save-state and world-flag identities used by mission scripts.");
        ImGui::BulletText("Check checkpoint/warp/exit destinations together before exporting progression edits.");
        ImGui::BulletText("Preview pickup presentation separately from permanent progression behavior.");
        break;
    case StudioKind::Weapon:
        ImGui::BulletText("Treat launcher, projectile, target, explosion, emitter and sound as one dependency chain.");
        ImGui::BulletText("Validate speed, acceleration, lifetime and collision filters before test firing.");
        ImGui::BulletText("Use RWM and trajectory previews where the concrete class supports them.");
        break;
    case StudioKind::Water:
        ImGui::BulletText(
            "Keep surface rendering, authored collision, material animation and VISMAP placement aligned.");
        ImGui::BulletText(
            "Use fixed collision for a static water boundary; gameplay water behavior remains class-specific.");
        ImGui::BulletText("Validate shader/texture compatibility and bounds after replacing the surface mesh.");
        break;
    case StudioKind::Shadow:
        ImGui::BulletText(
            "DYSH is the projected-shadow helper referenced by a caster ALO; it is not ordinary scenery.");
        ImGui::BulletText(
            "Configure cast-shadow properties on the caster and keep the Dynamic Shadow Object reference valid.");
        ImGui::BulletText(
            "Check projection radius, near/far cast, cone angle, shader and receiving geometry in the viewport.");
        break;
    }
}

void DrawShadowControls(BrxDocument &document, SceneRenderer &renderer, BrxObjectRecord &object, std::string &status)
{
    bool changed = false;
    bool preview = renderer.DynamicShadowPreviewEnabled();
    if (ImGui::Checkbox("Preview dynamic projections", &preview))
    {
        renderer.SetDynamicShadowPreviewEnabled(preview);
        status = preview ? "Dynamic projected-shadow preview enabled." : "Dynamic projected-shadow preview disabled.";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Editor-only preview switch; it is not serialized.");

    if (object.cid == 97)
    {
        ImGui::SeparatorText("DYSH silhouette helper");
        ImGui::TextWrapped(
            "This object's model is the silhouette ProjectCane renders into a dynamic shadow texture. Projection size, direction and shader belong to the caster ALO that references this DYSH, not to the helper itself.");
        std::size_t casterCount = 0;
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
        {
            const auto &candidate = document.Objects()[index];
            const auto *link = FindProperty(candidate, 51);
            if (link == nullptr || link->intValue != object.oid)
                continue;
            ++casterCount;
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::SmallButton("Select caster"))
                renderer.SelectObject(static_cast<std::int32_t>(index));
            ImGui::SameLine();
            ImGui::TextUnformatted(Label(candidate).c_str());
            ImGui::PopID();
        }
        if (casterCount == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.22f, 1.0f),
                               "This DYSH is not referenced by any caster through EOPID 51.");
        return;
    }

    ImGui::SeparatorText("Fast contact shadow");
    auto *fastRadius = FindProperty(object, 41);
    auto *fastDepth = FindProperty(object, 42);
    bool fastEnabled =
        fastRadius != nullptr && std::isfinite(fastRadius->floatValue[0]) && fastRadius->floatValue[0] > 0.0f;
    if (ImGui::Checkbox("Enable fast/contact shadow", &fastEnabled))
    {
        auto &radius = EnsureProperty(object, 41);
        radius.floatValue[0] = fastEnabled ? 100.0f : 0.0f;
        radius.modified = true;
        if (fastEnabled)
        {
            auto &depth = EnsureProperty(object, 42);
            if (!std::isfinite(depth.floatValue[0]) || depth.floatValue[0] <= 0.0f)
                depth.floatValue[0] = 100.0f;
            depth.modified = true;
        }
        changed = true;
        fastRadius = FindProperty(object, 41);
        fastDepth = FindProperty(object, 42);
    }
    if (fastRadius != nullptr)
    {
        float radius = fastRadius->floatValue[0];
        if (ImGui::DragFloat("Contact radius", &radius, 1.0f, 0.0f, 100000.0f, "%.2f"))
        {
            fastRadius->floatValue[0] = std::max(radius, 0.0f);
            fastRadius->modified = true;
            changed = true;
        }
    }
    if (fastDepth != nullptr)
    {
        float depth = fastDepth->floatValue[0];
        if (ImGui::DragFloat("Contact depth", &depth, 1.0f, 0.0f, 100000.0f, "%.2f"))
        {
            fastDepth->floatValue[0] = std::max(depth, 0.0f);
            fastDepth->modified = true;
            changed = true;
        }
    }
    ImGui::TextDisabled("Fast shadows use the retail stock shadow model (ALO OID 29).");

    ImGui::SeparatorText("Projected shadow");
    bool projectedEnabled = false;
    if (const auto *cast = FindProperty(object, 43))
        projectedEnabled = cast->boolValue;
    for (const auto &property : object.properties)
        if (property.eopid >= 44 && property.eopid <= 51)
            projectedEnabled = true;
    if (ImGui::Checkbox("Enable projected shadow", &projectedEnabled))
    {
        if (projectedEnabled)
        {
            auto &cast = EnsureProperty(object, 43);
            cast.boolValue = true;
            cast.modified = true;
        }
        else
        {
            object.properties.erase(std::remove_if(object.properties.begin(), object.properties.end(),
                                                   [](const BrxPropertyRecord &property)
                                                   { return property.eopid >= 43 && property.eopid <= 51; }),
                                    object.properties.end());
            auto &cast = EnsureProperty(object, 43);
            cast.boolValue = false;
            cast.modified = true;
        }
        changed = true;
    }
    if (projectedEnabled)
    {
        auto editFloatOverride = [&](const std::int16_t eopid, const char *label, const float speed,
                                     const float minimum, const float maximum)
        {
            auto *property = FindProperty(object, eopid);
            bool authored = property != nullptr;
            ImGui::PushID(eopid);
            if (ImGui::Checkbox("##authored", &authored))
            {
                if (authored)
                    EnsureProperty(object, eopid);
                else
                    object.properties.erase(std::remove_if(object.properties.begin(), object.properties.end(),
                                                           [&](const BrxPropertyRecord &value)
                                                           { return value.eopid == eopid; }),
                                            object.properties.end());
                changed = true;
                property = FindProperty(object, eopid);
            }
            ImGui::SameLine();
            if (!authored)
                ImGui::BeginDisabled();
            float value = property != nullptr ? property->floatValue[0] : 0.0f;
            if (ImGui::DragFloat(label, &value, speed, minimum, maximum, "%.3f") && property != nullptr)
            {
                property->floatValue[0] = value;
                property->modified = true;
                changed = true;
            }
            if (!authored)
                ImGui::EndDisabled();
            ImGui::PopID();
        };

        auto *shader = FindProperty(object, 44);
        bool authoredShader = shader != nullptr;
        if (ImGui::Checkbox("Override shadow shader", &authoredShader))
        {
            if (authoredShader)
                EnsureProperty(object, 44);
            else
                object.properties.erase(std::remove_if(object.properties.begin(), object.properties.end(),
                                                       [](const BrxPropertyRecord &value)
                                                       { return value.eopid == 44; }),
                                        object.properties.end());
            changed = true;
            shader = FindProperty(object, 44);
        }
        int shaderOid = shader != nullptr ? shader->intValue : -1;
        if (!authoredShader)
            ImGui::BeginDisabled();
        if (ImGui::InputInt("Shadow shader OID", &shaderOid) && shader != nullptr)
        {
            shader->intValue = shaderOid;
            shader->modified = true;
            changed = true;
        }
        if (!authoredShader)
            ImGui::EndDisabled();
        const std::int32_t effectiveShaderOid = shaderOid == -1 ? 0x2c : shaderOid;
        const auto shaderRecord =
            std::find_if(document.Shaders().begin(), document.Shaders().end(),
                         [&](const BrxShaderRecord &candidate) { return candidate.oid == effectiveShaderOid; });
        if (shaderRecord == document.Shaders().end())
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Shader OID %d does not exist.", effectiveShaderOid);
        else if (shaderRecord->kind != 2 && shaderRecord->kind != 3)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                               "Shader is kind %d; projected shadows require SHDK 2 or 3.", shaderRecord->kind);
        else
            ImGui::TextDisabled("SHDK %d: %s", shaderRecord->kind,
                                shaderRecord->kind == 2 ? "world -Z projection" : "caster-local -Z/Y projection");

        editFloatOverride(45, "Near radius", 1.0f, -1.0f, 100000.0f);
        editFloatOverride(46, "Far radius", 1.0f, -1.0f, 100000.0f);
        editFloatOverride(47, "Near cast distance", 1.0f, -100000.0f, 100000.0f);
        editFloatOverride(48, "Far cast distance", 1.0f, -100000.0f, 100000.0f);
        editFloatOverride(49, "Cone angle (degrees)", 0.1f, -179.0f, 179.0f);

        auto *up = FindProperty(object, 50);
        bool authoredUp = up != nullptr;
        if (ImGui::Checkbox("Override frustum up", &authoredUp))
        {
            if (authoredUp)
                EnsureProperty(object, 50);
            else
                object.properties.erase(std::remove_if(object.properties.begin(), object.properties.end(),
                                                       [](const BrxPropertyRecord &value)
                                                       { return value.eopid == 50; }),
                                        object.properties.end());
            changed = true;
            up = FindProperty(object, 50);
        }
        if (authoredUp && up != nullptr && ImGui::DragFloat3("Frustum up vector", up->floatValue, 0.01f, -1.0f, 1.0f))
        {
            up->modified = true;
            changed = true;
        }
        if (up != nullptr)
        {
            const float lengthSquared = up->floatValue[0] * up->floatValue[0] + up->floatValue[1] * up->floatValue[1] +
                                        up->floatValue[2] * up->floatValue[2];
            if (!std::isfinite(lengthSquared) || lengthSquared <= 0.00000001f)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Frustum up must be a finite non-zero vector.");
        }

        auto *helper = FindProperty(object, 51);
        int selectedHelper = helper != nullptr ? helper->intValue : -1;
        const std::string helperPreview =
            selectedHelper == -1 ? "Static textured projection" : "DYSH OID " + std::to_string(selectedHelper);
        if (ImGui::BeginCombo("Projection source", helperPreview.c_str()))
        {
            if (ImGui::Selectable("Static textured projection", selectedHelper == -1))
            {
                auto &link = EnsureProperty(object, 51);
                link.intValue = -1;
                link.modified = true;
                changed = true;
            }
            for (const auto &candidate : document.Objects())
            {
                if (candidate.cid != 97)
                    continue;
                const std::string candidateLabel = Label(candidate);
                if (ImGui::Selectable(candidateLabel.c_str(), selectedHelper == candidate.oid))
                {
                    auto &link = EnsureProperty(object, 51);
                    link.intValue = candidate.oid;
                    link.modified = true;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        helper = FindProperty(object, 51);
        if (helper != nullptr && helper->intValue != -1 && ResolveReference(document, object, 51, helper->intValue) < 0)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                               "Dynamic helper OID %d does not resolve to a context-compatible DYSH.",
                               helper->intValue);
    }

    if (changed)
        RefreshShadows(document, renderer, status, "Updated shadow settings and refreshed shadow previews.");
}

void DrawShadowReceivers(BrxDocument &document, SceneRenderer &renderer, const std::int32_t selectedObject,
                         std::string &status)
{
    std::size_t kind2Count = 0, kind3Count = 0;
    for (const auto &glob : document.Globs())
    {
        kind2Count += (glob.flags & 0x01u) != 0;
        kind3Count += (glob.flags & 0x02u) != 0;
    }
    ImGui::SeparatorText("Projected-shadow receivers");
    ImGui::Text("%zu GLOBs receive SHDK 2 | %zu GLOBs receive SHDK 3", kind2Count, kind3Count);
    ImGui::TextDisabled(
        "These are retail GLOB grfglob bits 0 and 1. They choose which projected-shadow shader kinds can affect each model.");

    static bool onlySelectedHierarchy = true;
    ImGui::Checkbox("Only selected hierarchy", &onlySelectedHierarchy);
    std::vector<std::size_t> visible;
    visible.reserve(document.Globs().size());
    for (std::size_t index = 0; index < document.Globs().size(); ++index)
    {
        const auto owner = document.Globs()[index].ownerObjectIndex;
        if (onlySelectedHierarchy && !IsDescendant(document, owner, selectedObject))
            continue;
        visible.push_back(index);
    }
    if (visible.empty())
    {
        ImGui::TextDisabled(
            onlySelectedHierarchy
                ? "The selected hierarchy owns no GLOB models. Disable the filter to inspect the whole level."
                : "The level has no decoded GLOB models.");
        return;
    }

    bool receiverChanged = false;
    if (ImGui::BeginTable("shadow_receivers", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 220.0f)))
    {
        ImGui::TableSetupColumn("Model");
        ImGui::TableSetupColumn("Owner");
        ImGui::TableSetupColumn("SHDK 2", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("SHDK 3", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const std::size_t globIndex = visible[static_cast<std::size_t>(row)];
                auto &glob = document.EditableGlobs()[globIndex];
                ImGui::PushID(static_cast<int>(globIndex));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string modelLabel =
                    glob.editorName.empty() ? "GLOB " + std::to_string(glob.renderGroup) : glob.editorName;
                ImGui::TextUnformatted(modelLabel.c_str());
                ImGui::TableSetColumnIndex(1);
                if (glob.ownerObjectIndex >= 0 &&
                    static_cast<std::size_t>(glob.ownerObjectIndex) < document.Objects().size())
                {
                    if (ImGui::SmallButton("Select"))
                        renderer.SelectObject(glob.ownerObjectIndex);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(
                        Label(document.Objects()[static_cast<std::size_t>(glob.ownerObjectIndex)]).c_str());
                }
                else
                    ImGui::TextDisabled("No owner");
                bool receives2 = (glob.flags & 0x01u) != 0;
                bool receives3 = (glob.flags & 0x02u) != 0;
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Checkbox("##shdk2", &receives2))
                {
                    glob.flags = receives2 ? static_cast<std::uint8_t>(glob.flags | 0x01u)
                                           : static_cast<std::uint8_t>(glob.flags & ~0x01u);
                    glob.modified = true;
                    receiverChanged = true;
                }
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Checkbox("##shdk3", &receives3))
                {
                    glob.flags = receives3 ? static_cast<std::uint8_t>(glob.flags | 0x02u)
                                           : static_cast<std::uint8_t>(glob.flags & ~0x02u);
                    glob.modified = true;
                    receiverChanged = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (receiverChanged)
    {
        document.RebuildRenderPacketsFromGlobs();
        renderer.Upload(document);
        status = "Updated projected-shadow receiver flags and rebuilt model render packets.";
    }
}

void DrawStudio(const StudioKind kind, bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                std::string &status)
{
    if (!open)
        return;
    const StudioInfo info = Info(kind);
    ImGui::SetNextWindowSize(ImVec2(1020.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(info.title, &open))
    {
        ImGui::End();
        return;
    }

    static std::array<std::array<char, 96>, 7> searches{};
    auto &search = searches[static_cast<std::size_t>(kind)];
    ImGui::InputTextWithHint("##studio_search", "Search name, class or OID", search.data(), search.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", info.subtitle);
    if (kind == StudioKind::Shadow)
    {
        std::size_t fastCount = 0, projectedCount = 0, helperCount = 0;
        for (const auto &object : document.Objects())
        {
            helperCount += object.cid == 97;
            const auto *radius = FindProperty(object, 41);
            fastCount += radius != nullptr && radius->floatValue[0] > 0.0f;
            bool projected = FindProperty(object, 43) != nullptr && FindProperty(object, 43)->boolValue;
            for (const auto &property : object.properties)
                if (property.eopid >= 44 && property.eopid <= 51)
                    projected = true;
            projectedCount += projected;
        }
        ImGui::TextDisabled("%zu contact | %zu projected | %zu DYSH helper(s)", fastCount, projectedCount, helperCount);
    }
    const std::string needle = Lower(search.data());

    static bool includeUnconfiguredShadowCasters = false;
    if (kind == StudioKind::Shadow)
    {
        ImGui::Checkbox("Show every ALO-compatible object", &includeUnconfiguredShadowCasters);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Enable this to add a new contact or projected shadow to an object that does not have shadow options yet.");
    }

    std::vector<std::int32_t> objects;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        const bool selectedUnconfiguredCaster = kind == StudioKind::Shadow &&
                                                renderer.SelectedObjectIndex() == static_cast<std::int32_t>(index) &&
                                                CidDerivesFrom(object.cid, 2);
        const bool listedUnconfiguredCaster =
            kind == StudioKind::Shadow && includeUnconfiguredShadowCasters && CidDerivesFrom(object.cid, 2);
        if (!FitsObject(kind, object) && !selectedUnconfiguredCaster && !listedUnconfiguredCaster)
            continue;
        if (!needle.empty() && Lower(Label(object)).find(needle) == std::string::npos)
            continue;
        objects.push_back(static_cast<std::int32_t>(index));
    }

    ImGui::BeginChild("studio_list", ImVec2(270.0f, 0.0f), true);
    ImGui::TextDisabled("%zu matching object(s)", objects.size());
    for (const auto index : objects)
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(index)];
        const std::string label =
            Label(object) + "\n" + std::to_string(object.properties.size()) + " authored option(s)";
        if (ImGui::Selectable(label.c_str(), renderer.SelectedObjectIndex() == index, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, 42.0f)))
        {
            renderer.SelectObject(index);
            status = "Selected " + Label(object);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("studio_detail", ImVec2(0.0f, 0.0f), true);

    const auto selected = renderer.SelectedObjectIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= document.Objects().size() ||
        (!FitsObject(kind, document.Objects()[static_cast<std::size_t>(selected)]) &&
         !(kind == StudioKind::Shadow &&
           CidDerivesFrom(document.Objects()[static_cast<std::size_t>(selected)].cid, 2))))
    {
        ImGui::TextDisabled("%s", info.emptyText);
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    auto &root = document.EditableObjects()[static_cast<std::size_t>(selected)];
    std::vector<std::int32_t> hierarchy;
    bool hasModel = false, hasCollision = false, hasAnimation = false, hasStateMachine = false;
    bool hasPath = false, hasEffect = false, hasPiece = false;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        if (!IsDescendant(document, static_cast<std::int32_t>(index), selected))
            continue;
        hierarchy.push_back(static_cast<std::int32_t>(index));
        const auto &object = document.Objects()[index];
        hasModel |= object.renderGroupCount > 0;
        hasCollision |= object.so.present && !object.so.collisionGeometry.surfaces.empty();
        hasPath |= object.cid == 98 || (object.cid >= 110 && object.cid <= 114);
        hasEffect |= object.cid == 80 || object.cid == 119;
        hasPiece |= object.cid == 39;
        hasStateMachine |= object.cid == 136;
    }
    for (const auto &segment : document.AnimationSegments())
        hasAnimation |= IsDescendant(document, segment.ownerObjectIndex, selected);
    for (const auto &machine : document.StateMachines())
        hasStateMachine |= IsDescendant(document, machine.ownerObjectIndex, selected);

    std::vector<std::int32_t> incomingReferences;
    std::vector<std::pair<std::int16_t, std::int32_t>> outgoingReferences;
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        for (const auto &property : object.properties)
        {
            if (!IsObjectReferenceOption(property.eopid))
                continue;
            if (ResolveReference(document, object, property.eopid, property.intValue) == selected)
                incomingReferences.push_back(static_cast<std::int32_t>(objectIndex));
            if (objectIndex == static_cast<std::size_t>(selected))
                outgoingReferences.emplace_back(property.eopid, property.intValue);
        }
    }

    ImGui::TextUnformatted(Label(root).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Frame"))
    {
        if (!renderer.FrameObject(camera, selected))
            status = "Selected object has no frameable bounds.";
    }
    ImGui::TextDisabled("%zu hierarchy object(s) | %zu incoming reference(s) | %zu outgoing reference(s)",
                        hierarchy.size(), incomingReferences.size(), outgoingReferences.size());

    if (kind == StudioKind::Shadow)
    {
        DrawShadowControls(document, renderer, root, status);
        DrawShadowReceivers(document, renderer, selected, status);
    }

    if (ImGui::CollapsingHeader("Readiness", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("studio_readiness", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Details");
        ImGui::TableHeadersRow();
        const bool modelRequired = RequiresModel(kind, root.cid);
        const bool collisionRequired = RequiresCollision(kind, root.cid);
        StateRow("Renderable geometry", hasModel, modelRequired, "Renderable model data is present.",
                 "No renderable model was found in this hierarchy.");
        StateRow("Gameplay collision", hasCollision, collisionRequired, "Authored collision surfaces are present.",
                 "No authored gameplay collision surface was found.");
        if (kind == StudioKind::Breakable)
            StateRow("Break pieces", hasPiece, false, "BRKP piece objects are present.",
                     "No BRKP pieces; valid for alternate-model or effect-only breaks.");
        if (kind == StudioKind::Mechanism)
            StateRow("Motion driver", hasAnimation || hasStateMachine || hasPath, false,
                     "Animation, state-machine or path data can drive the mechanism.",
                     "No decoded motion driver; check Splice or class-specific velocity options.");
        if (kind == StudioKind::Weapon || kind == StudioKind::Breakable)
            StateRow("Effects", hasEffect, false, "Emitter or explosion support is present.",
                     "No emitter/explosion child was found.");
        if (kind == StudioKind::Shadow && root.cid == 97)
            StateRow("Caster references", !incomingReferences.empty(), true,
                     "At least one object references this DYSH.", "No caster ALO references this helper.");
        StateRow("Object references",
                 outgoingReferences.empty() ||
                     std::all_of(outgoingReferences.begin(), outgoingReferences.end(), [&](const auto &reference)
                                 { return ResolveReference(document, root, reference.first, reference.second) >= 0; }),
                 true, "All authored object-reference options resolve to compatible objects.",
                 "One or more authored object references are missing or incompatible.");
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Hierarchy package", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("studio_hierarchy", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 190.0f)))
    {
        ImGui::TableSetupColumn("Object");
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Payload");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();
        for (const auto index : hierarchy)
        {
            const auto &object = document.Objects()[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(Label(object).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(Role(object));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u model(s), %zu surface(s)", object.renderGroupCount,
                        object.so.collisionGeometry.surfaces.size());
            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Select"))
                renderer.SelectObject(index);
            ImGui::SameLine();
            if (ImGui::SmallButton("Frame"))
                renderer.FrameObject(camera, index);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("References and dependencies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (std::size_t referenceIndex = 0; referenceIndex < outgoingReferences.size(); ++referenceIndex)
        {
            const auto &reference = outgoingReferences[referenceIndex];
            const auto targetIndex = ResolveReference(document, root, reference.first, reference.second);
            ImGui::PushID(500000 + static_cast<int>(referenceIndex));
            if (targetIndex >= 0)
            {
                if (ImGui::SmallButton("Select target"))
                    renderer.SelectObject(targetIndex);
                ImGui::SameLine();
                ImGui::Text("%s -> %s", PropertyLabel(root.cid, reference.first).c_str(),
                            Label(document.Objects()[static_cast<std::size_t>(targetIndex)]).c_str());
            }
            else
                ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.28f, 1.0f), "%s -> missing/incompatible OID %d",
                                   PropertyLabel(root.cid, reference.first).c_str(), reference.second);
            ImGui::PopID();
        }
        if (outgoingReferences.empty())
            ImGui::TextDisabled("No typed object-reference options are authored on this object.");
        if (!incomingReferences.empty())
        {
            ImGui::SeparatorText("Referenced by");
            std::sort(incomingReferences.begin(), incomingReferences.end());
            incomingReferences.erase(std::unique(incomingReferences.begin(), incomingReferences.end()),
                                     incomingReferences.end());
            for (const auto index : incomingReferences)
            {
                if (ImGui::SmallButton(("Select##incoming" + std::to_string(index)).c_str()))
                    renderer.SelectObject(index);
                ImGui::SameLine();
                ImGui::TextUnformatted(Label(document.Objects()[static_cast<std::size_t>(index)]).c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Friendly authored settings"))
    {
        if (root.properties.empty())
            ImGui::TextDisabled("This object relies entirely on ProjectCane defaults.");
        for (const auto &property : root.properties)
        {
            if (OptionBehavior(property.eopid) != EditorOptionBehavior::Stored)
                continue;
            ImGui::BulletText("%s: %s", PropertyLabel(root.cid, property.eopid).c_str(),
                              ValueSummary(property).c_str());
        }
        ImGui::TextWrapped(
            "Select the object to edit these values in its specialized inspector. This studio summarizes authored data and dependencies without creating non-retail properties.");
    }

    if (ImGui::CollapsingHeader("Authoring guidance", ImGuiTreeNodeFlags_DefaultOpen))
        Guidance(kind);
    ImGui::EndChild();
    ImGui::End();
}
}

void DrawBreakableStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Breakable, o, d, r, c, s);
}
void DrawSecurityStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Security, o, d, r, c, s);
}
void DrawMechanismStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Mechanism, o, d, r, c, s);
}
void DrawPickupStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Pickup, o, d, r, c, s);
}
void DrawWeaponStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Weapon, o, d, r, c, s);
}
void DrawWaterStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Water, o, d, r, c, s);
}
void DrawShadowStudioPanel(bool &o, BrxDocument &d, SceneRenderer &r, EditorCamera &c, std::string &s)
{
    DrawStudio(StudioKind::Shadow, o, d, r, c, s);
}
