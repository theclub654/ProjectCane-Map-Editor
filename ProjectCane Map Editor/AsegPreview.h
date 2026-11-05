#pragma once

#include "BrxDocument.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct AsegPreviewResult
{
    std::vector<glm::mat4> objectDeltas;
    std::vector<BrxRenderVertex> skinnedVertices;
    std::vector<BrxCelEdge> skinnedCelEdges;
    bool hasSkinnedGeometry = false;
    std::size_t resolvedChannels = 0;
    std::size_t unresolvedChannels = 0;
};

inline float EvaluateAsegScalar(const BrxAsegScalarController &controller, const float time, const float fallback)
{
    const auto &keys = controller.keys;
    if (keys.empty())
        return fallback;
    if (keys.size() == 1 || time <= keys.front().time)
        return keys.front().value;
    if (time >= keys.back().time)
        return keys.back().value;
    const auto upper =
        std::upper_bound(keys.begin(), keys.end(), time,
                         [](const float value, const BrxAsegScalarKey &key) { return value < key.time; });
    const auto &right = *upper;
    const auto &left = *(upper - 1);
    const float duration = right.time - left.time;
    if (duration <= 0.000001f || left.tangentOutKind == 5)
        return left.value;
    if (controller.kind == 2)
        return left.value + (right.value - left.value) * ((time - left.time) / duration);
    if (controller.kind == 1)
    {
        const float segmentTime = time - left.time;
        float low = 0.0f, high = 1.0f;
        const float controlTime0 = left.tangentOutTime * 3.0f;
        const float controlTime1 = (duration - right.tangentInTime) * 3.0f;
        for (int iteration = 1; iteration <= 16; ++iteration)
        {
            const float u = low + 1.0f / static_cast<float>(1u << iteration);
            const float bezierTime = u * (u * (u * ((controlTime0 - controlTime1) + duration) +
                                               ((controlTime1 - controlTime0) - controlTime0)) +
                                          controlTime0) -
                                     segmentTime;
            if (bezierTime < 0.0f)
                low = u;
            else if (bezierTime > 0.0f)
                high = u;
            else
            {
                low = high = u;
                break;
            }
        }
        const float u = 0.5f * (low + high), v = 1.0f - u;
        return v * v * v * left.value + 3.0f * u * v * v * left.tangentOut + 3.0f * u * u * v * right.tangentIn +
               u * u * u * right.value;
    }
    const float u = (time - left.time) / duration, v = 1.0f - u;
    return (v * v * v + 3.0f * u * v * v) * left.value + u * v * v * duration * left.tangentOut -
           u * u * v * duration * right.tangentIn + (u * u * u + 3.0f * u * u * v) * right.value;
}

inline float EvaluateAsegScalar(const BrxAsegControllerSummary &controller, const float time, const float fallback)
{
    BrxAsegScalarController scalar;
    scalar.kind = controller.kind;
    scalar.keys = controller.scalarKeys;
    return EvaluateAsegScalar(scalar, time, fallback);
}

inline glm::vec3 EvaluateAsegVector(const BrxAsegControllerSummary &controller, const float time,
                                    const glm::vec3 &fallback)
{
    if (controller.kind == 0)
    {
        glm::vec3 value = glm::make_vec3(controller.defaultValue.data());
        for (int component = 0; component < 3; ++component)
            if ((controller.componentMask & (1u << component)) != 0)
                value[component] = EvaluateAsegScalar(controller.components[component], time, value[component]);
        return value;
    }
    if (controller.kind != 1 || controller.vectorKeys.empty())
        return fallback;
    const auto &keys = controller.vectorKeys;
    if (keys.size() == 1 || time <= keys.front().time)
        return glm::make_vec3(keys.front().value.data());
    if (time >= keys.back().time)
        return glm::make_vec3(keys.back().value.data());
    const auto upper =
        std::upper_bound(keys.begin(), keys.end(), time,
                         [](const float value, const BrxAsegVectorKey &key) { return value < key.time; });
    const auto &right = *upper;
    const auto &left = *(upper - 1);
    const float duration = right.time - left.time;
    if (duration <= 0.000001f)
        return glm::make_vec3(left.value.data());
    const float u = (time - left.time) / duration, v = 1.0f - u;
    return glm::make_vec3(left.value.data()) * (v * v * v + 3.0f * u * v * v) +
           glm::make_vec3(left.tangentOut.data()) * (u * v * v * duration) -
           glm::make_vec3(right.tangentIn.data()) * (u * u * v * duration) +
           glm::make_vec3(right.value.data()) * (u * u * u + 3.0f * u * u * v);
}

inline glm::mat3 AsegEulerMatrix(const glm::vec3 &euler)
{
    const float sx = std::sin(euler.x), cx = std::cos(euler.x);
    const float sy = std::sin(euler.y), cy = std::cos(euler.y);
    const float sz = std::sin(euler.z), cz = std::cos(euler.z);
    glm::mat3 result(1.0f);
    result[0] = glm::vec3(cy * cz, cy * sz, -sy);
    result[1] = glm::vec3(sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy);
    result[2] = glm::vec3(cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy);
    return result;
}

