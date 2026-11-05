#include "ObjectOperationsPanel.h"

#include "BrxDocument.h"
#include "EditorOptions.h"

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
bool IsScalar(const BrxPropertyKind kind)
{
    return kind == BrxPropertyKind::Bool || kind == BrxPropertyKind::Float || kind == BrxPropertyKind::Int16 ||
           kind == BrxPropertyKind::TypedInt16 || kind == BrxPropertyKind::Int32 || kind == BrxPropertyKind::UInt32;
}

std::string DefaultText(const EditorOptionDefinition &definition)
{
    if (!definition.runtimeDefaultable)
        return "descriptor default unavailable";
    switch (definition.kind)
    {
    case BrxPropertyKind::Bool:
        return definition.runtimeDefaultBits ? "true" : "false";
    case BrxPropertyKind::Float:
        return std::to_string(std::bit_cast<float>(definition.runtimeDefaultBits));
    case BrxPropertyKind::UInt32:
        return std::to_string(definition.runtimeDefaultBits);
    case BrxPropertyKind::Int16:
    case BrxPropertyKind::TypedInt16:
    case BrxPropertyKind::Int32:
        return std::to_string(static_cast<std::int32_t>(definition.runtimeDefaultBits));
    default:
        return "typed runtime default";
    }
}

std::string ValueLiteral(const BrxPropertyKind kind)
{
    switch (kind)
    {
    case BrxPropertyKind::Bool:
        return "true";
    case BrxPropertyKind::Float:
        return "0.0";
    case BrxPropertyKind::UInt32:
    case BrxPropertyKind::Int16:
    case BrxPropertyKind::TypedInt16:
    case BrxPropertyKind::Int32:
        return "0";
    default:
        return {};
    }
}

std::string ExampleFor(const std::int16_t eopid, const EditorOptionDefinition &definition,
                       const EditorOptionBehavior behavior)
{
    if (eopid == 347)
        return {};
    if (behavior == EditorOptionBehavior::Query || behavior == EditorOptionBehavior::Derived)
        return "(geto @Target " + std::to_string(eopid) + ")";
    if (behavior == EditorOptionBehavior::Action)
    {
        if (definition.argumentCount != 0)
            return {};
        return "(seto! @Target " + std::to_string(eopid) + " 0)";
    }
    const auto literal = ValueLiteral(definition.kind);
    if (literal.empty())
        return {};
    return "(seto! @Target " + std::to_string(eopid) + " " + literal + ")";
}

const char *VerifiedNote(const std::int16_t eopid)
{
    switch (eopid)
    {
    case 162:
        return "WARP action: TriggerWarp teleports the player and dispatches the WARP Trigger event.";
    case 347:
        return "BARRIER destination is resolved and cached during PostBarrierLoad. Changing only this OID at runtime does not refresh the cached WARP pointer.";
    default:
        return nullptr;
    }
}

std::string DescriptionFor(const std::int16_t cid, const std::int16_t eopid, const EditorOptionDefinition &definition,
                           const EditorOptionBehavior behavior)
{
    if (const auto *note = VerifiedNote(eopid))
        return note;
    std::string description = std::string(PropertyNameForCid(cid, eopid)) + " is a " + OptionBehaviorName(behavior) +
                              " in " + OptionGroupName(OptionGroup(eopid)) + ".";
    if (behavior == EditorOptionBehavior::Stored)
        description += " It can be authored as a BRX override when the CID exposes a SetUser lane.";
    else if (behavior == EditorOptionBehavior::Query || behavior == EditorOptionBehavior::Derived)
        description +=
            " Treat it as read-only; it is produced by runtime state rather than serialized as an editable constant.";
    else if (behavior == EditorOptionBehavior::Action)
        description += " Calling it invokes native gameplay behavior; sandbox preview may only log the side effect.";
    if (definition.argumentCount != 0)
        description += " Descriptor argument count: " + std::to_string(definition.argumentCount) + ".";
    return description;
}
}

