#include "ExitManagerPanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorDocumentOperations.h"
#include "EditorOptions.h"
#include "EditorWorkspace.h"
#include "SceneRenderer.h"

#include <imgui.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace
{
BrxPropertyRecord *FindProperty(BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.end() ? nullptr : &*found;
}

BrxPropertyRecord &EnsureInt16Property(BrxObjectRecord &object, const std::int16_t eopid,
                                       const std::int32_t defaultValue = -1)
{
    if (auto *property = FindProperty(object, eopid))
        return *property;
    BrxPropertyRecord property;
    property.eopid = eopid;
    property.kind = BrxPropertyKind::Int16;
    property.intValue = defaultValue;
    property.editable = true;
    property.modified = true;
    object.properties.push_back(std::move(property));
    return object.properties.back();
}

struct CatalogWarp
{
    std::int16_t oid = -1;
    std::string label;
};
struct CatalogLevel
{
    std::filesystem::path path;
    std::string serializedName;
    std::vector<CatalogWarp> warps;
    std::string error;
};

struct ExitLevelCatalog
{
    std::filesystem::path root;
    std::vector<CatalogLevel> levels;

    void Rebuild(const BrxDocument &current)
    {
        levels.clear();
        root = current.Path().parent_path();
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error),
            end;
        while (iterator != end)
        {
            if (error)
            {
                error.clear();
                iterator.increment(error);
                continue;
            }
            if (iterator->is_regular_file(error))
            {
                std::string extension = iterator->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
                if (extension == ".brx")
                {
                    CatalogLevel level;
                    level.path = iterator->path();
                    level.serializedName = iterator->path().stem().string();
                    BrxDocument destination;
                    if (destination.OpenReadOnly(level.path, level.error))
                    {
                        for (const auto &warp : destination.Warps())
                        {
                            if (warp.ownerObjectIndex < 0 ||
                                static_cast<std::size_t>(warp.ownerObjectIndex) >= destination.Objects().size())
                                continue;
                            const auto &object = destination.Objects()[static_cast<std::size_t>(warp.ownerObjectIndex)];
                            CatalogWarp item;
                            item.oid = object.oid;
                            item.label = "WARP OID " + std::to_string(object.oid);
                            if (!warp.destinationLevel.empty())
                                item.label += "  " + warp.destinationLevel;
                            if (warp.isDefault)
                                item.label += "  [default]";
                            if (warp.isSpecialDefault)
                                item.label += "  [special]";
                            level.warps.push_back(std::move(item));
                        }
                        std::sort(level.warps.begin(), level.warps.end(),
                                  [](const CatalogWarp &left, const CatalogWarp &right)
                                  { return left.oid < right.oid; });
                    }
                    levels.push_back(std::move(level));
                }
            }
            iterator.increment(error);
        }
        std::sort(levels.begin(), levels.end(), [](const CatalogLevel &left, const CatalogLevel &right)
                  { return left.serializedName < right.serializedName; });
    }
};

