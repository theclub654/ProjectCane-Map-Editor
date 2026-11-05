#include "PathZoneRuntime.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace
{
float Distance(const std::array<float, 3> &a, const std::array<float, 3> &b)
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}
float Area2(const std::array<float, 3> &a, const std::array<float, 3> &b, const std::array<float, 3> &c)
{
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

int TriangleFromBspQuick(const BrxPathZoneRecord &zone, const std::array<float, 3> &point)
{
    if (zone.bspNodes.empty())
        return -1;
    std::vector<bool> visiting(zone.bspNodes.size(), false);
    std::int16_t child = 0;
    while (child >= 0)
    {
        const auto index = static_cast<std::size_t>(child);
        if (index >= zone.bspNodes.size() || visiting[index])
            return -1;
        visiting[index] = true;
        const auto &node = zone.bspNodes[index];
        const float side =
            point[0] * node.normal[0] + point[1] * node.normal[1] + point[2] * node.normal[2] - node.distance;
        child = side > 0.0001f ? node.positiveChild : node.negativeChild;
    }
    if (child == -1)
        return -1;
    const int triangle = -static_cast<int>(child) - 2;
    return triangle >= 0 && static_cast<std::size_t>(triangle) < zone.triangles.size() ? triangle : -1;
}

int TriangleFromBspSafe(const BrxPathZoneRecord &zone, const std::array<float, 3> &point)
{
    if (zone.bspNodes.empty())
        return -1;
    std::vector<bool> visiting(zone.bspNodes.size(), false);
    const std::function<int(std::int16_t)> resolve = [&](const std::int16_t child) -> int
    {
        if (child == -1)
            return -1;
        if (child < 0)
        {
            const int triangle = -static_cast<int>(child) - 2;
            return triangle >= 0 && static_cast<std::size_t>(triangle) < zone.triangles.size() ? triangle : -1;
        }
        const auto index = static_cast<std::size_t>(child);
        if (index >= zone.bspNodes.size() || visiting[index])
            return -1;
        visiting[index] = true;
        const auto &node = zone.bspNodes[index];
        const float side =
            point[0] * node.normal[0] + point[1] * node.normal[1] + point[2] * node.normal[2] - node.distance;
        int result = -1;
        if (side > 0.01f)
            result = resolve(node.positiveChild);
        else if (side < -0.01f)
            result = resolve(node.negativeChild);
        else
        {
            result = resolve(node.positiveChild);
            if (result < 0)
                result = resolve(node.negativeChild);
        }
        visiting[index] = false;
        return result;
    };
    return resolve(0);
}

std::array<float, 3> ClosestBoundaryPoint(const BrxPathZoneRecord &zone, const std::array<float, 3> &point)
{
    // FindClosestPointInCg leaves points accepted by the safe BSP query alone.
    if (TriangleFromBspSafe(zone, point) >= 0)
        return point;
    std::array<float, 3> best = point;
    float bestDistance2 = std::numeric_limits<float>::max();
    const std::size_t count = (std::min)(static_cast<std::size_t>(zone.boundaryEdgeCount), zone.edges.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto &edge = zone.edges[i];
        if (edge[0] >= zone.vertices.size() || edge[1] >= zone.vertices.size())
            continue;
        const auto &a = zone.vertices[edge[0]];
        const auto &b = zone.vertices[edge[1]];
        const float dx = b[0] - a[0], dy = b[1] - a[1], length2 = dx * dx + dy * dy;
        const float t = length2 > 0.000001f
                            ? std::clamp(((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length2, 0.0f, 1.0f)
                            : 0.0f;
        const std::array<float, 3> candidate{a[0] + dx * t, a[1] + dy * t, a[2] + (b[2] - a[2]) * t};
        const float ex = candidate[0] - point[0], ey = candidate[1] - point[1], distance2 = ex * ex + ey * ey;
        if (distance2 < bestDistance2)
        {
            bestDistance2 = distance2;
            best = candidate;
        }
    }
    return best;
}

float SegmentInsideFraction(const BrxPathZoneRecord &zone, const std::array<float, 3> &start,
                            const std::array<float, 3> &end)
{
    if (TriangleFromBspQuick(zone, start) < 0)
        return 0.0f;
    std::vector<float> crossings{0.0f, 1.0f};
    const std::array<float, 3> delta{end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float length2 = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    if (length2 < 0.00000001f)
        return TriangleFromBspQuick(zone, end) >= 0 ? 1.0f : 0.0f;

    // ProjectCane clips movement against CG::acbsp, not against the displayed
    // boundary edge list. Partitioning at every serialized BSP plane yields the
    // same first contiguous triangle-leaf span while also handling concavity.
    for (const auto &node : zone.bspNodes)
    {
        const float g0 =
            start[0] * node.normal[0] + start[1] * node.normal[1] + start[2] * node.normal[2] - node.distance;
        const float g1 = end[0] * node.normal[0] + end[1] * node.normal[1] + end[2] * node.normal[2] - node.distance;
        const float denominator = g0 - g1;
        if (std::abs(denominator) < 0.000001f)
            continue;
        const float t = g0 / denominator;
        if (t > 0.0f && t < 1.0f)
            crossings.push_back(t);
    }
    std::sort(crossings.begin(), crossings.end());
    crossings.erase(std::unique(crossings.begin(), crossings.end(),
                                [](const float a, const float b) { return std::abs(a - b) < 0.00001f; }),
                    crossings.end());
    for (std::size_t interval = 1; interval < crossings.size(); ++interval)
    {
        const float begin = crossings[interval - 1], finish = crossings[interval];
        const float midpoint = (begin + finish) * 0.5f;
        const std::array<float, 3> point{start[0] + (end[0] - start[0]) * midpoint,
                                         start[1] + (end[1] - start[1]) * midpoint,
                                         start[2] + (end[2] - start[2]) * midpoint};
        if (TriangleFromBspQuick(zone, point) < 0)
            return begin;
    }
    return TriangleFromBspQuick(zone, end) >= 0 ? 1.0f : crossings.back();
}
} // namespace

void RebuildPathZoneAdjacency(BrxPathZoneRecord &zone)
{
    zone.vertexEdges.assign(zone.vertices.size(), {});
    for (std::size_t edgeIndex = 0; edgeIndex < zone.edges.size() && edgeIndex <= UINT16_MAX; ++edgeIndex)
        for (const auto vertexIndex : zone.edges[edgeIndex])
            if (vertexIndex < zone.vertexEdges.size())
                zone.vertexEdges[vertexIndex].push_back(static_cast<std::uint16_t>(edgeIndex));
    zone.modified = true;
}

void RebuildPathZoneEdges(BrxPathZoneRecord &zone)
{
    struct EdgeInfo
    {
        std::array<std::uint16_t, 2> edge{};
        std::uint32_t uses = 0;
    };
    std::unordered_map<std::uint32_t, EdgeInfo> unique;
    const auto key = [](std::uint16_t a, std::uint16_t b)
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<std::uint32_t>(a) << 16) | b;
    };
    for (const auto &triangle : zone.triangles)
        for (int side = 0; side < 3; ++side)
        {
            std::uint16_t a = triangle[side], b = triangle[(side + 1) % 3];
            if (a > b)
                std::swap(a, b);
            auto &info = unique[key(a, b)];
            info.edge = {a, b};
            ++info.uses;
        }
    std::vector<std::array<std::uint16_t, 2>> boundary, interior;
    for (const auto &[unused, info] : unique)
        (info.uses == 1 ? boundary : interior).push_back(info.edge);
    std::sort(boundary.begin(), boundary.end());
    std::sort(interior.begin(), interior.end());
    zone.edges = boundary;
    zone.boundaryEdgeCount =
        static_cast<std::uint16_t>((std::min)(zone.edges.size(), static_cast<std::size_t>(UINT16_MAX)));
    zone.edges.insert(zone.edges.end(), interior.begin(), interior.end());
    RebuildPathZoneAdjacency(zone);
}

std::size_t FixPathZoneTriangleWinding(BrxPathZoneRecord &zone)
{
    std::size_t fixed = 0;
    for (auto &triangle : zone.triangles)
    {
        if (triangle[0] >= zone.vertices.size() || triangle[1] >= zone.vertices.size() ||
            triangle[2] >= zone.vertices.size())
            continue;
        if (Area2(zone.vertices[triangle[0]], zone.vertices[triangle[1]], zone.vertices[triangle[2]]) < 0.0f)
        {
            std::swap(triangle[1], triangle[2]);
            ++fixed;
        }
    }
    if (fixed != 0)
        zone.modified = true;
    return fixed;
}

bool RebuildPathZoneBsp(BrxPathZoneRecord &zone)
{
    if (zone.triangles.empty() || zone.triangles.size() * 3 > static_cast<std::size_t>(INT16_MAX))
        return false;
    for (const auto &triangle : zone.triangles)
    {
        if (triangle[0] >= zone.vertices.size() || triangle[1] >= zone.vertices.size() ||
            triangle[2] >= zone.vertices.size() ||
            !(Area2(zone.vertices[triangle[0]], zone.vertices[triangle[1]], zone.vertices[triangle[2]]) > 0.000001f))
            return false;
    }
    zone.bspNodes.clear();
    zone.bspNodes.resize(zone.triangles.size() * 3);
    for (std::size_t ti = 0; ti < zone.triangles.size(); ++ti)
    {
        const auto &triangle = zone.triangles[ti];
        const std::int16_t next = ti + 1 < zone.triangles.size() ? static_cast<std::int16_t>((ti + 1) * 3) : -1;
        for (int side = 0; side < 3; ++side)
        {
            const auto &a = zone.vertices[triangle[side]];
            const auto &b = zone.vertices[triangle[(side + 1) % 3]];
            auto &node = zone.bspNodes[ti * 3 + static_cast<std::size_t>(side)];
            const float dx = b[0] - a[0], dy = b[1] - a[1], length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.000001f)
            {
                node.normal = {-dy / length, dx / length, 0.0f};
                node.distance = node.normal[0] * a[0] + node.normal[1] * a[1];
            }
            node.negativeChild = next;
            node.positiveChild = side < 2 ? static_cast<std::int16_t>(ti * 3 + side + 1)
                                          : static_cast<std::int16_t>(-static_cast<int>(ti) - 2);
        }
    }
    zone.modified = true;
    return true;
}

