#include "PlacementTools.h"
#include "BrxDocument.h"
#include "EditorDocumentOperations.h"
#include "SceneRenderer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

namespace
{
std::vector<std::int32_t> Selection(const BrxDocument &document, const SceneRenderer &renderer)
{
    std::vector<std::int32_t> result;
    result.reserve(renderer.SelectedObjectIndices().size());
    for (const std::int32_t index : renderer.SelectedObjectIndices())
        if (index >= 0 && static_cast<std::size_t>(index) < document.Objects().size())
            result.push_back(index);
    return result;
}
void Snap(float *matrix, float grid)
{
    for (int axis = 12; axis < 15; ++axis)
        matrix[axis] = std::round(matrix[axis] / grid) * grid;
}

bool ClosestPointOnTriangle(const glm::vec3 &point, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
                            glm::vec3 &closest)
{
    // Ericson's Voronoi-region closest-point test. This avoids the old
    // centroid snap, which could move an object far across a large triangle.
    const glm::vec3 ab = b - a, ac = c - a, ap = point - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
    {
        closest = a;
        return true;
    }
    const glm::vec3 bp = point - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
    {
        closest = b;
        return true;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
    {
        closest = a + ab * (d1 / (d1 - d3));
        return true;
    }
    const glm::vec3 cp = point - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
    {
        closest = c;
        return true;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
    {
        closest = a + ac * (d2 / (d2 - d6));
        return true;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
    {
        closest = b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        return true;
    }
    const float denominator = 1.0f / (va + vb + vc);
    closest = a + ab * (vb * denominator) + ac * (vc * denominator);
    return true;
}
}

void DrawPlacementToolsPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, int &gizmoMode,
                             std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(360, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Placement Tools", &open))
    {
        ImGui::End();
        return;
    }
    static float grid = 1.0f;
    static float scatterRadius = 5.0f;
    static int scatterCount = 8;
    static bool alignToNormal = true;
    static int splineCopies = 6;
    ImGui::DragFloat("Grid size", &grid, .1f, .01f, 1000.0f);
    auto selection = Selection(document, renderer);
    ImGui::BeginDisabled(selection.empty());
    if (ImGui::Button("Snap selection to grid", ImVec2(-1, 0)))
    {
        for (const auto index : selection)
            Snap(document.EditableObjects()[static_cast<std::size_t>(index)].worldTransform, grid);
        renderer.Upload(document);
        status = "Snapped selection to the placement grid.";
    }
    if (ImGui::Button("Snap selection to nearest surface", ImVec2(-1, 0)))
    {
        for (const auto index : selection)
        {
            auto &matrix = document.EditableObjects()[static_cast<std::size_t>(index)].worldTransform;
            const glm::vec3 position(matrix[12], matrix[13], matrix[14]);
            float bestDistance = (std::numeric_limits<float>::max)();
            glm::vec3 bestPoint{};
            glm::vec3 bestNormal(0, 0, 1);
            for (const auto &triangle : document.CollisionTriangles())
            {
                // Placement and walkable surfaces use SO gameplay GEOM.
                // geomCamera/bspcCamera is consumed only by the camera solver.
                if (triangle.cameraGeometry)
                    continue;
                const glm::vec3 a(triangle.positions[0][0], triangle.positions[0][1], triangle.positions[0][2]);
                const glm::vec3 b(triangle.positions[1][0], triangle.positions[1][1], triangle.positions[1][2]);
                const glm::vec3 c(triangle.positions[2][0], triangle.positions[2][1], triangle.positions[2][2]);
                glm::vec3 point{};
                ClosestPointOnTriangle(position, a, b, c, point);
                const float distance = glm::length2(point - position);
                if (distance >= bestDistance)
                    continue;
                bestDistance = distance;
                bestPoint = point;
                const glm::vec3 normal = glm::cross(b - a, c - a);
                if (glm::length2(normal) > 0.000001f)
                    bestNormal = glm::normalize(normal);
            }
            if (bestDistance == (std::numeric_limits<float>::max)())
                continue;
            matrix[12] = bestPoint.x;
            matrix[13] = bestPoint.y;
            matrix[14] = bestPoint.z;
            if (grid > 0.0f)
                Snap(matrix, grid);
            if (alignToNormal)
            {
                const float scaleX = glm::length(glm::vec3(matrix[0], matrix[1], matrix[2]));
                const float scaleY = glm::length(glm::vec3(matrix[4], matrix[5], matrix[6]));
                const float scaleZ = glm::length(glm::vec3(matrix[8], matrix[9], matrix[10]));
                const glm::vec3 up = std::abs(bestNormal.z) > .98f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
                const glm::vec3 x = glm::normalize(glm::cross(up, bestNormal)) * scaleX;
                const glm::vec3 y = glm::normalize(glm::cross(bestNormal, x)) * scaleY;
                const glm::vec3 z = bestNormal * scaleZ;
                matrix[0] = x.x;
                matrix[1] = x.y;
                matrix[2] = x.z;
                matrix[4] = y.x;
                matrix[5] = y.y;
                matrix[6] = y.z;
                matrix[8] = z.x;
                matrix[9] = z.y;
                matrix[10] = z.z;
            }
        }
        renderer.Upload(document);
        status = "Snapped selection to nearest collision surfaces.";
    }
    const auto alignAxis = [&](int axis)
    {
        if (selection.empty())
            return;
        const float value = document.Objects()[static_cast<std::size_t>(selection.front())].worldTransform[12 + axis];
        for (const auto index : selection)
            document.EditableObjects()[static_cast<std::size_t>(index)].worldTransform[12 + axis] = value;
        renderer.Upload(document);
        status = "Aligned selected objects.";
    };
    if (ImGui::Button("Align on X"))
        alignAxis(0);
    ImGui::SameLine();
    if (ImGui::Button("Align on Y"))
        alignAxis(1);
    ImGui::SameLine();
    if (ImGui::Button("Align on Z"))
        alignAxis(2);
    const auto distributeAxis = [&](const int axis)
    {
        if (selection.size() <= 2)
            return;
        std::sort(selection.begin(), selection.end(),
                  [&](auto a, auto b)
                  {
                      return document.Objects()[a].worldTransform[12 + axis] <
                             document.Objects()[b].worldTransform[12 + axis];
                  });
        const float first = document.Objects()[selection.front()].worldTransform[12 + axis];
        const float last = document.Objects()[selection.back()].worldTransform[12 + axis];
        for (std::size_t i = 1; i + 1 < selection.size(); ++i)
            document.EditableObjects()[selection[i]].worldTransform[12 + axis] =
                first + (last - first) * static_cast<float>(i) / static_cast<float>(selection.size() - 1);
        renderer.Upload(document);
        status = "Distributed selected objects evenly.";
    };
    if (ImGui::Button("Distribute X"))
        distributeAxis(0);
    ImGui::SameLine();
    if (ImGui::Button("Distribute Y"))
        distributeAxis(1);
    ImGui::SameLine();
    if (ImGui::Button("Distribute Z"))
        distributeAxis(2);
    ImGui::SeparatorText("Duplicate-drag / scatter");
    if (ImGui::Button("Duplicate selection and begin Move", ImVec2(-1, 0)) && selection.size() == 1)
    {
        const auto result = EditorDocumentOperations::DuplicateHierarchy(document, selection.front());
        status = result.message;
        if (result.succeeded)
        {
            renderer.Upload(document);
            renderer.SelectObject(result.primaryObjectIndex);
            gizmoMode = 0;
        }
    }
    ImGui::DragFloat("Scatter radius", &scatterRadius, .1f, 0, 10000);
    ImGui::DragInt("Copies", &scatterCount, 1, 1, 256);
    if (ImGui::Button("Scatter selected hierarchy", ImVec2(-1, 0)) && selection.size() == 1)
    {
        const auto source = selection.front();
        for (int i = 0; i < scatterCount; ++i)
        {
            const auto result = EditorDocumentOperations::DuplicateHierarchy(document, source);
            if (!result.succeeded)
                break;
            const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(scatterCount);
            auto &transform = document.EditableObjects()[result.primaryObjectIndex].worldTransform;
            transform[12] += std::cos(angle) * scatterRadius;
            transform[13] += std::sin(angle) * scatterRadius;
        }
        renderer.Upload(document);
        status = "Scattered hierarchy instances in a deterministic ring.";
    }
    ImGui::Checkbox("Align placed objects to surface normal", &alignToNormal);
    ImGui::SameLine();
    ImGui::TextDisabled("Grid is applied after surface projection");
    ImGui::TextWrapped(
        "Viewport drops use the hit surface when available, then apply grid snapping. Surface-normal alignment preserves the object's scale.");
    ImGui::SeparatorText("Spline placement");
    ImGui::DragInt("Spline copies", &splineCopies, 1, 1, 256);
    ImGui::TextWrapped("The primary object is duplicated between the other two selected endpoint objects.");
    if (ImGui::Button("Place along selected span", ImVec2(-1, 0)) && selection.size() >= 3)
    {
        const auto source = renderer.SelectedObjectIndex();
        selection.erase(std::remove(selection.begin(), selection.end(), source), selection.end());
        const auto &start = document.Objects()[selection[0]].worldTransform;
        const auto &end = document.Objects()[selection[1]].worldTransform;
        for (int i = 1; i <= splineCopies; ++i)
        {
            const auto result = EditorDocumentOperations::DuplicateHierarchy(document, source);
            if (!result.succeeded)
                break;
            const float t = static_cast<float>(i) / static_cast<float>(splineCopies + 1);
            auto &transform = document.EditableObjects()[result.primaryObjectIndex].worldTransform;
            for (int axis = 0; axis < 3; ++axis)
                transform[12 + axis] = start[12 + axis] + (end[12 + axis] - start[12 + axis]) * t;
        }
        renderer.Upload(document);
        status = "Placed hierarchy copies along the selected span.";
    }
    ImGui::EndDisabled();
    ImGui::End();
}