void DrawDestinationBrowser(const char *id, ExitLevelCatalog &catalog, std::string &levelName, BrxObjectRecord &object,
                            const std::int16_t warpEopid, std::string &status)
{
    ImGui::PushID(id);
    const CatalogLevel *selectedLevel = nullptr;
    for (const auto &level : catalog.levels)
        if (level.serializedName == levelName)
        {
            selectedLevel = &level;
            break;
        }
    if (ImGui::BeginCombo("Level", levelName.empty() ? "Choose destination level..." : levelName.c_str()))
    {
        for (const auto &level : catalog.levels)
        {
            const bool selected = level.serializedName == levelName;
            std::string label = level.serializedName + "  (" + std::to_string(level.warps.size()) + " WARPs)";
            if (!level.error.empty())
                label += "  [unreadable]";
            if (ImGui::Selectable(label.c_str(), selected) && level.error.empty())
            {
                levelName = level.serializedName;
                auto &warp = EnsureInt16Property(object, warpEopid);
                warp.intValue = level.warps.size() == 1 ? level.warps.front().oid : -1;
                warp.modified = true;
                status = "Selected destination level " + levelName + ".";
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (selectedLevel && selectedLevel->error.empty())
    {
        auto &property = EnsureInt16Property(object, warpEopid);
        const CatalogWarp *selectedWarp = nullptr;
        for (const auto &warp : selectedLevel->warps)
            if (warp.oid == property.intValue)
            {
                selectedWarp = &warp;
                break;
            }
        const std::string preview = selectedWarp ? selectedWarp->label : "Choose destination WARP...";
        if (ImGui::BeginCombo("WARP", preview.c_str()))
        {
            for (const auto &warp : selectedLevel->warps)
            {
                const bool selected = warp.oid == property.intValue;
                if (ImGui::Selectable(warp.label.c_str(), selected))
                {
                    property.intValue = warp.oid;
                    property.modified = true;
                    status = "Assigned " + warp.label + ".";
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (selectedLevel->warps.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f), "That level contains no decoded WARPs.");
        else if (!selectedWarp)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f), "WARP OID %d is not in that level.", property.intValue);
        else
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Destination WARP verified.");
    }
    ImGui::PopID();
}

const BrxPropertyRecord *FindProperty(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.end() ? nullptr : &*found;
}

bool BoolProperty(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto *property = FindProperty(object, eopid);
    return property != nullptr && property->boolValue;
}

int IntProperty(const BrxObjectRecord &object, const std::int16_t eopid, const int fallback = -1)
{
    const auto *property = FindProperty(object, eopid);
    return property != nullptr ? property->intValue : fallback;
}

std::vector<std::string> ExitIssues(const BrxDocument &document, const BrxExitRecord &exit,
                                    const BrxObjectRecord &object)
{
    std::vector<std::string> issues;
    const bool useDefault = BoolProperty(object, 171);
    const int alternateMode = IntProperty(object, 164);
    const int wipe = IntProperty(object, 166, 0);
    if (!useDefault && exit.destinationLevel.empty())
        issues.emplace_back("Primary destination level is empty.");
    if (exit.destinationLevel.size() > 31)
        issues.emplace_back("Primary level name exceeds the 31-byte EXIT payload limit.");
    if (exit.alternateLevel.size() > 31)
        issues.emplace_back("Alternate level name exceeds the 31-byte EXIT payload limit.");
    if (alternateMode >= 0 && exit.alternateLevel.empty())
        issues.emplace_back("Alternate key routing is enabled without an alternate level.");
    if (alternateMode >= 0 && IntProperty(object, 165) < 0)
        issues.emplace_back("Alternate key routing is enabled without an alternate WARP OID.");
    if (!useDefault && IntProperty(object, 163) < 0)
        issues.emplace_back("Primary destination WARP OID is not assigned.");
    if (useDefault && (FindProperty(object, 163) || FindProperty(object, 164) || FindProperty(object, 165)))
        issues.emplace_back("Explicit destination routing is dormant while game-default routing is enabled.");
    if (wipe < -1 || wipe > 3)
        issues.emplace_back("Screen-transition type is outside the retail range.");
    if (const auto *delay = FindProperty(object, 167); delay && delay->floatValue[0] < 0.0f)
        issues.emplace_back("Re-arm delay cannot be negative.");
    if (const auto *delay = FindProperty(object, 173); delay && delay->floatValue[0] < 0.0f)
        issues.emplace_back("Wipe delay cannot be negative.");
    if (FindProperty(object, 175))
        issues.emplace_back("Runtime EXIT state EOPID 175 must not be serialized.");
    if (BoolProperty(object, 169))
    {
        std::int32_t root = exit.ownerObjectIndex;
        while (root >= 0 && static_cast<std::size_t>(root) < document.Objects().size() &&
               document.Objects()[static_cast<std::size_t>(root)].parentIndex >= 0)
            root = document.Objects()[static_cast<std::size_t>(root)].parentIndex;
        if (root != exit.ownerObjectIndex && root >= 0)
            issues.emplace_back("Key gating removes the entire hierarchy root OID " +
                                std::to_string(document.Objects()[static_cast<std::size_t>(root)].oid) +
                                "; verify that root contains nothing unrelated.");
    }
    std::size_t ownedAsegs = 0;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &candidate = document.Objects()[index];
        if (candidate.parentIndex != exit.ownerObjectIndex || !CidDerivesFrom(candidate.cid, 131))
            continue;
        ++ownedAsegs;
        if (std::none_of(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                         [&](const BrxAsegRecord &segment)
                         { return segment.ownerObjectIndex == static_cast<std::int32_t>(index); }))
            issues.emplace_back("An embedded ASEG child has no decoded animation payload.");
    }
    if (ownedAsegs != static_cast<std::size_t>((std::max)(0, static_cast<int>(exit.embeddedAnimationCount))))
        issues.emplace_back("Embedded ASEG ownership/count is inconsistent.");
    const auto volume =
        std::find_if(document.Volumes().begin(), document.Volumes().end(), [&](const BrxVolumeRecord &candidate)
                     { return candidate.ownerObjectIndex == exit.ownerObjectIndex; });
    if (volume == document.Volumes().end() || volume->planes.empty() || volume->nodes.empty())
        issues.emplace_back("Trigger BSP is missing or empty.");
    return issues;
}

void EditFixedLevelName(const char *label, std::string &value)
{
    std::array<char, 32> buffer{};
    const std::size_t count = (std::min)(value.size(), buffer.size() - 1);
    std::copy_n(value.data(), count, buffer.data());
    if (ImGui::InputText(label, buffer.data(), buffer.size()))
        value = buffer.data();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Serialized as a fixed 32-byte field; at most 31 visible characters.");
}

void SetBoxVolume(BrxVolumeRecord &volume, const glm::vec3 &halfExtent)
{
    const glm::vec3 extent = glm::max(halfExtent, glm::vec3(1.0f));
    const float planes[6][4]{{1, 0, 0, extent.x},  {-1, 0, 0, extent.x}, {0, 1, 0, extent.y},
                             {0, -1, 0, extent.y}, {0, 0, 1, extent.z},  {0, 0, -1, extent.z}};
    volume.planes.resize(6);
    volume.nodes.resize(6);
    for (std::size_t index = 0; index < 6; ++index)
    {
        volume.planes[index].normal = {planes[index][0], planes[index][1], planes[index][2]};
        volume.planes[index].distance = planes[index][3];
        volume.nodes[index].surfaceIndex = static_cast<std::uint16_t>(index);
        volume.nodes[index].negativeChild = index + 1 < 6 ? static_cast<std::uint16_t>(index + 1) : 0xffff;
        volume.nodes[index].positiveChild = 0xffff;
    }
}

bool IsSafeBoxVolume(const BrxVolumeRecord &volume, glm::vec3 &halfExtent)
{
    if (volume.planes.size() != 6 || volume.nodes.size() != 6)
        return false;
    constexpr float expected[6][3]{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (std::size_t index = 0; index < 6; ++index)
    {
        const auto &plane = volume.planes[index];
        if (std::abs(plane.normal[0] - expected[index][0]) > 0.001f ||
            std::abs(plane.normal[1] - expected[index][1]) > 0.001f ||
            std::abs(plane.normal[2] - expected[index][2]) > 0.001f || plane.distance <= 0.0f ||
            volume.nodes[index].surfaceIndex != index || volume.nodes[index].positiveChild != 0xffff ||
            volume.nodes[index].negativeChild != (index + 1 < 6 ? index + 1 : 0xffff))
            return false;
    }
    halfExtent = {volume.planes[0].distance, volume.planes[2].distance, volume.planes[4].distance};
    return true;
}

std::vector<std::string> VolumeIssues(const BrxVolumeRecord &volume)
{
    std::vector<std::string> issues;
    if (volume.planes.empty())
        issues.emplace_back("No clipping planes.");
    if (volume.nodes.empty())
        issues.emplace_back("No BSP nodes.");
    std::vector<bool> reachable(volume.nodes.size(), false);
    std::vector<std::uint16_t> pending;
    if (!volume.nodes.empty())
        pending.push_back(0);
    while (!pending.empty())
    {
        const auto index = pending.back();
        pending.pop_back();
        if (index >= volume.nodes.size() || reachable[index])
            continue;
        reachable[index] = true;
        const auto &node = volume.nodes[index];
        if (node.surfaceIndex >= volume.planes.size())
            issues.emplace_back("A BSP node references a missing plane.");
        for (const auto child : {node.negativeChild, node.positiveChild})
            if (child != 0xffff)
            {
                if (child >= volume.nodes.size())
                    issues.emplace_back("A BSP child index is out of range.");
                else
                    pending.push_back(child);
            }
    }
    if (std::find(reachable.begin(), reachable.end(), false) != reachable.end())
        issues.emplace_back("The BSP contains unreachable nodes.");
    for (const auto &plane : volume.planes)
    {
        const glm::vec3 normal(plane.normal[0], plane.normal[1], plane.normal[2]);
        if (glm::dot(normal, normal) < 0.000001f)
            issues.emplace_back("A trigger plane has a zero normal.");
        if (!std::isfinite(plane.distance))
            issues.emplace_back("A trigger plane has a non-finite distance.");
    }
    return issues;
}

void DrawWipePreview(const int wipeKind, const double startTime)
{
    const float elapsed = static_cast<float>(ImGui::GetTime() - startTime);
    if (elapsed < 0.0f || elapsed > 1.6f)
        return;
    const float phase = std::clamp(elapsed / 1.6f, 0.0f, 1.0f);
    const ImVec2 minimum = ImGui::GetMainViewport()->Pos;
    const ImVec2 maximum(minimum.x + ImGui::GetMainViewport()->Size.x, minimum.y + ImGui::GetMainViewport()->Size.y);
    auto *draw = ImGui::GetForegroundDrawList();
    if (wipeKind == 1)
    {
        const ImVec2 center((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
        const float halfWidth = (maximum.x - minimum.x) * 0.5f * (1.0f - phase);
        const float halfHeight = (maximum.y - minimum.y) * 0.5f * (1.0f - phase);
        draw->AddRectFilled(minimum, ImVec2(maximum.x, center.y - halfHeight), IM_COL32(0, 0, 0, 235));
        draw->AddRectFilled(ImVec2(minimum.x, center.y + halfHeight), maximum, IM_COL32(0, 0, 0, 235));
        draw->AddRectFilled(ImVec2(minimum.x, center.y - halfHeight),
                            ImVec2(center.x - halfWidth, center.y + halfHeight), IM_COL32(0, 0, 0, 235));
        draw->AddRectFilled(ImVec2(center.x + halfWidth, center.y - halfHeight),
                            ImVec2(maximum.x, center.y + halfHeight), IM_COL32(0, 0, 0, 235));
    }
    else
    {
        ImU32 color = IM_COL32(0, 0, 0, static_cast<int>(220.0f * phase));
        if (wipeKind == 2)
            color = IM_COL32(25, 45, 95, static_cast<int>(210.0f * phase));
        if (wipeKind == 3)
            color = IM_COL32(160, 225, 255, static_cast<int>(190.0f * phase));
        draw->AddRectFilled(minimum, maximum, color);
    }
    const char *names[]{"FADE", "KEYHOLE", "WORLD MAP", "FROZEN"};
    const int index = std::clamp(wipeKind, 0, 3);
    draw->AddText(ImVec2(minimum.x + 24.0f, minimum.y + 48.0f), IM_COL32(255, 255, 255, 230), names[index]);
}
}

void DrawExitManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                          EditorWorkspace &workspace, std::int32_t &visualizedExitObjectIndex, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(860.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("EXIT Transition Manager", &open))
    {
        ImGui::End();
        return;
    }

    enum class PendingAsegAction
    {
        None,
        Add,
        Earlier,
        Later,
        Remove
    };
    static PendingAsegAction pendingAsegAction = PendingAsegAction::None;
    static std::int32_t pendingExitObject = -1;
    static std::int32_t pendingAsegObject = -1;
    static int previewWipeKind = -1;
    static double previewWipeStart = -10.0;
    DrawWipePreview(previewWipeKind, previewWipeStart);

    static ExitLevelCatalog catalog;
    static std::string catalogDocument;
    const std::string documentKey = document.Path().string();
    if (catalogDocument != documentKey)
    {
        catalogDocument = documentKey;
        catalog.Rebuild(document);
    }

    std::size_t issueCount = 0;
    std::size_t defaultCount = 0;
    for (const auto &exit : document.Exits())
    {
        if (exit.ownerObjectIndex < 0 || static_cast<std::size_t>(exit.ownerObjectIndex) >= document.Objects().size())
            continue;
        const auto &object = document.Objects()[static_cast<std::size_t>(exit.ownerObjectIndex)];
        issueCount += !ExitIssues(document, exit, object).empty();
        defaultCount += BoolProperty(object, 168) ? 1u : 0u;
    }

    ImGui::TextDisabled("LEVEL TRANSITION SYSTEM");
    ImGui::Text("%zu EXIT%s", document.Exits().size(), document.Exits().size() == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu with issues | %zu registered default%s", issueCount, defaultCount,
                        defaultCount == 1 ? "" : "s");
    ImGui::TextWrapped(
        "EXIT combines an animated ALO, a local BSP trigger, destination-level routing, an external WARP OID, optional key routing, and a timed screen transition.");
    ImGui::TextDisabled("Destination catalog: %zu level%s under %s", catalog.levels.size(),
                        catalog.levels.size() == 1 ? "" : "s", catalog.root.string().c_str());
    if (ImGui::SmallButton("Refresh level/WARP catalog"))
        catalog.Rebuild(document);
    if (defaultCount > 1)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                           "Multiple default EXITs are authored. Retail chooses the first one it finds.");

    ImGui::SeparatorText("Create transition");
    ImGui::TextDisabled("Creates an EXIT with a valid box trigger at the selected object, or at world origin.");
    if (ImGui::Button("Create EXIT"))
    {
        const std::int32_t anchor = renderer.SelectedObjectIndex();
        const bool validAnchor = anchor >= 0 && static_cast<std::size_t>(anchor) < document.Objects().size();
        const std::int32_t parent =
            validAnchor && document.Objects()[static_cast<std::size_t>(anchor)].hasAloPayload ? anchor : -1;
        const float *transform =
            validAnchor ? document.Objects()[static_cast<std::size_t>(anchor)].worldTransform : nullptr;
        const auto result = EditorDocumentOperations::CreateExit(document, parent, transform);
        status = result.message;
        if (result.succeeded)
        {
            workspace.Refresh(document);
            renderer.Upload(document);
            renderer.SelectObject(result.primaryObjectIndex);
            visualizedExitObjectIndex = result.primaryObjectIndex;
        }
    }

    static bool issuesOnly = false;
    ImGui::Checkbox("Issues only", &issuesOnly);
    static ImGuiTextFilter filter;
    filter.Draw("Search EXITs", 300.0f);
    ImGui::SeparatorText("Transitions");
    ImGui::BeginChild("ExitList", ImVec2(0.0f, 0.0f), true);

    auto &exits = document.EditableExits();
    auto &objects = document.EditableObjects();
    for (std::size_t exitIndex = 0; exitIndex < exits.size(); ++exitIndex)
    {
        auto &exit = exits[exitIndex];
        if (exit.ownerObjectIndex < 0 || static_cast<std::size_t>(exit.ownerObjectIndex) >= objects.size())
            continue;
        auto &object = objects[static_cast<std::size_t>(exit.ownerObjectIndex)];
        const auto issues = ExitIssues(document, exit, object);
        if (issuesOnly && issues.empty())
            continue;
        const std::string route = exit.destinationLevel.empty() ? "(no destination)" : exit.destinationLevel;
        const std::string searchable = "EXIT OID " + std::to_string(object.oid) + " " + route;
        if (!filter.PassFilter(searchable.c_str()))
            continue;

        ImGui::PushID(static_cast<int>(exitIndex));
        const bool selected = renderer.SelectedObjectIndex() == exit.ownerObjectIndex;
        const std::string heading = std::string(issues.empty() ? "EXIT   OID " : "!  EXIT   OID ") +
                                    std::to_string(object.oid) + "     " + route + "##exit";
        const bool expanded = ImGui::CollapsingHeader(heading.c_str(), selected ? ImGuiTreeNodeFlags_DefaultOpen
                                                                                : ImGuiTreeNodeFlags_None);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            renderer.SelectObject(exit.ownerObjectIndex);
            status = "Selected EXIT OID " + std::to_string(object.oid) + ".";
        }
        if (expanded)
        {
            ImGui::Indent(14.0f);
            if (ImGui::Button("Select"))
                renderer.SelectObject(exit.ownerObjectIndex);
            ImGui::SameLine();
            if (ImGui::Button("Frame"))
            {
                const glm::vec3 center = glm::vec3(glm::make_mat4(object.worldTransform)[3]);
                camera.FocusPoint(center, 450.0f);
                renderer.SelectObject(exit.ownerObjectIndex);
            }
            ImGui::SameLine();
            const bool showingThisExit = visualizedExitObjectIndex == exit.ownerObjectIndex;
            if (ImGui::Button(showingThisExit ? "Hide EXIT volume and destination"
                                              : "Show EXIT volume and destination"))
                visualizedExitObjectIndex = showingThisExit ? -1 : exit.ownerObjectIndex;

            const bool useDefault = BoolProperty(object, 171);
            ImGui::BeginDisabled(useDefault);
            DrawDestinationBrowser("Primary", catalog, exit.destinationLevel, object, 163, status);
            const int alternateMode = IntProperty(object, 164);
            ImGui::BeginDisabled(alternateMode < 0);
            DrawDestinationBrowser("Alternate", catalog, exit.alternateLevel, object, 165, status);
            ImGui::EndDisabled();
            if (ImGui::TreeNode("Advanced manual routing"))
            {
                EditFixedLevelName("Primary serialized name", exit.destinationLevel);
                auto &primary = EnsureInt16Property(object, 163);
                if (ImGui::InputInt("Primary raw WARP OID", &primary.intValue))
                    primary.modified = true;
                EditFixedLevelName("Alternate serialized name", exit.alternateLevel);
                auto &alternate = EnsureInt16Property(object, 165);
                if (ImGui::InputInt("Alternate raw WARP OID", &alternate.intValue))
                    alternate.modified = true;
                ImGui::TreePop();
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("WARP OIDs are resolved in the destination level, not this scene.");

            ImGui::SeparatorText("Progression analysis");
            if (useDefault)
            {
                ImGui::TextWrapped(
                    "Retail default progression first uses the first registered default EXIT in a non-hub level. If none exists it returns to the current level, advances from a hub to its level-1 WARP, or falls back to the hideout for the final-world/default case.");
                const BrxObjectRecord *winner = nullptr;
                for (const auto &candidate : objects)
                    if (candidate.cid == 125 && BoolProperty(candidate, 168))
                    {
                        winner = &candidate;
                        break;
                    }
                if (winner)
                    ImGui::Text("Current authored default winner: EXIT OID %d", winner->oid);
                else
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.65f, 0.2f, 1.0f),
                        "No default EXIT is registered; engine fallback rules will choose the destination.");
            }
            else
                ImGui::Text("Explicit route: %s / WARP OID %d",
                            exit.destinationLevel.empty() ? "(missing)" : exit.destinationLevel.c_str(),
                            IntProperty(object, 163));
            if (defaultCount > 1 && ImGui::Button("Keep this as the only default"))
            {
                for (auto &candidate : objects)
                    if (candidate.cid == 125)
                        if (auto *property = FindProperty(candidate, 168))
                        {
                            property->boolValue = &candidate == &object;
                            property->modified = true;
                        }
                status = "Resolved duplicate defaults; EXIT OID " + std::to_string(object.oid) + " now wins.";
            }
            if (useDefault && (FindProperty(object, 163) || FindProperty(object, 164) || FindProperty(object, 165)) &&
                ImGui::Button("Remove dormant explicit routing"))
            {
                object.properties.erase(
                    std::remove_if(object.properties.begin(), object.properties.end(),
                                   [](const BrxPropertyRecord &property)
                                   { return property.eopid == 163 || property.eopid == 164 || property.eopid == 165; }),
                    object.properties.end());
                exit.destinationLevel.clear();
                exit.alternateLevel.clear();
                status = "Removed dormant explicit EXIT routing.";
            }
            ImGui::Text("Embedded transition animations: %d", exit.embeddedAnimationCount);
            std::vector<std::int32_t> embeddedAsegs;
            for (std::size_t candidate = 0; candidate < objects.size(); ++candidate)
                if (objects[candidate].parentIndex == exit.ownerObjectIndex &&
                    CidDerivesFrom(objects[candidate].cid, 131))
                    embeddedAsegs.push_back(static_cast<std::int32_t>(candidate));
            if (ImGui::Button("Add transition animation"))
            {
                pendingAsegAction = PendingAsegAction::Add;
                pendingExitObject = exit.ownerObjectIndex;
            }
            bool hasWipeEvent = false;
            float firstDuration = 0.0f;
            for (std::size_t order = 0; order < embeddedAsegs.size(); ++order)
            {
                const std::int32_t asegObjectIndex = embeddedAsegs[order];
                const auto segment = std::find_if(
                    document.AnimationSegments().begin(), document.AnimationSegments().end(),
                    [&](const BrxAsegRecord &candidate) { return candidate.ownerObjectIndex == asegObjectIndex; });
                if (segment != document.AnimationSegments().end())
                {
                    if (order == 0)
                        firstDuration = segment->maximumFrame;
                    hasWipeEvent |=
                        std::any_of(segment->events.begin(), segment->events.end(), [](const BrxAsegEventRecord &event)
                                    { return event.type == 25 || event.type == 26; });
                }
                ImGui::PushID(10000 + static_cast<int>(order));
                ImGui::Text("%zu. ASEG OID %d", order + 1, objects[static_cast<std::size_t>(asegObjectIndex)].oid);
                ImGui::SameLine();
                if (ImGui::SmallButton("Open"))
                {
                    renderer.SelectObject(asegObjectIndex);
                    status = "Selected embedded EXIT ASEG; open the Animation inspector to edit and preview it.";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Earlier"))
                {
                    pendingAsegAction = PendingAsegAction::Earlier;
                    pendingAsegObject = asegObjectIndex;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Later"))
                {
                    pendingAsegAction = PendingAsegAction::Later;
                    pendingAsegObject = asegObjectIndex;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    pendingAsegAction = PendingAsegAction::Remove;
                    pendingAsegObject = asegObjectIndex;
                    pendingExitObject = exit.ownerObjectIndex;
                }
                ImGui::PopID();
            }
            const auto *wipeDelay = FindProperty(object, 173);
            if (hasWipeEvent)
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Wipe timing: embedded ASEG wipe event.");
            else if (wipeDelay && wipeDelay->floatValue[0] > 0.0f)
                ImGui::Text("Wipe timing: explicit delay %.3f.", wipeDelay->floatValue[0]);
            else if (!embeddedAsegs.empty())
                ImGui::Text("Wipe timing: first ASEG duration (authored maximum %.3f).", firstDuration);
            else
                ImGui::Text("Wipe timing: immediate.");
            if (ImGui::Button("Preview screen transition"))
            {
                previewWipeKind = std::clamp(IntProperty(object, 166, 0), 0, 3);
                previewWipeStart = ImGui::GetTime();
            }

            ImGui::SeparatorText("Trigger volume");
            auto volume = std::find_if(document.EditableVolumes().begin(), document.EditableVolumes().end(),
                                       [&](const BrxVolumeRecord &candidate)
                                       { return candidate.ownerObjectIndex == exit.ownerObjectIndex; });
            if (volume != document.EditableVolumes().end())
            {
                glm::vec3 halfExtent(180.0f, 180.0f, 240.0f);
                const bool safeBox = IsSafeBoxVolume(*volume, halfExtent);
                if (safeBox)
                {
                    if (ImGui::DragFloat3("Half extents", glm::value_ptr(halfExtent), 2.0f, 1.0f, 100000.0f, "%.1f"))
                        SetBoxVolume(*volume, halfExtent);
                    ImGui::TextDisabled("The six viewport planes update immediately as the box is resized.");
                }
                else
                    ImGui::TextDisabled("Imported convex BSP: raw planes are preserved until explicitly replaced.");
                if (ImGui::Button("Reset to safe box"))
                    SetBoxVolume(*volume, {180.0f, 180.0f, 240.0f});
                ImGui::SameLine();
                if (ImGui::Button("Fit convex box to EXIT geometry"))
                {
                    glm::vec3 minimum(std::numeric_limits<float>::max());
                    glm::vec3 maximum(-std::numeric_limits<float>::max());
                    bool found = false;
                    const glm::mat4 worldToExit = glm::inverse(glm::make_mat4(object.worldTransform));
                    for (const auto &batch : document.RenderBatches())
                    {
                        if (batch.ownerObjectIndex != exit.ownerObjectIndex)
                            continue;
                        const std::size_t begin = batch.firstIndex;
                        const std::size_t end = (std::min)(document.RenderIndices().size(), begin + batch.indexCount);
                        for (std::size_t index = begin; index < end; ++index)
                        {
                            const auto vertexIndex = document.RenderIndices()[index];
                            if (vertexIndex >= document.RenderVertices().size())
                                continue;
                            const auto &position = document.RenderVertices()[vertexIndex].position;
                            const glm::vec3 local =
                                glm::vec3(worldToExit * glm::vec4(position[0], position[1], position[2], 1.0f));
                            minimum = glm::min(minimum, local);
                            maximum = glm::max(maximum, local);
                            found = true;
                        }
                    }
                    if (found)
                    {
                        const glm::vec3 symmetric = glm::max(glm::abs(minimum), glm::abs(maximum));
                        SetBoxVolume(*volume, symmetric + glm::vec3(10.0f));
                        status = "Fitted a safe convex EXIT trigger around its rendered geometry.";
                    }
                    else
                        status = "The EXIT has no owned render geometry to fit.";
                }
                for (const auto &volumeIssue : VolumeIssues(*volume))
                    ImGui::BulletText("%s", volumeIssue.c_str());
            }
            else if (ImGui::Button("Create safe trigger volume"))
            {
                BrxVolumeRecord created;
                created.ownerObjectIndex = exit.ownerObjectIndex;
                SetBoxVolume(created, {180.0f, 180.0f, 240.0f});
                document.EditableVolumes().push_back(std::move(created));
            }
            for (const auto &issue : issues)
                ImGui::BulletText("%s", issue.c_str());
            ImGui::Unindent(14.0f);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (pendingAsegAction != PendingAsegAction::None)
    {
        EditorOperationResult result;
        if (pendingAsegAction == PendingAsegAction::Add)
            result = EditorDocumentOperations::CreateEmbeddedExitAseg(document, pendingExitObject);
        else if (pendingAsegAction == PendingAsegAction::Earlier)
            result = EditorDocumentOperations::MoveEmbeddedWarpAseg(document, pendingAsegObject, -1);
        else if (pendingAsegAction == PendingAsegAction::Later)
            result = EditorDocumentOperations::MoveEmbeddedWarpAseg(document, pendingAsegObject, 1);
        else
            result = EditorDocumentOperations::DeleteHierarchy(document, pendingAsegObject);
        status = result.message;
        if (result.succeeded)
        {
            workspace.Refresh(document);
            renderer.Upload(document);
            renderer.SelectObject(pendingAsegAction == PendingAsegAction::Remove ? pendingExitObject
                                                                                 : result.primaryObjectIndex);
        }
        pendingAsegAction = PendingAsegAction::None;
        pendingExitObject = pendingAsegObject = -1;
    }
    ImGui::End();
}