bool BuildPathZoneFromPolygon(BrxPathZoneRecord &zone, const std::vector<std::array<float, 3>> &polygon,
                              std::string &error)
{
    error.clear();
    zone = {};
    if (polygon.size() < 3 || polygon.size() > 10922)
    {
        error = "A PATHZONE boundary needs 3 to 10,922 points.";
        return false;
    }
    zone.vertices = polygon;
    double signedArea = 0.0;
    for (std::size_t i = 0; i < zone.vertices.size(); ++i)
    {
        const auto &a = zone.vertices[i];
        const auto &b = zone.vertices[(i + 1) % zone.vertices.size()];
        signedArea += static_cast<double>(a[0]) * b[1] - static_cast<double>(b[0]) * a[1];
    }
    if (std::abs(signedArea) < 0.000001)
    {
        error = "The PATHZONE boundary has no usable XY area.";
        return false;
    }
    if (signedArea < 0.0)
        std::reverse(zone.vertices.begin(), zone.vertices.end());
    std::vector<std::uint16_t> remaining(zone.vertices.size());
    for (std::size_t i = 0; i < remaining.size(); ++i)
        remaining[i] = static_cast<std::uint16_t>(i);
    const auto inside = [](const std::array<float, 3> &p, const std::array<float, 3> &a, const std::array<float, 3> &b,
                           const std::array<float, 3> &c)
    {
        const float ab = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
        const float bc = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0]);
        const float ca = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0]);
        return ab >= -0.0001f && bc >= -0.0001f && ca >= -0.0001f;
    };
    std::size_t guard = 0;
    while (remaining.size() > 3 && guard++ < polygon.size() * polygon.size())
    {
        bool clipped = false;
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            const auto prev = remaining[(i + remaining.size() - 1) % remaining.size()], cur = remaining[i],
                       next = remaining[(i + 1) % remaining.size()];
            if (Area2(zone.vertices[prev], zone.vertices[cur], zone.vertices[next]) <= 0.000001f)
                continue;
            bool occupied = false;
            for (const auto candidate : remaining)
                if (candidate != prev && candidate != cur && candidate != next &&
                    inside(zone.vertices[candidate], zone.vertices[prev], zone.vertices[cur], zone.vertices[next]))
                {
                    occupied = true;
                    break;
                }
            if (occupied)
                continue;
            zone.triangles.push_back({prev, cur, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped)
        {
            error = "The boundary self-intersects or cannot be triangulated safely.";
            return false;
        }
    }
    if (remaining.size() == 3)
        zone.triangles.push_back({remaining[0], remaining[1], remaining[2]});
    RebuildPathZoneEdges(zone);
    if (!RebuildPathZoneBsp(zone))
    {
        error = "The triangulated PATHZONE could not produce a valid BSP.";
        return false;
    }
    return true;
}