inline AsegPreviewResult EvaluateAsegPreview(const BrxDocument &document, const BrxAsegRecord &segment,
                                             const std::int32_t selectedRoot, const float time,
                                             const bool preserveRootPlacement = true)
{
    const auto &objects = document.Objects();
    AsegPreviewResult result;
    result.objectDeltas.assign(objects.size(), glm::mat4(1.0f));
    if (selectedRoot < 0 || static_cast<std::size_t>(selectedRoot) >= objects.size())
        return result;
    // PROXY roots are serialized as children of the PROXY record, but the
    // retail loader reparents them to the PROXY owner's parent after cloning.
    // Rendering already observes that runtime hierarchy. Animation must do
    // the same or every animated joint is evaluated in the wrapper's space
    // and the skinned model collapses as soon as a channel becomes active.
    const auto runtimeParentIndex = [&](const std::int32_t objectIndex) -> std::int32_t
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects.size())
            return -1;
        const auto &object = objects[static_cast<std::size_t>(objectIndex)];
        const bool serializedUnderProxyWrapper = object.parentIndex >= 0 &&
                                                 static_cast<std::size_t>(object.parentIndex) < objects.size() &&
                                                 objects[static_cast<std::size_t>(object.parentIndex)].cid == 83 &&
                                                 object.proxyRecordIndex >= 0 && object.proxyRootIndex >= 0;
        return serializedUnderProxyWrapper ? object.proxyRuntimeParentIndex : object.parentIndex;
    };
    const auto isDescendant = [&](std::int32_t candidate, const std::int32_t root)
    {
        for (std::size_t guard = 0;
             candidate >= 0 && static_cast<std::size_t>(candidate) < objects.size() && guard++ <= objects.size();
             candidate = runtimeParentIndex(candidate))
            if (candidate == root)
                return true;
        return false;
    };
    const auto &previewRoot = objects[static_cast<std::size_t>(selectedRoot)];
    const auto isInPreviewInstance = [&](const std::int32_t candidate)
    {
        if (candidate < 0 || static_cast<std::size_t>(candidate) >= objects.size())
            return false;
        const auto &object = objects[static_cast<std::size_t>(candidate)];
        if (previewRoot.proxyRecordIndex < 0)
            return object.proxyRecordIndex < 0;
        if (object.proxyRecordIndex != previewRoot.proxyRecordIndex)
            return false;
        // Selecting the PROXY wrapper previews all roots owned by that wrapper.
        // Selecting an instantiated root/child is restricted to that one clone.
        return previewRoot.proxyRootIndex < 0 || object.proxyRootIndex == previewRoot.proxyRootIndex;
    };
    std::vector<std::int32_t> channelRoots;
    for (const auto searchOid : segment.searchRootOids)
    {
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].oid == searchOid && isInPreviewInstance(static_cast<std::int32_t>(index)))
                channelRoots.push_back(static_cast<std::int32_t>(index));
    }

    const auto contextualMatch = [&](const std::int16_t oid, const std::int32_t context) -> std::int32_t
    {
        if (context < 0 || static_cast<std::size_t>(context) >= objects.size())
            return -1;
        // CploFindSwObjects mode 4 marks the context and each ancestor with a
        // one-based depth, then keeps candidates meeting the closest marked
        // ancestor. Reproduce that ranking and return one object, as
        // PloFindSwObject does, rather than animating every duplicate OID.
        std::unordered_map<std::int32_t, int> contextDepth;
        std::int32_t ancestor = context;
        for (int depth = 1; ancestor >= 0 && static_cast<std::size_t>(ancestor) < objects.size() &&
                            static_cast<std::size_t>(depth) <= objects.size();
             ++depth)
        {
            if (contextDepth.contains(ancestor))
                break;
            contextDepth.emplace(ancestor, depth);
            ancestor = runtimeParentIndex(ancestor);
        }
        std::int32_t best = -1;
        int bestDepth = (std::numeric_limits<int>::max)();
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            if (objects[index].oid != oid || !isInPreviewInstance(static_cast<std::int32_t>(index)))
                continue;
            std::int32_t current = static_cast<std::int32_t>(index);
            for (std::size_t guard = 0;
                 current >= 0 && static_cast<std::size_t>(current) < objects.size() && guard++ <= objects.size();
                 current = runtimeParentIndex(current))
            {
                const auto foundDepth = contextDepth.find(current);
                if (foundDepth == contextDepth.end())
                    continue;
                if (foundDepth->second < bestDepth)
                {
                    best = static_cast<std::int32_t>(index);
                    bestDepth = foundDepth->second;
                }
                break;
            }
        }
        return best;
    };

    std::unordered_map<std::int32_t, const BrxAsegChannelRecord *> channelByObject;
    std::unordered_set<std::int32_t> strippedObjects;
    for (const auto &channel : segment.channels)
    {
        // The stripped channel is still visited by ApplyAseg, but ProjectCane
        // permits only its scale/pose controllers. Position and rotation are
        // root-motion data and never become active ACT channels.
        if (channel.stripChannel && channel.scale.kind < 0 && channel.poses.empty())
            continue;
        std::int32_t resolvedObject = contextualMatch(channel.targetOid, selectedRoot);
        // ApplyAseg only consults aoidSearchRoot after its primary contextual
        // lookup fails. Search roots are tried in serialized order and each
        // returns one descendant match.
        if (resolvedObject < 0)
        {
            for (const std::int32_t root : channelRoots)
            {
                for (std::size_t index = 0; index < objects.size(); ++index)
                {
                    const std::int32_t candidate = static_cast<std::int32_t>(index);
                    if (objects[index].oid == channel.targetOid && isInPreviewInstance(candidate) &&
                        isDescendant(candidate, root))
                    {
                        resolvedObject = candidate;
                        break;
                    }
                }
                if (resolvedObject >= 0)
                    break;
            }
        }
        if (resolvedObject < 0 && previewRoot.proxyRecordIndex < 0)
            for (std::size_t index = 0; index < objects.size(); ++index)
                if (objects[index].oid == channel.targetOid)
                {
                    resolvedObject = static_cast<std::int32_t>(index);
                    break;
                }
        if (resolvedObject >= 0)
        {
            channelByObject[resolvedObject] = &channel;
            if (channel.stripChannel)
                strippedObjects.insert(resolvedObject);
            ++result.resolvedChannels;
        }
        else
            ++result.unresolvedChannels;
    }
    // ProjectCane's UpdateAloXfWorldHierarchy propagates only xf.pos/xf.mat.
    // ACT scale is a render-time matrix on the object itself and is not
    // inherited by child joints or consumed by UpdateAloInfluences. Keep an
    // unscaled hierarchy for bones and a separate matrix for object drawing.
    std::vector<glm::mat4> projected(objects.size(), glm::mat4(1.0f));
    std::vector<glm::mat4> projectedBone(objects.size(), glm::mat4(1.0f));
    std::vector<glm::vec3> evaluatedPosition(objects.size(), glm::vec3(0.0f));
    std::vector<glm::mat3> evaluatedRotation(objects.size(), glm::mat3(1.0f));
    std::vector<glm::vec3> evaluatedScale(objects.size(), glm::vec3(1.0f));
    std::vector<std::uint8_t> scaleActive(objects.size(), 0);
    std::vector<std::uint8_t> state(objects.size(), 0), affected(objects.size(), 0);
    std::function<void(std::size_t)> project = [&](const std::size_t index)
    {
        if (state[index] == 2)
            return;
        if (state[index] == 1)
        {
            projected[index] = glm::make_mat4(objects[index].worldTransform);
            projectedBone[index] = projected[index];
            state[index] = 2;
            return;
        }
        state[index] = 1;
        const auto &object = objects[index];
        const std::int32_t runtimeParent = runtimeParentIndex(static_cast<std::int32_t>(index));
        const bool hasParent = runtimeParent >= 0 && static_cast<std::size_t>(runtimeParent) < objects.size();
        glm::mat4 authoredWorld = glm::make_mat4(object.worldTransform);
        if (hasParent)
            project(static_cast<std::size_t>(runtimeParent));
        glm::mat4 local = hasParent ? glm::make_mat4(object.localTransform) : authoredWorld;
        if (hasParent && runtimeParent != object.parentIndex)
        {
            // localTransform is decoded in serialized hierarchy space.  A
            // proxy root's actual runtime local transform is relative to its
            // repaired parent, so recover it from the authored world poses.
            const glm::mat4 runtimeParentWorld =
                glm::make_mat4(objects[static_cast<std::size_t>(runtimeParent)].worldTransform);
            local = glm::inverse(runtimeParentWorld) * authoredWorld;
        }
        glm::vec3 position(local[3]);
        glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])),
                        glm::length(glm::vec3(local[2])));
        glm::mat3 rotation(local);
        for (int axis = 0; axis < 3; ++axis)
            if (scale[axis] > 0.000001f)
                rotation[axis] /= scale[axis];
        const auto found = channelByObject.find(static_cast<std::int32_t>(index));
        if (found != channelByObject.end())
        {
            const auto &channel = *found->second;
            const bool stripped = strippedObjects.contains(static_cast<std::int32_t>(index));
            // ASEG root position/rotation are locomotion data. Applying those
            // absolute controller values directly in an editor preview moves
            // a placed character back toward the animation's authoring origin
            // and changes its map-facing rotation. Keep them available for a
            // true root-motion preview, but default character playback to an
            // in-place pose exactly where the user placed the hierarchy.
            const bool lockAnimationRoot =
                preserveRootPlacement && segment.rootOid != -1 && channel.targetOid == segment.rootOid;
            if (!stripped && !lockAnimationRoot && (channel.flags & 1u) != 0)
                position = EvaluateAsegVector(channel.position, time, position);
            if (!stripped && !lockAnimationRoot && (channel.flags & 2u) != 0)
            {
                rotation = AsegEulerMatrix(EvaluateAsegVector(channel.rotation, time, glm::vec3(0.0f)));
                // ProjectCane's EvaluateAcrb/EvaluateAcrc/EvaluateAcrbl all
                // finish through AdjustAloRotation.  These matrices convert
                // the controller's Euler basis into the authored joint basis;
                // omitting them makes a valid pose shear/explode at joints.
                if ((object.alox.flags & 0x01u) != 0)
                    rotation = rotation * glm::make_mat3(object.alox.preRotation);
                if ((object.alox.flags & 0x02u) != 0)
                    rotation = glm::make_mat3(object.alox.postRotation) * rotation;
            }
            if ((channel.flags & 4u) != 0)
                scale = EvaluateAsegVector(channel.scale, time, scale);
            if ((channel.flags & 4u) != 0)
                scaleActive[index] = 1;
            affected[index] = 1;
        }
        evaluatedScale[index] = scale;
        evaluatedPosition[index] = position;
        evaluatedRotation[index] = rotation;

        std::int32_t positionParent = hasParent ? runtimeParent : -1;
        std::int32_t rotationParent = positionParent;
        if ((object.alox.flags & 0x0cu) != 0 && object.alox.rotationSourceOid != -1)
        {
            const std::int32_t basis = contextualMatch(object.alox.rotationSourceOid, static_cast<std::int32_t>(index));
            if (basis >= 0 && basis != static_cast<std::int32_t>(index))
            {
                project(static_cast<std::size_t>(basis));
                if ((object.alox.flags & 0x04u) != 0)
                    positionParent = basis;
                if ((object.alox.flags & 0x08u) != 0)
                    rotationParent = basis;
            }
        }

        glm::vec3 worldPosition = position;
        glm::mat3 worldRotation = rotation;
        if (positionParent >= 0)
        {
            const glm::mat4 &basis = projectedBone[static_cast<std::size_t>(positionParent)];
            worldPosition = glm::vec3(basis[3]) + glm::mat3(basis) * position;
            if (affected[static_cast<std::size_t>(positionParent)] != 0)
                affected[index] = 1;
        }
        if (rotationParent >= 0)
        {
            const glm::mat4 &basis = projectedBone[static_cast<std::size_t>(rotationParent)];
            worldRotation = glm::mat3(basis) * rotation;
            if (affected[static_cast<std::size_t>(rotationParent)] != 0)
                affected[index] = 1;
        }

        projectedBone[index] = glm::mat4(1.0f);
        for (int axis = 0; axis < 3; ++axis)
            projectedBone[index][axis] = glm::vec4(worldRotation[axis], 0.0f);
        projectedBone[index][3] = glm::vec4(worldPosition, 1.0f);

        // RenderAloAll applies this object's ACT scale on the right after its
        // world position/rotation has been established. It never feeds that
        // scale back into child xf matrices or the skinning palette.
        projected[index] = projectedBone[index];
        for (int axis = 0; axis < 3; ++axis)
            projected[index][axis] *= scale[axis];
        if (affected[index] != 0)
        {
            result.objectDeltas[index] = projected[index] * glm::inverse(authoredWorld);
        }
        state[index] = 2;
    };
    for (std::size_t index = 0; index < objects.size(); ++index)
        project(index);

    // UpdateAloInfluences calls UpdateAloConstraints for every palette bone.
    // ALOX 0x10 joints therefore cause their ALOX 0x20 handle to solve the
    // shoulder/elbow chain before the matrices are uploaded. Without this
    // stage animation channels are all present, but blended elbow vertices
    // are skinned against the unsolved chain and visibly stretch apart.
    const auto rotationBetween = [](const glm::vec3 &from, const glm::vec3 &to)
    {
        const float fromLength = glm::length(from), toLength = glm::length(to);
        if (fromLength < 0.0001f || toLength < 0.0001f)
            return glm::mat3(1.0f);
        const glm::vec3 a = from / fromLength, b = to / toLength;
        const float cosine = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
        glm::vec3 axis = glm::cross(a, b);
        float axisLength = glm::length(axis);
        if (axisLength < 0.0001f)
        {
            if (cosine > 0.0f)
                return glm::mat3(1.0f);
            axis = glm::cross(a, std::abs(a.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
            axisLength = glm::length(axis);
        }
        return glm::mat3(glm::rotate(glm::mat4(1.0f), std::acos(cosine), axis / axisLength));
    };
    const auto axisRotation = [](const float radians, const glm::vec3 &axis)
    {
        const float length = glm::length(axis);
        return length < 0.0001f ? glm::mat3(1.0f) : glm::mat3(glm::rotate(glm::mat4(1.0f), radians, axis / length));
    };
    const auto recomputeHierarchy = [&]()
    {
        std::vector<std::uint8_t> solveState(objects.size(), 0);
        std::function<void(std::size_t)> solveObject = [&](const std::size_t index)
        {
            if (solveState[index] == 2)
                return;
            if (solveState[index] == 1)
            {
                solveState[index] = 2;
                return;
            }
            solveState[index] = 1;
            const auto &object = objects[index];
            const std::int32_t parent = runtimeParentIndex(static_cast<std::int32_t>(index));
            std::int32_t positionParent = parent, rotationParent = parent;
            if ((object.alox.flags & 0x0cu) != 0 && object.alox.rotationSourceOid != -1)
            {
                const std::int32_t basis =
                    contextualMatch(object.alox.rotationSourceOid, static_cast<std::int32_t>(index));
                if (basis >= 0 && basis != static_cast<std::int32_t>(index))
                {
                    solveObject(static_cast<std::size_t>(basis));
                    if ((object.alox.flags & 0x04u) != 0)
                        positionParent = basis;
                    if ((object.alox.flags & 0x08u) != 0)
                        rotationParent = basis;
                }
            }
            if (positionParent >= 0)
                solveObject(static_cast<std::size_t>(positionParent));
            if (rotationParent >= 0)
                solveObject(static_cast<std::size_t>(rotationParent));
            glm::vec3 worldPosition = evaluatedPosition[index];
            glm::mat3 worldRotation = evaluatedRotation[index];
            if (positionParent >= 0)
            {
                const glm::mat4 &basis = projectedBone[static_cast<std::size_t>(positionParent)];
                worldPosition = glm::vec3(basis[3]) + glm::mat3(basis) * worldPosition;
            }
            if (rotationParent >= 0)
                worldRotation = glm::mat3(projectedBone[static_cast<std::size_t>(rotationParent)]) * worldRotation;
            projectedBone[index] = glm::mat4(1.0f);
            for (int axis = 0; axis < 3; ++axis)
                projectedBone[index][axis] = glm::vec4(worldRotation[axis], 0.0f);
            projectedBone[index][3] = glm::vec4(worldPosition, 1.0f);
            projected[index] = projectedBone[index];
            for (int axis = 0; axis < 3; ++axis)
                projected[index][axis] *= evaluatedScale[index][axis];
            if (affected[index] != 0)
                result.objectDeltas[index] = projected[index] * glm::inverse(glm::make_mat4(object.worldTransform));
            solveState[index] = 2;
        };
        for (std::size_t index = 0; index < objects.size(); ++index)
            solveObject(index);
    };
    for (std::size_t handleIndex = 0; handleIndex < objects.size(); ++handleIndex)
    {
        const auto &handle = objects[handleIndex];
        if ((handle.alox.flags & 0x20u) == 0 || !isInPreviewInstance(static_cast<std::int32_t>(handleIndex)))
            continue;
        const std::int32_t elbowObject = contextualMatch(handle.alox.elbowOid, static_cast<std::int32_t>(handleIndex));
        if (elbowObject < 0)
            continue;
        const std::int32_t shoulderObject = runtimeParentIndex(elbowObject);
        if (shoulderObject < 0)
            continue;
        const std::size_t elbow = static_cast<std::size_t>(elbowObject);
        const std::size_t shoulder = static_cast<std::size_t>(shoulderObject);
        if (affected[elbow] == 0 && affected[shoulder] == 0 && affected[handleIndex] == 0)
            continue;

        evaluatedRotation[shoulder] =
            glm::make_mat3(objects[shoulder].alox.postRotation) * glm::make_mat3(objects[shoulder].alox.preRotation);
        evaluatedRotation[elbow] =
            (objects[elbow].alox.flags & 3u) != 0
                ? glm::make_mat3(objects[elbow].alox.postRotation) * glm::make_mat3(objects[elbow].alox.preRotation)
                : glm::mat3(1.0f);
        affected[shoulder] = affected[elbow] = 1;
        recomputeHierarchy();

        const glm::vec3 shoulderPosition(projectedBone[shoulder][3]);
        const glm::vec3 elbowPosition(projectedBone[elbow][3]);
        const glm::vec3 wristPosition =
            elbowPosition + glm::mat3(projectedBone[elbow]) * glm::make_vec3(handle.alox.wristPosition);
        const glm::vec3 targetPosition =
            glm::vec3(projectedBone[handleIndex] * glm::vec4(glm::make_vec3(handle.alox.ikPosition), 1.0f));
        glm::vec3 upper = elbowPosition - shoulderPosition;
        glm::vec3 lower = wristPosition - elbowPosition;
        const glm::vec3 target = targetPosition - shoulderPosition;
        const float upperLength = glm::length(upper), lowerLength = glm::length(lower);
        const float targetLength = glm::length(target);
        if (upperLength < 0.0001f || lowerLength < 0.0001f || targetLength < 0.0001f)
            continue;
        glm::vec3 elbowAxis = glm::cross(upper, lower);
        if (glm::length(elbowAxis) < 0.0001f)
            elbowAxis = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            elbowAxis = glm::normalize(elbowAxis);
        const float currentElbow =
            std::acos(glm::clamp(glm::dot(glm::normalize(upper), glm::normalize(lower)), -1.0f, 1.0f));
        const float targetCosine =
            glm::clamp((targetLength * targetLength - upperLength * upperLength - lowerLength * lowerLength) /
                           (2.0f * upperLength * lowerLength),
                       -1.0f, 1.0f);
        const glm::mat3 elbowDelta = axisRotation(std::acos(targetCosine) - currentElbow, elbowAxis);
        lower = elbowDelta * lower;
        const glm::mat3 shoulderAim = rotationBetween(upper + lower, target);
        const glm::mat3 shoulderTwist = axisRotation(handle.alox.twistRadians, target);
        const glm::mat3 shoulderDelta = shoulderTwist * shoulderAim;
        const std::int32_t shoulderParentObject = runtimeParentIndex(shoulderObject);
        const glm::mat3 shoulderParentWorld =
            shoulderParentObject >= 0 ? glm::mat3(projectedBone[static_cast<std::size_t>(shoulderParentObject)])
                                      : glm::mat3(1.0f);
        const glm::mat3 shoulderWorldBefore(projectedBone[shoulder]);
        const glm::mat3 elbowWorldBefore(projectedBone[elbow]);
        evaluatedRotation[elbow] = glm::transpose(shoulderWorldBefore) * (elbowDelta * elbowWorldBefore);
        evaluatedRotation[shoulder] = glm::transpose(shoulderParentWorld) * (shoulderDelta * shoulderWorldBefore);
        recomputeHierarchy();
    }

    // RenderAloAll carries a separate RO matrix once an animated scale is
    // encountered. Children inherit that matrix even though their ordinary
    // xf world transforms remain unscaled. ALOX 0x80 joints then expose the
    // RO matrix to UpdateAloInfluences through joint.matInfluence. Recreate
    // that render-only hierarchy; using projectedBone for those joints bends
    // blended vertices around the wrong pivot (most visibly at elbows).
    std::vector<glm::mat4> renderModel(objects.size(), glm::mat4(1.0f));
    std::vector<std::uint8_t> renderOverride(objects.size(), 0), renderState(objects.size(), 0);
    std::function<void(std::size_t)> projectRender = [&](const std::size_t index)
    {
        if (renderState[index] == 2)
            return;
        if (renderState[index] == 1)
        {
            renderModel[index] = projectedBone[index];
            renderState[index] = 2;
            return;
        }
        renderState[index] = 1;
        const auto &object = objects[index];
        const std::int32_t parent = runtimeParentIndex(static_cast<std::int32_t>(index));
        const bool hasParent = parent >= 0 && static_cast<std::size_t>(parent) < objects.size();
        if (hasParent)
            projectRender(static_cast<std::size_t>(parent));

        glm::mat4 model = projectedBone[index];
        bool overridden = false;
        if (hasParent && renderOverride[static_cast<std::size_t>(parent)] != 0)
        {
            // Both normal and ALOX 0x0c child branches in RenderAloAll reduce
            // to this relation after the animated xf hierarchy is resolved.
            model = renderModel[static_cast<std::size_t>(parent)] *
                    glm::inverse(projectedBone[static_cast<std::size_t>(parent)]) * projectedBone[index];
            overridden = true;
        }

        if (overridden && (object.alox.flags & 0x80u) != 0 && object.alox.segmentScaleCompensation && hasParent)
        {
            const std::size_t parentIndex = static_cast<std::size_t>(parent);
            if (scaleActive[parentIndex] != 0 && (objects[parentIndex].alox.flags & 0x80u) != 0)
            {
                glm::mat4 inverseParentScale(1.0f);
                for (int axis = 0; axis < 3; ++axis)
                    if (std::abs(evaluatedScale[parentIndex][axis]) > 0.000001f)
                        inverseParentScale[axis][axis] = 1.0f / evaluatedScale[parentIndex][axis];
                const glm::vec3 position(model[3]);
                model = inverseParentScale * model;
                model[3] = glm::vec4(position, 1.0f);
            }
        }
        if (scaleActive[index] != 0)
        {
            model = model * glm::scale(glm::mat4(1.0f), evaluatedScale[index]);
            overridden = true;
        }
        renderModel[index] = model;
        renderOverride[index] = overridden ? 1 : 0;
        renderState[index] = 2;
    };
    for (std::size_t index = 0; index < objects.size(); ++index)
        projectRender(index);
    for (std::size_t index = 0; index < objects.size(); ++index)
        if (affected[index] != 0 && renderOverride[index] != 0)
            result.objectDeltas[index] =
                renderModel[index] * glm::inverse(glm::make_mat4(objects[index].worldTransform));

    // ASEG channels animate ALO bones. Skin GLOBSET geometry through its
    // bone-OID palette so the complete model follows the pose.
    const auto &sourceVertices = document.RenderVertices();
    const auto &renderIndices = document.RenderIndices();
    const auto &sourceCelEdges = document.CelEdges();
    const auto &globs = document.Globs();
    for (const auto &globset : document.Globsets())
    {
        // A gameplay object can drive a separately-owned visual ALO through
        // ASEG search roots. Do not require the GLOBSET owner to be a strict
        // child of the inspector selection; ProjectCane binds the palette by
        // bone OID and the palette is relevant when one of those resolved
        // bones received a channel.
        if (globset.ownerObjectIndex < 0 || globset.boneOids.empty())
            continue;
        if (!isInPreviewInstance(globset.ownerObjectIndex))
            continue;
        std::vector<float> poseWeights = globset.originalPoseWeights;
        if (poseWeights.size() != globset.poseWeights.size())
            poseWeights = globset.poseWeights;
        const auto poseChannel = channelByObject.find(globset.ownerObjectIndex);
        if (poseChannel != channelByObject.end())
        {
            const auto &controllers = poseChannel->second->poses;
            const std::size_t count = (std::min)(poseWeights.size(), controllers.size());
            for (std::size_t pose = 0; pose < count; ++pose)
                if (controllers[pose].kind >= 0)
                    poseWeights[pose] = EvaluateAsegScalar(controllers[pose], time, poseWeights[pose]);
        }
        std::vector<glm::mat4> boneDeltas(globset.boneOids.size(), glm::mat4(1.0f));
        const std::size_t ownerIndex = static_cast<std::size_t>(globset.ownerObjectIndex);
        glm::mat4 bindOwner = glm::make_mat4(objects[ownerIndex].worldTransform);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float length = glm::length(glm::vec3(bindOwner[axis]));
            if (length > 0.000001f)
                bindOwner[axis] /= length;
        }
        const glm::mat4 currentOwner = projectedBone[ownerIndex];
        bool paletteAnimated = false;
        for (std::size_t bone = 0; bone < globset.boneOids.size(); ++bone)
        {
            // BindGlobset does not choose the first descendant with this OID.
            // It calls PloFindSwNearest with the GLOBSET owner as context.
            // Character scenes commonly contain multiple copies of the same
            // skeleton OIDs (Last Call has both foreground and throne MBG
            // hierarchies); taking the first affected duplicate pairs bind
            // geometry with another instance's bones and collapses the mesh.
            const std::int32_t objectIndex = contextualMatch(globset.boneOids[bone], globset.ownerObjectIndex);
            if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= affected.size() ||
                affected[static_cast<std::size_t>(objectIndex)] == 0)
                continue;
            // UpdateAloInfluences normally removes the owner's unscaled xf.
            // For an ALOX 0x80 joint reached through a render override, retail
            // instead removes the owner's RO model and uses the joint's RO
            // model. Express that exact choice in bind-owner space.
            const std::size_t boneIndex = static_cast<std::size_t>(objectIndex);
            const bool useJointInfluence =
                (objects[boneIndex].alox.flags & 0x80u) != 0 && renderOverride[boneIndex] != 0;
            const glm::mat4 &paletteOwner =
                useJointInfluence && renderOverride[ownerIndex] != 0 ? renderModel[ownerIndex] : currentOwner;
            const glm::mat4 &paletteBone = useJointInfluence ? renderModel[boneIndex] : projectedBone[boneIndex];
            glm::mat4 bindBone = glm::make_mat4(objects[boneIndex].worldTransform);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float length = glm::length(glm::vec3(bindBone[axis]));
                if (length > 0.000001f)
                    bindBone[axis] /= length;
            }
            boneDeltas[bone] = bindOwner * glm::inverse(paletteOwner) * paletteBone * glm::inverse(bindBone);
            paletteAnimated = true;
        }
        if (!paletteAnimated)
            continue;
        if (!result.hasSkinnedGeometry)
        {
            result.skinnedVertices = sourceVertices;
            result.skinnedCelEdges = sourceCelEdges;
            result.hasSkinnedGeometry = true;
        }
        const auto skinPoint = [&](const glm::vec3 &point, const std::uint16_t *indices, const float *weights)
        {
            glm::vec3 value(0.0f);
            float total = 0.0f;
            for (std::size_t influence = 0; influence < 4; ++influence)
            {
                const float weight = weights[influence];
                if (weight <= 0.0f || indices[influence] >= boneDeltas.size())
                    continue;
                value += glm::vec3(boneDeltas[indices[influence]] * glm::vec4(point, 1.0f)) * weight;
                total += weight;
            }
            // The retail/ProjectCane shader uses the weighted matrix sum as
            // soon as any valid influence exists. It does not append an
            // identity remainder when authored weights total below one.
            return total > 0.0f ? value : point;
        };
        const auto skinNormal = [&](const glm::vec3 &normal, const std::uint16_t *indices, const float *weights)
        {
            glm::vec3 value(0.0f);
            float total = 0.0f;
            for (std::size_t influence = 0; influence < 4; ++influence)
            {
                const float weight = weights[influence];
                if (weight <= 0.0f || indices[influence] >= boneDeltas.size())
                    continue;
                value += glm::mat3(boneDeltas[indices[influence]]) * normal * weight;
                total += weight;
            }
            if (total <= 0.0f)
                value = normal;
            return glm::dot(value, value) > 0.000001f ? glm::normalize(value) : normal;
        };
        for (const std::size_t globIndex : globset.globIndices)
        {
            if (globIndex >= globs.size())
                continue;
            const auto &glob = globs[globIndex];
            const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
            // The runtime GLOB shader uses mat3(model), not inverse-transpose,
            // after applying pose/skin deltas.
            const glm::mat3 normalToWorld = glm::mat3(sourceToWorld);
            // Every source SUBGLOB/SUBCEL must retain a one-to-one derived
            // packet. Continuing with a partial clone would pair skin weights
            // with another packet and can animate only the cel silhouette.
            if (glob.sourceSubglobs.size() != glob.derivedBatches.size() ||
                glob.sourceCelRecords.size() != glob.derivedCelPackets.size())
                continue;
            for (std::size_t subglobIndex = 0;
                 subglobIndex < glob.sourceSubglobs.size() && subglobIndex < glob.derivedBatches.size(); ++subglobIndex)
            {
                const auto &source = glob.sourceSubglobs[subglobIndex];
                const auto &batch = glob.derivedBatches[subglobIndex];
                if (batch.indexCount != source.triangles.size() * 3u)
                    continue;
                for (std::size_t triangle = 0; triangle < source.triangles.size(); ++triangle)
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const std::size_t slot = static_cast<std::size_t>(batch.firstIndex) + triangle * 3 + corner;
                        const std::uint16_t local = source.triangles[triangle][corner];
                        if (slot >= renderIndices.size() || local >= source.vertices.size())
                            continue;
                        const std::uint32_t destination = renderIndices[slot];
                        if (destination >= sourceVertices.size() || destination >= result.skinnedVertices.size())
                            continue;
                        const auto &influence = source.vertices[local];
                        glm::vec3 localPoint = glm::make_vec3(influence.position);
                        glm::vec3 localNormal = glm::make_vec3(influence.normal);
                        for (std::size_t pose = 0; pose < poseWeights.size(); ++pose)
                        {
                            // ProjectCane's GLOB shader stores pose deltas
                            // vertex-major: gl_VertexID * poseCount + pose.
                            // Treating the stream as pose-major assigns a
                            // neighboring vertex's morph to this vertex and
                            // visibly tears blended joints such as elbows.
                            const std::size_t deltaIndex = static_cast<std::size_t>(local) * poseWeights.size() + pose;
                            if (deltaIndex >= source.posePositionDeltas.size() ||
                                deltaIndex >= source.poseNormalDeltas.size())
                                break;
                            const auto &positionDelta = source.posePositionDeltas[deltaIndex];
                            const auto &normalDelta = source.poseNormalDeltas[deltaIndex];
                            localPoint +=
                                glm::vec3(positionDelta[0], positionDelta[1], positionDelta[2]) * poseWeights[pose];
                            localNormal +=
                                glm::vec3(normalDelta[0], normalDelta[1], normalDelta[2]) * poseWeights[pose];
                        }
                        const glm::vec3 point = glm::vec3(sourceToWorld * glm::vec4(localPoint, 1.0f));
                        glm::vec3 normal = normalToWorld * localNormal;
                        if (glm::dot(normal, normal) > 0.000001f)
                            normal = glm::normalize(normal);
                        const glm::vec3 skinnedPoint = skinPoint(point, influence.boneIndices, influence.boneWeights);
                        const glm::vec3 skinnedNormal =
                            skinNormal(normal, influence.boneIndices, influence.boneWeights);
                        for (int component = 0; component < 3; ++component)
                        {
                            result.skinnedVertices[destination].position[component] = skinnedPoint[component];
                            result.skinnedVertices[destination].normal[component] = skinnedNormal[component];
                        }
                    }
            }
            for (std::size_t cel = 0; cel < glob.sourceCelRecords.size() && cel < glob.derivedCelPackets.size(); ++cel)
            {
                const auto &source = glob.sourceCelRecords[cel];
                const auto &packet = glob.derivedCelPackets[cel];
                for (std::size_t pointIndex = 0; pointIndex < source.points.size(); ++pointIndex)
                {
                    const std::size_t edge = static_cast<std::size_t>(packet.firstEdge) + pointIndex / 4;
                    if (edge >= result.skinnedCelEdges.size())
                        continue;
                    const auto &influence = source.points[pointIndex];
                    glm::vec3 localPoint(influence.position[0], influence.position[1], influence.position[2]);
                    for (std::size_t pose = 0; pose < poseWeights.size(); ++pose)
                    {
                        const std::size_t deltaIndex = pointIndex * poseWeights.size() + pose;
                        if (deltaIndex >= source.posePositionDeltas.size())
                            break;
                        const auto &delta = source.posePositionDeltas[deltaIndex];
                        localPoint += glm::vec3(delta[0], delta[1], delta[2]) * poseWeights[pose];
                    }
                    const glm::vec3 point = glm::vec3(sourceToWorld * glm::vec4(localPoint, 1.0f));
                    const glm::vec3 skinned = skinPoint(point, influence.boneIndices, influence.boneWeights);
                    auto &destination = result.skinnedCelEdges[edge].points[pointIndex & 3u];
                    destination[0] = skinned.x;
                    destination[1] = skinned.y;
                    destination[2] = skinned.z;
                }
            }
        }
    }
    return result;
}
