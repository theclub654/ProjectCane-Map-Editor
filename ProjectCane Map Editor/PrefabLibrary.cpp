#include "PrefabLibrary.h"

#include "EditorOptions.h"
#include "ResolvedScene.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_set>
#include <unordered_map>
#include <type_traits>

namespace
{
std::uint64_t HashPrefab(const std::string &name, const BrxObjectRecord &root)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto value : name)
    {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<std::uint16_t>(root.cid);
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint16_t>(root.oid);
    hash *= 1099511628211ull;
    return hash;
}

void RemapObjectReferences(BrxObjectRecord &object, const std::unordered_map<std::int16_t, std::int16_t> &oidRemap)
{
    const auto remap = [&](std::int16_t &oid)
    {
        if (const auto found = oidRemap.find(oid); found != oidRemap.end())
            oid = found->second;
    };
    remap(object.alox.rotationSourceOid);
    remap(object.alox.editorFosterOid);
    remap(object.alox.ikTargetOid);
    remap(object.alox.elbowOid);
    remap(object.alox.jointParentOid);
    remap(object.alox.jointChildOid);
    for (auto &pose : object.poseCombinations)
        remap(pose.oid);
    for (auto &property : object.properties)
    {
        if (!IsObjectReferenceOption(property.eopid))
            continue;
        auto oid = static_cast<std::int16_t>(property.intValue);
        remap(oid);
        property.intValue = oid;
    }
}
}

