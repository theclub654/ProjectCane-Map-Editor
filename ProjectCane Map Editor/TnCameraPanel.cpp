#include "TnCameraPanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorDocumentOperations.h"
#include "EditorWorkspace.h"
#include "SceneRenderer.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
enum class TnKindFilter
{
    All,
    StaticFrame,
    LinearPath,
    CubicPath
};

struct TnDiagnostic
{
    bool error = false;
    std::string text;
};

const BrxPropertyRecord *FindProperty(const BrxObjectRecord &object, std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.rbegin(), object.properties.rend(),
                                    [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.rend() ? nullptr : &*found;
}

int IntProperty(const BrxObjectRecord &object, std::int16_t eopid, int fallback)
{
    const auto *property = FindProperty(object, eopid);
    return property != nullptr ? property->intValue : fallback;
}

float FloatProperty(const BrxObjectRecord &object, std::int16_t eopid, float fallback)
{
    const auto *property = FindProperty(object, eopid);
    return property != nullptr ? property->floatValue[0] : fallback;
}

const char *CurveKindName(const BrxTnPayload &tn)
{
    if (!tn.present)
        return "Payload unavailable";
    if (tn.curveKind < 0)
        return "Static camera frame";
    if (tn.curveKind == 0)
        return "Linear camera path";
    if (tn.curveKind == 1)
        return "Cubic camera path";
    return "Invalid camera path";
}

bool MatchesKind(const BrxTnPayload &tn, TnKindFilter filter)
{
    if (filter == TnKindFilter::All)
        return true;
    if (!tn.present)
        return false;
    if (filter == TnKindFilter::StaticFrame)
        return tn.curveKind == -1;
    if (filter == TnKindFilter::LinearPath)
        return tn.curveKind == 0;
    return tn.curveKind == 1;
}

std::string DirectionSummary(int flags)
{
    std::string result;
    const auto add = [&](const char *name)
    {
        if (!result.empty())
            result += " + ";
        result += name;
    };
    if ((flags & 1) != 0)
        add("Forward");
    if ((flags & 2) != 0)
        add("Reverse");
    if ((flags & 4) != 0)
        add("Manual");
    return result.empty() ? "None" : result;
}

std::vector<TnDiagnostic> ValidateTn(const BrxObjectRecord &object)
{
    std::vector<TnDiagnostic> out;
    const auto error = [&](std::string text) { out.push_back({true, std::move(text)}); };
    const auto warning = [&](std::string text) { out.push_back({false, std::move(text)}); };
    const auto &tn = object.tn;
    if (!tn.present)
    {
        error("TN class payload was not decoded.");
        return out;
    }
    if (tn.surfaces.empty() || tn.bspNodes.empty())
        error("Trigger BSP is empty; volume containment cannot activate this TN.");
    if (tn.surfaces.size() > 65535u)
        error("Trigger BSP exceeds the 16-bit BRX plane count.");
    if (tn.bspNodes.size() > 32768u)
        error("Trigger BSP exceeds the signed 16-bit child-index range.");
    for (std::size_t i = 0; i < tn.surfaces.size(); ++i)
    {
        const auto &surface = tn.surfaces[i];
        const float length2 = surface.normal[0] * surface.normal[0] + surface.normal[1] * surface.normal[1] +
                              surface.normal[2] * surface.normal[2];
        if (!std::isfinite(surface.planeDistance) || !std::isfinite(length2) || length2 <= 0.000001f)
            error("Trigger plane " + std::to_string(i) + " has a non-finite or zero normal.");
    }
    for (std::size_t i = 0; i < tn.bspNodes.size(); ++i)
    {
        const auto &node = tn.bspNodes[i];
        if (node.surfaceIndex >= tn.surfaces.size())
            error("BSP node " + std::to_string(i) + " references an invalid plane.");
        for (const auto child : {node.negativeChild, node.positiveChild})
            if (child < -1 || child >= static_cast<std::int32_t>(tn.bspNodes.size()))
                error("BSP node " + std::to_string(i) + " references an invalid child.");
            else if (child == static_cast<std::int32_t>(i))
                error("BSP node " + std::to_string(i) + " references itself.");
    }
    if (!tn.bspNodes.empty())
    {
        std::vector<std::uint8_t> state(tn.bspNodes.size(), 0);
        bool cycleReported = false;
        std::function<void(std::int32_t)> visit = [&](std::int32_t index)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= tn.bspNodes.size())
                return;
            auto &mark = state[static_cast<std::size_t>(index)];
            if (mark == 1)
            {
                if (!cycleReported)
                    error("Trigger BSP has a cycle reachable from node 0.");
                cycleReported = true;
                return;
            }
            if (mark == 2)
                return;
            mark = 1;
            const auto &node = tn.bspNodes[static_cast<std::size_t>(index)];
            visit(node.negativeChild);
            visit(node.positiveChild);
            mark = 2;
        };
        visit(0);
    }
    if (tn.curveKind < -1 || tn.curveKind > 1)
        error("Camera curve kind is invalid.");
    else if (tn.curveKind >= 0)
    {
        if (tn.curvePoints.size() < 2)
            error("Camera path requires at least two points.");
        if (tn.curvePoints.size() > 255u)
            error("Camera path exceeds the 8-bit BRX point count.");
        float previous = -std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < tn.curvePoints.size(); ++i)
        {
            const auto &point = tn.curvePoints[i];
            if (!std::isfinite(point.parameter) || point.parameter <= previous)
                error("Path parameters must be finite and strictly increasing at point " + std::to_string(i) + ".");
            previous = point.parameter;
            for (const float component : point.position)
                if (!std::isfinite(component))
                {
                    error("Path point " + std::to_string(i) + " is non-finite.");
                    break;
                }
            if (tn.curveKind == 1)
            {
                for (const float component : point.incomingTangent)
                    if (!std::isfinite(component))
                    {
                        error("Cubic tangent " + std::to_string(i) + " is non-finite.");
                        break;
                    }
                for (const float component : point.outgoingTangent)
                    if (!std::isfinite(component))
                    {
                        error("Cubic tangent " + std::to_string(i) + " is non-finite.");
                        break;
                    }
            }
        }
    }
    else
    {
        for (const float component : tn.staticFrame)
            if (!std::isfinite(component))
            {
                error("Static camera frame has a non-finite value.");
                break;
            }
        const glm::mat4 frame = glm::make_mat4(tn.staticFrame);
        if (glm::dot(glm::vec3(frame[0]), glm::vec3(frame[0])) < 0.000001f ||
            glm::dot(glm::vec3(frame[1]), glm::vec3(frame[1])) < 0.000001f ||
            glm::dot(glm::vec3(frame[2]), glm::vec3(frame[2])) < 0.000001f)
            error("Static camera frame has a collapsed basis axis.");
    }
    const int directions = IntProperty(object, 867, tn.curveKind >= 0 ? 7 : 5);
    if ((directions & ~7) != 0 || (directions & 7) == 0)
        error("Directions must enable Forward, Reverse, or Manual with no unknown bits.");
    const int priority = IntProperty(object, 870, 0);
    // SetTnTns maps this directly onto CPP_Tunnel_Pri0..CPP_Tunnel_Pri3.
    // ProjectCane therefore supports four tunnel-policy priorities, not three.
    if (priority < 0 || priority > 3)
        error("Camera priority must be 0, 1, 2, or 3.");
    const int state = IntProperty(object, 884, -1);
    if (state < -1 || state > 1)
        error("State override must be Automatic, Outside, or Inside.");
    const float fov = FloatProperty(object, 878, 0.0f);
    if (!std::isfinite(fov) || (fov != 0.0f && (fov <= 0.0f || fov >= 3.141592654f)))
        error("FOV override must be zero or between 0 and pi radians.");
    for (const auto id : {std::int16_t{851}, std::int16_t{859}, std::int16_t{860}})
        if (const auto *value = FindProperty(object, id); value != nullptr && !std::isfinite(value->floatValue[0]))
            error("A signed camera offset is non-finite.");
    for (const auto id : {std::int16_t{858}, std::int16_t{873}, std::int16_t{874}, std::int16_t{875}, std::int16_t{876},
                          std::int16_t{885}, std::int16_t{886}})
        if (const auto *value = FindProperty(object, id); value != nullptr && !std::isfinite(value->floatValue[0]))
            error("A TN pan, slack, or spring control is non-finite.");
    if (FindProperty(object, 864) != nullptr && FindProperty(object, 865) != nullptr)
        warning("Focus Ratio and Focus Scale are both serialized; property order decides the result.");
    if (FindProperty(object, 880) != nullptr || FindProperty(object, 883) != nullptr)
        error("A runtime-owned TN pointer/state property was serialized.");
    return out;
}