PathZoneClipResult ClipPathZoneMovement(const BrxPathZoneRecord &zone, const std::array<float, 3> &start,
                                        const std::array<float, 3> &end)
{
    PathZoneClipResult result;
    result.point = start;
    if (zone.bspNodes.empty() || zone.triangles.empty())
    {
        result.message = "PATHZONE has no usable triangle BSP.";
        return result;
    }
    if (TriangleFromBspQuick(zone, start) < 0)
    {
        result.message = "Movement starts outside the PATHZONE.";
        return result;
    }
    result.valid = true;
    const float insideFraction = SegmentInsideFraction(zone, start, end);
    if (insideFraction >= 0.999999f)
    {
        result.fraction = 1.0f;
        result.point = end;
        result.message = "Movement remains inside the PATHZONE.";
        return result;
    }

    result.clipped = true;
    result.fraction = insideFraction;
    result.point = {start[0] + (end[0] - start[0]) * insideFraction, start[1] + (end[1] - start[1]) * insideFraction,
                    start[2] + (end[2] - start[2]) * insideFraction};
    result.message = "Movement clipped at the PATHZONE boundary.";
    return result;
}

bool DeletePathZoneVertex(BrxPathZoneRecord &zone, const std::size_t vertexIndex)
{
    if (vertexIndex >= zone.vertices.size() || zone.vertices.size() <= 3)
        return false;
    const BrxPathZoneRecord original = zone;
    zone.vertices.erase(zone.vertices.begin() + static_cast<std::ptrdiff_t>(vertexIndex));
    zone.triangles.erase(
        std::remove_if(
            zone.triangles.begin(), zone.triangles.end(), [&](const auto &triangle)
            { return triangle[0] == vertexIndex || triangle[1] == vertexIndex || triangle[2] == vertexIndex; }),
        zone.triangles.end());
    for (auto &triangle : zone.triangles)
        for (auto &vertex : triangle)
            if (vertex > vertexIndex)
                --vertex;
    RebuildPathZoneEdges(zone);
    if (RebuildPathZoneBsp(zone))
        return true;
    zone = original;
    return false;
}