bool PrefabLibrary::Capture(const BrxDocument &document, const std::int32_t rootObjectIndex, const std::string &name,
                            std::string &error)
{
    error.clear();
    if (rootObjectIndex < 0 || static_cast<std::size_t>(rootObjectIndex) >= document.Objects().size())
    {
        error = "Select a valid hierarchy root first.";
        return false;
    }
    std::vector<std::vector<std::int32_t>> children(document.Objects().size());
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto parent = document.Objects()[index].parentIndex;
        if (parent >= 0 && static_cast<std::size_t>(parent) < children.size() &&
            parent != static_cast<std::int32_t>(index))
            children[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(index));
    }
    std::vector<std::int32_t> hierarchy{rootObjectIndex};
    std::unordered_set<std::int32_t> visited{rootObjectIndex};
    for (std::size_t cursor = 0; cursor < hierarchy.size(); ++cursor)
        for (const auto child : children[static_cast<std::size_t>(hierarchy[cursor])])
            if (visited.insert(child).second)
                hierarchy.push_back(child);
    if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const std::int32_t index)
                    { return document.Objects()[static_cast<std::size_t>(index)].proxyRecordIndex >= 0; }))
    {
        error =
            "This hierarchy participates in a PROXY/PXR instance. Use Create linked PROXY instance so retail clone roots and overrides remain authoritative.";
        return false;
    }
    if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const std::int32_t index)
                    { return document.Objects()[static_cast<std::size_t>(index)].spliceIndex >= 0; }))
    {
        error =
            "This hierarchy owns splice code. Script modules are document-global; detach them or save the assembly after converting its behavior to a reusable module package.";
        return false;
    }
    std::unordered_map<std::int32_t, std::int32_t> localIndex;
    for (std::size_t index = 0; index < hierarchy.size(); ++index)
        localIndex.emplace(hierarchy[index], static_cast<std::int32_t>(index));

    EditorPrefab prefab;
    prefab.name = name.empty() ? "Prefab " + std::to_string(prefabs_.size() + 1) : name;
    prefab.rootLocalIndex = 0;
    prefab.stableId = HashPrefab(prefab.name, document.Objects()[static_cast<std::size_t>(rootObjectIndex)]);
    for (const auto sourceIndex : hierarchy)
    {
        auto copy = document.Objects()[static_cast<std::size_t>(sourceIndex)];
        const auto parent = localIndex.find(copy.parentIndex);
        copy.parentIndex = parent == localIndex.end() ? -1 : parent->second;
        prefab.objects.push_back(std::move(copy));
    }
    // Preserve the exact contextual target for internal references. Retail
    // worlds may legally reuse an OID in separate splice/PROXY contexts, so a
    // flat old-OID -> new-OID map is insufficient for reusable assemblies.
    ResolvedScene resolved;
    resolved.Rebuild(document);
    const auto addBinding = [&](const std::int32_t sourceIndex, const std::size_t sourceLocal, const std::int16_t oid,
                                const EditorPrefab::ReferenceBinding::Kind kind, const std::uint32_t slot)
    {
        const auto targetIndex = resolved.FindObjectFrom(oid, sourceIndex);
        const auto target = localIndex.find(targetIndex);
        if (target == localIndex.end())
            return;
        prefab.internalReferences.push_back({static_cast<std::int32_t>(sourceLocal), target->second, kind, slot});
    };
    for (std::size_t sourceLocal = 0; sourceLocal < hierarchy.size(); ++sourceLocal)
    {
        const auto sourceIndex = hierarchy[sourceLocal];
        const auto &source = document.Objects()[static_cast<std::size_t>(sourceIndex)];
        for (std::size_t propertyIndex = 0; propertyIndex < source.properties.size(); ++propertyIndex)
            if (IsObjectReferenceOption(source.properties[propertyIndex].eopid))
                addBinding(sourceIndex, sourceLocal,
                           static_cast<std::int16_t>(source.properties[propertyIndex].intValue),
                           EditorPrefab::ReferenceBinding::Kind::Property, static_cast<std::uint32_t>(propertyIndex));
        addBinding(sourceIndex, sourceLocal, source.alox.rotationSourceOid,
                   EditorPrefab::ReferenceBinding::Kind::RotationSource, 0);
        addBinding(sourceIndex, sourceLocal, source.alox.editorFosterOid, EditorPrefab::ReferenceBinding::Kind::Foster,
                   0);
        addBinding(sourceIndex, sourceLocal, source.alox.ikTargetOid, EditorPrefab::ReferenceBinding::Kind::IkTarget,
                   0);
        addBinding(sourceIndex, sourceLocal, source.alox.elbowOid, EditorPrefab::ReferenceBinding::Kind::Elbow, 0);
        addBinding(sourceIndex, sourceLocal, source.alox.jointParentOid,
                   EditorPrefab::ReferenceBinding::Kind::JointParent, 0);
        addBinding(sourceIndex, sourceLocal, source.alox.jointChildOid,
                   EditorPrefab::ReferenceBinding::Kind::JointChild, 0);
        for (std::size_t poseIndex = 0; poseIndex < source.poseCombinations.size(); ++poseIndex)
            addBinding(sourceIndex, sourceLocal, source.poseCombinations[poseIndex].oid,
                       EditorPrefab::ReferenceBinding::Kind::Pose, static_cast<std::uint32_t>(poseIndex));
    }
    std::unordered_map<std::size_t, std::size_t> globRemap;
    for (std::size_t index = 0; index < document.Globs().size(); ++index)
    {
        const auto owner = localIndex.find(document.Globs()[index].ownerObjectIndex);
        if (owner == localIndex.end())
            continue;
        auto copy = document.Globs()[index];
        copy.ownerObjectIndex = owner->second;
        globRemap[index] = prefab.globs.size();
        prefab.globs.push_back(std::move(copy));
    }
    for (auto &glob : prefab.globs)
    {
        if (const auto source = globRemap.find(glob.sourceRecordIndex); source != globRemap.end())
            glob.sourceRecordIndex = source->second;
        else if (glob.instanced)
        {
            // The source sits outside the captured hierarchy. The copied GLOB
            // already contains complete source geometry, so detach it rather
            // than retaining a document-global record index that will point at
            // an unrelated GLOB when the prefab is placed elsewhere.
            glob.instanced = false;
            glob.sourceGlobIndex = -1;
            glob.sourceRecordIndex = static_cast<std::size_t>(-1);
        }
    }
    for (const auto &source : document.Globsets())
    {
        const auto owner = localIndex.find(source.ownerObjectIndex);
        if (owner == localIndex.end())
            continue;
        auto copy = source;
        copy.ownerObjectIndex = owner->second;
        copy.globIndices.clear();
        for (const auto index : source.globIndices)
            if (const auto found = globRemap.find(index); found != globRemap.end())
                copy.globIndices.push_back(found->second);
        prefab.globsets.push_back(std::move(copy));
    }
    // GLOB instance sources are serialized as a slot in their owning
    // GLOBSET, not as sourceRecordIndex. Capturing only part of a hierarchy
    // can compact that slot list, so rebuild every serialized source slot
    // from the remapped prefab-local records. The record index remains only
    // an editor cache.
    for (std::size_t globIndex = 0; globIndex < prefab.globs.size(); ++globIndex)
    {
        auto &glob = prefab.globs[globIndex];
        if (!glob.instanced)
            continue;
        bool resolved = false;
        for (const auto &globset : prefab.globsets)
        {
            const auto instance = std::find(globset.globIndices.begin(), globset.globIndices.end(), globIndex);
            if (instance == globset.globIndices.end())
                continue;
            const auto source =
                std::find(globset.globIndices.begin(), globset.globIndices.end(), glob.sourceRecordIndex);
            if (source != globset.globIndices.end() && source < instance)
            {
                glob.sourceGlobIndex = static_cast<std::int32_t>(std::distance(globset.globIndices.begin(), source));
                resolved = true;
            }
            break;
        }
        if (!resolved)
        {
            // The geometry was captured in full, so a source that cannot be
            // represented in the prefab must become an independent GLOB.
            glob.instanced = false;
            glob.sourceGlobIndex = -1;
            glob.sourceRecordIndex = static_cast<std::size_t>(-1);
        }
    }
    for (const auto &source : document.CollisionTriangles())
    {
        const auto owner = localIndex.find(source.ownerObjectIndex);
        if (owner == localIndex.end())
            continue;
        auto copy = source;
        copy.ownerObjectIndex = owner->second;
        prefab.collision.push_back(std::move(copy));
    }
    const auto captureOwned = [&](const auto &sourceRecords, auto &destination)
    {
        for (const auto &source : sourceRecords)
        {
            const auto owner = localIndex.find(source.ownerObjectIndex);
            if (owner == localIndex.end())
                continue;
            auto copy = source;
            copy.ownerObjectIndex = owner->second;
            destination.push_back(std::move(copy));
        }
    };
    captureOwned(document.Lights(), prefab.lights);
    captureOwned(document.Cameras(), prefab.cameras);
    captureOwned(document.Checkpoints(), prefab.checkpoints);
    captureOwned(document.Warps(), prefab.warps);
    captureOwned(document.Exits(), prefab.exits);
    captureOwned(document.Scans(), prefab.scans);
    captureOwned(document.AnimationSegments(), prefab.animationSegments);
    captureOwned(document.GlobWarps(), prefab.globWarps);
    captureOwned(document.ShapePaths(), prefab.shapePaths);
    captureOwned(document.PathZones(), prefab.pathZones);
    captureOwned(document.StateMachines(), prefab.stateMachines);
    captureOwned(document.ReachMaps(), prefab.reachMaps);
    captureOwned(document.Keyholes(), prefab.keyholes);
    captureOwned(document.Effects(), prefab.effects);
    captureOwned(document.Volumes(), prefab.volumes);
    for (const auto &source : document.LaserBeams())
    {
        const auto owner = localIndex.find(source.ownerObjectIndex);
        if (owner == localIndex.end())
            continue;
        auto copy = source;
        copy.ownerObjectIndex = owner->second;
        const auto shape = localIndex.find(copy.shapeObjectIndex);
        copy.shapeObjectIndex = shape == localIndex.end() ? -1 : shape->second;
        prefab.laserBeams.push_back(std::move(copy));
    }
    for (auto &machine : prefab.stateMachines)
        for (auto &transition : machine.transitions)
        {
            if (transition.animationObjectIndex < 0)
                continue;
            const auto animation = localIndex.find(transition.animationObjectIndex);
            transition.animationObjectIndex = animation == localIndex.end() ? -1 : animation->second;
            if (transition.animationObjectIndex < 0)
                transition.ownsAnimation = false;
        }
    auto existing = std::find_if(prefabs_.begin(), prefabs_.end(),
                                 [&](const EditorPrefab &item) { return item.stableId == prefab.stableId; });
    if (existing == prefabs_.end())
        prefabs_.push_back(std::move(prefab));
    else
        *existing = std::move(prefab);
    return true;
}