void FrameTn(const BrxObjectRecord &object, EditorCamera &camera)
{
    const glm::mat4 objectToWorld = glm::make_mat4(object.worldTransform);
    glm::vec3 minimum(object.worldTransform[12], object.worldTransform[13], object.worldTransform[14]);
    glm::vec3 maximum = minimum;
    const auto include = [&](const glm::vec3 &point)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    };
    if (object.tn.present && object.tn.curveKind >= 0)
    {
        for (const auto &point : object.tn.curvePoints)
            include(
                glm::vec3(objectToWorld * glm::vec4(point.position[0], point.position[1], point.position[2], 1.0f)));
    }
    else if (object.tn.present)
    {
        const glm::mat4 frame = objectToWorld * glm::make_mat4(object.tn.staticFrame);
        include(glm::vec3(frame[3]));
    }
    const glm::vec3 padding(180.0f);
    camera.FrameBounds(minimum - padding, maximum + padding);
}
}

void DrawTnCameraPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                       EditorWorkspace &workspace, TnCameraDisplaySettings &display, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(860.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("TN Camera Manager", &open))
    {
        ImGui::End();
        return;
    }

    std::size_t tnCount = 0;
    std::size_t decodedCount = 0;
    std::size_t issueCount = 0;
    for (const auto &object : document.Objects())
    {
        if (object.cid != 99)
            continue;
        ++tnCount;
        if (object.tn.present)
            ++decodedCount;
        if (!ValidateTn(object).empty())
            ++issueCount;
    }

    ImGui::TextDisabled("SCENE CAMERA SYSTEM");
    ImGui::Text("%zu camera target node%s", tnCount, tnCount == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu decoded | %zu with issues", decodedCount, issueCount);
    ImGui::TextWrapped(
        "A TN is a volume-driven tunnel camera policy. Its trigger BSP decides Inside/Outside; CPTN then chooses an allowed Forward, Reverse, or Manual camera and evaluates the TN path or static frame.");

    ImGui::SeparatorText("Create camera volume");
    ImGui::TextDisabled("Creates a valid trigger box at the selected object's transform (or world origin).");
    const std::int32_t selectedAnchor = renderer.SelectedObjectIndex();
    const bool validAnchor =
        selectedAnchor >= 0 && static_cast<std::size_t>(selectedAnchor) < document.Objects().size();
    const std::int32_t selectedParent =
        validAnchor && document.Objects()[static_cast<std::size_t>(selectedAnchor)].hasAloPayload ? selectedAnchor : -1;
    const float *creationTransform =
        validAnchor ? document.Objects()[static_cast<std::size_t>(selectedAnchor)].worldTransform : nullptr;
    const auto createTn = [&](EditorDocumentOperations::TnPreset preset)
    {
        const auto result = EditorDocumentOperations::CreateTn(document, preset, selectedParent, creationTransform);
        status = result.message;
        if (result.succeeded)
        {
            workspace.Refresh(document);
            renderer.Upload(document);
            renderer.SelectObject(result.primaryObjectIndex);
            display.showHelpers = true;
            display.showAllTns = false;
        }
    };
    if (ImGui::Button("Fixed camera"))
        createTn(EditorDocumentOperations::TnPreset::StaticFrame);
    ImGui::SameLine();
    if (ImGui::Button("Linear path"))
        createTn(EditorDocumentOperations::TnPreset::LinearPath);
    ImGui::SameLine();
    if (ImGui::Button("Cubic path"))
        createTn(EditorDocumentOperations::TnPreset::CubicPath);
    ImGui::SeparatorText("Viewport helpers");
    ImGui::Checkbox("Show TN helpers", &display.showHelpers);
    ImGui::SameLine();
    ImGui::Checkbox("Show every TN", &display.showAllTns);
    ImGui::BeginDisabled(!display.showHelpers);
    ImGui::Checkbox("Trigger volumes", &display.showTriggerVolumes);
    ImGui::SameLine();
    ImGui::Checkbox("Camera paths", &display.showCameraPaths);
    ImGui::SameLine();
    ImGui::Checkbox("Static frames", &display.showCameraFrames);
    ImGui::Checkbox("Camera icons", &display.showCameraIcons);
    ImGui::SameLine();
    ImGui::Checkbox("Frustums", &display.showFrustums);
    ImGui::SameLine();
    ImGui::Checkbox("Labels", &display.showLabels);
    ImGui::Checkbox("Detailed BSP/debug geometry", &display.debugGeometry);
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "Click a TN icon, label, rail, frustum, or trigger volume in the viewport to select it. Unselected TNs use a compact authoring view; detailed BSP geometry is optional.");

    static ImGuiTextFilter filter;
    static TnKindFilter kindFilter = TnKindFilter::All;
    static int directionFilter = 0;
    filter.Draw("Search TNs", 300.0f);
    ImGui::SameLine();
    const char *kinds[] = {"All placements", "Static frames", "Linear paths", "Cubic paths"};
    int kind = static_cast<int>(kindFilter);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("##TnKind", &kind, kinds, 4))
        kindFilter = static_cast<TnKindFilter>(kind);
    ImGui::SameLine();
    ImGui::Checkbox("Issues only", &display.showInvalidOnly);
    const char *directionFilters[] = {"Any direction", "Forward enabled", "Reverse enabled", "Manual enabled"};
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("Direction filter", &directionFilter, directionFilters, 4);
    ImGui::SeparatorText("Camera nodes");
    ImGui::TextDisabled("Click a row to select it; expand it for details and viewport actions.");
    ImGui::BeginChild("TnCameraList", ImVec2(0.0f, 0.0f), true);
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        if (object.cid != 99)
            continue;
        if (!MatchesKind(object.tn, kindFilter))
            continue;
        const auto diagnostics = ValidateTn(object);
        if (display.showInvalidOnly && diagnostics.empty())
            continue;
        const int defaultDirections = object.tn.curveKind >= 0 ? 7 : 5;
        const int directions = IntProperty(object, 867, defaultDirections);
        const int requiredDirection = directionFilter == 0 ? 0 : (1 << (directionFilter - 1));
        if (requiredDirection != 0 && (directions & requiredDirection) == 0)
            continue;
        const std::string searchable = "TN OID " + std::to_string(object.oid) + " " + CurveKindName(object.tn) + " " +
                                       DirectionSummary(directions);
        if (!filter.PassFilter(searchable.c_str()))
            continue;

        ImGui::PushID(static_cast<int>(objectIndex));
        const bool selected = renderer.SelectedObjectIndex() == static_cast<std::int32_t>(objectIndex);
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 9.0f));
        const std::string heading = std::string(diagnostics.empty() ? "TN   OID " : "!  TN   OID ") +
                                    std::to_string(object.oid) + "     " + CurveKindName(object.tn) + "     " +
                                    DirectionSummary(directions) + "##node";
        const bool openNode = ImGui::CollapsingHeader(heading.c_str(), selected ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::PopStyleVar();
        if (selected)
            ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
            status = "Selected TN OID " + std::to_string(object.oid) + ".";
        }
        if (openNode)
        {
            ImGui::Indent(14.0f);
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            if (object.tn.present)
            {
                ImGui::Text("Directions: %s  |  Priority %d", DirectionSummary(directions).c_str(),
                            IntProperty(object, 870, 0));
                ImGui::Text("%zu path point%s  |  %zu trigger plane%s  |  %zu BSP node%s", object.tn.curvePoints.size(),
                            object.tn.curvePoints.size() == 1 ? "" : "s", object.tn.surfaces.size(),
                            object.tn.surfaces.size() == 1 ? "" : "s", object.tn.bspNodes.size(),
                            object.tn.bspNodes.size() == 1 ? "" : "s");
                const int overrideState = IntProperty(object, 884, -1);
                if (overrideState != -1)
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Volume state overridden: %s",
                                       overrideState == 0 ? "forced Outside" : "forced Inside");
            }
            else
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "TN class payload is unavailable.");

            for (const auto &diagnostic : diagnostics)
                ImGui::TextColored(diagnostic.error ? ImVec4(1.0f, 0.30f, 0.22f, 1.0f)
                                                    : ImVec4(1.0f, 0.68f, 0.22f, 1.0f),
                                   "%s %s", diagnostic.error ? "Error:" : "Warning:", diagnostic.text.c_str());

            if (ImGui::Button("Select", ImVec2(100.0f, 32.0f)))
            {
                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                status = "Selected TN OID " + std::to_string(object.oid) + ".";
            }
            ImGui::SameLine();
            if (ImGui::Button("Frame in viewport", ImVec2(150.0f, 32.0f)))
            {
                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                FrameTn(object, camera);
                status = "Framed TN OID " + std::to_string(object.oid) + ".";
            }
            ImGui::SameLine();
            if (ImGui::Button("Isolate helpers", ImVec2(135.0f, 32.0f)))
            {
                renderer.SelectObject(static_cast<std::int32_t>(objectIndex));
                display.showHelpers = true;
                display.showAllTns = false;
                status = "Showing helpers for TN OID " + std::to_string(object.oid) + ".";
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::Unindent(14.0f);
        }
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
