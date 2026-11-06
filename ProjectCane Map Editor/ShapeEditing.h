#pragma once

#include "BrxDocument.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <glm/glm.hpp>

namespace ShapeEditing
{
// Editor-only pivot; never serialized as a native SHAPE transform.
inline glm::vec3 Pivot(const BrxShapePathRecord &shape, const int mode, const int selectedPoint)
{
    if (shape.points.empty()) return glm::vec3(0.0f);
    if (mode == 1) return glm::vec3(shape.points.front()[0], shape.points.front()[1], shape.points.front()[2]);
    if (mode == 2)
    {
        const auto &p = shape.points[static_cast<std::size_t>(std::clamp(selectedPoint, 0, static_cast<int>(shape.points.size()) - 1))];
        return glm::vec3(p[0], p[1], p[2]);
    }
    const auto count = shape.closed && shape.points.size() > 1 ? shape.points.size() - 1 : shape.points.size();
    glm::vec3 result(0.0f);
    for (std::size_t i = 0; i < count; ++i)
        result += glm::vec3(shape.points[i][0], shape.points[i][1], shape.points[i][2]);
    return result / static_cast<float>(count);
}

// Match ConvertCrvc: points use w=1; derivatives use only the linear part.
// Stage all values so invalid input cannot partially rewrite the curve.
inline bool Transform(BrxShapePathRecord &shape, const glm::mat4 &localDelta)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!std::isfinite(localDelta[c][r])) return false;
    auto points = shape.points;
    auto incoming = shape.tangentsIn;
    auto outgoing = shape.tangentsOut;
    const auto transform = [&](auto &values, const float w)
    {
        for (auto &p : values)
        {
            const glm::vec3 v(localDelta * glm::vec4(p[0], p[1], p[2], w));
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
            p = {v.x, v.y, v.z};
        }
        return true;
    };
    if (!transform(points, 1.0f) || !transform(incoming, 0.0f) || !transform(outgoing, 0.0f)) return false;
    shape.points = std::move(points);
    shape.tangentsIn = std::move(incoming);
    shape.tangentsOut = std::move(outgoing);
    return true;
}
}