EditorOperationResult PrefabLibrary::Instantiate(BrxDocument &document, const std::uint64_t prefabId,
                                                 const std::int32_t parentObjectIndex, const float *worldTransform,
                                                 const bool linked)
{
    EditorOperationResult result;
    const auto *prefab = Find(prefabId);
    if (!prefab)
    {
        result.message = "Prefab asset no longer exists.";
        return result;
    }
    const auto originalObjectCount = document.Objects().size();
    std::vector<std::int32_t> newIndices(prefab->objects.size(), -1);
    std::unordered_map<std::int16_t, std::int16_t> oidRemap;
    std::unordered_set<std::int16_t> ambiguousSourceOids;
    std::array<bool, 32768> usedOids{};
    for (const auto &object : document.Objects())
        if (object.oid >= 0)
            usedOids[static_cast<std::size_t>(object.oid)] = true;
    std::size_t nextFreeOid = 1;
    const auto allocateOid = [&]() -> std::int16_t
    {
        while (nextFreeOid < usedOids.size() && usedOids[nextFreeOid])
            ++nextFreeOid;
        if (nextFreeOid >= usedOids.size())
            return -1;
        usedOids[nextFreeOid] = true;
        return static_cast<std::int16_t>(nextFreeOid++);
    };
    float placementDelta[3]{};
    if (worldTransform && prefab->rootLocalIndex >= 0)
    {
        const auto &sourceRoot = prefab->objects[static_cast<std::size_t>(prefab->rootLocalIndex)];
        for (int axis = 0; axis < 3; ++axis)
            placementDelta[axis] = worldTransform[12 + axis] - sourceRoot.worldTransform[12 + axis];
    }
    for (std::size_t index = 0; index < prefab->objects.size(); ++index)
    {
        auto copy = prefab->objects[index];
        const auto newOid = allocateOid();
        if (newOid < 0)
        {
            document.EditableObjects().resize(originalObjectCount);
            result.message = "No free OIDs remain for prefab placement.";
            return result;
        }
        const auto sourceOid = copy.oid;
        if (!ambiguousSourceOids.contains(sourceOid))
        {
            const bool inserted = oidRemap.emplace(sourceOid, newOid).second;
            if (!inserted)
            {
                oidRemap.erase(sourceOid);
                ambiguousSourceOids.insert(sourceOid);
            }
        }
        copy.oid = newOid;
        const auto localParent = copy.parentIndex;
        copy.parentIndex = localParent >= 0 && static_cast<std::size_t>(localParent) < newIndices.size()
                               ? newIndices[static_cast<std::size_t>(localParent)]
                               : parentObjectIndex;
        copy.offset = copy.dataOffset = copy.endOffset = 0;
        copy.loaderName = "Prefab instance";
        copy.editorName.clear();
        if (worldTransform)
            for (int axis = 0; axis < 3; ++axis)
                copy.worldTransform[12 + axis] += placementDelta[axis];
        if (worldTransform && copy.so.present && copy.so.fixedPhysics)
        {
            // ProjectCane does not run UpdateGeomWorld for fFixedPhys SOs.
            // Their gameplay and camera GEOM are already world-space, so a
            // relocated prefab must move the authoritative geometry too.
            const auto translateGeometry = [&](BrxSoGeometryInfo &geometry)
            {
                for (auto &position : geometry.positions)
                    for (int axis = 0; axis < 3; ++axis)
                        position[axis] += placementDelta[axis];
                for (auto &surface : geometry.surfaces)
                    for (int axis = 0; axis < 3; ++axis)
                        surface.center[axis] += placementDelta[axis];
                for (int axis = 0; axis < 3; ++axis)
                {
                    geometry.boundsMin[axis] += placementDelta[axis];
                    geometry.boundsMax[axis] += placementDelta[axis];
                }
            };
            translateGeometry(copy.so.collisionGeometry);
            translateGeometry(copy.so.cameraGeometry);
        }
        document.EditableObjects().push_back(std::move(copy));
        newIndices[index] = static_cast<std::int32_t>(document.Objects().size() - 1);
    }
    if (prefab->rootLocalIndex >= 0 && static_cast<std::size_t>(prefab->rootLocalIndex) < newIndices.size())
    {
        auto &root = document.EditableObjects()[static_cast<std::size_t>(
            newIndices[static_cast<std::size_t>(prefab->rootLocalIndex)])];
        glm::mat4 parentWorld(1.0f);
        if (parentObjectIndex >= 0 && static_cast<std::size_t>(parentObjectIndex) < document.Objects().size())
            parentWorld =
                glm::make_mat4(document.Objects()[static_cast<std::size_t>(parentObjectIndex)].worldTransform);
        const glm::mat4 rootLocal = glm::inverse(parentWorld) * glm::make_mat4(root.worldTransform);
        std::copy(glm::value_ptr(rootLocal), glm::value_ptr(rootLocal) + 16, root.localTransform);
    }
    for (const auto index : newIndices)
        RemapObjectReferences(document.EditableObjects()[static_cast<std::size_t>(index)], oidRemap);
    for (const auto &binding : prefab->internalReferences)
    {
        if (binding.sourceLocalIndex < 0 || binding.targetLocalIndex < 0 ||
            static_cast<std::size_t>(binding.sourceLocalIndex) >= newIndices.size() ||
            static_cast<std::size_t>(binding.targetLocalIndex) >= newIndices.size())
            continue;
        auto &object = document.EditableObjects()[static_cast<std::size_t>(
            newIndices[static_cast<std::size_t>(binding.sourceLocalIndex)])];
        const auto targetOid =
            document.Objects()[static_cast<std::size_t>(newIndices[static_cast<std::size_t>(binding.targetLocalIndex)])]
                .oid;
        using Kind = EditorPrefab::ReferenceBinding::Kind;
        switch (binding.kind)
        {
        case Kind::Property:
            if (binding.slot < object.properties.size())
                object.properties[binding.slot].intValue = targetOid;
            break;
        case Kind::RotationSource:
            object.alox.rotationSourceOid = targetOid;
            break;
        case Kind::Foster:
            object.alox.editorFosterOid = targetOid;
            break;
        case Kind::IkTarget:
            object.alox.ikTargetOid = targetOid;
            break;
        case Kind::Elbow:
            object.alox.elbowOid = targetOid;
            break;
        case Kind::JointParent:
            object.alox.jointParentOid = targetOid;
            break;
        case Kind::JointChild:
            object.alox.jointChildOid = targetOid;
            break;
        case Kind::Pose:
            if (binding.slot < object.poseCombinations.size())
                object.poseCombinations[binding.slot].oid = targetOid;
            break;
        }
    }
    std::vector<std::size_t> newGlobs(prefab->globs.size());
    std::unordered_map<std::uint64_t, std::uint32_t> renderGroupRemap;
    std::uint32_t nextRenderGroup = 1;
    for (const auto &glob : document.Globs())
        nextRenderGroup = (std::max)(nextRenderGroup, glob.renderGroup + 1);
    for (std::size_t index = 0; index < prefab->globs.size(); ++index)
    {
        auto copy = prefab->globs[index];
        const auto sourceOwner = copy.ownerObjectIndex;
        if (worldTransform)
        {
            glm::mat4 sourceToWorld = glm::make_mat4(copy.sourceToWorld);
            sourceToWorld[3][0] += placementDelta[0];
            sourceToWorld[3][1] += placementDelta[1];
            sourceToWorld[3][2] += placementDelta[2];
            std::copy(glm::value_ptr(sourceToWorld), glm::value_ptr(sourceToWorld) + 16, copy.sourceToWorld);
            if (sourceOwner >= 0 && static_cast<std::size_t>(sourceOwner) < prefab->objects.size() &&
                prefab->objects[static_cast<std::size_t>(sourceOwner)].so.present &&
                prefab->objects[static_cast<std::size_t>(sourceOwner)].so.fixedPhysics)
                for (int axis = 0; axis < 3; ++axis)
                    copy.center[axis] += placementDelta[axis];
        }
        copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
        const auto sourceRenderGroup = copy.renderGroup;
        copy.renderGroup = nextRenderGroup++;
        const auto sourceGroupKey =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(sourceOwner)) << 32) | sourceRenderGroup;
        renderGroupRemap.try_emplace(sourceGroupKey, copy.renderGroup);
        newGlobs[index] = document.EditableGlobs().size();
        document.EditableGlobs().push_back(std::move(copy));
    }
    for (const auto index : newGlobs)
    {
        auto &glob = document.EditableGlobs()[index];
        if (glob.sourceRecordIndex < newGlobs.size())
            glob.sourceRecordIndex = newGlobs[glob.sourceRecordIndex];
    }
    for (auto copy : prefab->globsets)
    {
        copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
        for (auto &index : copy.globIndices)
            index = newGlobs[index];
        document.EditableGlobsets().push_back(std::move(copy));
    }
    for (auto copy : prefab->collision)
    {
        copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
        if (worldTransform)
            for (auto &position : copy.positions)
                for (int axis = 0; axis < 3; ++axis)
                    position[axis] += placementDelta[axis];
        document.EditableCollisionTriangles().push_back(std::move(copy));
    }
    const auto remapOid = [&](std::int16_t &oid)
    {
        if (const auto found = oidRemap.find(oid); found != oidRemap.end())
            oid = found->second;
    };
    const auto appendOwned = [&](const auto &sourceRecords, auto &destination)
    {
        for (auto copy : sourceRecords)
        {
            if (copy.ownerObjectIndex < 0 || static_cast<std::size_t>(copy.ownerObjectIndex) >= newIndices.size())
                continue;
            const auto sourceOwner = copy.ownerObjectIndex;
            copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
            using Record = std::decay_t<decltype(copy)>;
            if constexpr (std::is_same_v<Record, BrxCameraRecord>)
                remapOid(copy.targetOid);
            else if constexpr (std::is_same_v<Record, BrxCheckpointRecord>)
                remapOid(copy.warpOid);
            else if constexpr (std::is_same_v<Record, BrxWarpRecord>)
            {
                // A reusable placement must not silently become the world's
                // second default destination or retain an alias identity.
                copy.isDefault = false;
                copy.isSpecialDefault = false;
                copy.aliasOid = -1;
                for (auto &oid : copy.hiddenObjectOids)
                    remapOid(oid);
            }
            else if constexpr (std::is_same_v<Record, BrxAsegRecord>)
            {
                remapOid(copy.rootOid);
                for (auto &oid : copy.searchRootOids)
                    remapOid(oid);
                for (auto &channel : copy.channels)
                    remapOid(channel.targetOid);
            }
            else if constexpr (std::is_same_v<Record, BrxGlobWarpRecord>)
            {
                remapOid(copy.controllerOid);
                const auto sourceGroupKey =
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(sourceOwner)) << 32) | copy.renderGroup;
                if (const auto group = renderGroupRemap.find(sourceGroupKey); group != renderGroupRemap.end())
                    copy.renderGroup = group->second;
            }
            else if constexpr (std::is_same_v<Record, BrxRchmRecord>)
                for (auto &oid : copy.animationOids)
                    remapOid(oid);
            destination.push_back(std::move(copy));
        }
    };
    appendOwned(prefab->lights, document.EditableLights());
    appendOwned(prefab->cameras, document.EditableCameras());
    appendOwned(prefab->checkpoints, document.EditableCheckpoints());
    appendOwned(prefab->warps, document.EditableWarps());
    appendOwned(prefab->exits, document.EditableExits());
    appendOwned(prefab->scans, document.EditableScans());
    appendOwned(prefab->animationSegments, document.EditableAnimationSegments());
    appendOwned(prefab->globWarps, document.EditableGlobWarps());
    appendOwned(prefab->shapePaths, document.EditableShapePaths());
    appendOwned(prefab->pathZones, document.EditablePathZones());
    appendOwned(prefab->reachMaps, document.EditableReachMaps());
    appendOwned(prefab->keyholes, document.EditableKeyholes());
    appendOwned(prefab->effects, document.EditableEffects());
    appendOwned(prefab->volumes, document.EditableVolumes());
    for (auto copy : prefab->stateMachines)
    {
        if (copy.ownerObjectIndex < 0 || static_cast<std::size_t>(copy.ownerObjectIndex) >= newIndices.size())
            continue;
        copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
        for (auto &transition : copy.transitions)
            if (transition.animationObjectIndex >= 0 &&
                static_cast<std::size_t>(transition.animationObjectIndex) < newIndices.size())
                transition.animationObjectIndex = newIndices[static_cast<std::size_t>(transition.animationObjectIndex)];
        document.EditableStateMachines().push_back(std::move(copy));
    }
    for (auto copy : prefab->laserBeams)
    {
        if (copy.ownerObjectIndex < 0 || static_cast<std::size_t>(copy.ownerObjectIndex) >= newIndices.size())
            continue;
        copy.ownerObjectIndex = newIndices[static_cast<std::size_t>(copy.ownerObjectIndex)];
        copy.shapeObjectIndex =
            copy.shapeObjectIndex >= 0 && static_cast<std::size_t>(copy.shapeObjectIndex) < newIndices.size()
                ? newIndices[static_cast<std::size_t>(copy.shapeObjectIndex)]
                : -1;
        document.EditableLaserBeams().push_back(std::move(copy));
    }
    document.RebuildRenderPacketsFromGlobs();
    result.succeeded = true;
    result.primaryObjectIndex = newIndices[static_cast<std::size_t>(prefab->rootLocalIndex)];
    result.affectedObjectIndices = std::move(newIndices);
    result.message = "Placed prefab " + prefab->name + ".";
    if (linked)
        linkedInstances_[result.primaryObjectIndex] = prefabId;
    return result;
}

