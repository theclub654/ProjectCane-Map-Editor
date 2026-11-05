#include "SpliceEventsPanel.h"

#include "BrxDocument.h"
#include "EditorWorkspace.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"
#include "SpliceRuntime.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
const char *SpliceOperationName(const std::int16_t cid, const std::int16_t descriptor)
{
    const auto *name = PropertyNameForCid(cid, descriptor);
    return name && name[0] != '\0' ? name : "Unnamed native operation";
}

struct SpliceSourceEditorState
{
    int requestedCursor = -1;
    int cursor = 0;
};

int SpliceSourceCursorCallback(ImGuiInputTextCallbackData *data)
{
    auto *state = static_cast<SpliceSourceEditorState *>(data->UserData);
    if (state && state->requestedCursor >= 0)
    {
        data->CursorPos = (std::min)(state->requestedCursor, data->BufTextLen);
        data->SelectionStart = data->SelectionEnd = data->CursorPos;
        state->requestedCursor = -1;
    }
    if (state)
        state->cursor = data->CursorPos;
    return 0;
}

void DrawHighlightedSpliceSource(const std::string &source, SpliceSourceEditorState &state)
{
    ImGui::BeginChild("SpliceHighlightedSource", ImVec2(-1.0f, 220.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    std::size_t lineStart = 0;
    std::uint32_t lineNumber = 1;
    while (lineStart <= source.size())
    {
        const auto lineEnd = source.find('\n', lineStart);
        const auto length = (lineEnd == std::string::npos ? source.size() : lineEnd) - lineStart;
        const auto line = std::string_view(source).substr(lineStart, length);
        ImGui::PushID(static_cast<int>(lineNumber));
        const std::string number = std::to_string(lineNumber);
        if (ImGui::Selectable(number.c_str(), false, ImGuiSelectableFlags_None, ImVec2(42.0f, 0.0f)))
            state.requestedCursor = static_cast<int>(lineStart);
        ImGui::SameLine(0.0f, 6.0f);
        std::size_t position = 0;
        bool first = true;
        while (position < line.size())
        {
            if (!first)
                ImGui::SameLine(0.0f, 0.0f);
            first = false;
            if (line[position] == ';')
            {
                ImGui::TextColored(ImVec4(.45f, .55f, .48f, 1), "%.*s", static_cast<int>(line.size() - position),
                                   line.data() + position);
                break;
            }
            if (line[position] == '(' || line[position] == ')')
            {
                ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1), "%c", line[position++]);
                continue;
            }
            const auto start = position;
            while (position < line.size() && line[position] != '(' && line[position] != ')' && line[position] != ';' &&
                   !std::isspace(static_cast<unsigned char>(line[position])))
                ++position;
            if (start == position)
            {
                while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
                    ++position;
                ImGui::TextUnformatted(line.data() + start, line.data() + position);
                continue;
            }
            const auto word = line.substr(start, position - start);
            ImVec4 color(.82f, .86f, .92f, 1);
            if (word == "true" || word == "false" || std::isdigit(static_cast<unsigned char>(word.front())) ||
                word.front() == '-' || word.front() == '+')
                color = ImVec4(.55f, .82f, 1.0f, 1);
            else if (word.front() == '$' || word.front() == '@' || word.starts_with("sym#"))
                color = word.front() == '@' ? ImVec4(.35f, .85f, 1.0f, 1) : ImVec4(.9f, .65f, 1.0f, 1);
            else if (word == "begin" || word == "define" || word == "lambda" || word == "if" || word == "set!" ||
                     word == "let" || word == "while" || word == "cond")
                color = ImVec4(1.0f, .48f, .65f, 1);
            else
                color = ImVec4(.45f, .9f, .72f, 1);
            ImGui::TextColored(color, "%.*s", static_cast<int>(word.size()), word.data());
        }
        ImGui::PopID();
        if (lineEnd == std::string::npos)
            break;
        lineStart = lineEnd + 1;
        ++lineNumber;
    }
    ImGui::EndChild();
}

const char *SpliceTagName(const std::uint8_t tag)
{
    switch (tag)
    {
    case 0:
        return "none";
    case 1:
        return "set!";
    case 2:
        return "define";
    case 3:
        return "define-macro";
    case 4:
        return "define-macro-lambda";
    case 5:
        return "assert";
    case 6:
        return "if";
    case 7:
        return "or";
    case 8:
        return "and";
    case 9:
        return "cond";
    case 10:
        return "else";
    case 11:
        return "case";
    case 12:
        return "let";
    case 13:
        return "while";
    case 14:
        return "lambda";
    case 15:
        return "begin";
    case 16:
        return "import";
    case 17:
        return "quote";
    case 18:
        return "dot";
    case 19:
        return "pipe";
    case 20:
        return "integer";
    case 21:
        return "float";
    case 22:
        return "vector";
    case 23:
        return "matrix";
    case 24:
        return "clq";
    case 25:
        return "lm";
    case 26:
        return "smp";
    case 27:
        return "boolean";
    case 28:
        return "symbol";
    case 29:
        return "built-in";
    case 30:
        return "pair";
    case 31:
        return "procedure";
    case 32:
        return "object";
    case 33:
        return "method";
    case 34:
        return "void";
    case 35:
        return "error";
    default:
        return "invalid";
    }
}

std::string FriendlySpliceNodeTitle(const BrxDocument &document, const std::int32_t nodeIndex)
{
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= document.SpliceNodes().size())
        return "Invalid expression";
    const auto &node = document.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
    const auto validNode = [&](const std::int32_t index)
    { return index >= 0 && static_cast<std::size_t>(index) < document.SpliceNodes().size(); };
    if (node.tag == 30 && validNode(node.childNode))
    {
        const auto &head = document.SpliceNodes()[static_cast<std::size_t>(node.childNode)];
        if (head.tag == 2 && validNode(head.nextNode))
        {
            const auto &symbol = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
            if (symbol.tag == 28 && symbol.intValue >= 0 && symbol.intValue <= 29)
                return std::string("Event: ") + SpliceEventName(static_cast<std::uint32_t>(symbol.intValue));
        }
        if (head.tag == 29)
            return std::string("Call: ") + SpliceBuiltinName(head.intValue);
        return "Expression";
    }
    switch (node.tag)
    {
    case 2:
        return "Define event";
    case 6:
        return "If condition";
    case 7:
        return "Any condition";
    case 8:
        return "All conditions";
    case 9:
        return "Conditional branches";
    case 12:
        return "Local values";
    case 13:
        return "Loop";
    case 14:
        return "Callback";
    case 15:
        return "Event sequence";
    case 16:
        return "Import module";
    case 20:
        return "Integer value";
    case 21:
        return "Float value";
    case 27:
        return "Boolean value";
    case 28:
        return "Named value";
    case 29:
        return std::string("Built-in: ") + SpliceBuiltinName(node.intValue);
    case 32:
        return "Object value";
    default:
        return SpliceTagName(node.tag);
    }
}

std::string FriendlySpliceNodeDetail(const BrxDocument &document, const std::int32_t nodeIndex)
{
    const auto &node = document.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
    if (node.tag == 20)
        return std::to_string(node.intValue);
    if (node.tag == 21)
        return std::to_string(node.floatValue[0]);
    if (node.tag == 27)
        return node.intValue != 0 ? "true" : "false";
    if (node.tag == 28)
        return "symbol " + std::to_string(node.intValue);
    if (node.tag == 29 && node.intValue == 101)
    {
        const auto self = node.nextNode;
        if (self >= 0 && static_cast<std::size_t>(self) < document.SpliceNodes().size())
        {
            const auto option = document.SpliceNodes()[static_cast<std::size_t>(self)].nextNode;
            if (option >= 0 && static_cast<std::size_t>(option) < document.SpliceNodes().size())
            {
                if (document.SpliceNodes()[static_cast<std::size_t>(option)].tag != 20)
                    return "Set computed native option";
                const auto eopid = document.SpliceNodes()[static_cast<std::size_t>(option)].intValue;
                const auto descriptor = SplicePropertyDescriptorForOptionId(-1, eopid);
                return descriptor >= 0 ? std::string("Set ") + SpliceOperationName(-1, descriptor)
                                       : "Set native option " + std::to_string(eopid) + " (target-dependent)";
            }
        }
    }
    if (node.tag == 29)
        return SpliceBuiltinName(node.intValue);
    return "expression node " + std::to_string(nodeIndex);
}

