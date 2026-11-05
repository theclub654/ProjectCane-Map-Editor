#include "HubProgressionStudioPanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
struct FixedLink
{
    std::int16_t oid;
    std::int16_t cid;
    const char *role;
    bool contextual;
};

constexpr std::array<const char *, 6> kWorldNames{"Intro", "Underwater", "Muggshot", "Voodoo", "Snow", "Clockwerk"};

constexpr std::array<FixedLink, 18> kHubselLinks{{{0x4f9, 136, "World selection state machine", true},
                                                  {0x4fa, 136, "Secondary state machine", true},
                                                  {0x4fb, 136, "Display state machine 0", true},
                                                  {0x4fc, 136, "Display state machine 1", false},
                                                  {0x50b, 131, "Underwater selection animation", true},
                                                  {0x50c, 131, "Muggshot selection animation", true},
                                                  {0x50d, 131, "Voodoo selection animation", true},
                                                  {0x50e, 131, "Snow selection animation", true},
                                                  {0x50f, 131, "Clockwerk selection animation", true},
                                                  {0x510, 2, "Underwater world model", false},
                                                  {0x511, 2, "Muggshot world model", false},
                                                  {0x512, 2, "Voodoo world model", false},
                                                  {0x513, 2, "Snow world model", false},
                                                  {0x514, 2, "Clockwerk world model", false},
                                                  {0x33b, 101, "Progress dialog 1", false},
                                                  {0x33c, 101, "Progress dialog 2", false},
                                                  {0x33d, 101, "Progress dialog 3", false},
                                                  {0x33e, 101, "Progress dialog 4", false}}};

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

std::int32_t FindLink(const BrxDocument &document, const std::int32_t hubselIndex, const FixedLink &link, int &matches)
{
    std::int32_t result = -1;
    matches = 0;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &candidate = document.Objects()[index];
        if (candidate.oid != link.oid || (candidate.cid != link.cid && !CidDerivesFrom(candidate.cid, link.cid)))
            continue;
        if (link.contextual && !IsDescendant(document, static_cast<std::int32_t>(index), hubselIndex))
            continue;
        ++matches;
        if (result < 0)
            result = static_cast<std::int32_t>(index);
    }
    return result;
}

std::string Label(const BrxObjectRecord &object)
{
    const std::string runtime = std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid);
    return object.editorName.empty() ? runtime : object.editorName + "  (" + runtime + ")";
}

void SelectAndFrame(SceneRenderer &renderer, EditorCamera &camera, const std::int32_t index)
{
    renderer.SelectObject(index);
    renderer.FrameObject(camera, index);
}
}

void DrawHubProgressionStudioPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                                   std::string &status)
{
    (void)status;
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(1080.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Hub Progression Studio", &open))
    {
        ImGui::End();
        return;
    }

    std::int32_t hubselIndex = -1;
    std::array<std::int32_t, 5> hubprogIndices{};
    hubprogIndices.fill(-1);
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.cid == 105 && hubselIndex < 0)
            hubselIndex = static_cast<std::int32_t>(index);
        if (object.cid == 106 && object.oid >= 1301 && object.oid <= 1305)
            hubprogIndices[static_cast<std::size_t>(object.oid - 1301)] = static_cast<std::int32_t>(index);
    }

    ImGui::TextWrapped(
        "Retail HUBSEL and HUBPROG are fixed-layout hub controllers. This studio exposes their named OID slots and save-driven behavior without serializing editor-only progression fields.");

    if (ImGui::CollapsingHeader("Hub selector (HUBSEL)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (hubselIndex < 0)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.22f, 1.0f), "No HUBSEL object exists in this level.");
        else
        {
            const auto &hubsel = document.Objects()[static_cast<std::size_t>(hubselIndex)];
            if (ImGui::Button("Select HUBSEL"))
                SelectAndFrame(renderer, camera, hubselIndex);
            ImGui::SameLine();
            ImGui::TextUnformatted(Label(hubsel).c_str());

            static int previewWorld = 0;
            static bool previewAccepted = false;
            ImGui::Combo("Preview world", &previewWorld, kWorldNames.data(), static_cast<int>(kWorldNames.size()));
            ImGui::SameLine();
            ImGui::Checkbox("Accepted", &previewAccepted);
            ImGui::Text("Retail state OID: %d", 1279 + previewWorld + (previewAccepted ? 6 : 0));
            ImGui::TextDisabled(
                "Preview controls explain state selection; they do not alter save data or serialize runtime state.");

            int valid = 0;
            if (ImGui::BeginTable("hubsel_links", 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollY,
                                  ImVec2(0.0f, 285.0f)))
            {
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("OID", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Named role");
                ImGui::TableSetupColumn("Lookup", ImGuiTableColumnFlags_WidthFixed, 95.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 85.0f);
                ImGui::TableHeadersRow();
                for (const auto &link : kHubselLinks)
                {
                    int matches = 0;
                    const auto index = FindLink(document, hubselIndex, link, matches);
                    const bool okay = index >= 0 && matches == 1;
                    valid += okay ? 1 : 0;
                    ImGui::PushID(link.oid);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(okay ? ImVec4(0.32f, 0.88f, 0.52f, 1.0f) : ImVec4(1.0f, 0.38f, 0.25f, 1.0f),
                                       "%s", index < 0 ? "MISSING" : (matches > 1 ? "DUPLICATE" : "READY"));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", link.oid);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(link.role);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(link.contextual ? "Descendant" : "Global");
                    ImGui::TableSetColumnIndex(4);
                    if (index >= 0 && ImGui::SmallButton("Select"))
                        SelectAndFrame(renderer, camera, index);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::Text("HUBSEL readiness: %d / %zu unique fixed relationships", valid, kHubselLinks.size());
        }
    }

    if (ImGui::CollapsingHeader("World progress displays (HUBPROG)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static std::array<bool, 9> previewComplete{};
        ImGui::TextDisabled("Completion preview is editor-only. Runtime visibility comes from level/world save flags.");
        if (ImGui::BeginTable("hubprog_worlds", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("World", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Progress slots");
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableHeadersRow();
            constexpr std::array<const char *, 9> slotNames{"Approach", "Hub",   "Job 1", "Job 2",         "Job 3",
                                                            "Job 4",    "Job 5", "Job 6", "World complete"};
            for (std::size_t world = 0; world < hubprogIndices.size(); ++world)
            {
                const auto controllerIndex = hubprogIndices[world];
                ImGui::PushID(static_cast<int>(world));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(kWorldNames[world + 1]);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(controllerIndex >= 0 ? ImVec4(0.32f, 0.88f, 0.52f, 1.0f)
                                                        : ImVec4(1.0f, 0.38f, 0.25f, 1.0f),
                                   "%s OID %zu", controllerIndex >= 0 ? "READY" : "MISSING", world + 1301);
                ImGui::TableSetColumnIndex(2);
                if (controllerIndex >= 0)
                {
                    int resolved = 0;
                    for (std::size_t slot = 0; slot < slotNames.size(); ++slot)
                    {
                        const std::int16_t oid = static_cast<std::int16_t>(0x51a + slot);
                        FixedLink link{oid, 2, slotNames[slot], true};
                        int matches = 0;
                        const auto marker = FindLink(document, controllerIndex, link, matches);
                        if (marker >= 0 && matches == 1)
                            ++resolved;
                    }
                    ImGui::Text("%d / 9 markers resolved", resolved);
                }
                else
                    ImGui::TextDisabled("No controller package");
                ImGui::TableSetColumnIndex(3);
                if (controllerIndex >= 0 && ImGui::SmallButton("Select"))
                    SelectAndFrame(renderer, camera, controllerIndex);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Progress visibility preview");
        for (std::size_t slot = 0; slot < previewComplete.size(); ++slot)
        {
            const std::string previewLabel = slot == 8 ? "World complete"
                                                       : (slot == 0   ? "Approach"
                                                          : slot == 1 ? "Hub"
                                                                      : "Job " + std::to_string(slot - 1));
            ImGui::Checkbox(previewLabel.c_str(), &previewComplete[slot]);
            if (slot != 8)
                ImGui::SameLine();
        }
    }

    if (ImGui::CollapsingHeader("Safe editing workflow", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BulletText(
            "Select a named dependency here, then edit its model, SM, ASEG, DIALOG or hierarchy in the normal inspector.");
        ImGui::BulletText(
            "Keep HUBSEL's fixed OIDs and contextual/global lookup scopes intact; ProjectCane resolves them after load.");
        ImGui::BulletText(
            "A HUBPROG controller must use OID 1301..1305 and own one contextual ALO marker at every OID 1306..1314.");
        ImGui::BulletText(
            "Do not serialize preview completion: the game derives it from FLS_Visited, FLS_KeyCollected and FWS_Complete.");
        ImGui::BulletText("Run Level Validation after moving, replacing or duplicating any hub package dependency.");
    }

    ImGui::End();
}