bool PrefabLibrary::MakeUnique(const std::int32_t instanceRoot)
{
    return linkedInstances_.erase(instanceRoot) != 0;
}

bool PrefabLibrary::Rename(const std::uint64_t prefabId, const std::string &name)
{
    if (name.empty())
        return false;
    const auto found = std::find_if(prefabs_.begin(), prefabs_.end(),
                                    [&](const EditorPrefab &prefab) { return prefab.stableId == prefabId; });
    if (found == prefabs_.end())
        return false;
    found->name = name;
    return true;
}

bool PrefabLibrary::Remove(const std::uint64_t prefabId)
{
    const auto found = std::find_if(prefabs_.begin(), prefabs_.end(),
                                    [&](const EditorPrefab &prefab) { return prefab.stableId == prefabId; });
    if (found == prefabs_.end())
        return false;
    prefabs_.erase(found);
    for (auto instance = linkedInstances_.begin(); instance != linkedInstances_.end();)
    {
        if (instance->second == prefabId)
            instance = linkedInstances_.erase(instance);
        else
            ++instance;
    }
    return true;
}

const EditorPrefab *PrefabLibrary::Find(const std::uint64_t stableId) const
{
    const auto found = std::find_if(prefabs_.begin(), prefabs_.end(),
                                    [&](const EditorPrefab &prefab) { return prefab.stableId == stableId; });
    return found == prefabs_.end() ? nullptr : &*found;
}

std::uint64_t PrefabLibrary::LinkedPrefab(const std::int32_t instanceRoot) const
{
    const auto found = linkedInstances_.find(instanceRoot);
    return found == linkedInstances_.end() ? 0 : found->second;
}
