#pragma once

#include "BrxDocument.h"
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// ProjectCane Sly1-Release/vis.cpp. Masks remain full U32 values: the row
// count controls row lookup, not which bits may occur in leaves or rows.
namespace VisibilityMap
{
inline constexpr std::uint32_t FallbackMask = 0x0fffffffu;

struct Result
{
    std::uint32_t mask = 0u;
    bool valid = false; // Invalid links/cycles have no terminating retail result.
};

inline std::uint32_t OneHop(const std::vector<std::uint32_t> &rows, const std::uint32_t leaf)
{
    if (leaf == 0u)
        return 0u;
    std::uint32_t expanded = 0xffffffffu;
    for (std::size_t zone = 0; zone < (std::min)(rows.size(), std::size_t{32}); ++zone)
        if ((leaf & (1u << zone)) != 0u)
            expanded &= rows[zone];
    return expanded | leaf;
}

inline Result PointNoHop(const std::vector<BrxVisibilityNode> &nodes, const glm::vec3 &position)
{
    if (nodes.empty())
        return {FallbackMask, true};
    std::int32_t index = 0;
    for (std::size_t step = 0; step < nodes.size(); ++step)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= nodes.size())
            return {};
        const auto &node = nodes[static_cast<std::size_t>(index)];
        const float distance =
            glm::dot(position, glm::vec3(node.normal[0], node.normal[1], node.normal[2])) - node.planeDistance;
        const bool positive = distance >= 0.0f;
        const auto child = positive ? node.positiveChild : node.negativeChild;
        if (child == -1)
            return {positive ? node.positiveZoneMask : node.negativeZoneMask, true};
        index = child;
    }
    return {};
}

inline Result SphereOneHop(const std::vector<BrxVisibilityNode> &nodes, const std::vector<std::uint32_t> &rows,
                           const glm::vec3 &position, const float radius)
{
    if (nodes.empty())
        return {FallbackMask, true};
    if (rows.size() > 32u)
        return {};
    std::uint32_t mask = 0u;
    // An active-path revisit is a cycle. A completed shared subtree is legal:
    // its contribution is already ORed into the result for this same sphere.
    std::vector<std::uint8_t> state(nodes.size(), 0u);
    std::vector<std::pair<std::int32_t, bool>> work{{0, false}};
    while (!work.empty())
    {
        const auto [index, leaving] = work.back();
        work.pop_back();
        if (index < 0 || static_cast<std::size_t>(index) >= nodes.size())
            return {};
        auto &visited = state[static_cast<std::size_t>(index)];
        if (leaving)
        {
            visited = 2u;
            continue;
        }
        if (visited == 1u)
            return {};
        if (visited == 2u)
            continue;
        visited = 1u;
        work.emplace_back(index, true);
        const auto &node = nodes[static_cast<std::size_t>(index)];
        const float distance =
            glm::dot(position, glm::vec3(node.normal[0], node.normal[1], node.normal[2])) - node.planeDistance;
        const auto side = [&](bool positive)
        {
            const auto child = positive ? node.positiveChild : node.negativeChild;
            if (child == -1)
                mask |= OneHop(rows, positive ? node.positiveZoneMask : node.negativeZoneMask);
            else
                work.emplace_back(child, false);
        };
        // Preserve retail's nested comparisons, including negative radii and
        // equality. Independent +/- tests are not equivalent for negative radii.
        if (distance >= -radius)
        {
            if (distance <= radius)
            {
                side(false);
                side(true);
            }
            else
                side(true);
        }
        else
            side(false);
    }
    return {mask, true};
}
}