void DrawSpliceGraphCanvas(BrxDocument &document, EditorWorkspace &workspace, const std::int32_t moduleIndex,
                           std::string &status, std::int32_t *synchronizedSelection = nullptr)
{
    if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= document.SpliceModules().size())
        return;
    static std::int32_t activeModule = -1;
    static std::filesystem::path activeDocument;
    static std::unordered_map<std::int32_t, ImVec2> positions;
    static ImVec2 pan(24.0f, 24.0f);
    static float zoom = 1.0f;
    static std::int32_t selectedNode = -1;
    static std::int32_t pendingLinkNode = -1;
    static int pendingLinkKind = 0;
    static bool friendlyLabels = true;
    static std::size_t positionedNodeCount = 0;
    if (activeDocument != document.Path() || activeModule != moduleIndex ||
        positionedNodeCount != document.SpliceNodes().size())
    {
        activeDocument = document.Path();
        activeModule = moduleIndex;
        positions.clear();
        pan = ImVec2(24.0f, 24.0f);
        zoom = 1.0f;
        selectedNode = -1;
        pendingLinkNode = -1;
        pendingLinkKind = 0;
        positionedNodeCount = document.SpliceNodes().size();
        if (synchronizedSelection)
            *synchronizedSelection = -1;
    }
    if (synchronizedSelection && *synchronizedSelection < 0)
    {
        // Source replacement can compact/reindex nodes without changing their
        // total count. Honor the explicit reset instead of editing a stale ID.
        selectedNode = -1;
        pendingLinkNode = -1;
        pendingLinkKind = 0;
    }
    if (synchronizedSelection && *synchronizedSelection >= 0 &&
        static_cast<std::size_t>(*synchronizedSelection) < document.SpliceNodes().size())
        selectedNode = *synchronizedSelection;

    struct LayoutEntry
    {
        std::int32_t node = -1;
        int depth = 0;
    };
    std::vector<LayoutEntry> entries;
    std::vector<LayoutEntry> pending{{document.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode, 0}};
    std::unordered_set<std::int32_t> visited;
    std::unordered_map<int, int> rows;
    while (!pending.empty())
    {
        const auto entry = pending.back();
        pending.pop_back();
        if (entry.node < 0 || static_cast<std::size_t>(entry.node) >= document.SpliceNodes().size() ||
            !visited.insert(entry.node).second)
            continue;
        entries.push_back(entry);
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(entry.node)];
        pending.push_back({node.nextNode, entry.depth});
        pending.push_back({node.childNode, entry.depth + 1});
    }
    bool compactAfterNodePass = false;
    for (const auto &entry : entries)
        if (positions.contains(entry.node))
            ++rows[entry.depth];
    for (const auto &entry : entries)
        if (!positions.contains(entry.node))
            positions[entry.node] = ImVec2(190.0f * entry.depth, 76.0f * rows[entry.depth]++);

    const float inspectorWidth = 270.0f;
    const float canvasWidth = (std::max)(320.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 10.0f);
    if (ImGui::Button("Frame graph"))
    {
        pan = ImVec2(24.0f, 24.0f);
        zoom = 1.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Friendly labels", &friendlyLabels);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Wheel: zoom | Middle drag: pan | Drag nodes | Click output then input to connect | Right-click output to disconnect");
    ImGui::BeginChild("SpliceGraphCanvas", ImVec2(canvasWidth, 410.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const bool canvasHovered = ImGui::IsWindowHovered();
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        pan.x += ImGui::GetIO().MouseDelta.x;
        pan.y += ImGui::GetIO().MouseDelta.y;
    }
    if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f)
        zoom = std::clamp(zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.45f, 1.8f);
    if (pendingLinkNode >= 0 && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        pendingLinkNode = -1;
        pendingLinkKind = 0;
        status = "Cancelled splice link creation.";
    }
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvasOrigin, ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y), true);
    const float grid = 32.0f * zoom;
    for (float x = std::fmod(pan.x, grid); x < canvasSize.x; x += grid)
        draw->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y),
                      ImVec2(canvasOrigin.x + x, canvasOrigin.y + canvasSize.y), IM_COL32(45, 51, 61, 130));
    for (float y = std::fmod(pan.y, grid); y < canvasSize.y; y += grid)
        draw->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y),
                      ImVec2(canvasOrigin.x + canvasSize.x, canvasOrigin.y + y), IM_COL32(45, 51, 61, 130));
    const auto screenPosition = [&](const std::int32_t node)
    {
        const auto found = positions.find(node);
        const ImVec2 local = found == positions.end() ? ImVec2() : found->second;
        return ImVec2(canvasOrigin.x + pan.x + local.x * zoom, canvasOrigin.y + pan.y + local.y * zoom);
    };
    const ImVec2 nodeSize(158.0f * zoom, 52.0f * zoom);
    for (const auto &entry : entries)
    {
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(entry.node)];
        const ImVec2 from = screenPosition(entry.node);
        const ImVec2 fromNext(from.x + nodeSize.x, from.y + nodeSize.y * 0.68f);
        const ImVec2 fromChild(from.x + nodeSize.x, from.y + nodeSize.y * 0.32f);
        if (node.nextNode >= 0 && positions.contains(node.nextNode))
        {
            const ImVec2 to = screenPosition(node.nextNode);
            draw->AddBezierCubic(fromNext, ImVec2(fromNext.x + 45.0f * zoom, fromNext.y),
                                 ImVec2(to.x - 45.0f * zoom, to.y + nodeSize.y * 0.68f),
                                 ImVec2(to.x, to.y + nodeSize.y * 0.68f), IM_COL32(92, 210, 130, 255), 2.0f);
        }
        if (node.childNode >= 0 && positions.contains(node.childNode))
        {
            const ImVec2 to = screenPosition(node.childNode);
            draw->AddBezierCubic(fromChild, ImVec2(fromChild.x + 45.0f * zoom, fromChild.y),
                                 ImVec2(to.x - 45.0f * zoom, to.y + nodeSize.y * 0.32f),
                                 ImVec2(to.x, to.y + nodeSize.y * 0.32f), IM_COL32(75, 155, 245, 255), 2.0f);
        }
    }
    if (pendingLinkNode >= 0 && positions.contains(pendingLinkNode))
    {
        const ImVec2 source = screenPosition(pendingLinkNode);
        const ImVec2 output(source.x + nodeSize.x, source.y + nodeSize.y * (pendingLinkKind == 1 ? 0.32f : 0.68f));
        draw->AddBezierCubic(output, ImVec2(output.x + 45.0f * zoom, output.y),
                             ImVec2(ImGui::GetIO().MousePos.x - 45.0f * zoom, ImGui::GetIO().MousePos.y),
                             ImGui::GetIO().MousePos,
                             pendingLinkKind == 1 ? IM_COL32(75, 155, 245, 255) : IM_COL32(92, 210, 130, 255), 2.0f);
    }
    for (const auto &entry : entries)
    {
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(entry.node)];
        const ImVec2 topLeft = screenPosition(entry.node);
        const ImVec2 bottomRight(topLeft.x + nodeSize.x, topLeft.y + nodeSize.y);
        ImGui::SetCursorScreenPos(topLeft);
        ImGui::PushID(entry.node);
        ImGui::InvisibleButton("graphNode", nodeSize);
        if (ImGui::IsItemClicked())
        {
            selectedNode = entry.node;
            if (synchronizedSelection)
                *synchronizedSelection = entry.node;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            positions[entry.node].x += ImGui::GetIO().MouseDelta.x / zoom;
            positions[entry.node].y += ImGui::GetIO().MouseDelta.y / zoom;
        }
        const bool selected = selectedNode == entry.node;
        draw->AddRectFilled(topLeft, bottomRight, selected ? IM_COL32(44, 92, 145, 255) : IM_COL32(31, 38, 48, 255),
                            6.0f);
        draw->AddRect(topLeft, bottomRight, selected ? IM_COL32(105, 190, 255, 255) : IM_COL32(82, 95, 112, 255), 6.0f,
                      0, selected ? 2.5f : 1.0f);
        const std::string title =
            std::to_string(entry.node) + "  " +
            (friendlyLabels ? FriendlySpliceNodeTitle(document, entry.node) : SpliceTagName(node.tag));
        draw->AddText(ImVec2(topLeft.x + 8.0f, topLeft.y + 7.0f), IM_COL32_WHITE, title.c_str());
        std::string payload;
        if (friendlyLabels)
            payload = FriendlySpliceNodeDetail(document, entry.node);
        else if (node.tag == 20 || node.tag == 27 || node.tag == 28 || node.tag == 29 || node.tag == 32)
            payload = "value " + std::to_string(node.intValue);
        else
            payload = "child " + std::to_string(node.childNode) + "  next " + std::to_string(node.nextNode);
        draw->AddText(ImVec2(topLeft.x + 8.0f, topLeft.y + 29.0f), IM_COL32(180, 192, 208, 255), payload.c_str());
        const ImVec2 input(topLeft.x, topLeft.y + nodeSize.y * 0.5f);
        const ImVec2 childOutput(bottomRight.x, topLeft.y + nodeSize.y * 0.32f);
        const ImVec2 nextOutput(bottomRight.x, topLeft.y + nodeSize.y * 0.68f);
        draw->AddCircleFilled(input, 5.0f, IM_COL32(205, 210, 220, 255));
        draw->AddCircleFilled(childOutput, 5.0f, IM_COL32(75, 155, 245, 255));
        draw->AddCircleFilled(nextOutput, 5.0f, IM_COL32(92, 210, 130, 255));
        const auto nearSocket = [](const ImVec2 a, const ImVec2 b)
        {
            const float dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy <= 100.0f;
        };
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto mouse = ImGui::GetIO().MousePos;
            if (nearSocket(mouse, childOutput))
            {
                pendingLinkNode = entry.node;
                pendingLinkKind = 1;
                selectedNode = entry.node;
                if (synchronizedSelection)
                    *synchronizedSelection = entry.node;
            }
            else if (nearSocket(mouse, nextOutput))
            {
                pendingLinkNode = entry.node;
                pendingLinkKind = 2;
                selectedNode = entry.node;
                if (synchronizedSelection)
                    *synchronizedSelection = entry.node;
            }
            else if (pendingLinkNode >= 0 && nearSocket(mouse, input))
            {
                std::vector<std::int32_t> cyclePending{entry.node};
                std::unordered_set<std::int32_t> cycleVisited;
                bool createsCycle = entry.node == pendingLinkNode;
                while (!cyclePending.empty() && !createsCycle)
                {
                    const auto candidate = cyclePending.back();
                    cyclePending.pop_back();
                    if (candidate < 0 || static_cast<std::size_t>(candidate) >= document.SpliceNodes().size() ||
                        !cycleVisited.insert(candidate).second)
                        continue;
                    if (candidate == pendingLinkNode)
                    {
                        createsCycle = true;
                        break;
                    }
                    const auto &candidateNode = document.SpliceNodes()[static_cast<std::size_t>(candidate)];
                    cyclePending.push_back(candidateNode.childNode);
                    cyclePending.push_back(candidateNode.nextNode);
                }
                if (createsCycle)
                {
                    status = "Connection rejected because it would create a splice expression cycle.";
                    pendingLinkNode = -1;
                    pendingLinkKind = 0;
                    ImGui::PopID();
                    continue;
                }
                bool alreadyOwned = false;
                for (std::size_t module = 0; module < document.SpliceModules().size(); ++module)
                    if (document.SpliceModules()[module].rootNode == entry.node)
                        alreadyOwned = true;
                for (std::size_t ownerIndex = 0; ownerIndex < document.SpliceNodes().size(); ++ownerIndex)
                {
                    const auto &owner = document.SpliceNodes()[ownerIndex];
                    const bool replacingThisChild = static_cast<std::int32_t>(ownerIndex) == pendingLinkNode &&
                                                    pendingLinkKind == 1 && owner.childNode == entry.node;
                    const bool replacingThisNext = static_cast<std::int32_t>(ownerIndex) == pendingLinkNode &&
                                                   pendingLinkKind == 2 && owner.nextNode == entry.node;
                    if ((owner.childNode == entry.node && !replacingThisChild) ||
                        (owner.nextNode == entry.node && !replacingThisNext))
                        alreadyOwned = true;
                }
                if (alreadyOwned)
                {
                    status =
                        "Connection rejected: ProjectCane Splice streams are trees, so a node cannot have two incoming links.";
                    pendingLinkNode = -1;
                    pendingLinkKind = 0;
                    ImGui::PopID();
                    continue;
                }
                auto &source = document.EditableSpliceNodes()[static_cast<std::size_t>(pendingLinkNode)];
                const auto replacedNode = pendingLinkKind == 1 ? source.childNode : source.nextNode;
                if (pendingLinkKind == 1)
                    source.childNode = entry.node;
                else
                    source.nextNode = entry.node;
                if (replacedNode >= 0 && replacedNode != entry.node)
                    compactAfterNodePass = true;
                source.modified = true;
                document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)].modified = true;
                status = std::string("Connected ") + (pendingLinkKind == 1 ? "child" : "next") + " link from node " +
                         std::to_string(pendingLinkNode) + " to " + std::to_string(entry.node) + ".";
                pendingLinkNode = -1;
                pendingLinkKind = 0;
                workspace.Refresh(document);
            }
        }
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            const auto mouse = ImGui::GetIO().MousePos;
            auto &editable = document.EditableSpliceNodes()[static_cast<std::size_t>(entry.node)];
            if (nearSocket(mouse, childOutput))
            {
                editable.childNode = -1;
                editable.modified = true;
                document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)].modified = true;
                status = "Disconnected child link and removed its newly unreachable nodes.";
                compactAfterNodePass = true;
            }
            else if (nearSocket(mouse, nextOutput))
            {
                editable.nextNode = -1;
                editable.modified = true;
                document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)].modified = true;
                status = "Disconnected next link and removed its newly unreachable nodes.";
                compactAfterNodePass = true;
            }
        }
        ImGui::PopID();
    }
    if (compactAfterNodePass)
    {
        CompactSpliceNodes(document);
        workspace.Refresh(document);
        selectedNode = -1;
        pendingLinkNode = -1;
        pendingLinkKind = 0;
        if (synchronizedSelection)
            *synchronizedSelection = -1;
    }
    draw->PopClipRect();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("SpliceGraphInspector", ImVec2(0.0f, 410.0f), true);
    ImGui::TextUnformatted("Node Inspector");
    ImGui::Separator();
    if (selectedNode < 0 || static_cast<std::size_t>(selectedNode) >= document.SpliceNodes().size())
        ImGui::TextDisabled("Select a graph node.");
    else
    {
        auto &node = document.EditableSpliceNodes()[static_cast<std::size_t>(selectedNode)];
        const auto originalNode = node;
        ImGui::Text("Node %d", selectedNode);
        bool changed = false;
        int tag = node.tag;
        if (ImGui::BeginCombo("Tag", SpliceTagName(node.tag)))
        {
            for (int candidate = 0; candidate < 36; ++candidate)
                if (ImGui::Selectable(
                        (std::to_string(candidate) + " - " + SpliceTagName(static_cast<std::uint8_t>(candidate)))
                            .c_str(),
                        tag == candidate))
                {
                    tag = candidate;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (changed)
            node.tag = static_cast<std::uint8_t>(tag);
        int child = node.childNode, next = node.nextNode;
        if (node.tag == 30)
            changed |= ImGui::InputInt("Child", &child);
        else
        {
            child = -1;
            ImGui::BeginDisabled();
            ImGui::InputInt("Child", &child);
            ImGui::EndDisabled();
        }
        changed |= ImGui::InputInt("Next", &next);
        if (node.tag == 20 || node.tag == 27 || node.tag == 28 || node.tag == 29 || node.tag == 32)
            changed |= ImGui::InputInt("Value", &node.intValue);
        else if (node.tag == 21)
            changed |= ImGui::InputFloat("Value", &node.floatValue[0]);
        if (changed)
        {
            const auto validLink = [&](const int candidate)
            {
                return candidate == -1 ||
                       (candidate >= 0 && static_cast<std::size_t>(candidate) < document.SpliceNodes().size());
            };
            const auto reachesSelected = [&](const int candidate)
            {
                std::vector<std::int32_t> pending{candidate};
                std::unordered_set<std::int32_t> visited;
                while (!pending.empty())
                {
                    const auto current = pending.back();
                    pending.pop_back();
                    if (current < 0 || static_cast<std::size_t>(current) >= document.SpliceNodes().size() ||
                        !visited.insert(current).second)
                        continue;
                    if (current == selectedNode)
                        return true;
                    const auto &linked = document.SpliceNodes()[static_cast<std::size_t>(current)];
                    pending.push_back(linked.childNode);
                    pending.push_back(linked.nextNode);
                }
                return false;
            };
            const auto ownedElsewhere = [&](const int candidate, const int originalLink)
            {
                if (candidate < 0 || candidate == originalLink)
                    return false;
                for (const auto &module : document.SpliceModules())
                    if (module.rootNode == candidate)
                        return true;
                for (std::size_t ownerIndex = 0; ownerIndex < document.SpliceNodes().size(); ++ownerIndex)
                {
                    if (static_cast<std::int32_t>(ownerIndex) == selectedNode)
                        continue;
                    const auto &owner = document.SpliceNodes()[ownerIndex];
                    if (owner.childNode == candidate || owner.nextNode == candidate)
                        return true;
                }
                return false;
            };
            if (!validLink(child) || !validLink(next))
            {
                node = originalNode;
                status = "Splice edit rejected: child/next links must be -1 or an existing node index.";
                workspace.Refresh(document);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(.95f, .35f, .25f, 1), "Invalid link rejected.");
                ImGui::EndChild();
                return;
            }
            if (reachesSelected(child) || reachesSelected(next))
            {
                node = originalNode;
                status = "Splice edit rejected because it would create an expression cycle.";
                workspace.Refresh(document);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(.95f, .35f, .25f, 1), "Cycle rejected.");
                ImGui::EndChild();
                return;
            }
            if ((child >= 0 && child == next) || ownedElsewhere(child, originalNode.childNode) ||
                ownedElsewhere(next, originalNode.nextNode))
            {
                node = originalNode;
                status =
                    "Splice edit rejected: ProjectCane's recursive format cannot preserve a node with multiple incoming links.";
                workspace.Refresh(document);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(.95f, .35f, .25f, 1), "Shared-node link rejected.");
                ImGui::EndChild();
                return;
            }
            node.childNode = child;
            node.nextNode = next;
            node.modified = true;
            document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)].modified = true;
            if ((originalNode.childNode >= 0 && originalNode.childNode != child) ||
                (originalNode.nextNode >= 0 && originalNode.nextNode != next))
            {
                CompactSpliceNodes(document);
                selectedNode = -1;
                if (synchronizedSelection)
                    *synchronizedSelection = -1;
                status = "Edited splice graph node and removed the detached expression branch.";
            }
            else
                status = "Edited splice graph node " + std::to_string(selectedNode) + ".";
            workspace.Refresh(document);
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(.3f, .75f, 1, 1), "Blue: child/list head");
        ImGui::TextColored(ImVec4(.36f, .85f, .5f, 1), "Green: next/sibling");
    }
    ImGui::EndChild();
}
}

void DrawSpliceEventsPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                           std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(980.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Splice Events", &open))
    {
        ImGui::End();
        return;
    }

    static ImGuiTextFilter filter;
    static bool includeModulesWithoutEvents = false;
    static SpliceSandboxResult preview;
    static std::int32_t previewObject = -1;
    static std::uint32_t previewEvent = 0;
    static std::uint64_t previewRevision = 0;
    static int eventToAdd = 2;
    static int moduleToAttach = 0;
    static int newNodeTag = 30;
    static int actionEvent = 2;
    static int actionOption = -1;
    static bool actionBoolean = true;
    static int actionValueKind = 0;
    static int actionInteger = 0;
    static float actionFloat = 0.0f;
    static std::int32_t alternatingWarpA = -1;
    static std::int32_t alternatingWarpB = -1;
    static bool graphWindowOpen = false;
    static std::int32_t graphObject = -1;
    static std::int32_t graphModule = -1;
    static std::int32_t sourceModule = -1;
    static std::vector<char> sourceBuffer(262144, '\0');
    static bool sourceDirty = false;
    static std::string validatedSource;
    static std::uint64_t validatedRevision = 0;
    static std::int16_t validatedCid = -1;
    static SpliceSourceResult sourceValidation;
    static std::unordered_map<std::int32_t, std::string> sourceDrafts;
    static SpliceSourceEditorState sourceEditorState;
    static std::int32_t synchronizedGraphNode = -1;
    static std::int32_t sharedEditConsentModule = -1;
    static int authoringMode = 0;
    static ImGuiTextFilter designerActionFilter;
    static ImGuiTextFilter designerTargetFilter;
    static std::int32_t designerTargetObject = -1;
    static std::int16_t designerTargetProperty = -1;
    static std::int32_t designerEventFocus = -1;
    static std::filesystem::path previewDocument;
    if (previewDocument != document.Path())
    {
        previewDocument = document.Path();
        previewObject = -1;
        previewEvent = 0;
        preview = {};
        sourceModule = -1;
        sourceDirty = false;
        validatedSource.clear();
        sourceValidation = {};
        sourceDrafts.clear();
        sharedEditConsentModule = -1;
        designerTargetObject = -1;
        designerTargetProperty = -1;
        designerEventFocus = -1;
        alternatingWarpA = -1;
        alternatingWarpB = -1;
    }

    ImGui::TextColored(
        ImVec4(0.35f, 1.0f, 0.45f, 1.0f),
        "Splice edits are serialized by the from-scratch BRX writer; validation must pass before export.");
    const std::int32_t selectedObject = renderer.SelectedObjectIndex();
    const bool validSelection =
        selectedObject >= 0 && static_cast<std::size_t>(selectedObject) < document.Objects().size();
    // Computing transitive import users scans module graphs. Reuse each answer
    // until a document edit refreshes the workspace, including in-place edits
    // whose node count and module root happen to stay the same.
    const auto moduleUserCount = [&](const std::int32_t module)
    {
        static const BrxDocument *cachedDocument = nullptr;
        static const EditorWorkspace *cachedWorkspace = nullptr;
        static std::uint64_t cachedRevision = 0;
        static std::unordered_map<std::int32_t, std::size_t> counts;
        if (cachedDocument != &document || cachedWorkspace != &workspace || cachedRevision != workspace.Revision())
        {
            cachedDocument = &document;
            cachedWorkspace = &workspace;
            cachedRevision = workspace.Revision();
            counts.clear();
        }
        const auto [entry, inserted] = counts.try_emplace(module, 0);
        if (inserted)
            entry->second = SpliceModuleUserCount(document, module);
        return entry->second;
    };
    if (ImGui::CollapsingHeader("Selected object authoring", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!validSelection)
            ImGui::TextDisabled("Select a world object to attach or edit splice logic.");
        else
        {
            const auto &selected = document.Objects()[static_cast<std::size_t>(selectedObject)];
            const std::int32_t moduleIndex = selected.spliceIndex;
            const bool validModule =
                moduleIndex >= 0 && static_cast<std::size_t>(moduleIndex) < document.SpliceModules().size();
            ImGui::Text("%s", document.ObjectEditorLabel(selectedObject).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s / OID %d", BrxDocument::EditorClassName(selected.cid).c_str(), selected.oid);
            ImGui::SameLine();
            ImGui::TextDisabled("splice %d", moduleIndex);
            const auto apply = [&](const SpliceAuthoringResult &result)
            {
                status = result.message;
                if (result.succeeded)
                {
                    previewObject = -1;
                    synchronizedGraphNode = -1;
                    workspace.Refresh(document);
                }
            };
            if (!validModule)
            {
                if (ImGui::Button("Create empty module"))
                    apply(CreateSpliceModule(document, selectedObject));
            }
            else
            {
                const auto users = moduleUserCount(moduleIndex);
                const auto analysis = AnalyzeSpliceModule(document, moduleIndex);
                ImGui::Text("%zu potential user%s | %u reachable nodes | depth %u", users, users == 1 ? "" : "s",
                            analysis.nodeCount, analysis.maximumDepth);
                if (users > 1)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f),
                                       "Shared module: edits may affect %zu objects through attachments/imports. Use Make unique for isolated changes.",
                                       users);
                    bool consent = sharedEditConsentModule == moduleIndex;
                    if (ImGui::Checkbox(("Enable shared editing for all " + std::to_string(users) + " users").c_str(),
                                        &consent))
                        sharedEditConsentModule = consent ? moduleIndex : -1;
                }
                const bool sharedLocked = users > 1 && sharedEditConsentModule != moduleIndex;
                if (ImGui::Button("Open visual graph window"))
                {
                    graphWindowOpen = true;
                    graphObject = selectedObject;
                    graphModule = moduleIndex;
                    status = "Opened visual graph for splice module " + std::to_string(moduleIndex) + ".";
                }
                ImGui::SameLine();
                if (ImGui::Button("Make unique"))
                    apply(MakeSpliceModuleUnique(document, selectedObject));
                ImGui::SameLine();
                if (ImGui::Button("Detach"))
                    apply(DetachSpliceModule(document, selectedObject));
                ImGui::SameLine();
                ImGui::BeginDisabled(sharedLocked);
                if (ImGui::Button("Repair invalid imports"))
                    apply(RepairSpliceImports(document, moduleIndex));

                ImGui::SeparatorText("Authoring mode");
                if (ImGui::Selectable("Designer", authoringMode == 0, 0, ImVec2(110, 0)))
                    authoringMode = 0;
                ImGui::SameLine();
                if (ImGui::Selectable("Source", authoringMode == 1, 0, ImVec2(110, 0)))
                    authoringMode = 1;
                ImGui::SameLine();
                if (ImGui::Selectable("Raw", authoringMode == 2, 0, ImVec2(110, 0)))
                    authoringMode = 2;
                ImGui::TextDisabled(
                    "Designer builds common behavior; Source accepts manual Splice; Raw preserves exact ProjectCane nodes.");

                if (authoringMode == 0)
                {
                    ImGui::SeparatorText("Designer overview");
                    if (users > 1 && ImGui::TreeNode("Shared module users", "Shared module users (%zu)", users))
                    {
                        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
                        {
                            const auto &user = document.Objects()[objectIndex];
                            if (user.spliceIndex != moduleIndex)
                                continue;
                            ImGui::PushID(static_cast<int>(objectIndex));
                            if (ImGui::SmallButton("Select"))
                                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                            ImGui::SameLine();
                            ImGui::Text("%s",
                                        document.ObjectEditorLabel(static_cast<std::int32_t>(objectIndex)).c_str());
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s / OID %d", BrxDocument::EditorClassName(user.cid).c_str(),
                                                user.oid);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    if (ImGui::BeginTable("DesignerEventCards", 4,
                                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("Event");
                        ImGui::TableSetupColumn("Origin");
                        ImGui::TableSetupColumn("Status");
                        ImGui::TableSetupColumn("Action");
                        ImGui::TableHeadersRow();
                        for (const auto eventId : analysis.eventIds)
                        {
                            const auto origin =
                                std::find_if(analysis.eventOrigins.begin(), analysis.eventOrigins.end(),
                                             [eventId](const auto &candidate) { return candidate.first == eventId; });
                            ImGui::PushID(static_cast<int>(eventId));
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(SpliceEventName(eventId));
                            ImGui::TableSetColumnIndex(1);
                            if (origin == analysis.eventOrigins.end())
                                ImGui::TextDisabled("Unknown");
                            else
                                ImGui::Text("Module %d", origin->second);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(origin != analysis.eventOrigins.end() && origin->second == moduleIndex
                                                   ? ImVec4(.35f, 1, .45f, 1)
                                                   : ImVec4(.45f, .7f, 1, 1),
                                               "%s",
                                               origin != analysis.eventOrigins.end() && origin->second == moduleIndex
                                                   ? "Local"
                                                   : "Imported");
                            ImGui::TableSetColumnIndex(3);
                            if (ImGui::SmallButton("Edit"))
                                designerEventFocus = static_cast<std::int32_t>(eventId);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Preview"))
                            {
                                preview = EvaluateSpliceEventSandbox(document, moduleIndex, eventId, selectedObject);
                                previewObject = selectedObject;
                                previewRevision = workspace.Revision();
                                previewEvent = eventId;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }

                    ImGui::SeparatorText("Object and property picker");
                    if (ImGui::Button("Use viewport selection as target"))
                        designerTargetObject = renderer.SelectedObjectIndex();
                    designerTargetFilter.Draw("Filter target by class or OID", 360.0f);
                    const char *targetPreview = "Choose target object";
                    std::string targetPreviewStorage;
                    if (designerTargetObject >= 0 &&
                        static_cast<std::size_t>(designerTargetObject) < document.Objects().size())
                    {
                        const auto &target = document.Objects()[static_cast<std::size_t>(designerTargetObject)];
                        targetPreviewStorage = document.ObjectEditorLabel(designerTargetObject) + "  (OID " +
                                               std::to_string(target.oid) + ")";
                        targetPreview = targetPreviewStorage.c_str();
                    }
                    ImGui::SetNextItemWidth(360.0f);
                    if (ImGui::BeginCombo("Target object", targetPreview))
                    {
                        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
                        {
                            const auto &candidate = document.Objects()[objectIndex];
                            const std::string visibleLabel =
                                document.ObjectEditorLabel(static_cast<std::int32_t>(objectIndex)) + "  " +
                                BrxDocument::EditorClassName(candidate.cid) + " OID " + std::to_string(candidate.oid);
                            if (!designerTargetFilter.PassFilter(visibleLabel.c_str()))
                                continue;
                            const std::string label = visibleLabel + "##target" + std::to_string(objectIndex);
                            if (ImGui::Selectable(label.c_str(),
                                                  designerTargetObject == static_cast<std::int32_t>(objectIndex)))
                                designerTargetObject = static_cast<std::int32_t>(objectIndex);
                        }
                        ImGui::EndCombo();
                    }
                    if (designerTargetObject >= 0 &&
                        static_cast<std::size_t>(designerTargetObject) < document.Objects().size())
                    {
                        const auto &target = document.Objects()[static_cast<std::size_t>(designerTargetObject)];
                        const auto &targetOptions =
                            target.cid >= 0 && static_cast<std::size_t>(target.cid) < CidOptionIds().size()
                                ? CidOptionIds()[static_cast<std::size_t>(target.cid)]
                                : CidOptionIds()[0];
                        if (!targetOptions.empty() && std::find(targetOptions.begin(), targetOptions.end(),
                                                                designerTargetProperty) == targetOptions.end())
                            designerTargetProperty = targetOptions.front();
                        if (!targetOptions.empty())
                        {
                            ImGui::SetNextItemWidth(360.0f);
                            if (ImGui::BeginCombo("Target property",
                                                  SpliceOperationName(target.cid, designerTargetProperty)))
                            {
                                for (const auto eopid : targetOptions)
                                    if (ImGui::Selectable((std::string(SpliceOperationName(target.cid, eopid)) +
                                                           "##targetProperty" + std::to_string(eopid))
                                                              .c_str(),
                                                          designerTargetProperty == eopid))
                                        designerTargetProperty = eopid;
                                ImGui::EndCombo();
                            }
                        }
                        // Numeric lookup also works for duplicate editor names
                        // and names requiring brace escaping.
                        const bool simpleAlias = !target.editorName.empty() &&
                            document.FindObjectByEditorName(target.editorName) == designerTargetObject &&
                            std::all_of(target.editorName.begin(), target.editorName.end(), [](const unsigned char ch)
                            { return std::isalnum(ch) || ch == '_' || ch == '-'; });
                        const std::string findSnippet = simpleAlias ? "@" + target.editorName
                            : "(find-object " + std::to_string(target.oid) + " tag#0:0)";
                        if (ImGui::Button(simpleAlias ? "Copy @Name reference" : "Copy Find Object source"))
                            ImGui::SetClipboardText(findSnippet.c_str());
                        ImGui::SameLine();
                        const std::string getSnippet =
                            "(geto " + findSnippet + " " + SplicePropertyToken(target.cid, designerTargetProperty) + ")";
                        const auto targetFlags = SplicePropertyRuntimeFlags(designerTargetProperty);
                        ImGui::BeginDisabled(targetOptions.empty() || (targetFlags & 0xFu) == 0 ||
                                             SplicePropertyOptionId(designerTargetProperty) >= 1173);
                        if (ImGui::Button("Copy Get Property source"))
                            ImGui::SetClipboardText(getSnippet.c_str());
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Select target in world"))
                            renderer.SelectObject(designerTargetObject);
                    }

                    ImGui::SeparatorText("Searchable action palette");
                    designerActionFilter.Draw("Search events or actions", 360.0f);
                    const auto paletteButton = [&](const char *label, const std::uint32_t eventId)
                    {
                        if (!designerActionFilter.PassFilter(label))
                            return;
                        if (ImGui::Button(label))
                            apply(AddSpliceEventStub(document, selectedObject, eventId));
                        ImGui::SameLine();
                    };
                    paletteButton("On Post-load", 0);
                    paletteButton("On Trigger", 2);
                    paletteButton("On Touch", 6);
                    paletteButton("On Death", 8);
                    paletteButton("On Animation frame", 10);
                    ImGui::NewLine();
                    if (designerActionFilter.PassFilter("Set Property"))
                        ImGui::TextDisabled("Set Property is available in Friendly callback action below.");
                    if (designerActionFilter.PassFilter("Return true false"))
                        ImGui::TextDisabled("Return controls are shown on each event below.");

                    ImGui::SeparatorText("Collapsed behavior flow");
                    if (analysis.builtinCalls.empty() && analysis.optionReads.empty() && analysis.optionWrites.empty())
                        ImGui::TextDisabled("No recognized native actions; raw expressions are preserved in Raw mode.");
                    for (const auto builtin : analysis.builtinCalls)
                        ImGui::BulletText("Call %s", SpliceBuiltinName(builtin));
                    for (const auto option : analysis.optionReads)
                    {
                        const auto descriptor = SplicePropertyDescriptorForOptionId(-1, option);
                        ImGui::BulletText("Read %s (native option %d)",
                            descriptor >= 0 ? SpliceOperationName(-1, descriptor) : "target-dependent operation", option);
                    }
                    for (const auto option : analysis.optionWrites)
                    {
                        const auto descriptor = SplicePropertyDescriptorForOptionId(-1, option);
                        ImGui::BulletText("Set %s (native option %d)",
                            descriptor >= 0 ? SpliceOperationName(-1, descriptor) : "target-dependent operation", option);
                    }

                    std::unordered_set<std::int32_t> designerSymbols;
                    std::unordered_set<std::int32_t> designerFormalSymbols;
                    std::vector<std::pair<std::int32_t, std::int32_t>> inferredObjects;
                    std::vector<std::int32_t> designerPending{
                        document.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode};
                    std::unordered_set<std::int32_t> designerVisited;
                    while (!designerPending.empty())
                    {
                        const auto nodeIndex = designerPending.back();
                        designerPending.pop_back();
                        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= document.SpliceNodes().size() ||
                            !designerVisited.insert(nodeIndex).second)
                            continue;
                        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
                        if (node.tag == 28)
                            designerSymbols.insert(node.intValue);
                        if (node.tag == 14 && node.nextNode >= 0 &&
                            static_cast<std::size_t>(node.nextNode) < document.SpliceNodes().size())
                        {
                            const auto &formals = document.SpliceNodes()[static_cast<std::size_t>(node.nextNode)];
                            auto formal = formals.tag == 30 ? formals.childNode : node.nextNode;
                            std::unordered_set<std::int32_t> formalVisited;
                            while (formal >= 0 && static_cast<std::size_t>(formal) < document.SpliceNodes().size() &&
                                   formalVisited.insert(formal).second)
                            {
                                const auto &formalNode = document.SpliceNodes()[static_cast<std::size_t>(formal)];
                                if (formalNode.tag == 28)
                                    designerFormalSymbols.insert(formalNode.intValue);
                                formal = formalNode.nextNode;
                            }
                        }
                        if (node.tag == 30 && node.childNode >= 0 &&
                            static_cast<std::size_t>(node.childNode) < document.SpliceNodes().size())
                        {
                            const auto &head = document.SpliceNodes()[static_cast<std::size_t>(node.childNode)];
                            if (head.tag == 29 && head.intValue == 103 && head.nextNode >= 0 &&
                                static_cast<std::size_t>(head.nextNode) < document.SpliceNodes().size())
                            {
                                const auto &oid = document.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
                                if (oid.tag == 20)
                                    inferredObjects.emplace_back(nodeIndex, oid.intValue);
                            }
                        }
                        designerPending.push_back(node.childNode);
                        designerPending.push_back(node.nextNode);
                    }
                    if (ImGui::TreeNode("Variables and inferred targets"))
                    {
                        for (const auto symbol : designerSymbols)
                            ImGui::BulletText("sym#%d  %s", symbol,
                                              symbol < 30 ? SpliceEventName(symbol)
                                                          : (designerFormalSymbols.contains(symbol)
                                                                 ? "callback parameter"
                                                                 : "local or imported symbol"));
                        for (const auto [nodeIndex, oid] : inferredObjects)
                        {
                            const auto found =
                                std::find_if(document.Objects().begin(), document.Objects().end(),
                                             [oid](const BrxObjectRecord &candidate) { return candidate.oid == oid; });
                            if (found == document.Objects().end())
                                ImGui::BulletText("find-object OID %d (not present in decoded world)", oid);
                            else
                                ImGui::BulletText("find-object OID %d -> %s", oid,
                                                  BrxDocument::EditorClassName(found->cid));
                        }
                        ImGui::TreePop();
                    }
                    ImGui::SeparatorText("Events and templates");
                    ImGui::SetNextItemWidth(240.0f);
                    if (ImGui::BeginCombo("Event to add", SpliceEventName(static_cast<std::uint32_t>(eventToAdd))))
                    {
                        for (int eventId = 0; eventId < 30; ++eventId)
                            if (ImGui::Selectable(
                                    (std::string(SpliceEventName(eventId)) + "##event" + std::to_string(eventId))
                                        .c_str(),
                                    eventToAdd == eventId))
                                eventToAdd = eventId;
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("Add safe callback"))
                        apply(AddSpliceEventStub(document, selectedObject, static_cast<std::uint32_t>(eventToAdd)));
                    ImGui::SameLine();
                    if (ImGui::Button("Template: Trigger"))
                        apply(AddSpliceEventStub(document, selectedObject, 2));
                    ImGui::SameLine();
                    if (ImGui::Button("Template: Touch"))
                        apply(AddSpliceEventStub(document, selectedObject, 6));
                    ImGui::SameLine();
                    if (ImGui::Button("Template: Death"))
                        apply(AddSpliceEventStub(document, selectedObject, 8));
                    ImGui::TextDisabled(
                        "Templates create a runnable ProjectCane callback with a self formal and harmless true body; actions remain sandboxed.");
                    if (ImGui::TreeNode("Alternating WARP template"))
                    {
                        std::vector<std::int32_t> warps;
                        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
                            if (CidDerivesFrom(document.Objects()[objectIndex].cid, 116))
                                warps.push_back(static_cast<std::int32_t>(objectIndex));
                        const auto drawWarpCombo = [&](const char *label, std::int32_t &selection)
                        {
                            const std::string previewLabel =
                                selection >= 0 && static_cast<std::size_t>(selection) < document.Objects().size()
                                    ? document.ObjectEditorLabel(selection)
                                    : "Choose WARP";
                            ImGui::SetNextItemWidth(360.0f);
                            if (ImGui::BeginCombo(label, previewLabel.c_str()))
                            {
                                for (const auto objectIndex : warps)
                                {
                                    const auto &warp = document.Objects()[static_cast<std::size_t>(objectIndex)];
                                    const std::string item = document.ObjectEditorLabel(objectIndex) + " (OID " +
                                                             std::to_string(warp.oid) + ")##warp" +
                                                             std::to_string(objectIndex);
                                    if (ImGui::Selectable(item.c_str(), selection == objectIndex))
                                        selection = objectIndex;
                                }
                                ImGui::EndCombo();
                            }
                        };
                        drawWarpCombo("Destination A", alternatingWarpA);
                        drawWarpCombo("Destination B", alternatingWarpB);
                        ImGui::TextWrapped(
                            "Each Trigger [2] calls ProjectCane's real WARP Trigger property (EOPID 162), alternating A, B, A, B. The controller's current splice module is preserved through an import.");
                        const bool validWarpIndices =
                            alternatingWarpA >= 0 && alternatingWarpB >= 0 &&
                            static_cast<std::size_t>(alternatingWarpA) < document.Objects().size() &&
                            static_cast<std::size_t>(alternatingWarpB) < document.Objects().size();
                        const bool contextualTargets =
                            validWarpIndices &&
                            workspace.Scene().FindObjectFrom(
                                document.Objects()[static_cast<std::size_t>(alternatingWarpA)].oid, selectedObject) ==
                                alternatingWarpA &&
                            workspace.Scene().FindObjectFrom(
                                document.Objects()[static_cast<std::size_t>(alternatingWarpB)].oid, selectedObject) ==
                                alternatingWarpB;
                        if (validWarpIndices && !contextualTargets)
                            ImGui::TextColored(
                                ImVec4(1.0f, .55f, .2f, 1.0f),
                                "A selected OID resolves differently from this controller's PROXY/PXR context.");
                        ImGui::BeginDisabled(!contextualTargets || alternatingWarpA == alternatingWarpB);
                        if (ImGui::Button("Create alternating WARP Trigger script"))
                            apply(CreateAlternatingWarpTemplate(document, selectedObject, alternatingWarpA,
                                                                alternatingWarpB));
                        ImGui::EndDisabled();
                        ImGui::TreePop();
                    }
                    if (!analysis.eventIds.empty())
                    {
                        if (designerEventFocus < 0 ||
                            std::find(analysis.eventIds.begin(), analysis.eventIds.end(),
                                      static_cast<std::uint32_t>(designerEventFocus)) == analysis.eventIds.end())
                            designerEventFocus = static_cast<std::int32_t>(analysis.eventIds.front());
                        ImGui::SetNextItemWidth(260.0f);
                        if (ImGui::BeginCombo("Focused event",
                                              designerEventFocus < 0
                                                  ? "None"
                                                  : SpliceEventName(static_cast<std::uint32_t>(designerEventFocus))))
                        {
                            for (const auto eventId : analysis.eventIds)
                                if (ImGui::Selectable(
                                        (std::string(SpliceEventName(eventId)) + "##focus" + std::to_string(eventId))
                                            .c_str(),
                                        designerEventFocus == static_cast<std::int32_t>(eventId)))
                                    designerEventFocus = static_cast<std::int32_t>(eventId);
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("Only this callback's controls are expanded below.");
                    }
                    for (const auto eventId : analysis.eventIds)
                    {
                        if (designerEventFocus >= 0 && eventId != static_cast<std::uint32_t>(designerEventFocus))
                            continue;
                        ImGui::PushID(static_cast<int>(eventId));
                        ImGui::BulletText("%s (%u)", SpliceEventName(eventId), eventId);
                        const auto origin =
                            std::find_if(analysis.eventOrigins.begin(), analysis.eventOrigins.end(),
                                         [eventId](const auto &candidate) { return candidate.first == eventId; });
                        if (origin != analysis.eventOrigins.end())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("from splice %d%s", origin->second,
                                                origin->second == moduleIndex ? " (local)" : " (imported)");
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Preview"))
                        {
                            preview = EvaluateSpliceEventSandbox(document, moduleIndex, eventId, selectedObject);
                            previewObject = selectedObject;
                            previewRevision = workspace.Revision();
                            previewEvent = eventId;
                            status = preview.completed ? "Safe splice preview completed."
                                                       : "Safe splice preview stopped with diagnostics.";
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove direct definition"))
                            apply(RemoveSpliceEvent(document, selectedObject, eventId));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Return true"))
                            apply(SetSpliceEventBooleanResult(document, selectedObject, eventId, true));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Return false"))
                            apply(SetSpliceEventBooleanResult(document, selectedObject, eventId, false));
                        ImGui::PopID();
                    }

                    ImGui::SeparatorText("Friendly callback action");
                    ImGui::TextWrapped(
                        "Insert a verified (seto! self EOPID value) call before the callback's existing return expression.");
                    if (!analysis.eventIds.empty())
                    {
                        if (std::find(analysis.eventIds.begin(), analysis.eventIds.end(),
                                      static_cast<std::uint32_t>(actionEvent)) == analysis.eventIds.end())
                            actionEvent = static_cast<int>(analysis.eventIds.front());
                        ImGui::SetNextItemWidth(220.0f);
                        if (ImGui::BeginCombo("Callback", SpliceEventName(static_cast<std::uint32_t>(actionEvent))))
                        {
                            for (const auto eventId : analysis.eventIds)
                                if (ImGui::Selectable(
                                        (std::string(SpliceEventName(eventId)) + "##action" + std::to_string(eventId))
                                            .c_str(),
                                        actionEvent == static_cast<int>(eventId)))
                                    actionEvent = static_cast<int>(eventId);
                            ImGui::EndCombo();
                        }
                        static const std::vector<std::int16_t> emptyOptionIds;
                        const auto &optionIds =
                            selected.cid >= 0 && static_cast<std::size_t>(selected.cid) < CidOptionIds().size()
                                ? CidOptionIds()[static_cast<std::size_t>(selected.cid)]
                                : emptyOptionIds;
                        std::vector<std::int16_t> scalarOptionIds;
                        scalarOptionIds.reserve(optionIds.size());
                        for (const auto eopid : optionIds)
                        {
                            const auto definition = EditorOptionDefinitionForCid(selected.cid, eopid);
                            const auto flags = SplicePropertyRuntimeFlags(eopid);
                            const auto kind = definition.kind;
                            if (kind == BrxPropertyKind::Bool || kind == BrxPropertyKind::Float ||
                                kind == BrxPropertyKind::Int16 || kind == BrxPropertyKind::TypedInt16 ||
                                kind == BrxPropertyKind::Int32 || kind == BrxPropertyKind::UInt32)
                            {
                                if (!definition.present || OptionGroup(eopid) == EditorOptionGroup::Identity ||
                                    (flags & 0x70u) == 0 || (flags & 1u) != 0 ||
                                    SplicePropertyOptionId(eopid) >= 1173)
                                    continue;
                                scalarOptionIds.push_back(eopid);
                            }
                        }
                        if (!scalarOptionIds.empty())
                        {
                            if (std::find(scalarOptionIds.begin(), scalarOptionIds.end(), actionOption) ==
                                scalarOptionIds.end())
                                actionOption = scalarOptionIds.front();
                            const char *optionName =
                                PropertyNameForCid(selected.cid, static_cast<std::int16_t>(actionOption));
                            const std::string optionPreview = std::string(optionName ? optionName : "Option") + " (" +
                                                              std::to_string(actionOption) + ")";
                            ImGui::SetNextItemWidth(360.0f);
                            if (ImGui::BeginCombo("Property", optionPreview.c_str()))
                            {
                                for (const auto eopid : scalarOptionIds)
                                {
                                    const char *name = PropertyNameForCid(selected.cid, eopid);
                                    const std::string label =
                                        std::string(name ? name : "Option") + " (" + std::to_string(eopid) + ")";
                                    if (ImGui::Selectable(label.c_str(), actionOption == eopid))
                                        actionOption = eopid;
                                }
                                ImGui::EndCombo();
                            }
                            const auto selectedKind =
                                EditorOptionDefinitionForCid(selected.cid, static_cast<std::int16_t>(actionOption))
                                    .kind;
                            actionValueKind = selectedKind == BrxPropertyKind::Bool    ? 0
                                              : selectedKind == BrxPropertyKind::Float ? 2
                                                                                       : 1;
                            ImGui::TextDisabled("Value type: %s", PropertyKindName(selectedKind));
                            if (actionValueKind == 0)
                                ImGui::Checkbox("Value", &actionBoolean);
                            else if (actionValueKind == 1)
                                ImGui::InputInt("Value", &actionInteger);
                            else
                                ImGui::InputFloat("Value", &actionFloat);
                            if (ImGui::Button("Add Set Property action"))
                            {
                                const auto eventId = static_cast<std::uint32_t>(actionEvent);
                                const auto eopid = static_cast<std::int16_t>(actionOption);
                                if (actionValueKind == 0)
                                    apply(PrependSpliceSetSelfBoolean(document, selectedObject, eventId, eopid,
                                                                      actionBoolean));
                                else if (actionValueKind == 1)
                                    apply(PrependSpliceSetSelfInteger(document, selectedObject, eventId, eopid,
                                                                      actionInteger));
                                else
                                    apply(PrependSpliceSetSelfFloat(document, selectedObject, eventId, eopid,
                                                                    actionFloat));
                            }
                        }
                        else
                            ImGui::TextDisabled(
                                "The selected CID exposes no safe scalar script properties for the friendly action builder.");
                    }
                    else
                        ImGui::TextDisabled("Add a callback before creating friendly actions.");

                    std::size_t animationSpliceEvents = 0;
                    for (const auto &segment : document.AnimationSegments())
                        if (segment.ownerObjectIndex == selectedObject)
                            animationSpliceEvents += static_cast<std::size_t>(
                                std::count_if(segment.events.begin(), segment.events.end(),
                                              [](const BrxAsegEventRecord &event) { return event.type == 40; }));
                    const auto dialogSpliceEvents = static_cast<std::size_t>(std::count_if(
                        selected.dialog.events.begin(), selected.dialog.events.end(),
                        [](const BrxDialogEvent &event) { return event.kind == BrxDialogEventKind::CallSplice; }));
                    if (animationSpliceEvents != 0 || dialogSpliceEvents != 0 || selected.cid == 131)
                    {
                        ImGui::SeparatorText("Serialized event links");
                        if (selected.cid == 131)
                            ImGui::BulletText("ASEG lifecycle dispatches Apply [9] and Retract [11].");
                        if (animationSpliceEvents != 0)
                            ImGui::BulletText("%zu animation event%s dispatch Animation frame [10].",
                                              animationSpliceEvents, animationSpliceEvents == 1 ? "" : "s");
                        if (dialogSpliceEvents != 0)
                            ImGui::BulletText("%zu dialog CallSplice event%s dispatch Dialog label [16].",
                                              dialogSpliceEvents, dialogSpliceEvents == 1 ? "" : "s");
                    }

                    if (ImGui::TreeNode("Dependencies and call graph"))
                    {
                        for (const auto imported : analysis.imports)
                            ImGui::BulletText("imports splice %d%s", imported,
                                              imported >= 0 && static_cast<std::size_t>(imported) <
                                                                   document.SpliceModules().size()
                                                  ? ""
                                                  : " (missing)");
                        for (const auto builtin : analysis.builtinCalls)
                            ImGui::BulletText("calls %s [%d]", SpliceBuiltinName(builtin), builtin);
                        for (const auto option : analysis.optionReads)
                            ImGui::BulletText("reads option %d", option);
                        for (const auto option : analysis.optionWrites)
                            ImGui::BulletText("writes option %d", option);
                        for (const auto token : analysis.basicTokens)
                            ImGui::BulletText("serialized BASIC token 0x%08X", static_cast<unsigned>(token));
                        for (const auto &diagnostic : analysis.diagnostics)
                            ImGui::TextColored(ImVec4(1.0f, .5f, .2f, 1.0f), "%s", diagnostic.c_str());
                        ImGui::TreePop();
                    }
                }
                if (authoringMode == 2 && ImGui::CollapsingHeader("Visual node graph", ImGuiTreeNodeFlags_DefaultOpen))
                    DrawSpliceGraphCanvas(document, workspace, moduleIndex, status, &synchronizedGraphNode);
                if (authoringMode == 1 && ImGui::CollapsingHeader("Source code", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextWrapped(
                        "Edit a temporary source buffer, validate it, then apply it to the same expression tree used by the graph. "
                        "Invalid source never changes the live module.");
                    ImGui::TextDisabled(
                        "Use qualified property names from Help, such as ALO.Hidden or ALO.Frozen. Raw native option IDs remain supported.");
                    ImGui::TextDisabled(
                        "Editor-only object aliases use @Name and compile to the current world's native (find-object OID null-basis) expression.");
                    if (sourceModule != moduleIndex)
                    {
                        if (sourceModule >= 0 && sourceDirty)
                            sourceDrafts[sourceModule] = std::string(sourceBuffer.data());
                        validatedSource.clear();
                        sourceValidation = {};
                        sourceModule = moduleIndex;
                        if (const auto draft = sourceDrafts.find(moduleIndex); draft != sourceDrafts.end())
                        {
                            if (sourceBuffer.size() <= draft->second.size())
                                sourceBuffer.resize(draft->second.size() + 65536, '\0');
                            std::fill(sourceBuffer.begin(), sourceBuffer.end(), '\0');
                            std::copy(draft->second.begin(), draft->second.end(), sourceBuffer.begin());
                            sourceDirty = true;
                            status = "Restored the unapplied source draft for splice module " +
                                     std::to_string(moduleIndex) + ".";
                        }
                        else
                            sourceDirty = false;
                    }
                    if (!sourceDirty)
                    {
                        const auto generated = FormatSpliceModuleSource(document, moduleIndex);
                        if (sourceBuffer.size() <= generated.size())
                            sourceBuffer.resize(generated.size() + 65536, '\0');
                        std::fill(sourceBuffer.begin(), sourceBuffer.end(), '\0');
                        std::copy(generated.begin(), generated.end(), sourceBuffer.begin());
                    }
                    ImGui::InputTextMultiline("##SpliceSource", sourceBuffer.data(), sourceBuffer.size(),
                                              ImVec2(-1.0f, 300.0f),
                                              ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
                                              SpliceSourceCursorCallback, &sourceEditorState);
                    if (ImGui::IsItemEdited())
                    {
                        sourceDirty = true;
                        sourceDrafts[moduleIndex] = std::string(sourceBuffer.data());
                        validatedSource.clear();
                        sourceValidation = {};
                    }
                    const std::string currentSource(sourceBuffer.data());
                    std::vector<std::int32_t> sourceTraversal;
                    std::vector<std::int32_t> traversalPending{
                        document.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode};
                    std::unordered_set<std::int32_t> traversalVisited;
                    while (!traversalPending.empty())
                    {
                        const auto node = traversalPending.back();
                        traversalPending.pop_back();
                        if (node < 0 || static_cast<std::size_t>(node) >= document.SpliceNodes().size() ||
                            !traversalVisited.insert(node).second)
                            continue;
                        sourceTraversal.push_back(node);
                        const auto &expression = document.SpliceNodes()[static_cast<std::size_t>(node)];
                        traversalPending.push_back(expression.nextNode);
                        traversalPending.push_back(expression.childNode);
                    }
                    if (!sourceDirty)
                    {
                        const auto navigation = ValidateSpliceModuleSource(
                            currentSource, selected.cid, document.WorldHeader().symbolCount, &document);
                        if (navigation.succeeded && navigation.nodeLocations.size() == sourceTraversal.size())
                        {
                            if (ImGui::Button("Graph selection -> source"))
                            {
                                const auto found =
                                    std::find(sourceTraversal.begin(), sourceTraversal.end(), synchronizedGraphNode);
                                if (found != sourceTraversal.end())
                                {
                                    const auto ordinal = static_cast<std::size_t>(found - sourceTraversal.begin());
                                    const auto [targetLine, targetColumn] = navigation.nodeLocations[ordinal];
                                    std::size_t offset = 0;
                                    std::uint32_t line = 1;
                                    while (offset < currentSource.size() && line < targetLine)
                                        if (currentSource[offset++] == '\n')
                                            ++line;
                                    offset += targetColumn > 0 ? targetColumn - 1 : 0;
                                    sourceEditorState.requestedCursor =
                                        static_cast<int>((std::min)(offset, currentSource.size()));
                                }
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Source cursor -> graph"))
                            {
                                std::uint32_t cursorLine = 1, cursorColumn = 1;
                                for (int index = 0; index < sourceEditorState.cursor &&
                                                    static_cast<std::size_t>(index) < currentSource.size();
                                     ++index)
                                {
                                    if (currentSource[index] == '\n')
                                    {
                                        ++cursorLine;
                                        cursorColumn = 1;
                                    }
                                    else
                                        ++cursorColumn;
                                }
                                std::size_t best = 0;
                                for (std::size_t index = 0; index < navigation.nodeLocations.size(); ++index)
                                {
                                    const auto [line, column] = navigation.nodeLocations[index];
                                    if (line < cursorLine || (line == cursorLine && column <= cursorColumn))
                                        best = index;
                                }
                                synchronizedGraphNode = sourceTraversal[best];
                            }
                        }
                    }
                    const auto cursor = static_cast<std::size_t>(
                        (std::clamp)(sourceEditorState.cursor, 0, static_cast<int>(currentSource.size())));
                    std::size_t wordStart = cursor;
                    while (wordStart > 0 && !std::isspace(static_cast<unsigned char>(currentSource[wordStart - 1])) &&
                           currentSource[wordStart - 1] != '(' && currentSource[wordStart - 1] != ')')
                        --wordStart;
                    const auto completionPrefix = currentSource.substr(wordStart, cursor - wordStart);
                    if (!completionPrefix.empty())
                    {
                        std::vector<std::string> completions;
                        const auto addCompletion = [&](const std::string &candidate)
                        {
                            const auto dot = candidate.find('.');
                            const bool matches = candidate.starts_with(completionPrefix) ||
                                (dot != std::string::npos && candidate.substr(dot + 1).starts_with(completionPrefix));
                            if (matches && candidate != completionPrefix &&
                                std::find(completions.begin(), completions.end(), candidate) == completions.end() &&
                                completions.size() < 8)
                                completions.push_back(candidate);
                        };
                        for (std::int32_t builtin = 0; builtin < 512; ++builtin)
                        {
                            const auto name = std::string(SpliceBuiltinName(builtin));
                            if (name == "invalid built-in")
                                break;
                            addCompletion(name);
                        }
                        for (const auto eventId : analysis.eventIds)
                        {
                            const std::string eventName = SpliceEventName(eventId);
                            addCompletion(eventName.find_first_of(" \t") == std::string::npos
                                              ? eventName
                                              : "sym#" + std::to_string(eventId));
                        }
                        if (selected.cid >= 0 && static_cast<std::size_t>(selected.cid) < CidOptionIds().size())
                            for (const auto eopid : CidOptionIds()[static_cast<std::size_t>(selected.cid)])
                            {
                                addCompletion(SplicePropertyToken(selected.cid, eopid));
                            }
                        for (std::size_t cid = 0; cid < CidOptionIds().size(); ++cid)
                        {
                            const auto prefix = std::string(BrxDocument::ClassName(static_cast<std::int16_t>(cid))) + ".";
                            if (completionPrefix.find('.') == std::string::npos)
                                addCompletion(prefix);
                            if (completionPrefix.starts_with(prefix))
                                for (const auto eopid : CidOptionIds()[cid])
                                    addCompletion(SplicePropertyToken(static_cast<std::int16_t>(cid), eopid));
                        }
                        if (completionPrefix.front() == '$')
                        {
                            addCompletion("$self");
                            addCompletion("$target");
                            addCompletion("$value");
                        }
                        if (completionPrefix.front() == '@')
                            for (const auto &candidate : document.Objects())
                                if (!candidate.editorName.empty())
                                    addCompletion("@" + candidate.editorName);
                        if (!completions.empty())
                        {
                            ImGui::TextDisabled("Completions:");
                            ImGui::SameLine();
                            for (std::size_t completionIndex = 0; completionIndex < completions.size();
                                 ++completionIndex)
                            {
                                if (completionIndex != 0)
                                    ImGui::SameLine();
                                if (ImGui::SmallButton((completions[completionIndex] + "##completion" +
                                                        std::to_string(completionIndex))
                                                           .c_str()))
                                {
                                    auto replaced = currentSource;
                                    replaced.replace(wordStart, cursor - wordStart, completions[completionIndex]);
                                    if (sourceBuffer.size() <= replaced.size())
                                        sourceBuffer.resize(replaced.size() + 65536, '\0');
                                    std::fill(sourceBuffer.begin(), sourceBuffer.end(), '\0');
                                    std::copy(replaced.begin(), replaced.end(), sourceBuffer.begin());
                                    sourceEditorState.requestedCursor =
                                        static_cast<int>(wordStart + completions[completionIndex].size());
                                    sourceDirty = true;
                                    validatedSource.clear();
                                    sourceValidation = {};
                                }
                            }
                        }
                    }
                    int parenthesisDepth = 0;
                    std::uint32_t unmatchedCloseLine = 0, scanLine = 1;
                    bool inComment = false;
                    for (const auto ch : currentSource)
                    {
                        if (ch == '\n')
                        {
                            ++scanLine;
                            inComment = false;
                            continue;
                        }
                        if (inComment)
                            continue;
                        if (ch == ';')
                        {
                            inComment = true;
                            continue;
                        }
                        if (ch == '(')
                            ++parenthesisDepth;
                        else if (ch == ')' && --parenthesisDepth < 0 && unmatchedCloseLine == 0)
                            unmatchedCloseLine = scanLine;
                    }
                    if (parenthesisDepth != 0 || unmatchedCloseLine != 0)
                        ImGui::TextColored(
                            ImVec4(1, .5f, .25f, 1), "Parentheses are unbalanced%s.",
                            unmatchedCloseLine ? (" near line " + std::to_string(unmatchedCloseLine)).c_str() : "");
                    if (ImGui::Button("Validate"))
                    {
                        sourceValidation = ValidateSpliceModuleSource(currentSource, selected.cid,
                                                                      document.WorldHeader().symbolCount, &document);
                        validatedSource = sourceValidation.succeeded ? currentSource : std::string();
                        validatedRevision = workspace.Revision();
                        validatedCid = selected.cid;
                        status = sourceValidation.succeeded
                                     ? "Splice source validated without changing the graph."
                                     : "Splice source validation failed; the live graph was not changed.";
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Check generated source coverage"))
                    {
                        sourceValidation = VerifySpliceModuleSourceRoundTrip(document, moduleIndex, selected.cid);
                        validatedSource.clear();
                        status = sourceValidation.succeeded
                                     ? "The current module passed exact source round-trip verification."
                                     : "This module did not pass exact source round-trip verification.";
                    }
                    ImGui::SameLine();
                    const bool canApply = sourceValidation.succeeded && validatedSource == currentSource &&
                                          validatedRevision == workspace.Revision() && validatedCid == selected.cid &&
                                          (users <= 1 || sharedEditConsentModule == moduleIndex);
                    if (!validatedSource.empty() &&
                        (validatedRevision != workspace.Revision() || validatedCid != selected.cid))
                        ImGui::TextDisabled("The scene or CID context changed; validate the source again before applying.");
                    ImGui::BeginDisabled(!canApply);
                    if (ImGui::Button("Apply to graph"))
                    {
                        sourceValidation = ApplySpliceModuleSource(document, moduleIndex, currentSource, selected.cid);
                        if (sourceValidation.succeeded)
                        {
                            workspace.Refresh(document);
                            sourceDirty = false;
                            sourceDrafts.erase(moduleIndex);
                            synchronizedGraphNode = -1;
                            validatedSource.clear();
                            status = "Applied validated Splice source to module " + std::to_string(moduleIndex) + ".";
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Revert from graph"))
                    {
                        sourceDirty = false;
                        sourceDrafts.erase(moduleIndex);
                        validatedSource.clear();
                        sourceValidation = {};
                        status = "Discarded the source buffer and restored it from the graph.";
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled(sourceDirty ? "Source has unapplied edits." : "Source matches the graph.");
                    if (sourceValidation.succeeded)
                        ImGui::TextColored(ImVec4(.35f, 1.0f, .45f, 1.0f),
                                           "Valid: %u expression nodes. Apply is enabled.", sourceValidation.nodeCount);
                    for (std::size_t diagnosticIndex = 0; diagnosticIndex < sourceValidation.diagnostics.size();
                         ++diagnosticIndex)
                    {
                        const auto &diagnostic = sourceValidation.diagnostics[diagnosticIndex];
                        const std::string label = "Line " + std::to_string(diagnostic.line) + ", column " +
                                                  std::to_string(diagnostic.column) + ": " + diagnostic.message +
                                                  "###SpliceDiagnostic" + std::to_string(diagnosticIndex);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, .45f, .25f, 1.0f));
                        if (ImGui::Selectable(label.c_str()))
                        {
                            std::size_t offset = 0;
                            std::uint32_t line = 1;
                            while (offset < currentSource.size() && line < diagnostic.line)
                                if (currentSource[offset++] == '\n')
                                    ++line;
                            offset += diagnostic.column > 0 ? diagnostic.column - 1 : 0;
                            sourceEditorState.requestedCursor =
                                static_cast<int>((std::min)(offset, currentSource.size()));
                            ImGui::SetKeyboardFocusHere(-1);
                        }
                        ImGui::PopStyleColor();
                    }
                    if (ImGui::CollapsingHeader("Highlighted source map"))
                        DrawHighlightedSpliceSource(currentSource, sourceEditorState);
                }
                if (authoringMode == 2 && ImGui::TreeNode("Advanced expression tree"))
                {
                    ImGui::TextWrapped(
                        "Raw expression editing mirrors ProjectCane's CPair graph. "
                        "Invalid links and cycles are preserved for diagnosis, never followed indefinitely.");
                    auto &editableModule = document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)];
                    int rootNode = editableModule.rootNode;
                    if (ImGui::InputInt("Module root node", &rootNode))
                    {
                        editableModule.rootNode = rootNode;
                        editableModule.modified = true;
                        previewObject = -1;
                        workspace.Refresh(document);
                        status = "Edited splice module root in memory.";
                    }
                    ImGui::SetNextItemWidth(230.0f);
                    if (ImGui::BeginCombo("New node tag", SpliceTagName(static_cast<std::uint8_t>(newNodeTag))))
                    {
                        for (int candidate = 0; candidate < 36; ++candidate)
                        {
                            const std::string candidateLabel =
                                std::to_string(candidate) + " - " + SpliceTagName(static_cast<std::uint8_t>(candidate));
                            if (ImGui::Selectable(candidateLabel.c_str(), newNodeTag == candidate))
                                newNodeTag = candidate;
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("Append reachable raw expression"))
                        apply(AppendSpliceTopLevelNode(document, moduleIndex, static_cast<std::uint8_t>(newNodeTag)));
                    ImGui::SameLine();
                    if (ImGui::Button("Remove unreachable nodes"))
                    {
                        const auto before = document.SpliceNodes().size();
                        CompactSpliceNodes(document);
                        const auto removed = before - document.SpliceNodes().size();
                        workspace.Refresh(document);
                        status = "Removed " + std::to_string(removed) + " unreachable splice node(s).";
                    }
                    std::vector<std::int32_t> pending{
                        document.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode};
                    std::unordered_set<std::int32_t> visited;
                    while (!pending.empty())
                    {
                        const auto nodeIndex = pending.back();
                        pending.pop_back();
                        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= document.SpliceNodes().size() ||
                            !visited.insert(nodeIndex).second)
                            continue;
                        const auto snapshot = document.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
                        pending.push_back(snapshot.nextNode);
                        pending.push_back(snapshot.childNode);
                        ImGui::PushID(nodeIndex);
                        const std::string label = "node " + std::to_string(nodeIndex) + ": " +
                                                  SpliceTagName(snapshot.tag) +
                                                  (snapshot.editorCreated ? " (editor-created)"
                                                   : snapshot.modified    ? " (modified)"
                                                                          : "");
                        if (ImGui::TreeNode("node", "%s", label.c_str()))
                        {
                            auto &node = document.EditableSpliceNodes()[static_cast<std::size_t>(nodeIndex)];
                            bool changed = false;
                            int tag = node.tag;
                            ImGui::SetNextItemWidth(230.0f);
                            if (ImGui::BeginCombo("Tag", SpliceTagName(node.tag)))
                            {
                                for (int candidate = 0; candidate < 36; ++candidate)
                                {
                                    const std::string candidateLabel =
                                        std::to_string(candidate) + " - " +
                                        SpliceTagName(static_cast<std::uint8_t>(candidate));
                                    if (ImGui::Selectable(candidateLabel.c_str(), tag == candidate))
                                    {
                                        tag = candidate;
                                        changed = true;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            if (changed)
                                node.tag = static_cast<std::uint8_t>(tag);
                            int child = node.childNode, next = node.nextNode;
                            changed |= ImGui::InputInt("Child node (-1 = none)", &child);
                            changed |= ImGui::InputInt("Next node (-1 = none)", &next);
                            node.childNode = child;
                            node.nextNode = next;
                            if (node.tag == 20 || node.tag == 27 || node.tag == 28 || node.tag == 29 || node.tag == 32)
                                changed |= ImGui::InputInt("Integer payload", &node.intValue);
                            else if (node.tag == 21)
                                changed |= ImGui::InputFloat("Float payload", &node.floatValue[0]);
                            else if (node.tag == 22 || node.tag == 24 || node.tag == 26)
                                changed |= ImGui::InputFloat3("Vector payload", node.floatValue);
                            else if (node.tag == 25)
                                changed |= ImGui::InputFloat2("LM payload", node.floatValue);
                            else if (node.tag == 23)
                            {
                                for (int row = 0; row < 4; ++row)
                                {
                                    ImGui::PushID(row);
                                    changed |= ImGui::InputFloat4("Matrix row", node.floatValue + row * 4);
                                    ImGui::PopID();
                                }
                            }
                            if (changed)
                            {
                                node.modified = true;
                                document.EditableSpliceModules()[static_cast<std::size_t>(moduleIndex)].modified = true;
                                previewObject = -1;
                                workspace.Refresh(document);
                                status = "Edited splice expression node " + std::to_string(nodeIndex) + " in memory.";
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::EndDisabled();
            }
            ImGui::SeparatorText("Attach existing module");
            if (!document.SpliceModules().empty())
            {
                moduleToAttach = std::clamp(moduleToAttach, 0, static_cast<int>(document.SpliceModules().size()) - 1);
                ImGui::SetNextItemWidth(140.0f);
                ImGui::InputInt("Module", &moduleToAttach);
                moduleToAttach = std::clamp(moduleToAttach, 0, static_cast<int>(document.SpliceModules().size()) - 1);
                ImGui::SameLine();
                if (ImGui::Button("Attach module"))
                    apply(AttachSpliceModule(document, selectedObject, moduleToAttach));
                ImGui::SameLine();
                const bool importSharedLocked = validModule && moduleUserCount(moduleIndex) > 1 &&
                                                sharedEditConsentModule != moduleIndex;
                ImGui::BeginDisabled(!validModule || importSharedLocked);
                if (ImGui::Button("Import into selected module"))
                    apply(AddSpliceImport(document, moduleIndex, moduleToAttach));
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Attach shares an entire module with this object. Import adds it as a ProjectCane lexical parent of the selected module.");
            }
        }
    }

    std::size_t scriptedObjects = 0;
    std::size_t eventObjects = 0;
    std::size_t eventBindings = 0;
    std::unordered_set<std::int32_t> usedModules;
    std::vector<SpliceModuleAnalysis> moduleAnalyses(document.SpliceModules().size());
    std::vector<bool> moduleAnalyzed(document.SpliceModules().size(), false);
    const auto analysisFor = [&](const std::int32_t spliceIndex) -> const SpliceModuleAnalysis &
    {
        const auto moduleIndex = static_cast<std::size_t>(spliceIndex);
        if (!moduleAnalyzed[moduleIndex])
        {
            moduleAnalyses[moduleIndex] = AnalyzeSpliceModule(document, spliceIndex);
            moduleAnalyzed[moduleIndex] = true;
        }
        return moduleAnalyses[moduleIndex];
    };
    for (const auto &object : document.Objects())
    {
        if (object.spliceIndex < 0 || static_cast<std::size_t>(object.spliceIndex) >= document.SpliceModules().size())
            continue;
        ++scriptedObjects;
        usedModules.insert(object.spliceIndex);
        const auto &analysis = analysisFor(object.spliceIndex);
        if (!analysis.eventIds.empty())
            ++eventObjects;
        eventBindings += analysis.eventIds.size();
    }

    ImGui::Text("%zu object%s with recognized events", eventObjects, eventObjects == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("| %zu bindings | %zu scripted objects | %zu used modules", eventBindings, scriptedObjects,
                        usedModules.size());
    filter.Draw("Search objects, OIDs or events", 360.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Include modules without recognized events", &includeModulesWithoutEvents);
    ImGui::Separator();

    if (ImGui::BeginTable("SpliceEventObjects", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, previewObject >= 0 ? -185.0f : -1.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 1.35f);
        ImGui::TableSetupColumn("Splice", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("Events", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Dependencies", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
        {
            const auto &object = document.Objects()[objectIndex];
            if (object.spliceIndex < 0 ||
                static_cast<std::size_t>(object.spliceIndex) >= document.SpliceModules().size())
                continue;
            const auto &analysis = analysisFor(object.spliceIndex);
            if (analysis.eventIds.empty() && !includeModulesWithoutEvents)
                continue;

            std::string events;
            for (const auto eventId : analysis.eventIds)
            {
                if (!events.empty())
                    events += ", ";
                events += SpliceEventName(eventId);
                events += " [" + std::to_string(eventId) + "]";
            }
            if (events.empty())
                events = "No recognized callback";
            const std::string objectName = document.ObjectEditorLabel(static_cast<std::int32_t>(objectIndex));
            const std::string searchable = objectName + " CID " + std::to_string(object.cid) + " OID " +
                                           std::to_string(object.oid) + " splice " +
                                           std::to_string(object.spliceIndex) + " " + events;
            if (!filter.PassFilter(searchable.c_str()))
                continue;

            ImGui::PushID(static_cast<int>(objectIndex));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = renderer.SelectedObjectIndex() == static_cast<std::int32_t>(objectIndex);
            if (ImGui::Selectable((objectName + "##object").c_str(), selected))
            {
                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                status = "Selected " + objectName + " from Splice Events.";
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", object.spliceIndex);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", events.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%zu import%s", analysis.imports.size(), analysis.imports.size() == 1 ? "" : "s");
            ImGui::TextDisabled("%zu read / %zu write", analysis.optionReads.size(), analysis.optionWrites.size());
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("Open Graph"))
            {
                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                graphWindowOpen = true;
                graphObject = static_cast<std::int32_t>(objectIndex);
                graphModule = object.spliceIndex;
                status = "Opened visual graph for " + objectName + ".";
            }
            for (const auto eventId : analysis.eventIds)
            {
                ImGui::PushID(static_cast<int>(eventId));
                if (ImGui::SmallButton((std::string("Preview ") + SpliceEventName(eventId)).c_str()))
                {
                    preview = EvaluateSpliceEventSandbox(document, object.spliceIndex, eventId,
                                                         static_cast<std::int32_t>(objectIndex));
                    previewObject = static_cast<std::int32_t>(objectIndex);
                    previewRevision = workspace.Revision();
                    previewEvent = eventId;
                    status = preview.completed ? "Safe splice preview completed."
                                               : "Safe splice preview stopped with diagnostics.";
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (previewObject >= 0 && previewRevision != workspace.Revision())
    {
        previewObject = -1;
        preview = {};
        status = "The document changed. Run the Splice preview again for the current scene.";
    }
    if (previewObject >= 0 && static_cast<std::size_t>(previewObject) < document.Objects().size())
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(previewObject)];
        ImGui::SeparatorText("Last safe preview");
        ImGui::Text("%s OID %d | %s | %u instructions", BrxDocument::ClassName(object.cid), object.oid,
                    SpliceEventName(previewEvent), preview.instructions);
        if (preview.approximated)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), "| safe approximation");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear preview"))
        {
            previewObject = -1;
            preview = {};
        }
        for (const auto &line : preview.trace)
            ImGui::BulletText("%s", line.c_str());
        for (const auto &action : preview.nativeActions)
            ImGui::BulletText("Sandboxed action: %s", action.c_str());
        for (const auto &diagnostic : preview.diagnostics)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "%s", diagnostic.c_str());
    }
    ImGui::End();

    if (graphWindowOpen)
    {
        const bool validGraphObject =
            graphObject >= 0 && static_cast<std::size_t>(graphObject) < document.Objects().size();
        if (validGraphObject)
        {
            const auto attachedModule = document.Objects()[static_cast<std::size_t>(graphObject)].spliceIndex;
            if (attachedModule >= 0)
                graphModule = attachedModule;
        }
        const bool validGraphModule =
            graphModule >= 0 && static_cast<std::size_t>(graphModule) < document.SpliceModules().size();
        if (!validGraphObject || !validGraphModule)
            graphWindowOpen = false;
        else
        {
            ImGui::SetNextWindowSize(ImVec2(1100.0f, 560.0f), ImGuiCond_FirstUseEver);
            const auto &graphOwner = document.Objects()[static_cast<std::size_t>(graphObject)];
            const std::string title =
                "Splice Graph - " + document.ObjectEditorLabel(graphObject) + "###SpliceGraphWindow";
            if (ImGui::Begin(title.c_str(), &graphWindowOpen))
            {
                ImGui::Text("Module %d", graphModule);
                ImGui::SameLine();
                ImGui::TextDisabled("Edits use the same nodes serialized by the BRX writer.");
                const auto graphUsers = moduleUserCount(graphModule);
                const bool graphLocked = graphUsers > 1 && sharedEditConsentModule != graphModule;
                if (graphLocked)
                    ImGui::TextColored(
                        ImVec4(1, .65f, .25f, 1),
                        "Shared module is view-only. Enable shared editing in Splice Events or make it unique.");
                ImGui::BeginDisabled(graphLocked);
                DrawSpliceGraphCanvas(document, workspace, graphModule, status, &synchronizedGraphNode);
                ImGui::EndDisabled();
            }
            ImGui::End();
        }
    }
}