bool SplitPathZoneBoundaryEdge(BrxPathZoneRecord &zone, const std::size_t edgeIndex)
{
    if (edgeIndex >= zone.boundaryEdgeCount || edgeIndex >= zone.edges.size() || zone.vertices.size() >= UINT16_MAX)
        return false;
    const auto edge = zone.edges[edgeIndex];
    if (edge[0] >= zone.vertices.size() || edge[1] >= zone.vertices.size())
        return false;
    const BrxPathZoneRecord original = zone;
    const auto &a = zone.vertices[edge[0]];
    const auto &b = zone.vertices[edge[1]];
    const std::uint16_t added = static_cast<std::uint16_t>(zone.vertices.size());
    zone.vertices.push_back({(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f});
    for (std::size_t i = 0; i < zone.triangles.size(); ++i)
    {
        auto triangle = zone.triangles[i];
        int edgeSide = -1;
        for (int side = 0; side < 3; ++side)
        {
            const auto v0 = triangle[side], v1 = triangle[(side + 1) % 3];
            if ((v0 == edge[0] && v1 == edge[1]) || (v0 == edge[1] && v1 == edge[0]))
            {
                edgeSide = side;
                break;
            }
        }
        if (edgeSide < 0)
            continue;
        const auto v0 = triangle[edgeSide], v1 = triangle[(edgeSide + 1) % 3], opposite = triangle[(edgeSide + 2) % 3];
        zone.triangles[i] = {v0, added, opposite};
        zone.triangles.push_back({added, v1, opposite});
        RebuildPathZoneEdges(zone);
        if (RebuildPathZoneBsp(zone))
            return true;
        zone = original;
        return false;
    }
    zone.vertices.pop_back();
    return false;
}

bool ProjectPathZoneVertexToCollision(BrxPathZoneRecord &zone, const std::size_t vertexIndex,
                                      const BrxDocument &document)
{
    if (vertexIndex >= zone.vertices.size() || document.CollisionTriangles().empty())
        return false;
    const BrxPathZoneRecord original = zone;
    const auto point = zone.vertices[vertexIndex];
    std::array<float, 3> best = point;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto &triangle : document.CollisionTriangles())
    {
        if (triangle.cameraGeometry)
            continue;
        const std::array<float, 3> a{triangle.positions[0][0], triangle.positions[0][1], triangle.positions[0][2]},
            b{triangle.positions[1][0], triangle.positions[1][1], triangle.positions[1][2]},
            c{triangle.positions[2][0], triangle.positions[2][1], triangle.positions[2][2]};
        const std::array<float, 3> normal{(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
                                          (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                                          (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])};
        const float normalLength = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (normalLength < 0.0001f || std::abs(normal[2] / normalLength) < 0.573576f)
            continue;
        const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]},
            ap{point[0] - a[0], point[1] - a[1], point[2] - a[2]};
        const float d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2],
                    d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
        std::array<float, 3> candidate{};
        if (d1 <= 0 && d2 <= 0)
            candidate = a;
        else
        {
            const std::array<float, 3> bp{point[0] - b[0], point[1] - b[1], point[2] - b[2]};
            const float d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2],
                        d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
            if (d3 >= 0 && d4 <= d3)
                candidate = b;
            else
            {
                const float vc = d1 * d4 - d3 * d2;
                if (vc <= 0 && d1 >= 0 && d3 <= 0)
                {
                    const float v = d1 / (d1 - d3);
                    candidate = {a[0] + v * ab[0], a[1] + v * ab[1], a[2] + v * ab[2]};
                }
                else
                {
                    const std::array<float, 3> cp{point[0] - c[0], point[1] - c[1], point[2] - c[2]};
                    const float d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2],
                                d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
                    if (d6 >= 0 && d5 <= d6)
                        candidate = c;
                    else
                    {
                        const float vb = d5 * d2 - d1 * d6;
                        if (vb <= 0 && d2 >= 0 && d6 <= 0)
                        {
                            const float w = d2 / (d2 - d6);
                            candidate = {a[0] + w * ac[0], a[1] + w * ac[1], a[2] + w * ac[2]};
                        }
                        else
                        {
                            const float va = d3 * d6 - d5 * d4;
                            if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                            {
                                const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                                candidate = {b[0] + w * (c[0] - b[0]), b[1] + w * (c[1] - b[1]),
                                             b[2] + w * (c[2] - b[2])};
                            }
                            else
                            {
                                const float denom = 1.0f / (va + vb + vc), v = vb * denom, w = vc * denom;
                                candidate = {a[0] + ab[0] * v + ac[0] * w, a[1] + ab[1] * v + ac[1] * w,
                                             a[2] + ab[2] * v + ac[2] * w};
                            }
                        }
                    }
                }
            }
        }
        const float distance = Distance(point, candidate);
        const float horizontalDistance = std::hypot(candidate[0] - point[0], candidate[1] - point[1]);
        if (horizontalDistance > 250.0f || distance > 1000.0f)
            continue;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    if (!std::isfinite(bestDistance))
        return false;
    zone.vertices[vertexIndex] = best;
    zone.modified = true;
    if (FixPathZoneTriangleWinding(zone) != 0)
        RebuildPathZoneEdges(zone);
    if (RebuildPathZoneBsp(zone))
        return true;
    zone = original;
    return false;
}

