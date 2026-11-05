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
const char *OperationName(const std::int16_t cid, const std::int16_t eopid)
{
    const auto *name = PropertyNameForCid(cid, eopid);
    return name && name[0] != '\0' ? name : "Unnamed native operation";
}

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

std::string ExampleFor(const std::int16_t cid, const std::int16_t eopid, const EditorOptionDefinition &definition)
{
    const auto token = SplicePropertyToken(cid, eopid);
    const auto flags = SplicePropertyRuntimeFlags(eopid);
    if (token.empty() || SplicePropertyOptionId(eopid) >= 1173)
        return {};
    if ((flags & 1u) != 0)
    {
        std::string call = "((geto @Target " + token + ")";
        for (std::uint16_t argument = 0; argument < definition.argumentCount; ++argument)
            call += " $argument" + std::to_string(argument + 1);
        return call + ")";
    }
    if ((flags & 0x70u) == 0)
        return (flags & 0xEu) != 0 ? "(geto @Target " + token + ")" : std::string{};
    const auto literal = ValueLiteral(definition.kind);
    if (literal.empty())
        return "(seto! @Target " + token + " $typedValue)";
    return "(seto! @Target " + token + " " + literal + ")";
}

const char *VerifiedNote(const std::int16_t eopid)
{
    switch (eopid)
    {
    case 28:
        return "Reads the object's frozen-state Boolean through GetAloFrozen. This descriptor has no Splice setter: use geto, not seto!. Reading it does not freeze or thaw the object.";
    case 125:
        return "Controls the ALO hidden flag through SetAloHidden/GetAloHidden. Set true to hide the object or false to show it. This changes the hidden flag; it does not delete the object or remove its script.";
    case 162:
        return "WARP action: TriggerWarp teleports the player and dispatches the WARP Trigger event.";
    case 347:
        return "BARRIER destination is resolved and cached during PostBarrierLoad. Changing only this OID at runtime does not refresh the cached WARP pointer.";
    default:
        return nullptr;
    }
}

std::string DescriptionFor(const std::int16_t cid, const std::int16_t eopid, const EditorOptionDefinition &definition)
{
    if (const auto *note = VerifiedNote(eopid))
        return note;
    const auto flags = SplicePropertyRuntimeFlags(eopid);
    std::string description = std::string(OperationName(cid, eopid)) + " belongs to " +
                              OptionGroupName(OptionGroup(eopid)) + " and has native value type " +
                              PropertyKindName(definition.kind) + ". ";
    if (SplicePropertyOptionId(eopid) >= 1173)
        description += "Its lookup ID reaches the native sidebag boundary. geto/seto! use a custom sidebag binding at this ID, not this descriptor's field or method.";
    else if ((flags & 1u) != 0)
        description += "geto returns a native method. Call it with " + std::to_string(definition.argumentCount) +
                       " argument(s) using the outer call shown below. A method may query state or perform a gameplay action; it is not a property setter.";
    else
    {
        description += (flags & 0xEu) != 0 ? "geto reads its current native value. " : "No native Splice getter is exposed. ";
        description += (flags & 0x70u) != 0 ? "seto! changes it using a value of the stated type. " : "No native Splice setter is exposed. ";
    }
    description += " " + SplicePropertyDetails(eopid);
    description += " Descriptor access does not imply a sandbox simulation of its gameplay effects.";
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
            const std::string haystack = std::string(OperationName(cid, eopid)) + " " + std::to_string(eopid) +
                                         " " + SplicePropertyToken(cid, eopid) + " " + std::to_string(SplicePropertyOptionId(eopid)) +
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
            const std::string selectable = std::string(OperationName(cid, eopid)) + "##op" + std::to_string(eopid);
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
        ImGui::SeparatorText(OperationName(cid, eopid));
        ImGui::TextWrapped("%s", DescriptionFor(cid, eopid, definition).c_str());
        ImGui::Text("EOPID %d | %s | %s | %s", eopid, PropertyKindName(definition.kind), OptionBehaviorName(behavior),
                    OptionGroupName(OptionGroup(eopid)));
        const auto token = SplicePropertyToken(cid, eopid);
        ImGui::Text("Source token: %s | Native script option ID: %d", token.c_str(), SplicePropertyOptionId(eopid));
        if (ImGui::Button("Copy property name"))
            ImGui::SetClipboardText(token.c_str());
        ImGui::TextWrapped("The EOPID above is the descriptor index. Native scripts use the option ID, which can differ. Qualified source names resolve to the option ID. @Target must be an object exposing this operation; replace $argumentN/$typedValue placeholders with correctly typed arguments.");
        if (ImGui::CollapsingHeader("Advanced native binding"))
            ImGui::TextWrapped("BuildEopids, spaprops.cpp: %s", SplicePropertyBinding(eopid).c_str());
        ImGui::Text("BRX authoring: %s | Runtime default: %s",
                    IsEditorWritableOptionForCid(cid, eopid) ? "writable" : "not a stored editor override",
                    DefaultText(definition).c_str());
        if (IsObjectReferenceOption(eopid))
            ImGui::Text("Object reference contract: %s", ObjectReferenceTypeName(eopid));
        const auto example = ExampleFor(cid, eopid, definition);
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