void DrawObjectOperationsPanel(bool &open)
{
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(1120.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Object Operations Reference", &open))
    {
        ImGui::End();
        return;
    }

    static int selectedCid = 0;
    static ImGuiTextFilter search;
    static bool actionsOnly = false;
    static bool writableOnly = false;
    static int selectedEopid = -1;

    ImGui::TextColored(ImVec4(.35f, 1.0f, .5f, 1.0f),
                       "Generated from ProjectCane CID/EOPID descriptors used by the editor");
    ImGui::TextWrapped(
        "Select a CID to see every inherited property, query, expression, and action available to that object class. @Target in examples is an editor alias; replace it with a unique object name or (find-object OID tag#0:0). The final value is ProjectCane's null search basis.");

    const std::string classPreview = BrxDocument::EditorClassName(static_cast<std::int16_t>(selectedCid)) + " (CID " +
                                     std::to_string(selectedCid) + ")";
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo("Object class", classPreview.c_str()))
    {
        for (int cid = 0; cid < static_cast<int>(CidOptionIds().size()); ++cid)
        {
            if (CidOptionIds()[static_cast<std::size_t>(cid)].empty())
                continue;
            const std::string label = BrxDocument::EditorClassName(static_cast<std::int16_t>(cid)) + " (CID " +
                                      std::to_string(cid) + ")##cid" + std::to_string(cid);
            if (ImGui::Selectable(label.c_str(), selectedCid == cid))
            {
                selectedCid = cid;
                selectedEopid = -1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    search.Draw("Search name, ID, group, type, or behavior", 360.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Actions only", &actionsOnly);
    ImGui::SameLine();
    ImGui::Checkbox("BRX-writable", &writableOnly);

    std::vector<std::int16_t> visible;
    const auto cid = static_cast<std::int16_t>(selectedCid);
    if (selectedCid >= 0 && static_cast<std::size_t>(selectedCid) < CidOptionIds().size())
    {
        for (const auto eopid : CidOptionIds()[static_cast<std::size_t>(selectedCid)])
        {
            const auto definition = EditorOptionDefinitionForCid(cid, eopid);
            if (!definition.present)
                continue;
            const auto behavior = OptionBehavior(eopid);
            if (actionsOnly && behavior != EditorOptionBehavior::Action)
                continue;
            if (writableOnly && !IsEditorWritableOptionForCid(cid, eopid))
                continue;
            const std::string haystack = std::string(PropertyNameForCid(cid, eopid)) + " " + std::to_string(eopid) +
                                         " " + OptionGroupName(OptionGroup(eopid)) + " " +
                                         PropertyKindName(definition.kind) + " " + OptionBehaviorName(behavior);
            if (!search.PassFilter(haystack.c_str()))
                continue;
            visible.push_back(eopid);
        }
    }
    ImGui::TextDisabled("%zu operation%s shown", visible.size(), visible.size() == 1 ? "" : "s");

    if (ImGui::BeginTable("ObjectOperations", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(-1.0f, 330.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Operation", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("EOPID", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Behavior", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const auto eopid : visible)
        {
            const auto definition = EditorOptionDefinitionForCid(cid, eopid);
            const auto behavior = OptionBehavior(eopid);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string selectable = std::string(PropertyNameForCid(cid, eopid)) + "##op" + std::to_string(eopid);
            if (ImGui::Selectable(selectable.c_str(), selectedEopid == eopid, ImGuiSelectableFlags_SpanAllColumns))
                selectedEopid = eopid;
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", eopid);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(OptionBehaviorName(behavior));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(PropertyKindName(definition.kind));
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(OptionGroupName(OptionGroup(eopid)));
            const auto ownerCid = PropertyOwnerCid(cid, eopid);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%s (%d)", BrxDocument::EditorClassName(ownerCid).c_str(), ownerCid);
        }
        ImGui::EndTable();
    }

    if (selectedEopid >= 0 &&
        std::find(visible.begin(), visible.end(), static_cast<std::int16_t>(selectedEopid)) != visible.end())
    {
        const auto eopid = static_cast<std::int16_t>(selectedEopid);
        const auto definition = EditorOptionDefinitionForCid(cid, eopid);
        const auto behavior = OptionBehavior(eopid);
        ImGui::SeparatorText(PropertyNameForCid(cid, eopid));
        ImGui::TextWrapped("%s", DescriptionFor(cid, eopid, definition, behavior).c_str());
        ImGui::Text("EOPID %d | %s | %s | %s", eopid, PropertyKindName(definition.kind), OptionBehaviorName(behavior),
                    OptionGroupName(OptionGroup(eopid)));
        ImGui::Text("BRX authoring: %s | Runtime default: %s",
                    IsEditorWritableOptionForCid(cid, eopid) ? "writable" : "not a stored editor override",
                    DefaultText(definition).c_str());
        if (IsObjectReferenceOption(eopid))
            ImGui::Text("Object reference contract: %s", ObjectReferenceTypeName(eopid));
        const auto example = ExampleFor(eopid, definition, behavior);
        if (!example.empty())
        {
            ImGui::Text("Splice example: %s", example.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy example"))
                ImGui::SetClipboardText(example.c_str());
        }
        else if (eopid == 347)
            ImGui::TextDisabled(
                "No runtime-write example is offered because the BARRIER keeps a post-load cached WARP pointer.");
        else if (behavior == EditorOptionBehavior::Action && definition.argumentCount != 0)
            ImGui::TextDisabled(
                "This native action has a structured argument contract; use a verified class-specific template instead of a guessed scalar call.");
        else
            ImGui::TextDisabled(
                "No scalar example is generated for this compound value type; use the property editor or a verified typed expression.");
        if (behavior == EditorOptionBehavior::Action)
            ImGui::TextColored(
                ImVec4(1.0f, .68f, .25f, 1.0f),
                "Native action: validate the target CID and test the rebuilt level; safe preview may only record the call.");
    }
    else
        ImGui::TextDisabled(
            "Select an operation for its description, default, reference contract, and Splice example.");
    ImGui::End();
}