std::vector<std::string> AuditPathZone(const BrxPathZoneRecord &zone)
{
    std::vector<std::string> issues;
    if (zone.vertices.size() > UINT16_MAX)
        issues.emplace_back("Vertex count exceeds the serialized U16 limit.");
    if (zone.edges.size() > UINT16_MAX)
        issues.emplace_back("Edge count exceeds the serialized U16 limit.");
    if (zone.triangles.size() > UINT16_MAX)
        issues.emplace_back("Triangle count exceeds the serialized U16 limit.");
    if (zone.triangles.size() > 32767)
        issues.emplace_back("Triangle count exceeds the signed BSP-leaf index limit.");
    if (zone.bspNodes.size() > static_cast<std::size_t>(INT16_MAX))
        issues.emplace_back("BSP node count exceeds the signed child-index limit.");
    if (zone.vertices.size() < 3)
        issues.emplace_back("The zone needs at least three vertices.");
    if (zone.triangles.empty())
        issues.emplace_back("The zone has no walkable triangles.");
    if (zone.bspNodes.empty())
        issues.emplace_back("The zone has no BSP root; runtime point queries cannot succeed.");
    if (zone.boundaryEdgeCount > zone.edges.size())
        issues.emplace_back("Boundary-edge count exceeds the edge count.");
    for (std::size_t vi = 0; vi < zone.vertices.size(); ++vi)
    {
        const auto &vertex = zone.vertices[vi];
        if (!std::isfinite(vertex[0]) || !std::isfinite(vertex[1]) || !std::isfinite(vertex[2]))
            issues.emplace_back("Vertex " + std::to_string(vi) + " contains a non-finite coordinate.");
    }
    if (zone.vertexEdges.size() != zone.vertices.size())
        issues.emplace_back("Vertex adjacency is missing or has the wrong size.");
    std::vector<int> edgeUse(zone.edges.size(), 0);
    std::vector<std::vector<std::uint16_t>> expectedAdjacency(zone.vertices.size());
    std::unordered_multimap<std::uint32_t, std::size_t> edgesByEndpoints;
    const auto edgeKey = [](std::uint16_t a, std::uint16_t b)
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<std::uint32_t>(a) << 16) | b;
    };
    for (std::size_t ei = 0; ei < zone.edges.size(); ++ei)
    {
        const auto key = edgeKey(zone.edges[ei][0], zone.edges[ei][1]);
        if (edgesByEndpoints.find(key) != edgesByEndpoints.end())
            issues.emplace_back("Edge " + std::to_string(ei) + " duplicates an existing edge.");
        edgesByEndpoints.emplace(key, ei);
        if (ei <= UINT16_MAX)
        {
            for (const auto vertex : zone.edges[ei])
                if (vertex < expectedAdjacency.size())
                    expectedAdjacency[vertex].push_back(static_cast<std::uint16_t>(ei));
        }
    }
    std::unordered_set<std::uint64_t> uniqueTriangles;
    for (std::size_t ti = 0; ti < zone.triangles.size(); ++ti)
    {
        const auto &triangle = zone.triangles[ti];
        if (triangle[0] >= zone.vertices.size() || triangle[1] >= zone.vertices.size() ||
            triangle[2] >= zone.vertices.size())
        {
            issues.emplace_back("Triangle " + std::to_string(ti) + " contains an invalid vertex.");
            continue;
        }
        auto canonical = triangle;
        std::sort(canonical.begin(), canonical.end());
        const std::uint64_t triangleKey = (static_cast<std::uint64_t>(canonical[0]) << 32) |
                                          (static_cast<std::uint64_t>(canonical[1]) << 16) | canonical[2];
        if (!uniqueTriangles.insert(triangleKey).second)
            issues.emplace_back("Triangle " + std::to_string(ti) + " duplicates an existing triangle.");
        if (!(Area2(zone.vertices[triangle[0]], zone.vertices[triangle[1]], zone.vertices[triangle[2]]) > 0.000001f))
            issues.emplace_back("Triangle " + std::to_string(ti) + " is clockwise or degenerate in XY.");
        for (int side = 0; side < 3; ++side)
        {
            const auto range = edgesByEndpoints.equal_range(edgeKey(triangle[side], triangle[(side + 1) % 3]));
            if (range.first == range.second)
                issues.emplace_back("Triangle " + std::to_string(ti) + " is missing serialized edge " +
                                    std::to_string(side) + ".");
            for (auto it = range.first; it != range.second; ++it)
                ++edgeUse[it->second];
        }
    }
    for (std::size_t ei = 0; ei < zone.edges.size(); ++ei)
    {
        const auto &edge = zone.edges[ei];
        if (edge[0] >= zone.vertices.size() || edge[1] >= zone.vertices.size())
            issues.emplace_back("Edge " + std::to_string(ei) + " contains an invalid vertex.");
        else if (edge[0] == edge[1] || Distance(zone.vertices[edge[0]], zone.vertices[edge[1]]) < 0.0001f)
            issues.emplace_back("Edge " + std::to_string(ei) + " has zero length.");
        if (edgeUse[ei] > 2)
            issues.emplace_back("Edge " + std::to_string(ei) + " is non-manifold.");
        if (ei < zone.boundaryEdgeCount && edgeUse[ei] != 1)
            issues.emplace_back("Boundary edge " + std::to_string(ei) + " must belong to exactly one triangle.");
        if (ei >= zone.boundaryEdgeCount && edgeUse[ei] != 2)
            issues.emplace_back("Interior edge " + std::to_string(ei) + " must belong to two triangles.");
    }
    std::vector<int> boundaryDegree(zone.vertices.size(), 0);
    for (std::size_t ei = 0; ei < (std::min)(static_cast<std::size_t>(zone.boundaryEdgeCount), zone.edges.size()); ++ei)
    {
        const auto &edge = zone.edges[ei];
        if (edge[0] < boundaryDegree.size())
            ++boundaryDegree[edge[0]];
        if (edge[1] < boundaryDegree.size())
            ++boundaryDegree[edge[1]];
    }
    for (std::size_t vi = 0; vi < boundaryDegree.size(); ++vi)
        if (boundaryDegree[vi] != 0 && boundaryDegree[vi] != 2)
            issues.emplace_back("Boundary is open or branching at vertex " + std::to_string(vi) + ".");
    for (std::size_t vi = 0; vi < zone.vertexEdges.size(); ++vi)
    {
        if (zone.vertexEdges[vi].size() > 127)
            issues.emplace_back("Vertex " + std::to_string(vi) +
                                " exceeds the runtime signed-U8 adjacency limit of 127 edges.");
        for (const auto ei : zone.vertexEdges[vi])
            if (ei >= zone.edges.size() || (zone.edges[ei][0] != vi && zone.edges[ei][1] != vi))
                issues.emplace_back("Vertex " + std::to_string(vi) + " has invalid edge adjacency.");
        if (vi < expectedAdjacency.size())
        {
            auto actual = zone.vertexEdges[vi];
            auto expected = expectedAdjacency[vi];
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected)
                issues.emplace_back("Vertex " + std::to_string(vi) +
                                    " adjacency is incomplete or contains duplicates.");
        }
    }
    if (!zone.vertices.empty() && zone.vertexEdges.size() == zone.vertices.size())
    {
        std::vector<bool> reached(zone.vertices.size(), false);
        std::queue<std::size_t> pending;
        pending.push(0);
        reached[0] = true;
        while (!pending.empty())
        {
            const auto v = pending.front();
            pending.pop();
            for (const auto ei : zone.vertexEdges[v])
            {
                if (ei >= zone.edges.size())
                    continue;
                const auto &edge = zone.edges[ei];
                const auto next = edge[0] == v ? edge[1] : edge[1] == v ? edge[0] : UINT16_MAX;
                if (next < reached.size() && !reached[next])
                {
                    reached[next] = true;
                    pending.push(next);
                }
            }
        }
        if (std::find(reached.begin(), reached.end(), false) != reached.end())
            issues.emplace_back("The navigation graph contains disconnected vertex islands.");
    }
    std::vector<std::uint8_t> bspState(zone.bspNodes.size(), 0);
    const std::function<void(std::int16_t)> visitBsp = [&](const std::int16_t child)
    {
        if (child < 0)
            return;
        const auto index = static_cast<std::size_t>(child);
        if (index >= zone.bspNodes.size())
            return;
        if (bspState[index] == 1)
        {
            issues.emplace_back("The BSP contains a cycle through node " + std::to_string(index) + ".");
            return;
        }
        if (bspState[index] == 2)
            return;
        bspState[index] = 1;
        visitBsp(zone.bspNodes[index].negativeChild);
        visitBsp(zone.bspNodes[index].positiveChild);
        bspState[index] = 2;
    };
    if (!zone.bspNodes.empty())
        visitBsp(0);
    for (std::size_t i = 0; i < bspState.size(); ++i)
        if (bspState[i] == 0)
            issues.emplace_back("BSP node " + std::to_string(i) + " is unreachable from the root.");
    std::vector<bool> referencedTriangleLeaves(zone.triangles.size(), false);
    const auto validChild = [&](const std::int16_t child)
    {
        if (child == -1)
            return true;
        if (child >= 0)
            return static_cast<std::size_t>(child) < zone.bspNodes.size();
        const int triangle = -static_cast<int>(child) - 2;
        return triangle >= 0 && static_cast<std::size_t>(triangle) < zone.triangles.size();
    };
    for (std::size_t i = 0; i < zone.bspNodes.size(); ++i)
    {
        const auto &node = zone.bspNodes[i];
        const float length2 =
            node.normal[0] * node.normal[0] + node.normal[1] * node.normal[1] + node.normal[2] * node.normal[2];
        if (!std::isfinite(length2) || length2 < 0.000001f || !std::isfinite(node.distance))
            issues.emplace_back("BSP node " + std::to_string(i) + " has an invalid plane.");
        if (!validChild(node.negativeChild) || !validChild(node.positiveChild))
            issues.emplace_back("BSP node " + std::to_string(i) + " has an invalid child or triangle leaf.");
        for (const auto child : {node.negativeChild, node.positiveChild})
            if (child < -1)
            {
                const int triangle = -static_cast<int>(child) - 2;
                if (triangle >= 0 && static_cast<std::size_t>(triangle) < referencedTriangleLeaves.size())
                    referencedTriangleLeaves[static_cast<std::size_t>(triangle)] = true;
            }
    }
    for (std::size_t triangle = 0; triangle < referencedTriangleLeaves.size(); ++triangle)
        if (!referencedTriangleLeaves[triangle])
            issues.emplace_back("Triangle " + std::to_string(triangle) + " has no BSP leaf reference.");
    for (std::size_t triangle = 0; triangle < zone.triangles.size(); ++triangle)
    {
        const auto &indices = zone.triangles[triangle];
        if (indices[0] >= zone.vertices.size() || indices[1] >= zone.vertices.size() ||
            indices[2] >= zone.vertices.size())
            continue;
        const std::array<float, 3> centroid{
            (zone.vertices[indices[0]][0] + zone.vertices[indices[1]][0] + zone.vertices[indices[2]][0]) / 3.0f,
            (zone.vertices[indices[0]][1] + zone.vertices[indices[1]][1] + zone.vertices[indices[2]][1]) / 3.0f,
            (zone.vertices[indices[0]][2] + zone.vertices[indices[1]][2] + zone.vertices[indices[2]][2]) / 3.0f};
        const int resolved = TriangleFromBspQuick(zone, centroid);
        if (resolved != static_cast<int>(triangle))
            issues.emplace_back(
                "Triangle " + std::to_string(triangle) + " centroid resolves through the runtime BSP to " +
                (resolved < 0 ? std::string("outside") : std::string("triangle ") + std::to_string(resolved)) + ".");
    }
    return issues;
}

PathZoneRouteResult FindPathZoneRoute(const BrxPathZoneRecord &zone, const std::array<float, 3> &start,
                                      const std::array<float, 3> &end)
{
    PathZoneRouteResult result;
    result.resolvedStart = start;
    result.resolvedEnd = end;
    int st = TriangleFromBspQuick(zone, result.resolvedStart);
    if (st < 0)
    {
        result.resolvedStart = ClosestBoundaryPoint(zone, start);
        result.startSnapped = Distance(result.resolvedStart, start) > 0.0001f;
        st = TriangleFromBspSafe(zone, result.resolvedStart);
    }
    int et = TriangleFromBspQuick(zone, result.resolvedEnd);
    if (et < 0)
    {
        result.resolvedEnd = ClosestBoundaryPoint(zone, end);
        result.endSnapped = Distance(result.resolvedEnd, end) > 0.0001f;
        et = TriangleFromBspSafe(zone, result.resolvedEnd);
    }
    if (st < 0 || et < 0)
    {
        result.message = "The BSP could not resolve one or both test markers.";
        return result;
    }
    if (st == et)
    {
        result.points = {result.resolvedStart, result.resolvedEnd};
        result.success = true;
        result.message = "Both markers are in the same navigation triangle.";
        return result;
    }
    const std::size_t n = zone.vertices.size();
    std::vector<float> cost(n, std::numeric_limits<float>::max());
    std::vector<std::int32_t> parent(n, -1);
    using Node = std::pair<float, std::uint16_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (const auto v : zone.triangles[static_cast<std::size_t>(st)])
        if (v < n)
        {
            cost[v] = Distance(result.resolvedStart, zone.vertices[v]);
            queue.emplace(cost[v], v);
        }
    std::unordered_set<std::uint16_t> goals;
    for (const auto v : zone.triangles[static_cast<std::size_t>(et)])
        if (v < n)
            goals.insert(v);
    std::int32_t reached = -1;
    float bestCompleteCost = std::numeric_limits<float>::max();
    while (!queue.empty())
    {
        const auto [queued, v] = queue.top();
        queue.pop();
        if (queued != cost[v])
            continue;
        if (queued >= bestCompleteCost)
            break;
        if (goals.contains(v))
        {
            const float completeCost = queued + Distance(zone.vertices[v], result.resolvedEnd);
            if (completeCost < bestCompleteCost)
            {
                bestCompleteCost = completeCost;
                reached = v;
            }
        }
        if (v >= zone.vertexEdges.size())
            continue;
        for (const auto ei : zone.vertexEdges[v])
        {
            if (ei >= zone.edges.size())
                continue;
            const auto &edge = zone.edges[ei];
            const std::uint16_t next = edge[0] == v ? edge[1] : edge[1] == v ? edge[0] : UINT16_MAX;
            if (next >= n)
                continue;
            const float nc = queued + Distance(zone.vertices[v], zone.vertices[next]);
            if (nc < cost[next])
            {
                cost[next] = nc;
                parent[next] = v;
                queue.emplace(nc, next);
            }
        }
    }
    if (reached < 0)
    {
        result.message = "No connected graph route exists between the markers.";
        return result;
    }
    std::vector<std::array<float, 3>> reverse;
    for (std::int32_t v = reached; v >= 0; v = parent[static_cast<std::size_t>(v)])
        reverse.push_back(zone.vertices[static_cast<std::size_t>(v)]);
    result.points.push_back(result.resolvedStart);
    result.points.insert(result.points.end(), reverse.rbegin(), reverse.rend());
    result.points.push_back(result.resolvedEnd);
    if (result.points.size() > 128)
    {
        result.points.clear();
        result.message = "The unsimplified route exceeds the runtime 128-point path limit.";
        return result;
    }
    if (result.points.size() > 2)
    {
        std::vector<std::array<float, 3>> simplified;
        simplified.push_back(result.points.front());
        std::size_t anchor = 0;
        while (anchor + 1 < result.points.size())
        {
            std::size_t furthest = anchor + 1;
            for (std::size_t candidate = result.points.size() - 1; candidate > anchor + 1; --candidate)
                if (SegmentInsideFraction(zone, result.points[anchor], result.points[candidate]) >= 0.999999f)
                {
                    furthest = candidate;
                    break;
                }
            simplified.push_back(result.points[furthest]);
            anchor = furthest;
        }
        result.points = std::move(simplified);
    }
    result.success = true;
    result.message = "Route follows and simplifies the serialized PATHZONE graph.";
    return result;
}
