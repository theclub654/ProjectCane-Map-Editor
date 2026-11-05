#include "EditorDocumentOperations.h"
#include "PathZoneRuntime.h"

#include "BrxDocument.h"
#include "EditorOptions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr float kIdentity[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

bool ValidObjectIndex(const BrxDocument &document, const std::int32_t index)
{
    return index >= 0 && static_cast<std::size_t>(index) < document.Objects().size();
}

std::size_t CountSpliceLookupReferences(const BrxDocument &document,
                                       const std::unordered_set<std::int16_t> &oids)
{
    if (oids.empty())
        return 0;
    const auto &nodes = document.SpliceNodes();
    const auto valid = [&](const std::int32_t index)
        { return index >= 0 && static_cast<std::size_t>(index) < nodes.size(); };
    std::vector<std::int32_t> pending;
    for (const auto &module : document.SpliceModules())
        pending.push_back(module.rootNode);
    std::unordered_set<std::int32_t> visited;
    std::size_t count = 0;
    while (!pending.empty())
    {
        const auto index = pending.back();
        pending.pop_back();
        if (!valid(index) || !visited.insert(index).second)
            continue;
        const auto &node = nodes[static_cast<std::size_t>(index)];
        pending.push_back(node.childNode);
        pending.push_back(node.nextNode);
        if (node.tag != 30 || !valid(node.childNode))
            continue;
        const auto &builtin = nodes[static_cast<std::size_t>(node.childNode)];
        if (builtin.tag != 29 || builtin.intValue < 103 || builtin.intValue > 106 || !valid(builtin.nextNode))
            continue;
        const auto &argument = nodes[static_cast<std::size_t>(builtin.nextNode)];
        if (argument.tag == 20 && oids.contains(static_cast<std::int16_t>(argument.intValue)))
            ++count;
    }
    return count;
}

std::string SpliceRemovedTargetNotice(const BrxDocument &document, std::unordered_set<std::int16_t> removedOids)
{
    // Duplicate OIDs are valid in contextual/PROXY lookup. Do not claim that
    // a target disappeared when another decoded object still carries its OID.
    for (const auto &object : document.Objects())
        removedOids.erase(object.oid);
    const auto count = CountSpliceLookupReferences(document, removedOids);
    return count == 0 ? std::string{} : " Splice contains " + std::to_string(count) +
        " literal lookup expression(s) for OIDs no longer present in the decoded scene."
        " Review those targets; runtime-created objects and computed lookups require separate review.";
}

// Only retarget the copied expressions. Existing modules may deliberately
// address the original hierarchy and must not be changed by package import.
void RemapSpliceLookupOids(BrxDocument &document, const std::size_t firstNode,
                          const std::unordered_map<std::int16_t, std::int16_t> &oidRemap)
{
    auto &nodes = document.EditableSpliceNodes();
    const auto valid = [&](const std::int32_t index)
        { return index >= 0 && static_cast<std::size_t>(index) < nodes.size(); };
    for (std::size_t index = firstNode; index < nodes.size(); ++index)
    {
        const auto &wrapper = nodes[index];
        if (wrapper.tag != 30 || !valid(wrapper.childNode))
            continue;
        const auto &builtin = nodes[static_cast<std::size_t>(wrapper.childNode)];
        if (builtin.tag != 29 || builtin.intValue < 103 || builtin.intValue > 106 || !valid(builtin.nextNode))
            continue;
        auto &argument = nodes[static_cast<std::size_t>(builtin.nextNode)];
        if (argument.tag != 20)
            continue;
        if (const auto mapped = oidRemap.find(static_cast<std::int16_t>(argument.intValue)); mapped != oidRemap.end())
        {
            argument.intValue = mapped->second;
            argument.modified = true;
        }
    }
}

bool IsMaterializedProxyClone(const BrxDocument &document, const BrxProxyRecord &proxy)
{
    return ValidObjectIndex(document, proxy.ownerObjectIndex) &&
           document.Objects()[static_cast<std::size_t>(proxy.ownerObjectIndex)].proxyClone;
}

void RebuildProxySourceListIndices(BrxDocument &document)
{
    // AddSwProxySource appends definitions to SW::apsl in stream order.  The
    // numeric index is therefore derived state, not a persistent asset ID.
    // Materialized nested PROXY records came from PloCloneLo and are not read
    // from the BRX again, so they must not register duplicate source entries.
    std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> sources;
    std::int32_t nextSourceIndex = 0;
    for (std::size_t proxyIndex = 0; proxyIndex < document.EditableProxies().size(); ++proxyIndex)
    {
        auto &proxy = document.EditableProxies()[proxyIndex];
        if (IsMaterializedProxyClone(document, proxy))
            continue;
        for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
        {
            auto &root = proxy.roots[rootIndex];
            if (root.kind != BrxProxyRootKind::InlineSource)
                continue;
            root.sourceListIndex = nextSourceIndex;
            root.sourceProxyRecordIndex = static_cast<std::int32_t>(proxyIndex);
            root.sourceProxyRootIndex = static_cast<std::int32_t>(rootIndex);
            sources.emplace_back(root.sourceProxyRecordIndex, root.sourceProxyRootIndex, nextSourceIndex++);
        }
    }
    for (auto &proxy : document.EditableProxies())
    {
        if (IsMaterializedProxyClone(document, proxy))
            continue;
        for (auto &root : proxy.roots)
        {
            if (root.kind != BrxProxyRootKind::SourceReference)
                continue;
            const auto source = std::find_if(sources.begin(), sources.end(),
                                             [&](const auto &entry)
                                             {
                                                 return std::get<0>(entry) == root.sourceProxyRecordIndex &&
                                                        std::get<1>(entry) == root.sourceProxyRootIndex;
                                             });
            root.sourceListIndex = source == sources.end() ? -1 : std::get<2>(*source);
        }
    }
}

void RepairProxySourceIdentities(BrxDocument &document)
{
    for (auto &proxy : document.EditableProxies())
        for (auto &root : proxy.roots)
        {
            if (root.kind != BrxProxyRootKind::SourceReference ||
                !ValidObjectIndex(document, root.instantiatedRootObjectIndex))
                continue;
            const auto sourceObjectIndex =
                document.Objects()[static_cast<std::size_t>(root.instantiatedRootObjectIndex)].proxySourceObjectIndex;
            if (!ValidObjectIndex(document, sourceObjectIndex))
                continue;
            std::int32_t matchedProxy = -1;
            std::int32_t matchedRoot = -1;
            for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
            {
                // Nested PROXY records materialized while cloning a reusable
                // source are runtime/editor instances, not source-pool
                // definitions.  Their roots can retain the same source object
                // identity and must never win this authored-source lookup.
                if (IsMaterializedProxyClone(document, document.Proxies()[proxyIndex]))
                    continue;
                for (std::size_t rootIndex = 0; rootIndex < document.Proxies()[proxyIndex].roots.size(); ++rootIndex)
                {
                    const auto &candidate = document.Proxies()[proxyIndex].roots[rootIndex];
                    if (candidate.kind != BrxProxyRootKind::InlineSource ||
                        candidate.instantiatedRootObjectIndex != sourceObjectIndex)
                        continue;
                    matchedProxy = static_cast<std::int32_t>(proxyIndex);
                    matchedRoot = static_cast<std::int32_t>(rootIndex);
                }
            }
            if (matchedProxy < 0)
                continue;
            root.sourceProxyRecordIndex = matchedProxy;
            root.sourceProxyRootIndex = matchedRoot;
        }
}

void SynchronizeWarpAnimationCounts(BrxDocument &document)
{
    for (auto &warp : document.EditableWarps())
    {
        const auto count =
            std::count_if(document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &object)
                          { return object.parentIndex == warp.ownerObjectIndex && CidDerivesFrom(object.cid, 131); });
        warp.embeddedAnimationCount = static_cast<std::int16_t>(
            (std::min)(count, static_cast<decltype(count)>(std::numeric_limits<std::int16_t>::max())));
    }
}

void SynchronizeExitAnimationCounts(BrxDocument &document)
{
    for (auto &exit : document.EditableExits())
    {
        const auto count =
            std::count_if(document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &object)
                          { return object.parentIndex == exit.ownerObjectIndex && CidDerivesFrom(object.cid, 131); });
        exit.embeddedAnimationCount = static_cast<std::int16_t>(
            (std::min)(count, static_cast<decltype(count)>(std::numeric_limits<std::int16_t>::max())));
    }
}

std::vector<std::int32_t> CollectHierarchy(const BrxDocument &document, const std::int32_t root)
{
    std::vector<std::int32_t> result;
    if (!ValidObjectIndex(document, root))
        return result;
    result.push_back(root);
    std::vector<bool> visited(document.Objects().size(), false);
    visited[static_cast<std::size_t>(root)] = true;
    for (std::size_t cursor = 0; cursor < result.size(); ++cursor)
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
            if (!visited[index] && document.Objects()[index].parentIndex == result[cursor])
            {
                visited[index] = true;
                result.push_back(static_cast<std::int32_t>(index));
            }
    return result;
}

void RemapOwner(std::int32_t &owner, const std::vector<std::int32_t> &remap)
{
    if (owner < 0)
        return;
    owner = static_cast<std::size_t>(owner) < remap.size() ? remap[owner] : -1;
}

template <class T> void RemoveDeletedOwners(std::vector<T> &records, const std::unordered_set<std::int32_t> &removed)
{
    records.erase(std::remove_if(records.begin(), records.end(), [&](const T &record)
                                 { return record.ownerObjectIndex >= 0 && removed.contains(record.ownerObjectIndex); }),
                  records.end());
}

void RemoveDerivedRecordsForDeletedObjects(BrxDocument &document, const std::unordered_set<std::int32_t> &removed)
{
    auto &proxies = document.EditableProxies();
    std::vector<std::int32_t> proxyRemap(proxies.size(), -1);
    std::vector<BrxProxyRecord> survivingProxies;
    survivingProxies.reserve(proxies.size());
    for (std::size_t index = 0; index < proxies.size(); ++index)
    {
        if (proxies[index].ownerObjectIndex >= 0 && removed.contains(proxies[index].ownerObjectIndex))
            continue;
        proxyRemap[index] = static_cast<std::int32_t>(survivingProxies.size());
        survivingProxies.push_back(std::move(proxies[index]));
    }
    proxies = std::move(survivingProxies);
    for (auto &object : document.EditableObjects())
    {
        if (object.proxyRecordIndex < 0)
            continue;
        object.proxyRecordIndex = static_cast<std::size_t>(object.proxyRecordIndex) < proxyRemap.size()
                                      ? proxyRemap[static_cast<std::size_t>(object.proxyRecordIndex)]
                                      : -1;
        if (object.proxyRecordIndex < 0)
        {
            object.proxyRootIndex = -1;
            object.proxySourceObjectIndex = -1;
            object.proxyClone = false;
        }
    }
    for (auto &proxy : proxies)
        for (auto &root : proxy.roots)
        {
            if (root.sourceProxyRecordIndex < 0)
                continue;
            root.sourceProxyRecordIndex = static_cast<std::size_t>(root.sourceProxyRecordIndex) < proxyRemap.size()
                                              ? proxyRemap[static_cast<std::size_t>(root.sourceProxyRecordIndex)]
                                              : -1;
        }
    RemoveDeletedOwners(document.EditableCollisionTriangles(), removed);
    RemoveDeletedOwners(document.EditableLights(), removed);
    RemoveDeletedOwners(document.EditableCameras(), removed);
    RemoveDeletedOwners(document.EditableCheckpoints(), removed);
    RemoveDeletedOwners(document.EditableWarps(), removed);
    RemoveDeletedOwners(document.EditableExits(), removed);
    RemoveDeletedOwners(document.EditableVolumes(), removed);
    RemoveDeletedOwners(document.EditableAnimationSegments(), removed);
    RemoveDeletedOwners(document.EditableScans(), removed);
    RemoveDeletedOwners(document.EditableEffects(), removed);
    RemoveDeletedOwners(document.EditableShapePaths(), removed);
    RemoveDeletedOwners(document.EditablePathZones(), removed);
    RemoveDeletedOwners(document.EditableStateMachines(), removed);
    RemoveDeletedOwners(document.EditableReachMaps(), removed);
    RemoveDeletedOwners(document.EditableKeyholes(), removed);
    RemoveDeletedOwners(document.EditableGlobWarps(), removed);
    auto &laserBeams = document.EditableLaserBeams();
    laserBeams.erase(
        std::remove_if(laserBeams.begin(), laserBeams.end(), [&](const BrxLaserBeamRecord &beam)
                       { return removed.contains(beam.ownerObjectIndex) || removed.contains(beam.shapeObjectIndex); }),
        laserBeams.end());

    auto &globs = document.EditableGlobs();
    std::vector<std::size_t> globRemap(globs.size(), static_cast<std::size_t>(-1));
    std::vector<BrxGlobRecord> survivingGlobs;
    survivingGlobs.reserve(globs.size());
    for (std::size_t index = 0; index < globs.size(); ++index)
    {
        if (globs[index].ownerObjectIndex >= 0 && removed.contains(globs[index].ownerObjectIndex))
            continue;
        globRemap[index] = survivingGlobs.size();
        survivingGlobs.push_back(std::move(globs[index]));
    }
    globs = std::move(survivingGlobs);
    // sourceRecordIndex is a document-global GLOB record index, while
    // sourceGlobIndex is the serialized slot inside the owning GLOBSET.
    // Compacting the global array changes only the former.  Keeping its old
    // value makes a valid instance appear to reference a different source
    // even though its local source slot is unchanged.
    for (auto &glob : globs)
    {
        if (!glob.instanced)
            continue;
        const auto oldSource = glob.sourceRecordIndex;
        glob.sourceRecordIndex = oldSource < globRemap.size() ? globRemap[oldSource] : static_cast<std::size_t>(-1);
    }
    auto &globsets = document.EditableGlobsets();
    RemoveDeletedOwners(globsets, removed);
    for (auto &globset : globsets)
    {
        std::vector<std::size_t> remapped;
        for (const auto oldIndex : globset.globIndices)
            if (oldIndex < globRemap.size() && globRemap[oldIndex] != static_cast<std::size_t>(-1))
                remapped.push_back(globRemap[oldIndex]);
        globset.globIndices = std::move(remapped);
    }
}

void RemapDocumentObjectIndices(BrxDocument &document, const std::vector<std::int32_t> &remap)
{
    for (auto &object : document.EditableObjects())
    {
        RemapOwner(object.parentIndex, remap);
        RemapOwner(object.proxyRuntimeParentIndex, remap);
        RemapOwner(object.proxySourceObjectIndex, remap);
        RemapOwner(object.alox.editorFosterObjectIndex, remap);
    }
    for (auto &proxy : document.EditableProxies())
    {
        RemapOwner(proxy.ownerObjectIndex, remap);
        for (auto &root : proxy.roots)
        {
            const std::size_t oldBegin = root.objectBegin;
            const std::size_t oldEnd = (std::min)(remap.size(), oldBegin + root.objectCount);
            std::int32_t firstSurvivor = -1;
            std::uint32_t survivorCount = 0;
            for (std::size_t oldIndex = oldBegin; oldIndex < oldEnd; ++oldIndex)
            {
                if (remap[oldIndex] < 0)
                    continue;
                if (firstSurvivor < 0)
                    firstSurvivor = remap[oldIndex];
                ++survivorCount;
            }
            root.objectBegin = firstSurvivor >= 0 ? static_cast<std::uint32_t>(firstSurvivor) : 0;
            root.objectCount = survivorCount;
            RemapOwner(root.instantiatedRootObjectIndex, remap);
        }
        for (auto &child : proxy.attachedChildObjectIndices)
            RemapOwner(child, remap);
        proxy.attachedChildObjectIndices.erase(
            std::remove(proxy.attachedChildObjectIndices.begin(), proxy.attachedChildObjectIndices.end(), -1),
            proxy.attachedChildObjectIndices.end());
        for (auto &overrideRecord : proxy.overrides)
            RemapOwner(overrideRecord.targetObjectIndex, remap);
        proxy.overrides.erase(std::remove_if(proxy.overrides.begin(), proxy.overrides.end(),
                                             [](const BrxProxyOverrideRecord &overrideRecord)
                                             { return overrideRecord.targetObjectIndex < 0; }),
                              proxy.overrides.end());
    }
    for (auto &globset : document.EditableGlobsets())
        RemapOwner(globset.ownerObjectIndex, remap);
    for (auto &glob : document.EditableGlobs())
        RemapOwner(glob.ownerObjectIndex, remap);
    for (auto &triangle : document.EditableCollisionTriangles())
        RemapOwner(triangle.ownerObjectIndex, remap);
    for (auto &light : document.EditableLights())
        RemapOwner(light.ownerObjectIndex, remap);
    for (auto &camera : document.EditableCameras())
        RemapOwner(camera.ownerObjectIndex, remap);
    for (auto &checkpoint : document.EditableCheckpoints())
        RemapOwner(checkpoint.ownerObjectIndex, remap);
    for (auto &warp : document.EditableWarps())
        RemapOwner(warp.ownerObjectIndex, remap);
    for (auto &exit : document.EditableExits())
        RemapOwner(exit.ownerObjectIndex, remap);
    for (auto &volume : document.EditableVolumes())
        RemapOwner(volume.ownerObjectIndex, remap);
    for (auto &segment : document.EditableAnimationSegments())
        RemapOwner(segment.ownerObjectIndex, remap);
    for (auto &scan : document.EditableScans())
        RemapOwner(scan.ownerObjectIndex, remap);
    for (auto &effect : document.EditableEffects())
        RemapOwner(effect.ownerObjectIndex, remap);
    for (auto &shape : document.EditableShapePaths())
        RemapOwner(shape.ownerObjectIndex, remap);
    for (auto &zone : document.EditablePathZones())
        RemapOwner(zone.ownerObjectIndex, remap);
    for (auto &machine : document.EditableStateMachines())
    {
        RemapOwner(machine.ownerObjectIndex, remap);
        for (auto &transition : machine.transitions)
        {
            if (!transition.ownsAnimation)
                continue;
            RemapOwner(transition.animationObjectIndex, remap);
            if (transition.animationObjectIndex >= 0)
                continue;
            // The inline ASEG was explicitly deleted with its hierarchy.  The
            // owns-animation bit controls whether an ASEG follows this SM
            // transition in the BRX stream, so keeping it set would make the
            // serialized transition claim a child that no longer exists.
            transition.ownsAnimation = false;
            machine.modified = true;
        }
    }
    for (auto &reach : document.EditableReachMaps())
        RemapOwner(reach.ownerObjectIndex, remap);
    for (auto &keyhole : document.EditableKeyholes())
        RemapOwner(keyhole.ownerObjectIndex, remap);
    for (auto &warp : document.EditableGlobWarps())
        RemapOwner(warp.ownerObjectIndex, remap);
    for (auto &beam : document.EditableLaserBeams())
    {
        RemapOwner(beam.ownerObjectIndex, remap);
        RemapOwner(beam.shapeObjectIndex, remap);
    }
}

void RemapKnownOidProperties(std::vector<BrxPropertyRecord> &properties,
                             const std::unordered_map<std::int16_t, std::int16_t> &oidRemap)
{
    const auto remapOid = [&](std::int16_t &oid)
    {
        if (const auto found = oidRemap.find(oid); found != oidRemap.end())
            oid = found->second;
    };
    // EOPID semantics determine whether an integer is an OID. The centralized
    // schema is the only authority; never rewrite arbitrary integer options.
    for (auto &property : properties)
    {
        // Guard SetUser records carry typed OID arguments, not a scalar OID.
        // Remap only OTYP_Oid slots; SGS, ZPK and attack flags are integers too.
        if (property.kind == BrxPropertyKind::Dynamic &&
            (property.eopid == 656 || property.eopid == 660 || property.eopid == 661 || property.eopid == 662 ||
             property.eopid == 664 || property.eopid == 703))
        {
            for (auto &argument : property.arguments)
            {
                if (argument.type != 6 || argument.intValue == -1)
                    continue;
                if (argument.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                    argument.intValue > (std::numeric_limits<std::int16_t>::max)())
                    continue;
                auto oid = static_cast<std::int16_t>(argument.intValue);
                remapOid(oid);
                if (argument.intValue != oid)
                {
                    argument.intValue = oid;
                    property.modified = true;
                }
            }
            continue;
        }
        if (!IsObjectReferenceOption(property.eopid) && !IsObjectNameFilterOption(property.eopid))
            continue;
        if (property.kind == BrxPropertyKind::Int16 || property.kind == BrxPropertyKind::TypedInt16)
        {
            auto oid = static_cast<std::int16_t>(property.intValue);
            remapOid(oid);
            property.intValue = oid;
        }
        else if (property.kind == BrxPropertyKind::Int32)
        {
            auto oid = static_cast<std::int16_t>(property.intValue);
            remapOid(oid);
            property.intValue = oid;
        }
    }
}

void RemapKnownOidReferences(BrxObjectRecord &object, const std::unordered_map<std::int16_t, std::int16_t> &oidRemap)
{
    const auto remapOid = [&](std::int16_t &oid)
    {
        if (const auto found = oidRemap.find(oid); found != oidRemap.end())
            oid = found->second;
    };
    remapOid(object.alox.rotationSourceOid);
    remapOid(object.alox.editorFosterOid);
    remapOid(object.alox.ikTargetOid);
    remapOid(object.alox.elbowOid);
    remapOid(object.alox.jointParentOid);
    remapOid(object.alox.jointChildOid);
    for (auto &combination : object.poseCombinations)
        remapOid(combination.oid);
    for (auto &event : object.dialog.events)
    {
        remapOid(event.primaryOid);
        remapOid(event.replacementOid);
        remapOid(event.speakerOid);
        remapOid(event.lipsyncAnimationOid);
        remapOid(event.focusOid);
        remapOid(event.anchorPointOid);
    }
    RemapKnownOidProperties(object.properties, oidRemap);
}

template <class T>
void DuplicateOwnedRecords(std::vector<T> &records, const std::unordered_map<std::int32_t, std::int32_t> &objectRemap)
{
    const auto originals = records;
    for (const auto &record : originals)
    {
        const auto owner = objectRemap.find(record.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        auto copy = record;
        copy.ownerObjectIndex = owner->second;
        records.push_back(std::move(copy));
    }
}

void DuplicateOwnedSceneData(BrxDocument &document, const std::unordered_map<std::int32_t, std::int32_t> &objectRemap,
                             const std::unordered_map<std::int16_t, std::int16_t> &oidRemap,
                             const bool preserveProxyIdentity = false)
{
    DuplicateOwnedRecords(document.EditableCollisionTriangles(), objectRemap);
    DuplicateOwnedRecords(document.EditableLights(), objectRemap);
    const auto originalCameras = document.Cameras();
    for (auto camera : originalCameras)
    {
        const auto owner = objectRemap.find(camera.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        camera.ownerObjectIndex = owner->second;
        if (const auto target = oidRemap.find(camera.targetOid); target != oidRemap.end())
            camera.targetOid = target->second;
        document.EditableCameras().push_back(std::move(camera));
    }
    const auto originalCheckpoints = document.Checkpoints();
    for (auto checkpoint : originalCheckpoints)
    {
        const auto owner = objectRemap.find(checkpoint.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        checkpoint.ownerObjectIndex = owner->second;
        if (const auto warp = oidRemap.find(checkpoint.warpOid); warp != oidRemap.end())
            checkpoint.warpOid = warp->second;
        document.EditableCheckpoints().push_back(std::move(checkpoint));
    }
    DuplicateOwnedRecords(document.EditableExits(), objectRemap);
    DuplicateOwnedRecords(document.EditableVolumes(), objectRemap);
    const auto originalWarps = document.Warps();
    for (const auto &source : originalWarps)
    {
        const auto owner = objectRemap.find(source.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        BrxWarpRecord copy = source;
        copy.ownerObjectIndex = owner->second;
        if (!preserveProxyIdentity)
        {
            copy.isDefault = false;
            copy.isSpecialDefault = false;
            copy.aliasOid = -1;
            for (auto &hiddenOid : copy.hiddenObjectOids)
                if (const auto remapped = oidRemap.find(hiddenOid); remapped != oidRemap.end())
                    hiddenOid = remapped->second;
        }
        document.EditableWarps().push_back(std::move(copy));
    }
    const auto originalAnimationSegments = document.AnimationSegments();
    for (auto segment : originalAnimationSegments)
    {
        const auto owner = objectRemap.find(segment.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        segment.ownerObjectIndex = owner->second;
        const auto remapOid = [&](std::int16_t &oid)
        {
            if (const auto mapped = oidRemap.find(oid); mapped != oidRemap.end())
                oid = mapped->second;
        };
        remapOid(segment.rootOid);
        for (auto &oid : segment.searchRootOids)
            remapOid(oid);
        for (auto &channel : segment.channels)
            remapOid(channel.targetOid);
        document.EditableAnimationSegments().push_back(std::move(segment));
    }
    DuplicateOwnedRecords(document.EditableScans(), objectRemap);
    DuplicateOwnedRecords(document.EditableEffects(), objectRemap);
    DuplicateOwnedRecords(document.EditableShapePaths(), objectRemap);
    DuplicateOwnedRecords(document.EditablePathZones(), objectRemap);
    const auto originalStateMachines = document.StateMachines();
    for (auto machine : originalStateMachines)
    {
        const auto owner = objectRemap.find(machine.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        machine.ownerObjectIndex = owner->second;
        for (auto &transition : machine.transitions)
            if (transition.animationObjectIndex >= 0)
            {
                const auto animation = objectRemap.find(transition.animationObjectIndex);
                transition.animationObjectIndex = animation == objectRemap.end() ? -1 : animation->second;
                if (transition.animationObjectIndex < 0)
                    transition.ownsAnimation = false;
            }
        document.EditableStateMachines().push_back(std::move(machine));
    }
    const auto originalReachMaps = document.ReachMaps();
    for (auto reach : originalReachMaps)
    {
        const auto owner = objectRemap.find(reach.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        reach.ownerObjectIndex = owner->second;
        for (auto &oid : reach.animationOids)
            if (const auto mapped = oidRemap.find(oid); mapped != oidRemap.end())
                oid = mapped->second;
        document.EditableReachMaps().push_back(std::move(reach));
    }
    DuplicateOwnedRecords(document.EditableKeyholes(), objectRemap);

    const auto originalLaserBeams = document.LaserBeams();
    for (const auto &sourceBeam : originalLaserBeams)
    {
        const auto owner = objectRemap.find(sourceBeam.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        BrxLaserBeamRecord copy = sourceBeam;
        copy.ownerObjectIndex = owner->second;
        if (const auto shape = objectRemap.find(sourceBeam.shapeObjectIndex); shape != objectRemap.end())
            copy.shapeObjectIndex = shape->second;
        document.EditableLaserBeams().push_back(std::move(copy));
    }

    const auto originalGlobsets = document.Globsets();
    for (const auto &sourceGlobset : originalGlobsets)
    {
        const auto owner = objectRemap.find(sourceGlobset.ownerObjectIndex);
        if (owner == objectRemap.end())
            continue;
        BrxGlobsetRecord copy = sourceGlobset;
        copy.ownerObjectIndex = owner->second;
        copy.globIndices.clear();
        for (const auto sourceGlobIndex : sourceGlobset.globIndices)
        {
            const auto newGlobIndex = document.DuplicateGlobAsInstance(sourceGlobIndex);
            if (newGlobIndex >= document.EditableGlobs().size())
                continue;
            auto &duplicatedGlob = document.EditableGlobs()[newGlobIndex];
            duplicatedGlob.ownerObjectIndex = owner->second;
            if (const auto controller = oidRemap.find(duplicatedGlob.warpControllerOid); controller != oidRemap.end())
                duplicatedGlob.warpControllerOid = controller->second;
            // DuplicateGlobAsInstance also clones an optional WRBG binding.
            // Its first owner is the source GLOB owner; retarget it alongside
            // the model and preserve a hierarchy-local controller reference.
            const auto duplicatedWarp = std::find_if(
                document.EditableGlobWarps().rbegin(), document.EditableGlobWarps().rend(),
                [&](const BrxGlobWarpRecord &warp) { return warp.renderGroup == duplicatedGlob.renderGroup; });
            if (duplicatedWarp != document.EditableGlobWarps().rend())
            {
                duplicatedWarp->ownerObjectIndex = owner->second;
                if (const auto controller = oidRemap.find(duplicatedWarp->controllerOid); controller != oidRemap.end())
                    duplicatedWarp->controllerOid = controller->second;
            }
            copy.globIndices.push_back(newGlobIndex);
        }
        document.EditableGlobsets().push_back(std::move(copy));
    }
    document.RebuildRenderPacketsFromGlobs();
}
}

std::int16_t EditorDocumentOperations::AllocateOid(const BrxDocument &document)
{
    std::array<bool, 32768> used{};
    for (const auto &object : document.Objects())
        if (object.oid >= 0)
            used[static_cast<std::size_t>(object.oid)] = true;
    for (std::size_t oid = 1; oid < used.size(); ++oid)
        if (!used[oid])
            return static_cast<std::int16_t>(oid);
    return -1;
}

EditorOperationResult EditorDocumentOperations::CreateObject(BrxDocument &document, const std::int16_t cid,
                                                             const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    EditorOperationResult result;
    if (cid < 0 || cid > 147)
    {
        result.message = "The requested CID is outside ProjectCane's retail class table.";
        return result;
    }
    // BASIC is only the runtime vtable/introspection root. ProjectCane's BRX
    // loader casts every authored object class to VTLO and calls pfnNewLo;
    // CID_BASIC has no VTLO tail or NewBasic constructor and cannot be loaded.
    if (cid == 0)
    {
        result.message = "BASIC is an abstract runtime foundation and cannot be created as a BRX object.";
        return result;
    }
    if (cid == 108)
    {
        result.message =
            "SW is the scene-world container encoded by the BRX header, options, resources, and root list; it cannot be created as a child object.";
        return result;
    }
    if (cid == 109)
    {
        result.message =
            "CM is the single runtime camera manager ProjectCane creates as OID 4 while loading SW. Edit persistent camera defaults on Scene World or create a CAMERA object for authored shots.";
        return result;
    }
    if (cid == 30)
    {
        result.message =
            "RIPG is a reusable runtime particle-group owner allocated by SW. Author RIP/model-particle settings on an EMITTER instead of creating a standalone RIPG record.";
        return result;
    }
    if (cid == 132)
    {
        result.message =
            "ASEGBL is allocated at runtime by the animation blender. Create/edit ASEG records and blend bindings instead of authoring an empty ASEGBL object.";
        return result;
    }
    if (cid == 133)
    {
        result.message =
            "VISZONE is not a standalone authored record. Create and edit visibility zones through the world's VISMAP structure.";
        return result;
    }
    // JT is not a self-contained record. PostJtLoad immediately dereferences
    // fixed-OID gut, hand, foot, cane, hook and physics children, in addition
    // to resolving a large stock animation rig. A bare generic object would
    // load far enough to crash rather than behaving as a usable player. Keep
    // exact editing available on the world's existing complete JT rig.
    if (cid == 7)
    {
        result.message =
            "JT requires the world's complete fixed-OID player rig. Edit the existing JT hierarchy; a bare or ordinarily duplicated JT is not runtime-safe.";
        return result;
    }
    if (cid == 83)
    {
        result.message =
            "PROXY cannot be created as an empty object. Create a reusable source hierarchy or a linked instance through the PROXY workflow.";
        return result;
    }
    if (cid == 134)
    {
        result.message =
            "VISMAP is the world's singleton visibility structure. Edit or rebuild the existing VISMAP through the VISMAP workflow.";
        return result;
    }
    if (cid == 139)
    {
        result.message =
            "RCHM cannot be created as an empty record. Duplicate and edit a validated reach map until the dedicated lattice/tetrahedron/BSP builder can produce the complete retail topology.";
        return result;
    }
    if (cid == 107)
    {
        result.message =
            "UNK11 is runtime integrity/anti-cheat infrastructure and is intentionally excluded from level authoring.";
        return result;
    }
    // ASEGA and SMA are runtime products assembled from authored ASEG and SM
    // payloads.  They have class-table entries for runtime dispatch, but no
    // legal standalone world-object record in LoadSwObjectsFromBrx.
    if (cid == 146 || cid == 147)
    {
        result.message = cid == 146
                             ? "ASEGA is built at runtime from ASEG data; create or edit CID 131 ASEG instead."
                             : "SMA is built at runtime from an SM definition; create or edit CID 136 SM instead.";
        return result;
    }
    if (parentIndex < -1 || (parentIndex >= 0 && !ValidObjectIndex(document, parentIndex)))
    {
        result.message = "The requested parent does not exist.";
        return result;
    }
    // LoadSwObjectsFromBrx receives an ALO* parent for ordinary recursive
    // hierarchy records. Four custom retail tails are legal exceptions:
    // EXPLG embeds arbitrary LO records, while WARP, EXIT and SM embed ASEGs.
    // Preserve those relationships; otherwise walk a placement-only LO to
    // its nearest serializable ALO ancestor.
    const auto canSerializeChild = [&](const std::int32_t candidate)
    {
        if (candidate < 0 || static_cast<std::size_t>(candidate) >= document.Objects().size())
            return false;
        const auto parentCid = document.Objects()[static_cast<std::size_t>(candidate)].cid;
        return CidDerivesFrom(parentCid, 2) || parentCid == 120 ||
               ((parentCid == 116 || parentCid == 125 || parentCid == 136) && cid == 131);
    };
    std::int32_t serializedParentIndex = parentIndex;
    for (std::size_t guard = 0; serializedParentIndex >= 0 &&
                                static_cast<std::size_t>(serializedParentIndex) < document.Objects().size() &&
                                !canSerializeChild(serializedParentIndex) && guard++ <= document.Objects().size();)
    {
        const auto next = document.Objects()[static_cast<std::size_t>(serializedParentIndex)].parentIndex;
        if (next == serializedParentIndex)
        {
            serializedParentIndex = -1;
            break;
        }
        serializedParentIndex = next;
    }
    if (serializedParentIndex >= 0 && (static_cast<std::size_t>(serializedParentIndex) >= document.Objects().size() ||
                                       !canSerializeChild(serializedParentIndex)))
        serializedParentIndex = -1;

    std::int32_t attachedProxyRecordIndex = -1;
    std::int32_t inheritedProxyRecordIndex = -1;
    std::int32_t inheritedProxyRootIndex = -1;
    std::vector<std::pair<std::int32_t, std::int32_t>> containingInlineRoots;
    if (serializedParentIndex >= 0)
    {
        const auto &serializedParent = document.Objects()[static_cast<std::size_t>(serializedParentIndex)];
        if (serializedParent.cid == 83)
        {
            const auto ownedProxy =
                std::find_if(document.Proxies().begin(), document.Proxies().end(), [&](const BrxProxyRecord &proxy)
                             { return proxy.ownerObjectIndex == serializedParentIndex; });
            if (ownedProxy == document.Proxies().end() || ownedProxy->roots.size() != 1)
            {
                result.message = "Only a single-root PROXY can own an attached child hierarchy.";
                return result;
            }
            attachedProxyRecordIndex = static_cast<std::int32_t>(std::distance(document.Proxies().begin(), ownedProxy));
            serializedParentIndex = ownedProxy->roots.front().instantiatedRootObjectIndex;
            if (!ValidObjectIndex(document, serializedParentIndex))
            {
                result.message = "The PROXY has no valid instantiated root for its attached child.";
                return result;
            }
        }

        const auto &effectiveParent = document.Objects()[static_cast<std::size_t>(serializedParentIndex)];
        if (attachedProxyRecordIndex < 0 && effectiveParent.proxyRecordIndex >= 0 &&
            effectiveParent.proxyRootIndex >= 0)
        {
            if (static_cast<std::size_t>(effectiveParent.proxyRecordIndex) >= document.Proxies().size() ||
                static_cast<std::size_t>(effectiveParent.proxyRootIndex) >=
                    document.Proxies()[static_cast<std::size_t>(effectiveParent.proxyRecordIndex)].roots.size())
            {
                result.message = "The selected PROXY parent has an invalid source identity.";
                return result;
            }
            const auto &nearestRoot = document.Proxies()[static_cast<std::size_t>(effectiveParent.proxyRecordIndex)]
                                          .roots[static_cast<std::size_t>(effectiveParent.proxyRootIndex)];
            if (nearestRoot.kind == BrxProxyRootKind::SourceReference)
            {
                result.message = "Linked PROXY instances inherit their hierarchy from the reusable source. "
                                 "Make the root unique before adding children.";
                return result;
            }
            inheritedProxyRecordIndex = effectiveParent.proxyRecordIndex;
            inheritedProxyRootIndex = effectiveParent.proxyRootIndex;
            const bool hasLinkedInstances = std::any_of(
                document.Proxies().begin(), document.Proxies().end(),
                [&](const BrxProxyRecord &candidateProxy)
                {
                    if (IsMaterializedProxyClone(document, candidateProxy))
                        return false;
                    return std::any_of(candidateProxy.roots.begin(), candidateProxy.roots.end(),
                                       [&](const BrxProxyRootRecord &candidateRoot)
                                       {
                                           return candidateRoot.kind == BrxProxyRootKind::SourceReference &&
                                                  candidateRoot.sourceProxyRecordIndex == inheritedProxyRecordIndex &&
                                                  candidateRoot.sourceProxyRootIndex == inheritedProxyRootIndex;
                                       });
                });
            if (hasLinkedInstances)
            {
                result.message = "This reusable PROXY source already has linked instances. "
                                 "Changing its hierarchy membership would require rematerializing every clone; "
                                 "make those roots unique or remove and recreate the linked instances first.";
                return result;
            }
            for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
                for (std::size_t rootIndex = 0; rootIndex < document.Proxies()[proxyIndex].roots.size(); ++rootIndex)
                {
                    const auto &root = document.Proxies()[proxyIndex].roots[rootIndex];
                    if (root.kind != BrxProxyRootKind::InlineSource)
                        continue;
                    const auto begin = static_cast<std::size_t>(root.objectBegin);
                    const auto end = begin + static_cast<std::size_t>(root.objectCount);
                    if (static_cast<std::size_t>(serializedParentIndex) >= begin &&
                        static_cast<std::size_t>(serializedParentIndex) < end)
                        containingInlineRoots.emplace_back(static_cast<std::int32_t>(proxyIndex),
                                                           static_cast<std::int32_t>(rootIndex));
                }
        }
    }
    const auto oid = AllocateOid(document);
    if (oid < 0)
    {
        result.message = "No free signed 16-bit object ID remains.";
        return result;
    }

    BrxObjectRecord object;
    object.cid = cid;
    object.oid = oid;
    object.parentIndex = serializedParentIndex;
    object.loaderImplemented = true;
    object.loaderName = "Editor-created";
    object.offset = 0;
    object.dataOffset = 0;
    object.endOffset = 0;
    glm::mat4 requestedWorld(1.0f);
    if (worldTransform)
        requestedWorld = glm::make_mat4(worldTransform);
    else if (parentIndex >= 0)
        requestedWorld = glm::make_mat4(document.Objects()[static_cast<std::size_t>(parentIndex)].worldTransform);
    const glm::mat4 parentWorld =
        serializedParentIndex >= 0
            ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(serializedParentIndex)].worldTransform)
            : glm::mat4(1.0f);
    const auto finiteMatrix = [](const glm::mat4 &matrix)
    {
        const float *values = glm::value_ptr(matrix);
        return std::all_of(values, values + 16, [](const float value) { return std::isfinite(value); });
    };
    if (!finiteMatrix(parentWorld) || !finiteMatrix(requestedWorld))
    {
        result.message = "Cannot create an object from a non-finite parent or requested transform.";
        return result;
    }
    const glm::mat4 inverseParent = glm::inverse(parentWorld);
    const glm::mat4 local = inverseParent * requestedWorld;
    if (!finiteMatrix(inverseParent) || !finiteMatrix(local))
    {
        result.message =
            "Cannot create an object under a singular parent transform or a transform that overflows local coordinates.";
        return result;
    }
    std::copy(glm::value_ptr(local), glm::value_ptr(local) + 16, object.localTransform);
    std::copy(glm::value_ptr(requestedWorld), glm::value_ptr(requestedWorld) + 16, object.worldTransform);
    object.hasAloPayload = cid == 2 || CidDerivesFrom(cid, 2);
    if (object.hasAloPayload)
    {
        // InitAlo distinguishes roots from children before InitLo runs. Match
        // those constructor defaults in the serialized payload instead of
        // creating a zero-mode ALO that behaves unlike a runtime-created one.
        object.zoneMode = serializedParentIndex < 0 ? 2u : 1u;
        object.visibilityMode = serializedParentIndex < 0 ? 2u : 0u;
        object.mrdMode = 0u;
        object.zoneMask = 0x0fffffffu;
        object.maximumRenderDistance = 1.0e10f;
        object.celBorderMaximumDistance = 2000.0f;
        // InitAlo uses FLT_MAX sentinels and LoadAloFromBrx expands them to
        // these runtime values. Preserve that distinction so a newly-created
        // object writes the retail default sentinels rather than unnecessary
        // explicit overrides with merely equivalent current values.
        object.maximumRenderDistanceDefault = true;
        object.celBorderMaximumDistanceDefault = true;
        object.renderRadiusSelf = 0.0f;
        object.renderRadiusAll = 0.0f;
    }
    object.so.present = cid == 3 || CidDerivesFrom(cid, 3);
    if (object.so.present)
    {
        // A geometry-less editor-created SO is safest as a fixed/static body.
        // Dynamic participation requires authored mass, inertia and collision;
        // exposing it with zeroed dynamic data would make AdjustSoMomint invert
        // a singular tensor as soon as the object is loaded.
        object.so.fixedPhysics = true;
        object.so.mass = 1.0f;
        object.so.inertia[0] = 1.0f;
        object.so.inertia[4] = 1.0f;
        object.so.inertia[8] = 1.0f;
    }
    if (CidDerivesFrom(cid, 73))
    {
        // InitMrkv leaves its raw vector zeroed, but an authored directional
        // slab needs a finite non-zero EOPID 1054. Give newly-created markers
        // the editor's explicit +Z orientation instead of serializing an
        // unusable zero direction.
        const auto definition = EditorOptionDefinitionForCid(cid, 1054);
        BrxPropertyRecord normal;
        normal.eopid = 1054;
        normal.kind = definition.kind;
        normal.flags = definition.flags;
        normal.argumentCount = definition.argumentCount;
        normal.editable = true;
        normal.modified = true;
        ResetPropertyValue(normal);
        normal.floatValue[2] = 1.0f;
        object.properties.push_back(std::move(normal));
    }

    std::size_t insertionIndex = document.Objects().size();
    if (inheritedProxyRecordIndex >= 0)
    {
        const auto &root = document.Proxies()[static_cast<std::size_t>(inheritedProxyRecordIndex)]
                               .roots[static_cast<std::size_t>(inheritedProxyRootIndex)];
        insertionIndex = static_cast<std::size_t>(root.objectBegin) + root.objectCount;
        insertionIndex = (std::min)(insertionIndex, document.Objects().size());
    }
    if (insertionIndex == document.Objects().size())
        document.EditableObjects().push_back(std::move(object));
    else
    {
        const auto oldObjectCount = document.Objects().size();
        std::vector<std::int32_t> remap(oldObjectCount, -1);
        for (std::size_t index = 0; index < oldObjectCount; ++index)
            remap[index] = static_cast<std::int32_t>(index >= insertionIndex ? index + 1 : index);
        document.EditableObjects().insert(
            document.EditableObjects().begin() + static_cast<std::ptrdiff_t>(insertionIndex), std::move(object));
        RemapDocumentObjectIndices(document, remap);
    }
    for (const auto &[proxyIndex, rootIndex] : containingInlineRoots)
        ++document.EditableProxies()[static_cast<std::size_t>(proxyIndex)]
              .roots[static_cast<std::size_t>(rootIndex)]
              .objectCount;
    result.succeeded = true;
    result.primaryObjectIndex = static_cast<std::int32_t>(insertionIndex);
    if (inheritedProxyRecordIndex >= 0)
    {
        auto &inserted = document.EditableObjects()[insertionIndex];
        inserted.proxyRecordIndex = inheritedProxyRecordIndex;
        inserted.proxyRootIndex = inheritedProxyRootIndex;
        inserted.proxySourceObjectIndex = result.primaryObjectIndex;
        inserted.proxyClone = false;
    }
    if (attachedProxyRecordIndex >= 0)
        document.EditableProxies()[static_cast<std::size_t>(attachedProxyRecordIndex)]
            .attachedChildObjectIndices.push_back(result.primaryObjectIndex);
    // LoadAloFromBrx unconditionally calls LoadGlobsetFromBrx.  Even an ALO
    // with no visible model therefore owns one inline GLOBSET containing zero
    // bones, poses and GLOB slots.  Keep that structural record separate from
    // "has geometry": omitting it shifts every following field and makes a
    // generic editor-created ALO impossible to serialize as a retail record.
    if (document.Objects()[static_cast<std::size_t>(result.primaryObjectIndex)].hasAloPayload)
    {
        BrxGlobsetRecord globset;
        globset.ownerObjectIndex = result.primaryObjectIndex;
        document.EditableGlobsets().push_back(std::move(globset));
    }
    // LoadEmitterFromBrx always has an inline effect payload after the ALO
    // record, even when its curve, mesh, and color table are empty. Give a
    // newly-authored EMITTER the same payload so every origin representation
    // remains editable instead of exposing only SetUser properties.
    if (CidDerivesFrom(cid, 80))
    {
        BrxEffectRecord effect;
        effect.ownerObjectIndex = result.primaryObjectIndex;
        effect.originKind = 0;
        effect.curveKind = -1;
        document.EditableEffects().push_back(std::move(effect));
    }
    // Initialize every mandatory non-option tail accepted by the generic
    // catalog path.  Without these records the object appears in the tree but
    // the from-scratch writer must reject it later as structurally incomplete.
    if (cid == 57)
        document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)].hasCnvoPayload = true;
    if (cid == 96 || cid == 124)
    {
        BrxVolumeRecord volume;
        volume.ownerObjectIndex = result.primaryObjectIndex;
        // LoadFlashFromBrx and LoadVolFromBrx unconditionally consume a TBSP.
        // An empty surface/node table is structurally writable but never has
        // an inside region, so a newly-created FLASH or RATHOLE cannot work.
        // Seed the same canonical convex box used by the dedicated VOL tool.
        constexpr float halfExtent = 200.0f;
        const float planes[6][4]{{1, 0, 0, halfExtent},  {-1, 0, 0, halfExtent}, {0, 1, 0, halfExtent},
                                 {0, -1, 0, halfExtent}, {0, 0, 1, halfExtent},  {0, 0, -1, halfExtent}};
        volume.planes.resize(6);
        volume.nodes.resize(6);
        for (std::size_t plane = 0; plane < 6; ++plane)
        {
            volume.planes[plane].normal = {planes[plane][0], planes[plane][1], planes[plane][2]};
            volume.planes[plane].distance = planes[plane][3];
            volume.nodes[plane].surfaceIndex = static_cast<std::uint16_t>(plane);
            volume.nodes[plane].negativeChild = plane + 1 < 6 ? static_cast<std::uint16_t>(plane + 1) : 0xffff;
            volume.nodes[plane].positiveChild = 0xffff;
        }
        document.EditableVolumes().push_back(std::move(volume));
    }
    if (cid == 121 || cid == 122)
    {
        BrxEffectRecord effect;
        effect.ownerObjectIndex = result.primaryObjectIndex;
        effect.originKind = 0;
        effect.curveKind = -1;
        document.EditableEffects().push_back(std::move(effect));
    }
    if (cid == 131)
    {
        BrxAsegRecord segment;
        segment.ownerObjectIndex = result.primaryObjectIndex;
        document.EditableAnimationSegments().push_back(std::move(segment));
    }
    if (cid == 136)
    {
        BrxSmRecord machine;
        machine.ownerObjectIndex = result.primaryObjectIndex;
        document.EditableStateMachines().push_back(std::move(machine));
    }
    if (cid == 142)
    {
        BrxKeyholeRecord keyhole;
        keyhole.ownerObjectIndex = result.primaryObjectIndex;
        // DrawKeyhole rejects a non-positive width and an empty mesh has no
        // visible transition.  Seed a normalized full-mask quad in layer 0;
        // authors can then reshape it or add the decorative/eye layers.
        keyhole.positions = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        keyhole.minimum = {-1.0f, -1.0f};
        keyhole.maximum = {1.0f, 1.0f};
        keyhole.layers[0] = {{0, 1, 2}, {0, 2, 3}};
        keyhole.modified = true;
        document.EditableKeyholes().push_back(std::move(keyhole));
    }
    document.SynchronizeDerivedObjectRecord(result.primaryObjectIndex);
    result.affectedObjectIndices.push_back(result.primaryObjectIndex);
    const char *className = BrxDocument::ClassName(cid);
    result.message = "Created " + std::string(className ? className : "object") + " OID " + std::to_string(oid) + ".";
    const auto spliceReferences = CountSpliceLookupReferences(document, {oid});
    if (spliceReferences != 0)
        result.message += " " + std::to_string(spliceReferences) +
            " existing Splice literal lookup expression(s) use this OID; review their intended targets.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateXfm(BrxDocument &document, std::int32_t parentIndex,
                                                          const float *worldTransform)
{
    EditorOperationResult result;
    if (parentIndex >= 0 && !ValidObjectIndex(document, parentIndex))
    {
        result.message = "The requested XFM owner does not exist.";
        return result;
    }

    // XFM::paloParent can only refer to an ALO. If creation was requested
    // from a structural LO/XFM row, place the new marker beside it under the
    // nearest transform-bearing ancestor instead of creating an invalid link.
    std::int32_t aloParentIndex = parentIndex;
    for (std::size_t guard = 0; aloParentIndex >= 0 &&
                                static_cast<std::size_t>(aloParentIndex) < document.Objects().size() &&
                                !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(aloParentIndex)].cid, 2) &&
                                guard++ <= document.Objects().size();)
    {
        const std::int32_t next = document.Objects()[static_cast<std::size_t>(aloParentIndex)].parentIndex;
        if (next == aloParentIndex)
        {
            aloParentIndex = -1;
            break;
        }
        aloParentIndex = next;
    }

    glm::mat4 targetWorld(1.0f);
    if (worldTransform != nullptr)
        std::memcpy(glm::value_ptr(targetWorld), worldTransform, sizeof(float) * 16);
    else if (parentIndex >= 0)
        std::memcpy(glm::value_ptr(targetWorld),
                    document.Objects()[static_cast<std::size_t>(parentIndex)].worldTransform, sizeof(float) * 16);

    result = CreateObject(document, 115, aloParentIndex, glm::value_ptr(targetWorld));
    if (!result.succeeded)
        return result;
    auto &marker = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    marker.loaderName = "Editor-created XFM";
    marker.hasAloPayload = false;
    // CreateObject already used the effective serialized parent, which can be
    // the instantiated root of a selected PROXY rather than the wrapper.
    marker.details.push_back({"Marker role", "Unassigned transform reference"});
    result.message = "Created XFM OID " + std::to_string(marker.oid) +
                     (marker.parentIndex >= 0 ? " under the selected ALO hierarchy." : " at world root.");
    return result;
}

EditorOperationResult EditorDocumentOperations::CreatePnt(BrxDocument &document, std::int32_t parentIndex,
                                                          const float *worldTransform)
{
    EditorOperationResult result;
    if (parentIndex >= 0 && !ValidObjectIndex(document, parentIndex))
    {
        result.message = "The requested PNT owner does not exist.";
        return result;
    }

    // PNT::paloParent is an ALO*. Structural selections are friendly creation
    // anchors, not legal runtime parents, so walk to the nearest ALO exactly as
    // the XFM/SHAPE creation paths do.
    std::int32_t aloParentIndex = parentIndex;
    for (std::size_t guard = 0; aloParentIndex >= 0 &&
                                static_cast<std::size_t>(aloParentIndex) < document.Objects().size() &&
                                !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(aloParentIndex)].cid, 2) &&
                                guard++ <= document.Objects().size();)
    {
        const std::int32_t next = document.Objects()[static_cast<std::size_t>(aloParentIndex)].parentIndex;
        if (next == aloParentIndex)
        {
            aloParentIndex = -1;
            break;
        }
        aloParentIndex = next;
    }

    glm::mat4 targetWorld(1.0f);
    if (worldTransform != nullptr)
        std::memcpy(glm::value_ptr(targetWorld), worldTransform, sizeof(float) * 16);
    else if (parentIndex >= 0)
        targetWorld = glm::make_mat4(document.Objects()[static_cast<std::size_t>(parentIndex)].worldTransform);

    // PNT owns only a position. Never carry rotation or scale from the creation
    // anchor into the editor record because the BRX payload cannot preserve it.
    const glm::vec3 worldPosition(targetWorld[3]);
    targetWorld = glm::mat4(1.0f);
    targetWorld[3] = glm::vec4(worldPosition, 1.0f);
    result = CreateObject(document, 126, aloParentIndex, glm::value_ptr(targetWorld));
    if (!result.succeeded)
        return result;

    auto &point = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    point.loaderName = "Editor-created PNT";
    point.hasAloPayload = false;
    const glm::vec3 localPosition = glm::make_vec3(point.localTransform + 12);
    const glm::mat4 local = glm::translate(glm::mat4(1.0f), localPosition);
    std::copy(glm::value_ptr(local), glm::value_ptr(local) + 16, point.localTransform);
    const glm::mat4 parentWorld = point.parentIndex >= 0
        ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(point.parentIndex)].worldTransform)
        : glm::mat4(1.0f);
    const glm::mat4 pointWorld = parentWorld * local;
    std::copy(glm::value_ptr(pointWorld), glm::value_ptr(pointWorld) + 16, point.worldTransform);
    point.details.push_back({"Marker role", "Unassigned position reference"});
    result.message = "Created PNT OID " + std::to_string(point.oid) +
                     (point.parentIndex >= 0 ? " under the nearest selected ALO." : " at world root.");
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateHpnt(BrxDocument &document, const std::int32_t parentIndex,
                                                           const float *worldTransform)
{
    auto result = CreatePnt(document, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 127;
    object.loaderName = "Editor-created HPNT";
    const auto markerRole = std::find_if(object.details.begin(), object.details.end(),
                                         [](const BrxObjectDetail &detail) { return detail.name == "Marker role"; });
    if (markerRole != object.details.end())
        markerRole->value = "Circular hide and jump target";
    else
        object.details.push_back({"Marker role", "Circular hide and jump target"});
    const auto addInt = [&](const std::int16_t id, const std::int32_t value, const BrxPropertyKind kind)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = kind;
        property.intValue = value;
        property.boolValue = value != 0;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    const auto addFloat = [&](const std::int16_t id, const float value)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = BrxPropertyKind::Float;
        property.floatValue[0] = value;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    // InitHpnt and the retail option defaults: a 50-unit flatten ring,
    // flatten posture, dynamic tunnel enabled, no authored TN, ordinary
    // detection, unlimited vertical reach, and jump-history recording enabled.
    addFloat(626, 50.0f);
    addInt(627, 0, BrxPropertyKind::Int16);
    addInt(628, 1, BrxPropertyKind::Bool);
    addInt(629, -1, BrxPropertyKind::Int16);
    addInt(630, 0, BrxPropertyKind::Bool);
    addFloat(631, (std::numeric_limits<float>::max)());
    addInt(632, 0, BrxPropertyKind::Bool);
    result.message = "Created HPNT OID " + std::to_string(object.oid) + " with ProjectCane runtime defaults.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateJmt(BrxDocument &document, const std::int32_t parentIndex,
                                                          const float *worldTransform)
{
    auto result = CreatePnt(document, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 128;
    object.loaderName = "Editor-created JMT";
    const auto markerRole = std::find_if(object.details.begin(), object.details.end(),
                                         [](const BrxObjectDetail &detail) { return detail.name == "Marker role"; });
    if (markerRole != object.details.end())
        markerRole->value = "Automatic mid-jump match target";
    else
        object.details.push_back({"Marker role", "Automatic mid-jump match target"});

    BrxPropertyRecord radius;
    radius.eopid = 836;
    radius.kind = BrxPropertyKind::Float;
    radius.floatValue[0] = 100.0f;
    radius.modified = true;
    object.properties.push_back(std::move(radius));

    BrxPropertyRecord ignoredObject;
    ignoredObject.eopid = 837;
    ignoredObject.kind = BrxPropertyKind::Int16;
    ignoredObject.intValue = -1;
    ignoredObject.modified = true;
    object.properties.push_back(std::move(ignoredObject));

    result.message = "Created JMT OID " + std::to_string(object.oid) + " with the runtime 100-unit capture radius.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateSpire(BrxDocument &document, const std::int32_t parentIndex,
                                                            const float *worldTransform)
{
    auto result = CreatePnt(document, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 129;
    object.loaderName = "Editor-created SPIRE";
    const auto markerRole = std::find_if(object.details.begin(), object.details.end(),
                                         [](const BrxObjectDetail &detail) { return detail.name == "Marker role"; });
    if (markerRole != object.details.end())
        markerRole->value = "Ninja-spire traversal perch";
    else
        object.details.push_back({"Marker role", "Ninja-spire traversal perch"});

    // Keep the class-table order used by SPIRE's option descriptor and carry
    // the authoritative SetUser flags instead of manufacturing bare records.
    BrxPropertyRecord history;
    const auto historyDefinition = EditorOptionDefinitionForCid(129, 1031);
    history.eopid = 1031;
    history.kind = historyDefinition.kind;
    history.flags = historyDefinition.flags;
    history.argumentCount = historyDefinition.argumentCount;
    history.boolValue = false;
    history.intValue = 0;
    history.modified = true;
    object.properties.push_back(std::move(history));

    BrxPropertyRecord height;
    const auto heightDefinition = EditorOptionDefinitionForCid(129, 1030);
    height.eopid = 1030;
    height.kind = heightDefinition.kind;
    height.flags = heightDefinition.flags;
    height.argumentCount = heightDefinition.argumentCount;
    height.floatValue[0] = (std::numeric_limits<float>::max)();
    height.modified = true;
    object.properties.push_back(std::move(height));

    result.message = "Created SPIRE OID " + std::to_string(object.oid) +
                     " with unlimited automatic height and normal jump-target history.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateScan(BrxDocument &document, const std::int32_t parentIndex,
                                                           const float *worldTransform)
{
    auto result = CreatePnt(document, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 130;
    object.loaderName = "Editor-created SCAN";
    const auto markerRole = std::find_if(object.details.begin(), object.details.end(),
                                         [](const BrxObjectDetail &detail) { return detail.name == "Marker role"; });
    if (markerRole != object.details.end())
        markerRole->value = "Binocucom dossier anchor";
    else
        object.details.push_back({"Marker role", "Binocucom dossier anchor"});

    BrxScanRecord scan;
    scan.ownerObjectIndex = result.primaryObjectIndex;
    scan.text = "New_scan_target";
    document.EditableScans().push_back(std::move(scan));
    result.message = "Created SCAN OID " + std::to_string(object.oid) + " with an editable binocular dossier.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateShape(BrxDocument &document, const bool cubic,
                                                            std::int32_t parentIndex, const float *worldTransform)
{
    // SHAPE::paloParent must be transform-bearing. Match CreateXfm's friendly
    // behavior by walking through structural rows to the nearest ALO.
    for (std::size_t guard = 0; parentIndex >= 0 && static_cast<std::size_t>(parentIndex) < document.Objects().size() &&
                                !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(parentIndex)].cid, 2) &&
                                guard++ <= document.Objects().size();)
    {
        const std::int32_t next = document.Objects()[static_cast<std::size_t>(parentIndex)].parentIndex;
        if (next == parentIndex)
        {
            parentIndex = -1;
            break;
        }
        parentIndex = next;
    }

    // Callers may pass a transform inside document.Objects(). Insertion can
    // reallocate that vector or shift a PROXY source span, so own the value
    // before creating the object and never reuse the caller's pointer afterward.
    const bool hasRequestedWorld = worldTransform != nullptr;
    const glm::mat4 requestedWorld = hasRequestedWorld ? glm::make_mat4(worldTransform) : glm::mat4(1.0f);
    auto result = CreateObject(document, 110, parentIndex,
                               hasRequestedWorld ? glm::value_ptr(requestedWorld) : nullptr);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created SHAPE";
    object.hasAloPayload = false;

    BrxShapePathRecord shape;
    shape.ownerObjectIndex = result.primaryObjectIndex;
    shape.hermite = cubic;
    shape.parameters = {0.0f, 100.0f};
    glm::vec3 anchorLocal(0.0f);
    glm::vec3 directionLocal(1.0f, 0.0f, 0.0f);
    if (hasRequestedWorld)
    {
        const glm::mat4 parentWorld =
            object.parentIndex >= 0
                ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(object.parentIndex)].worldTransform)
                : glm::mat4(1.0f);
        const glm::mat4 worldToParent =
            std::abs(glm::determinant(parentWorld)) > 0.000001f ? glm::inverse(parentWorld) : glm::mat4(1.0f);
        anchorLocal = glm::vec3(worldToParent * glm::vec4(glm::vec3(requestedWorld[3]), 1.0f));
        glm::vec3 requestedDirection = glm::vec3(requestedWorld[0]);
        if (glm::length(requestedDirection) <= 0.0001f)
            requestedDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 transformedDirection = glm::mat3(worldToParent) * glm::normalize(requestedDirection);
        if (glm::length(transformedDirection) > 0.0001f)
            directionLocal = glm::normalize(transformedDirection);
    }
    const glm::vec3 endpointLocal = anchorLocal + directionLocal * 100.0f;
    shape.points = {std::array<float, 3>{anchorLocal.x, anchorLocal.y, anchorLocal.z},
                    std::array<float, 3>{endpointLocal.x, endpointLocal.y, endpointLocal.z}};
    if (cubic)
    {
        // CRVC stores d(position)/d(u), not Bezier control-point offsets.
        shape.tangentsIn = {std::array<float, 3>{directionLocal.x, directionLocal.y, directionLocal.z},
                            std::array<float, 3>{directionLocal.x, directionLocal.y, directionLocal.z}};
        shape.tangentsOut = shape.tangentsIn;
    }
    document.EditableShapePaths().push_back(std::move(shape));
    result.message = std::string("Created a valid ") + (cubic ? "cubic" : "linear") + " SHAPE OID " +
                     std::to_string(object.oid) + " with two editable control points.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateLasen(BrxDocument &document, const std::int32_t parentIndex,
                                                            const float *worldTransform)
{
    auto result = CreateObject(document, 49, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    const std::int32_t lasenIndex = result.primaryObjectIndex;
    document.EditableObjects()[static_cast<std::size_t>(lasenIndex)].loaderName = "Editor-created LASEN";
    const float *beamWorld = document.Objects()[static_cast<std::size_t>(lasenIndex)].worldTransform;
    auto beam = CreateShape(document, false, lasenIndex, beamWorld);
    if (!beam.succeeded)
    {
        result.message = "Created LASEN OID " +
                         std::to_string(document.Objects()[static_cast<std::size_t>(lasenIndex)].oid) +
                         ", but a starter beam could not be created: " + beam.message;
        return result;
    }

    result.affectedObjectIndices.insert(result.affectedObjectIndices.end(), beam.affectedObjectIndices.begin(),
                                        beam.affectedObjectIndices.end());
    result.message =
        "Created LASEN with one direct linear SHAPE beam. Edit the beam's control points and add up to fifteen more direct linear SHAPEs.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateCamsen(BrxDocument &document, const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateObject(document, 50, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created CAMSEN";
    const auto definition = EditorOptionDefinitionForCid(object.cid, 133);
    BrxPropertyRecord sphere;
    sphere.eopid = 133;
    sphere.kind = definition.kind;
    sphere.flags = definition.flags;
    sphere.argumentCount = definition.argumentCount;
    sphere.editable = true;
    sphere.modified = true;
    sphere.floatValue[0] = 500.0f;
    object.properties.push_back(std::move(sphere));
    object.so.collisionGeometry.radius = 500.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        object.so.collisionGeometry.boundsMin[axis] = -500.0f;
        object.so.collisionGeometry.boundsMax[axis] = 500.0f;
    }
    result.message =
        "Created CAMSEN with a 500-unit spherical detection volume. Resize the sphere or replace it with authored collision BSP geometry.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateHshape(BrxDocument &document, const bool cubic,
                                                             const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateShape(document, cubic, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 111;
    object.loaderName = "Editor-created HSHAPE";
    const auto addInt = [&](const std::int16_t id, const std::int32_t value, const BrxPropertyKind kind)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = kind;
        property.intValue = value;
        property.boolValue = value != 0;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    const auto addFloat = [&](const std::int16_t id, const float value)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = BrxPropertyKind::Float;
        property.floatValue[0] = value;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    // InitHshape defaults: flatten, tunnel enabled, no authored TN, both
    // endpoint/peek directions, ordinary detection, and an unlimited height.
    addInt(633, 0, BrxPropertyKind::Int16);
    addInt(634, 1, BrxPropertyKind::Bool);
    addInt(635, -1, BrxPropertyKind::Int16);
    addInt(636, 3, BrxPropertyKind::Int16);
    addInt(637, 0, BrxPropertyKind::Bool);
    addFloat(638, (std::numeric_limits<float>::max)());
    addInt(639, 0, BrxPropertyKind::Bool);
    result.message = std::string("Created a valid ") + (cubic ? "cubic" : "linear") + " HSHAPE OID " +
                     std::to_string(object.oid) + " with ProjectCane runtime defaults.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreatePipe(BrxDocument &document, const bool cubic,
                                                           const std::int32_t parentIndex, const float *worldTransform)
{
    auto result = CreateShape(document, cubic, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 112;
    object.loaderName = "Editor-created PIPE";
    const auto addInt = [&](const std::int16_t id, const std::int32_t value, const BrxPropertyKind kind)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = kind;
        property.intValue = value;
        property.boolValue = value != 0;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    const auto addFloat = [&](const std::int16_t id, const float value)
    {
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = BrxPropertyKind::Float;
        property.floatValue[0] = value;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    BrxPropertyRecord facing;
    facing.eopid = 643;
    facing.kind = BrxPropertyKind::Vector3;
    facing.floatValue[0] = 1.0f;
    facing.modified = true;
    object.properties.push_back(std::move(facing));
    addInt(644, 8, BrxPropertyKind::Int16);
    addInt(645, 9, BrxPropertyKind::Int16);
    addInt(646, -1, BrxPropertyKind::Int16);
    addInt(648, 0, BrxPropertyKind::Bool);
    addInt(649, -1, BrxPropertyKind::Int16);
    addFloat(651, (std::numeric_limits<float>::max)());

    auto path = std::find_if(document.EditableShapePaths().begin(), document.EditableShapePaths().end(),
                             [&](const BrxShapePathRecord &candidate)
                             { return candidate.ownerObjectIndex == result.primaryObjectIndex; });
    if (path != document.EditableShapePaths().end() && path->points.size() >= 2)
    {
        const glm::vec3 first(path->points[0][0], path->points[0][1], path->points[0][2]);
        glm::mat4 parentWorld(1.0f);
        if (object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < document.Objects().size())
            parentWorld =
                glm::make_mat4(document.Objects()[static_cast<std::size_t>(object.parentIndex)].worldTransform);
        const glm::mat4 worldToParent =
            std::abs(glm::determinant(parentWorld)) > 0.000001f ? glm::inverse(parentWorld) : glm::mat4(1.0f);
        glm::vec3 direction = glm::mat3(worldToParent) * glm::vec3(0.0f, 0.0f, 1.0f);
        if (glm::length(direction) <= 0.0001f)
            direction = glm::vec3(0.0f, 0.0f, 1.0f);
        else
            direction = glm::normalize(direction);
        const glm::vec3 endpoint = first + direction * 300.0f;
        path->points[1] = {endpoint.x, endpoint.y, endpoint.z};
        path->parameters[1] = 300.0f;
        if (cubic && path->tangentsIn.size() == path->points.size() && path->tangentsOut.size() == path->points.size())
            for (std::size_t point = 0; point < path->points.size(); ++point)
            {
                path->tangentsIn[point] = {direction.x, direction.y, direction.z};
                path->tangentsOut[point] = {direction.x, direction.y, direction.z};
            }
    }
    result.message = std::string("Created a valid ") + (cubic ? "cubic" : "linear") + " PIPE OID " +
                     std::to_string(object.oid) + " with ProjectCane runtime defaults.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateRail(BrxDocument &document, const bool cubic,
                                                           const std::int32_t parentIndex, const float *worldTransform)
{
    auto result = CreateShape(document, cubic, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 113;
    object.loaderName = "Editor-created RAIL";
    const auto addInt = [&](const std::int16_t id, const std::int32_t value, const BrxPropertyKind kind)
    {
        const auto definition = EditorOptionDefinitionForCid(object.cid, id);
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.intValue = value;
        property.boolValue = value != 0;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };
    const auto addFloat = [&](const std::int16_t id, const float value)
    {
        const auto definition = EditorOptionDefinitionForCid(object.cid, id);
        BrxPropertyRecord property;
        property.eopid = id;
        property.kind = BrxPropertyKind::Float;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.floatValue[0] = value;
        property.modified = true;
        object.properties.push_back(std::move(property));
    };

    // InitRail plus SPA defaults.
    addInt(1032, 0, BrxPropertyKind::Bool);
    addFloat(1033, 1000.0f);
    addFloat(1034, 2.0f);
    addFloat(1035, (std::numeric_limits<float>::max)());
    addInt(1036, 0, BrxPropertyKind::Bool);
    addInt(1037, -1, BrxPropertyKind::Int16);

    // JtbsChooseJtLanding excludes 50 units at each open endpoint. The base
    // SHAPE's 100-unit quick path therefore has an empty RAIL search range.
    auto path = std::find_if(document.EditableShapePaths().begin(), document.EditableShapePaths().end(),
                             [&](const BrxShapePathRecord &candidate)
                             { return candidate.ownerObjectIndex == result.primaryObjectIndex; });
    if (path != document.EditableShapePaths().end() && path->points.size() >= 2)
    {
        const glm::vec3 first(path->points[0][0], path->points[0][1], path->points[0][2]);
        glm::vec3 direction(path->points[1][0] - first.x, path->points[1][1] - first.y, path->points[1][2] - first.z);
        if (glm::length(direction) <= 0.0001f)
            direction = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            direction = glm::normalize(direction);
        const glm::vec3 endpoint = first + direction * 300.0f;
        path->points[1] = {endpoint.x, endpoint.y, endpoint.z};
        path->parameters[1] = 300.0f;
        if (cubic && path->tangentsIn.size() == path->points.size() && path->tangentsOut.size() == path->points.size())
            for (std::size_t point = 0; point < path->points.size(); ++point)
            {
                path->tangentsIn[point] = {direction.x, direction.y, direction.z};
                path->tangentsOut[point] = {direction.x, direction.y, direction.z};
            }
    }

    result.message = std::string("Created a valid ") + (cubic ? "cubic" : "linear") + " RAIL OID " +
                     std::to_string(object.oid) + " with ProjectCane runtime defaults.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateLanding(BrxDocument &document, const bool cubic,
                                                              const std::int32_t parentIndex,
                                                              const float *worldTransform)
{
    auto result = CreateShape(document, cubic, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.cid = 114;
    object.loaderName = "Editor-created LANDING";
    result.message = std::string("Created a ") + (cubic ? "cubic" : "linear") + " LANDING OID " +
                     std::to_string(object.oid) +
                     ". ProjectCane projects its curve points up to 500 units downward onto collision.";
    return result;
}

namespace
{
bool IsDescendantOf(const BrxDocument &document, std::int32_t objectIndex, const std::int32_t ancestorIndex)
{
    for (std::size_t guard = 0; ValidObjectIndex(document, objectIndex) && guard++ <= document.Objects().size();)
    {
        objectIndex = document.Objects()[static_cast<std::size_t>(objectIndex)].parentIndex;
        if (objectIndex == ancestorIndex)
            return true;
        if (objectIndex < 0)
            return false;
    }
    return false;
}

std::vector<std::int32_t> ClueBounceFrames(const BrxDocument &document, const std::int32_t clueObjectIndex,
                                           bool &hasGap)
{
    std::vector<std::int32_t> frames;
    hasGap = false;
    bool missing = false;
    for (std::int32_t oid = 792; oid <= 823; ++oid)
    {
        std::int32_t found = -1;
        std::size_t scopedMatches = 0;
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
        {
            const auto &candidate = document.Objects()[index];
            if (candidate.oid != oid || !IsDescendantOf(document, static_cast<std::int32_t>(index), clueObjectIndex))
                continue;
            ++scopedMatches;
            if (CidDerivesFrom(candidate.cid, 115))
            {
                if (found >= 0)
                    hasGap = true;
                found = static_cast<std::int32_t>(index);
            }
            else
                hasGap = true;
        }
        if (scopedMatches > 1)
            hasGap = true;
        if (found < 0)
            missing = true;
        else if (missing)
            hasGap = true;
        else
            frames.push_back(found);
    }
    return frames;
}
}

EditorOperationResult EditorDocumentOperations::AddClueBounceFrame(BrxDocument &document,
                                                                   const std::int32_t clueObjectIndex,
                                                                   const std::int32_t sourceFrameObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, clueObjectIndex) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(clueObjectIndex)].cid, 46))
    {
        result.message = "Select a valid CLUE object first.";
        return result;
    }

    bool hasGap = false;
    const auto frames = ClueBounceFrames(document, clueObjectIndex, hasGap);
    if (hasGap)
    {
        result.message =
            "Repair the CLUE's duplicate or non-contiguous OID 792-823 frames before adding another frame.";
        return result;
    }
    if (frames.size() >= 32)
    {
        result.message = "This CLUE already uses all 32 runtime bounce frames.";
        return result;
    }
    if (sourceFrameObjectIndex >= 0 && std::find(frames.begin(), frames.end(), sourceFrameObjectIndex) == frames.end())
    {
        result.message = "The frame to duplicate does not belong to this CLUE's active sequence.";
        return result;
    }

    const float *targetWorld = sourceFrameObjectIndex >= 0
                                   ? document.Objects()[static_cast<std::size_t>(sourceFrameObjectIndex)].worldTransform
                                   : document.Objects()[static_cast<std::size_t>(clueObjectIndex)].worldTransform;
    result = CreateXfm(document, clueObjectIndex, targetWorld);
    if (!result.succeeded)
        return result;
    auto &frame = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    frame.oid = static_cast<std::int16_t>(792 + frames.size());
    frame.loaderName = "Editor-created CLUE bounce frame";
    frame.details.clear();
    frame.details.push_back({"CLUE role", "Bounce transform frame " + std::to_string(frames.size() + 1)});
    if (sourceFrameObjectIndex >= 0)
    {
        const auto &source = document.Objects()[static_cast<std::size_t>(sourceFrameObjectIndex)];
        std::copy_n(source.localTransform, 16, frame.localTransform);
        std::copy_n(source.worldTransform, 16, frame.worldTransform);
    }

    auto &clue = document.EditableObjects()[static_cast<std::size_t>(clueObjectIndex)];
    const auto duration = std::find_if(clue.properties.begin(), clue.properties.end(),
                                       [](const BrxPropertyRecord &property) { return property.eopid == 623; });
    if (duration == clue.properties.end())
    {
        const auto definition = EditorOptionDefinitionForCid(clue.cid, 623);
        BrxPropertyRecord property;
        property.eopid = 623;
        property.kind = definition.kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.modified = true;
        ResetPropertyValue(property);
        property.floatValue[0] = 0.5f;
        clue.properties.push_back(std::move(property));
    }
    result.message = sourceFrameObjectIndex >= 0
                         ? "Duplicated the CLUE bounce frame into the next contiguous runtime slot."
                         : "Added the CLUE's first/next contiguous runtime bounce frame.";
    return result;
}

EditorOperationResult EditorDocumentOperations::RemoveClueBounceFrame(BrxDocument &document,
                                                                      const std::int32_t clueObjectIndex,
                                                                      const std::int32_t frameObjectIndex)
{
    EditorOperationResult result;
    bool hasGap = false;
    const auto frames = ClueBounceFrames(document, clueObjectIndex, hasGap);
    if (hasGap)
    {
        result.message =
            "Repair the CLUE's duplicate, incompatible, or non-contiguous frame slots before removing a frame.";
        return result;
    }
    const auto position = std::find(frames.begin(), frames.end(), frameObjectIndex);
    if (position == frames.end())
    {
        result.message = "Select a frame in this CLUE's contiguous OID 792-823 sequence.";
        return result;
    }
    if (std::any_of(document.Objects().begin(), document.Objects().end(),
                    [&](const BrxObjectRecord &candidate) { return candidate.parentIndex == frameObjectIndex; }))
    {
        result.message = "This bounce frame owns child objects and cannot be safely removed.";
        return result;
    }

    for (auto next = position + 1; next != frames.end(); ++next)
    {
        auto &object = document.EditableObjects()[static_cast<std::size_t>(*next)];
        --object.oid;
        object.loaderName = "Editor-renumbered CLUE bounce frame";
    }
    result = DeleteHierarchy(document, frameObjectIndex);
    if (result.succeeded)
        result.message = "Removed the CLUE bounce frame and closed the reserved OID sequence gap.";
    return result;
}

EditorOperationResult EditorDocumentOperations::MoveClueBounceFrame(BrxDocument &document,
                                                                    const std::int32_t clueObjectIndex,
                                                                    const std::int32_t frameObjectIndex,
                                                                    const int direction)
{
    EditorOperationResult result;
    bool hasGap = false;
    const auto frames = ClueBounceFrames(document, clueObjectIndex, hasGap);
    if (hasGap)
    {
        result.message = "Repair the CLUE frame sequence before reordering it.";
        return result;
    }
    const auto current = std::find(frames.begin(), frames.end(), frameObjectIndex);
    if (current == frames.end())
    {
        result.message = "Select a frame in this CLUE's active bounce sequence.";
        return result;
    }
    const auto position = static_cast<std::ptrdiff_t>(current - frames.begin());
    const auto targetPosition = position + (direction < 0 ? -1 : 1);
    if (targetPosition < 0 || targetPosition >= static_cast<std::ptrdiff_t>(frames.size()))
    {
        result.message = "The bounce frame is already at that end of the sequence.";
        return result;
    }
    auto &source = document.EditableObjects()[static_cast<std::size_t>(frameObjectIndex)];
    auto &target =
        document.EditableObjects()[static_cast<std::size_t>(frames[static_cast<std::size_t>(targetPosition)])];
    for (std::size_t element = 0; element < 16; ++element)
    {
        std::swap(source.localTransform[element], target.localTransform[element]);
        std::swap(source.worldTransform[element], target.worldTransform[element]);
    }
    source.loaderName = target.loaderName = "Editor-reordered CLUE bounce frame";
    result.succeeded = true;
    result.primaryObjectIndex = frames[static_cast<std::size_t>(targetPosition)];
    result.affectedObjectIndices = {frameObjectIndex, result.primaryObjectIndex};
    result.message = direction < 0 ? "Moved the CLUE bounce pose earlier." : "Moved the CLUE bounce pose later.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateClueCollectionHelper(BrxDocument &document,
                                                                           const std::int32_t clueObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, clueObjectIndex) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(clueObjectIndex)].cid, 46))
    {
        result.message = "Select a valid CLUE object first.";
        return result;
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
        if (document.Objects()[index].oid == 825 &&
            IsDescendantOf(document, static_cast<std::int32_t>(index), clueObjectIndex))
        {
            result.message = "This CLUE already owns OID 825; select or repair that helper instead.";
            result.primaryObjectIndex = static_cast<std::int32_t>(index);
            return result;
        }
    result = CreateObject(document, 2, clueObjectIndex,
                          document.Objects()[static_cast<std::size_t>(clueObjectIndex)].worldTransform);
    if (!result.succeeded)
        return result;
    auto &helper = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    helper.oid = 825;
    helper.loaderName = "Editor-created CLUE collection helper";
    helper.details.push_back({"CLUE role", "HUD-flight/smack presentation helper"});
    result.message = "Created the CLUE's ALO-compatible OID 825 collection helper.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreatePathZone(BrxDocument &document,
                                                               const std::vector<std::array<float, 3>> &boundary,
                                                               const std::int32_t parentIndex)
{
    EditorOperationResult result;
    BrxPathZoneRecord zone;
    std::string error;
    if (!BuildPathZoneFromPolygon(zone, boundary, error))
    {
        result.message = error;
        return result;
    }
    result = CreateObject(document, 138, parentIndex, nullptr);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created PATHZONE";
    zone.ownerObjectIndex = result.primaryObjectIndex;
    zone.modified = true;
    document.EditablePathZones().push_back(std::move(zone));
    result.message = "Created PATHZONE OID " + std::to_string(object.oid) +
                     " with triangulation, graph adjacency and a constrained BSP.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreatePathZoneFromCollision(BrxDocument &document,
                                                                            const std::int32_t sourceObjectIndex,
                                                                            const std::int32_t parentIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, sourceObjectIndex))
    {
        result.message = "Select an object with collision first.";
        return result;
    }
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    const auto vertexIndex = [&](const float *position) -> std::uint16_t
    {
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            const float dx = vertices[i][0] - position[0], dy = vertices[i][1] - position[1],
                        dz = vertices[i][2] - position[2];
            if (dx * dx + dy * dy + dz * dz < 0.0001f)
                return static_cast<std::uint16_t>(i);
        }
        if (vertices.size() >= UINT16_MAX)
            return UINT16_MAX;
        vertices.push_back({position[0], position[1], position[2]});
        return static_cast<std::uint16_t>(vertices.size() - 1);
    };
    std::unordered_set<std::uint64_t> uniqueTriangles;
    for (const auto &collision : document.CollisionTriangles())
    {
        if (collision.ownerObjectIndex != sourceObjectIndex)
            continue;
        if (collision.cameraGeometry)
            continue;
        const glm::vec3 a(collision.positions[0][0], collision.positions[0][1], collision.positions[0][2]);
        const glm::vec3 b(collision.positions[1][0], collision.positions[1][1], collision.positions[1][2]);
        const glm::vec3 c(collision.positions[2][0], collision.positions[2][1], collision.positions[2][2]);
        const glm::vec3 normal = glm::cross(b - a, c - a);
        const float length = glm::length(normal);
        if (length < 0.0001f || std::abs(normal.z / length) < 0.573576f)
            continue;
        std::array<std::uint16_t, 3> triangle{};
        for (int vertex = 0; vertex < 3; ++vertex)
            triangle[vertex] = vertexIndex(collision.positions[vertex]);
        if (triangle[0] == UINT16_MAX || triangle[1] == UINT16_MAX || triangle[2] == UINT16_MAX ||
            triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[2] == triangle[0])
            continue;
        auto canonical = triangle;
        std::sort(canonical.begin(), canonical.end());
        const std::uint64_t key = (static_cast<std::uint64_t>(canonical[0]) << 32) |
                                  (static_cast<std::uint64_t>(canonical[1]) << 16) | canonical[2];
        if (uniqueTriangles.insert(key).second)
            triangles.push_back(triangle);
    }
    if (triangles.empty())
    {
        result.message = "The selected object has no decoded collision triangles.";
        return result;
    }
    if (triangles.size() * 3 > static_cast<std::size_t>(INT16_MAX))
    {
        result.message = "The collision floor has too many triangles for PATHZONE BSP indices.";
        return result;
    }

    BrxPathZoneRecord zone;
    zone.vertices = std::move(vertices);
    zone.triangles = std::move(triangles);
    FixPathZoneTriangleWinding(zone);
    RebuildPathZoneEdges(zone);
    if (!RebuildPathZoneBsp(zone))
    {
        result.message = "Collision produced invalid or degenerate PATHZONE triangles.";
        return result;
    }
    const auto issues = AuditPathZone(zone);
    if (!issues.empty())
    {
        result.message = "Collision cannot form one navigable PATHZONE: " + issues.front();
        return result;
    }

    result = CreateObject(document, 138, parentIndex, nullptr);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created PATHZONE from collision";
    zone.ownerObjectIndex = result.primaryObjectIndex;
    zone.modified = true;
    const auto triangleCount = zone.triangles.size();
    const auto boundaryCount = zone.boundaryEdgeCount;
    document.EditablePathZones().push_back(std::move(zone));
    result.message = "Created PATHZONE OID " + std::to_string(object.oid) + " from " + std::to_string(triangleCount) +
                     " walkable collision triangles (" + std::to_string(boundaryCount) +
                     " boundary edges; holes preserved).";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateTn(BrxDocument &document, const TnPreset preset,
                                                         const std::int32_t parentIndex, const float *worldTransform)
{
    auto result = CreateObject(document, 99, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created TN";
    auto &tn = object.tn;
    tn.present = true;
    tn.modified = true;

    // FCheckTbspPoint follows the negative child while dot(normal, point) is
    // below planeDistance. Chain six inward tests; a null negative child on
    // the final plane means inside, while any null positive child means out.
    constexpr float halfX = 300.0f;
    constexpr float halfY = 300.0f;
    constexpr float halfZ = 200.0f;
    const float planes[6][4]{{1, 0, 0, halfX},  {-1, 0, 0, halfX}, {0, 1, 0, halfY},
                             {0, -1, 0, halfY}, {0, 0, 1, halfZ},  {0, 0, -1, halfZ}};
    tn.surfaces.resize(6);
    tn.bspNodes.resize(6);
    for (std::size_t i = 0; i < 6; ++i)
    {
        auto &surface = tn.surfaces[i];
        surface.normal[0] = planes[i][0];
        surface.normal[1] = planes[i][1];
        surface.normal[2] = planes[i][2];
        surface.planeDistance = planes[i][3];
        auto &node = tn.bspNodes[i];
        node.surfaceIndex = static_cast<std::uint16_t>(i);
        node.negativeChild = i + 1 < 6 ? static_cast<std::int16_t>(i + 1) : -1;
        node.positiveChild = -1;
    }

    tn.curveClosed = false;
    tn.curvePoints.clear();
    if (preset == TnPreset::StaticFrame)
    {
        tn.curveKind = -1;
        std::memcpy(tn.staticFrame, kIdentity, sizeof(tn.staticFrame));
    }
    else
    {
        tn.curveKind = preset == TnPreset::LinearPath ? 0 : 1;
        tn.curvePoints.resize(2);
        tn.curvePoints[0].parameter = 0.0f;
        tn.curvePoints[0].position[0] = -300.0f;
        tn.curvePoints[1].parameter = 1.0f;
        tn.curvePoints[1].position[0] = 300.0f;
        if (preset == TnPreset::CubicPath)
        {
            // CRVC stores endpoint derivatives, not Bezier handle offsets.
            // A +600 derivative over u=[0,1] keeps this starter rail straight.
            tn.curvePoints[0].incomingTangent[0] = 600.0f;
            tn.curvePoints[0].outgoingTangent[0] = 600.0f;
            tn.curvePoints[1].incomingTangent[0] = 600.0f;
            tn.curvePoints[1].outgoingTangent[0] = 600.0f;
        }
    }

    result.message =
        std::string("Created ") +
        (preset == TnPreset::StaticFrame ? "fixed-camera"
                                         : (preset == TnPreset::LinearPath ? "linear-path" : "cubic-path")) +
        " TN OID " + std::to_string(object.oid) + " with a valid trigger box.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateExit(BrxDocument &document, const std::int32_t parentIndex,
                                                           const float *worldTransform)
{
    auto result = CreateObject(document, 125, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created EXIT";

    BrxExitRecord exit;
    exit.ownerObjectIndex = result.primaryObjectIndex;
    document.EditableExits().push_back(std::move(exit));

    BrxVolumeRecord volume;
    volume.ownerObjectIndex = result.primaryObjectIndex;
    constexpr float halfX = 180.0f;
    constexpr float halfY = 180.0f;
    constexpr float halfZ = 240.0f;
    const float planes[6][4]{{1, 0, 0, halfX},  {-1, 0, 0, halfX}, {0, 1, 0, halfY},
                             {0, -1, 0, halfY}, {0, 0, 1, halfZ},  {0, 0, -1, halfZ}};
    volume.planes.resize(6);
    volume.nodes.resize(6);
    for (std::size_t index = 0; index < 6; ++index)
    {
        volume.planes[index].normal = {planes[index][0], planes[index][1], planes[index][2]};
        volume.planes[index].distance = planes[index][3];
        volume.nodes[index].surfaceIndex = static_cast<std::uint16_t>(index);
        volume.nodes[index].negativeChild = index + 1 < 6 ? static_cast<std::uint16_t>(index + 1) : 0xffff;
        volume.nodes[index].positiveChild = 0xffff;
    }
    document.EditableVolumes().push_back(std::move(volume));
    result.message = "Created EXIT OID " + std::to_string(object.oid) +
                     " with a safe box trigger. Assign its destination level and WARP.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateVolume(BrxDocument &document, const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateObject(document, 123, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created VOL";
    BrxVolumeRecord volume;
    volume.ownerObjectIndex = result.primaryObjectIndex;
    constexpr float halfX = 200.0f;
    constexpr float halfY = 200.0f;
    constexpr float halfZ = 200.0f;
    const float planes[6][4]{{1, 0, 0, halfX},  {-1, 0, 0, halfX}, {0, 1, 0, halfY},
                             {0, -1, 0, halfY}, {0, 0, 1, halfZ},  {0, 0, -1, halfZ}};
    volume.planes.resize(6);
    volume.nodes.resize(6);
    for (std::size_t index = 0; index < 6; ++index)
    {
        volume.planes[index].normal = {planes[index][0], planes[index][1], planes[index][2]};
        volume.planes[index].distance = planes[index][3];
        volume.nodes[index].surfaceIndex = static_cast<std::uint16_t>(index);
        volume.nodes[index].negativeChild = index + 1 < 6 ? static_cast<std::uint16_t>(index + 1) : 0xffff;
        volume.nodes[index].positiveChild = 0xffff;
    }
    document.EditableVolumes().push_back(std::move(volume));
    result.message = "Created VOL OID " + std::to_string(object.oid) + " with a safe 400 x 400 x 400 box.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateCheckpoint(BrxDocument &document, const std::int32_t parentIndex,
                                                                 const float *worldTransform)
{
    auto result = CreateObject(document, 56, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    const std::int32_t checkpointIndex = result.primaryObjectIndex;
    auto &checkpoint = document.EditableObjects()[static_cast<std::size_t>(checkpointIndex)];
    checkpoint.loaderName = "Editor-created CHKPNT";
    checkpoint.so.present = true;
    checkpoint.so.fixedPhysics = true;
    const std::int16_t checkpointOid = checkpoint.oid;
    float checkpointWorld[16];
    std::copy_n(checkpoint.worldTransform, 16, checkpointWorld);

    BrxCheckpointRecord record;
    record.ownerObjectIndex = checkpointIndex;
    record.warpOid = -1;
    document.EditableCheckpoints().push_back(record);

    const auto trigger = CreateVolume(document, checkpointIndex, checkpointWorld);
    if (trigger.succeeded)
        result.affectedObjectIndices.insert(result.affectedObjectIndices.end(), trigger.affectedObjectIndices.begin(),
                                            trigger.affectedObjectIndices.end());

    const auto warp = CreateWarp(document, checkpointIndex, checkpointWorld);
    if (warp.succeeded)
    {
        result.affectedObjectIndices.insert(result.affectedObjectIndices.end(), warp.affectedObjectIndices.begin(),
                                            warp.affectedObjectIndices.end());
        document.EditableCheckpoints().back().warpOid =
            document.Objects()[static_cast<std::size_t>(warp.primaryObjectIndex)].oid;
    }

    result.primaryObjectIndex = checkpointIndex;
    result.message =
        "Created CHKPNT OID " + std::to_string(checkpointOid) + " with an owned box trigger and respawn WARP.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateCheckpointAseg(BrxDocument &document,
                                                                     const std::int32_t checkpointObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, checkpointObjectIndex) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(checkpointObjectIndex)].cid, 56))
    {
        result.message = "Select a valid CHKPNT before creating its animation.";
        return result;
    }

    result = CreateObject(document, 131, checkpointObjectIndex,
                          document.Objects()[static_cast<std::size_t>(checkpointObjectIndex)].worldTransform);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created checkpoint ASEG";
    result.message = "Created owned checkpoint ASEG OID " + std::to_string(object.oid) +
                     ". Open it in the animation viewer to author channels and events.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateVolzp(BrxDocument &document, const std::int32_t parentIndex,
                                                            const float *worldTransform)
{
    auto result = CreateObject(document, 55, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created VOLZP";
    object.so.present = true;
    object.so.fixedPhysics = true;
    object.so.modified = true;
    object.so.mass = 0.0f;
    object.so.collisionGeometry.radius = 200.0f;
    object.so.collisionGeometry.boundsMin[0] = -200.0f;
    object.so.collisionGeometry.boundsMin[1] = -200.0f;
    object.so.collisionGeometry.boundsMin[2] = -200.0f;
    object.so.collisionGeometry.boundsMax[0] = 200.0f;
    object.so.collisionGeometry.boundsMax[1] = 200.0f;
    object.so.collisionGeometry.boundsMax[2] = 200.0f;

    const auto addProperty = [&](const std::int16_t eopid) -> BrxPropertyRecord &
    {
        const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
        BrxPropertyRecord property;
        property.eopid = eopid;
        property.kind = definition.kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.modified = true;
        ResetPropertyValue(property);
        object.properties.push_back(std::move(property));
        return object.properties.back();
    };
    addProperty(133).floatValue[0] = 200.0f; // Safe sphere collision override.
    addProperty(609).intValue = 1;           // Electric damage, matching VOLZP's role.

    result.message =
        "Created VOLZP OID " + std::to_string(object.oid) + " with a fixed 200-unit electric damage sphere.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateTzp(BrxDocument &document, const std::int32_t parentIndex,
                                                          const float *worldTransform)
{
    auto result = CreateObject(document, 54, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created TZP";
    object.so.present = true;
    object.so.fixedPhysics = true;
    object.so.modified = true;
    object.so.mass = 0.0f;
    object.so.collisionGeometry.radius = 100.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        object.so.collisionGeometry.boundsMin[axis] = -100.0f;
        object.so.collisionGeometry.boundsMax[axis] = 100.0f;
    }

    const auto sphereDefinition = EditorOptionDefinitionForCid(object.cid, 133);
    BrxPropertyRecord sphere;
    sphere.eopid = 133;
    sphere.kind = sphereDefinition.kind;
    sphere.flags = sphereDefinition.flags;
    sphere.argumentCount = sphereDefinition.argumentCount;
    sphere.editable = true;
    sphere.modified = true;
    ResetPropertyValue(sphere);
    sphere.floatValue[0] = 100.0f;
    object.properties.push_back(std::move(sphere));

    const auto definition = EditorOptionDefinitionForCid(object.cid, 609);
    BrxPropertyRecord damage;
    damage.eopid = 609;
    damage.kind = definition.kind;
    damage.flags = definition.flags;
    damage.argumentCount = definition.argumentCount;
    damage.editable = true;
    damage.modified = true;
    ResetPropertyValue(damage);
    damage.intValue = 1; // Safe, visible electric-hazard default.
    object.properties.push_back(std::move(damage));

    result.message = "Created TZP OID " + std::to_string(object.oid) +
                     " as a fixed electric contact hazard with a 100-unit sphere collision override.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateVolbtn(BrxDocument &document, const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateObject(document, 41, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created VOLBTN";
    object.so.present = true;
    object.so.fixedPhysics = true;
    object.so.modified = true;
    object.so.mass = 0.0f;
    object.so.collisionGeometry.radius = 200.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        object.so.collisionGeometry.boundsMin[axis] = -200.0f;
        object.so.collisionGeometry.boundsMax[axis] = 200.0f;
    }

    const auto addProperty = [&](const std::int16_t eopid) -> BrxPropertyRecord &
    {
        const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
        BrxPropertyRecord property;
        property.eopid = eopid;
        property.kind = definition.kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.modified = true;
        ResetPropertyValue(property);
        object.properties.push_back(std::move(property));
        return object.properties.back();
    };
    addProperty(133).floatValue[0] = 200.0f;
    addProperty(269).floatValue[0] = 1.0f;
    addProperty(270).floatValue[0] = 1.0f;
    addProperty(271).boolValue = true;
    addProperty(274).boolValue = true;

    result.message =
        "Created VOLBTN OID " + std::to_string(object.oid) + " with a fixed 200-unit player trigger sphere.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateJlovol(BrxDocument &document, const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateVolbtn(document, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    const std::int32_t volumeIndex = result.primaryObjectIndex;
    auto &volume = document.EditableObjects()[static_cast<std::size_t>(volumeIndex)];
    volume.cid = 42;
    volume.loaderName = "Editor-created JLOVOL";
    if (auto delay = std::find_if(volume.properties.begin(), volume.properties.end(),
                                  [](const BrxPropertyRecord &property) { return property.eopid == 269; });
        delay != volume.properties.end())
    {
        delay->floatValue[0] = 0.0f;
        delay->modified = true;
    }

    float volumeWorld[16];
    std::copy_n(volume.worldTransform, 16, volumeWorld);
    const auto pointResult = CreatePnt(document, volumeIndex, volumeWorld);
    if (!pointResult.succeeded)
    {
        result.message =
            "Created JLOVOL, but its required direct landing PNT could not be created: " + pointResult.message;
        return result;
    }
    auto &point = document.EditableObjects()[static_cast<std::size_t>(pointResult.primaryObjectIndex)];
    point.loaderName = "Editor-created JLOVOL landing PNT";
    point.details.push_back({"JLOVOL role", "Direct fallback landing marker"});

    const auto controllerResult = CreateObject(document, 100, volumeIndex, volumeWorld);
    if (!controllerResult.succeeded)
    {
        result.message =
            "Created JLOVOL and landing PNT, but its required JLOC could not be created: " + controllerResult.message;
        return result;
    }
    const std::int32_t controllerIndex = controllerResult.primaryObjectIndex;
    auto &controller = document.EditableObjects()[static_cast<std::size_t>(controllerIndex)];
    controller.loaderName = "Editor-created JLOVOL firing controller";
    controller.details.push_back({"JLOVOL role", "JLOC firing settings and target collection"});
    const std::int16_t controllerOid = controller.oid;

    const auto targetResult = CreateXfm(document, controllerIndex, volumeWorld);
    if (!targetResult.succeeded)
    {
        result.message = "Created JLOVOL, landing PNT, and JLOC, but its initial target XFM could not be created: " +
                         targetResult.message;
        return result;
    }
    auto &target = document.EditableObjects()[static_cast<std::size_t>(targetResult.primaryObjectIndex)];
    target.loaderName = "Editor-created JLOC target XFM";
    target.details.push_back({"JLOVOL role", "Initial cannon target"});

    auto &completedVolume = document.EditableObjects()[static_cast<std::size_t>(volumeIndex)];
    const auto controllerDefinition = EditorOptionDefinitionForCid(completedVolume.cid, 896);
    BrxPropertyRecord controllerProperty;
    controllerProperty.eopid = 896;
    controllerProperty.kind = controllerDefinition.kind;
    controllerProperty.flags = controllerDefinition.flags;
    controllerProperty.argumentCount = controllerDefinition.argumentCount;
    controllerProperty.editable = true;
    controllerProperty.modified = true;
    ResetPropertyValue(controllerProperty);
    controllerProperty.intValue = controllerOid;
    completedVolume.properties.push_back(std::move(controllerProperty));

    result.primaryObjectIndex = volumeIndex;
    result.message =
        "Created JLOVOL OID " + std::to_string(document.Objects()[static_cast<std::size_t>(volumeIndex)].oid) +
        " with a landing PNT, linked JLOC, and initial target XFM. Move the PNT and XFM to the intended landing and firing positions.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateBarrier(BrxDocument &document, const std::int32_t parentIndex,
                                                              const float *worldTransform)
{
    auto result = CreateObject(document, 52, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;
    auto &barrier = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    barrier.loaderName = "Editor-created BARRIER";
    barrier.so.present = true;
    barrier.so.fixedPhysics = true;
    barrier.so.modified = true;
    barrier.so.mass = 0.0f;
    barrier.so.collisionGeometry.radius = 200.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        barrier.so.collisionGeometry.boundsMin[axis] = -200.0f;
        barrier.so.collisionGeometry.boundsMax[axis] = 200.0f;
    }
    const auto sphereDefinition = EditorOptionDefinitionForCid(barrier.cid, 133);
    BrxPropertyRecord sphere;
    sphere.eopid = 133;
    sphere.kind = sphereDefinition.kind;
    sphere.flags = sphereDefinition.flags;
    sphere.argumentCount = sphereDefinition.argumentCount;
    sphere.editable = true;
    sphere.modified = true;
    sphere.floatValue[0] = 200.0f;
    barrier.properties.push_back(std::move(sphere));
    const auto definition = EditorOptionDefinitionForCid(barrier.cid, 346);
    BrxPropertyRecord allow;
    allow.eopid = 346;
    allow.kind = definition.kind;
    allow.flags = definition.flags;
    allow.argumentCount = definition.argumentCount;
    allow.editable = true;
    allow.modified = true;
    ResetPropertyValue(allow);
    allow.boolValue = true; // Empty allow-through list means collide with everything.
    barrier.properties.push_back(std::move(allow));
    result.message =
        "Created a fixed BARRIER that blocks every object with a 200-unit collision sphere. Resize the sphere or replace it with collision BSP geometry.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateBarrierDestinationPoint(BrxDocument &document,
                                                                              const std::int32_t barrierObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, barrierObjectIndex) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(barrierObjectIndex)].cid, 52))
    {
        result.message = "Select a valid BARRIER first.";
        return result;
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
        if (document.Objects()[index].parentIndex == barrierObjectIndex &&
            CidDerivesFrom(document.Objects()[index].cid, 126))
        {
            result.message = "This BARRIER already has a direct destination PNT.";
            result.primaryObjectIndex = static_cast<std::int32_t>(index);
            return result;
        }
    result = CreateObject(document, 126, barrierObjectIndex,
                          document.Objects()[static_cast<std::size_t>(barrierObjectIndex)].worldTransform);
    if (!result.succeeded)
        return result;
    auto &point = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    point.loaderName = "Editor-created BARRIER destination PNT";
    point.details.push_back({"BARRIER role", "Embedded teleport destination"});
    result.message = "Created the BARRIER destination point. Move it with the transform gizmo.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateWater(BrxDocument &document, const std::int32_t parentIndex,
                                                            const float *worldTransform)
{
    auto result = CreateObject(document, 31, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created WATER";
    object.so.present = true;
    object.so.fixedPhysics = true;
    object.so.modified = true;
    object.so.mass = 0.0f;
    const glm::vec3 halfExtent(300.0f, 300.0f, 100.0f);
    const glm::mat4 transform = glm::make_mat4(object.worldTransform);
    const glm::vec3 localCorners[8]{
        {-halfExtent.x, -halfExtent.y, -halfExtent.z}, {halfExtent.x, -halfExtent.y, -halfExtent.z},
        {halfExtent.x, halfExtent.y, -halfExtent.z},   {-halfExtent.x, halfExtent.y, -halfExtent.z},
        {-halfExtent.x, -halfExtent.y, halfExtent.z},  {halfExtent.x, -halfExtent.y, halfExtent.z},
        {halfExtent.x, halfExtent.y, halfExtent.z},    {-halfExtent.x, halfExtent.y, halfExtent.z}};
    glm::vec3 corners[8];
    for (std::size_t i = 0; i < 8; ++i)
        corners[i] = glm::vec3(transform * glm::vec4(localCorners[i], 1.0f));
    auto &geometry = object.so.collisionGeometry;
    glm::vec3 worldLower((std::numeric_limits<float>::max)());
    glm::vec3 worldUpper((std::numeric_limits<float>::lowest)());
    for (const auto &corner : corners)
    {
        geometry.positions.push_back({corner.x, corner.y, corner.z});
        worldLower = glm::min(worldLower, corner);
        worldUpper = glm::max(worldUpper, corner);
    }
    constexpr std::array<std::array<std::uint16_t, 3>, 6> planeTriangles{
        {{{0, 2, 1}}, {{4, 5, 6}}, {{0, 1, 5}}, {{3, 7, 6}}, {{0, 4, 7}}, {{1, 2, 6}}}};
    constexpr std::array<std::array<std::uint16_t, 4>, 6> planeLoops{
        {{{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}}, {{3, 7, 6, 2}}, {{0, 4, 7, 3}}, {{1, 2, 6, 5}}}};
    for (std::size_t faceIndex = 0; faceIndex < planeTriangles.size(); ++faceIndex)
    {
        BrxSoGeometryInfo::Surface surface;
        surface.positionIndices = planeTriangles[faceIndex];
        glm::vec3 center(0.0f);
        for (const auto index : planeLoops[faceIndex])
            center += corners[index];
        center *= 0.25f;
        surface.center = {center.x, center.y, center.z};
        for (const auto index : planeLoops[faceIndex])
            surface.radius = (std::max)(surface.radius, glm::length(corners[index] - center));
        geometry.surfaces.push_back(std::move(surface));
    }
    struct EdgeOwner
    {
        std::uint16_t surface = 0;
        std::uint16_t edge = 0;
    };
    std::unordered_map<std::uint32_t, EdgeOwner> openEdges;
    for (std::size_t surfaceIndex = 0; surfaceIndex < geometry.surfaces.size(); ++surfaceIndex)
        for (std::uint16_t edgeIndex = 0; edgeIndex < 4; ++edgeIndex)
        {
            const auto first = planeLoops[surfaceIndex][edgeIndex];
            const auto second = planeLoops[surfaceIndex][(edgeIndex + 1u) % 4u];
            const auto low = (std::min)(first, second), high = (std::max)(first, second);
            const std::uint32_t key = (static_cast<std::uint32_t>(low) << 16u) | high;
            const auto found = openEdges.find(key);
            if (found == openEdges.end())
            {
                const auto stored = static_cast<std::uint16_t>(geometry.surfaces[surfaceIndex].edges.size());
                geometry.surfaces[surfaceIndex].edges.push_back(
                    {first, second, static_cast<std::uint16_t>(surfaceIndex)});
                openEdges.emplace(key, EdgeOwner{static_cast<std::uint16_t>(surfaceIndex), stored});
            }
            else
            {
                geometry.surfaces[found->second.surface].edges[found->second.edge].adjacentSurface =
                    static_cast<std::uint16_t>(surfaceIndex);
                openEdges.erase(found);
            }
        }
    for (std::size_t plane = 0; plane < geometry.surfaces.size(); ++plane)
        geometry.bspNodes.push_back({static_cast<std::uint16_t>(plane),
                                     plane + 1u < geometry.surfaces.size() ? static_cast<std::uint16_t>(plane + 1u)
                                                                           : static_cast<std::uint16_t>(0xffffu),
                                     static_cast<std::uint16_t>(0xffffu)});
    geometry.positionCount = static_cast<std::uint16_t>(geometry.positions.size());
    geometry.surfaceCount = static_cast<std::uint16_t>(geometry.surfaces.size());
    geometry.edgeCount = 12;
    geometry.fullBspNodeCount = static_cast<std::uint16_t>(geometry.bspNodes.size());
    geometry.radius = glm::length(worldUpper - (worldLower + worldUpper) * 0.5f);
    std::copy(&worldLower.x, &worldLower.x + 3, geometry.boundsMin);
    std::copy(&worldUpper.x, &worldUpper.x + 3, geometry.boundsMax);
    object.so.bspNodeToCameraGroup.assign(geometry.bspNodes.size(), -1);
    constexpr std::uint8_t faces[12][3]{{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                        {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    for (const auto &face : faces)
    {
        BrxCollisionTriangle triangle;
        triangle.ownerObjectIndex = result.primaryObjectIndex;
        for (int vertex = 0; vertex < 3; ++vertex)
            for (int axis = 0; axis < 3; ++axis)
                triangle.positions[vertex][axis] = corners[face[vertex]][axis];
        document.EditableCollisionTriangles().push_back(triangle);
    }
    const auto addBool = [&](const std::int16_t eopid, const bool value)
    {
        const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
        BrxPropertyRecord property;
        property.eopid = eopid;
        property.kind = definition.kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.modified = true;
        ResetPropertyValue(property);
        property.boolValue = value;
        object.properties.push_back(std::move(property));
    };
    addBool(201, true);
    addBool(210, true);
    addBool(211, true);

    result.message = "Created WATER OID " + std::to_string(object.oid) +
                     " with a fixed 600 x 600 x 200 collision box. Assign a visible water GLOB when ready.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateWaterFromObject(BrxDocument &document,
                                                                      const std::int32_t sourceObjectIndex)
{
    EditorOperationResult failed;
    if (!ValidObjectIndex(document, sourceObjectIndex))
    {
        failed.message = "Select a source object first.";
        return failed;
    }
    const BrxObjectRecord source = document.Objects()[static_cast<std::size_t>(sourceObjectIndex)];
    auto result = CreateWater(document, source.parentIndex, source.worldTransform);
    if (!result.succeeded)
        return result;
    const std::int32_t waterIndex = result.primaryObjectIndex;
    auto &water = document.EditableObjects()[static_cast<std::size_t>(waterIndex)];

    auto &triangles = document.EditableCollisionTriangles();
    triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](const BrxCollisionTriangle &triangle)
                                   { return triangle.ownerObjectIndex == waterIndex; }),
                    triangles.end());
    std::size_t copiedCollision = 0;
    const auto sourceTriangles = triangles;
    for (const auto &sourceTriangle : sourceTriangles)
    {
        if (sourceTriangle.ownerObjectIndex != sourceObjectIndex || sourceTriangle.cameraGeometry)
            continue;
        BrxCollisionTriangle copy = sourceTriangle;
        copy.ownerObjectIndex = waterIndex;
        triangles.push_back(copy);
        ++copiedCollision;
    }
    water.so.collisionGeometry = source.so.collisionGeometry;
    if (!source.so.fixedPhysics && !water.so.collisionGeometry.positions.empty())
    {
        const glm::mat4 sourceWorld = glm::make_mat4(source.worldTransform);
        glm::vec3 lower((std::numeric_limits<float>::max)());
        glm::vec3 upper((std::numeric_limits<float>::lowest)());
        for (auto &position : water.so.collisionGeometry.positions)
        {
            const glm::vec3 transformed =
                glm::vec3(sourceWorld * glm::vec4(position[0], position[1], position[2], 1.0f));
            position = {transformed.x, transformed.y, transformed.z};
            lower = glm::min(lower, transformed);
            upper = glm::max(upper, transformed);
        }
        const float radiusScale =
            (std::max)({glm::length(glm::vec3(sourceWorld[0])), glm::length(glm::vec3(sourceWorld[1])),
                        glm::length(glm::vec3(sourceWorld[2]))});
        water.so.collisionGeometry.radius *= radiusScale;
        std::copy(&lower.x, &lower.x + 3, water.so.collisionGeometry.boundsMin);
        std::copy(&upper.x, &upper.x + 3, water.so.collisionGeometry.boundsMax);
        for (auto &surface : water.so.collisionGeometry.surfaces)
        {
            const glm::vec3 center =
                glm::vec3(sourceWorld * glm::vec4(surface.center[0], surface.center[1], surface.center[2], 1.0f));
            surface.center = {center.x, center.y, center.z};
            surface.radius *= radiusScale;
        }
    }
    water.so.bspNodeToCameraGroup.assign(water.so.collisionGeometry.bspNodes.size(), -1);
    water.so.hitSurfaceGroups.clear();
    water.so.surfaceToHitSurfaceGroup.clear();
    water.so.fixedPhysics = true;
    water.so.mass = 0.0f;
    water.so.modified = true;

    std::size_t copiedGlobsets = 0;
    const auto sourceGlobsets = document.Globsets();
    const bool hasSourceGlobset =
        std::any_of(sourceGlobsets.begin(), sourceGlobsets.end(),
                    [&](const BrxGlobsetRecord &candidate) { return candidate.ownerObjectIndex == sourceObjectIndex; });
    if (hasSourceGlobset)
    {
        auto &destinationGlobsets = document.EditableGlobsets();
        destinationGlobsets.erase(std::remove_if(destinationGlobsets.begin(), destinationGlobsets.end(),
                                                 [&](const BrxGlobsetRecord &candidate)
                                                 { return candidate.ownerObjectIndex == waterIndex; }),
                                  destinationGlobsets.end());
    }
    for (const auto &sourceGlobset : sourceGlobsets)
    {
        if (sourceGlobset.ownerObjectIndex != sourceObjectIndex)
            continue;
        BrxGlobsetRecord copy = sourceGlobset;
        copy.ownerObjectIndex = waterIndex;
        copy.globIndices.clear();
        for (const auto sourceGlobIndex : sourceGlobset.globIndices)
        {
            const auto newGlobIndex = document.DuplicateGlobAsInstance(sourceGlobIndex);
            if (newGlobIndex >= document.EditableGlobs().size())
                continue;
            document.EditableGlobs()[newGlobIndex].ownerObjectIndex = waterIndex;
            copy.globIndices.push_back(newGlobIndex);
        }
        document.EditableGlobsets().push_back(std::move(copy));
        ++copiedGlobsets;
    }
    document.RebuildRenderPacketsFromGlobs();
    result.message = "Created WATER OID " + std::to_string(water.oid) + " from " +
                     std::string(BrxDocument::ClassName(source.cid)) + " OID " + std::to_string(source.oid) + ": " +
                     std::to_string(copiedCollision) + " collision triangles and " + std::to_string(copiedGlobsets) +
                     " linked model group(s).";
    if (copiedCollision == 0)
        result.message += " The source had no gameplay collision; add or import a closed collision hull.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateButton(BrxDocument &document, const ButtonPreset preset,
                                                             const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateObject(document, 40, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    const std::int32_t buttonIndex = result.primaryObjectIndex;
    auto &button = document.EditableObjects()[static_cast<std::size_t>(buttonIndex)];
    button.loaderName = "Editor-created BUTTON";
    button.so.present = true;
    button.so.modified = true;

    const auto addProperty = [&](const std::int16_t eopid) -> BrxPropertyRecord &
    {
        const auto definition = EditorOptionDefinitionForCid(button.cid, eopid);
        BrxPropertyRecord property;
        property.eopid = eopid;
        property.kind = definition.kind;
        property.flags = definition.flags;
        property.argumentCount = definition.argumentCount;
        property.editable = true;
        property.modified = true;
        ResetPropertyValue(property);
        button.properties.push_back(std::move(property));
        return button.properties.back();
    };
    addProperty(258).intValue = static_cast<std::int32_t>(preset);
    addProperty(257).floatValue[0] = 1.0f;
    addProperty(259).floatValue[0] = 1.0f;
    addProperty(263).boolValue = true;
    if (preset == ButtonPreset::Push)
        addProperty(131).intValue = 4; // CNSTR_Z, matching the starter stop offset.
    else if (preset == ButtonPreset::Lever)
        addProperty(132).intValue = 3; // CNSTR_Y, matching the starter rotation.

    float buttonWorldTransform[16];
    std::memcpy(buttonWorldTransform, button.worldTransform, sizeof(buttonWorldTransform));
    const std::int16_t buttonOid = button.oid;

    const auto createHelper = [&](const std::int16_t cid, const float offset, const bool restStop) -> std::int32_t
    {
        float localStopTransform[16];
        std::memcpy(localStopTransform, kIdentity, sizeof(localStopTransform));
        localStopTransform[14] = offset;
        const glm::mat4 helperWorld = glm::make_mat4(buttonWorldTransform) * glm::make_mat4(localStopTransform);
        auto helper = CreateObject(document, cid, buttonIndex, glm::value_ptr(helperWorld));
        if (!helper.succeeded)
            return -1;
        auto &child = document.EditableObjects()[static_cast<std::size_t>(helper.primaryObjectIndex)];
        child.loaderName = restStop ? "Editor-created BUTTON rest helper" : "Editor-created BUTTON active helper";
        std::memcpy(child.localTransform, localStopTransform, sizeof(child.localTransform));
        if (restStop)
            child.oid = 512;
        result.affectedObjectIndices.push_back(helper.primaryObjectIndex);
        return helper.primaryObjectIndex;
    };

    if (preset == ButtonPreset::Push)
    {
        createHelper(126, 0.0f, true);
        createHelper(126, -25.0f, false);
    }
    else if (preset == ButtonPreset::Lever)
    {
        createHelper(115, 0.0f, true);
        const auto active = createHelper(115, 0.0f, false);
        if (active >= 0)
        {
            // A visible starter orientation around local Y; users can refine it
            // with the ordinary rotation gizmo and the BUTTON arc preview.
            auto &transform = document.EditableObjects()[static_cast<std::size_t>(active)].worldTransform;
            auto &local = document.EditableObjects()[static_cast<std::size_t>(active)].localTransform;
            local[0] = 0.70710678f;
            local[2] = -0.70710678f;
            local[8] = 0.70710678f;
            local[10] = 0.70710678f;
            const glm::mat4 helperWorld = glm::make_mat4(buttonWorldTransform) * glm::make_mat4(local);
            std::copy(glm::value_ptr(helperWorld), glm::value_ptr(helperWorld) + 16, transform);
        }
    }
    else if (preset == ButtonPreset::Hang)
        createHelper(118, 0.0f, false);

    const char *kind = preset == ButtonPreset::Push      ? "push"
                       : preset == ButtonPreset::Lever   ? "lever"
                       : preset == ButtonPreset::Contact ? "contact"
                       : preset == ButtonPreset::Whack   ? "whack"
                                                         : "hang";
    result.message = "Created " + std::string(kind) + " BUTTON OID " + std::to_string(buttonOid) +
                     " with its required starter hierarchy.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateDialog(BrxDocument &document, const std::int32_t parentIndex,
                                                             const float *worldTransform)
{
    auto result = CreateObject(document, 101, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created DIALOG";
    object.hidden = true;
    object.dialog.present = true;
    object.dialog.modified = true;

    BrxDialogEvent line;
    line.kind = BrxDialogEventKind::Talk;
    line.zoom = -1.0f;
    line.pauseSeconds = -1.0f;
    line.speakerOid = -1;
    line.lipsyncAnimationOid = -1;
    line.focusOid = -1;
    line.anchorPointOid = -1;
    line.subtitle = "New dialog line";
    line.modified = true;
    object.dialog.events.push_back(std::move(line));

    result.message = "Created standard DIALOG OID " + std::to_string(object.oid) + " with one safe talk event.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateWarp(BrxDocument &document, const std::int32_t parentIndex,
                                                           const float *worldTransform)
{
    auto result = CreateObject(document, 116, parentIndex, worldTransform);
    if (!result.succeeded)
        return result;

    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created WARP";
    BrxWarpRecord warp;
    warp.ownerObjectIndex = result.primaryObjectIndex;
    warp.aliasOid = -1;
    document.EditableWarps().push_back(std::move(warp));
    result.message = "Created WARP OID " + std::to_string(object.oid) + " at the selected transform.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateEmbeddedWarpAseg(BrxDocument &document,
                                                                       const std::int32_t warpObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, warpObjectIndex) ||
        document.Objects()[static_cast<std::size_t>(warpObjectIndex)].cid != 116)
    {
        result.message = "Select a WARP before creating an embedded animation.";
        return result;
    }

    result = CreateObject(document, 131, warpObjectIndex,
                          document.Objects()[static_cast<std::size_t>(warpObjectIndex)].worldTransform);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created embedded WARP ASEG";

    SynchronizeWarpAnimationCounts(document);
    SynchronizeExitAnimationCounts(document);
    result.message = "Created an embedded ASEG for WARP OID " +
                     std::to_string(document.Objects()[static_cast<std::size_t>(warpObjectIndex)].oid) + ".";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateEmbeddedExitAseg(BrxDocument &document,
                                                                       const std::int32_t exitObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, exitObjectIndex) ||
        document.Objects()[static_cast<std::size_t>(exitObjectIndex)].cid != 125)
    {
        result.message = "Select an EXIT before creating an embedded transition animation.";
        return result;
    }
    result = CreateObject(document, 131, exitObjectIndex,
                          document.Objects()[static_cast<std::size_t>(exitObjectIndex)].worldTransform);
    if (!result.succeeded)
        return result;
    auto &object = document.EditableObjects()[static_cast<std::size_t>(result.primaryObjectIndex)];
    object.loaderName = "Editor-created embedded EXIT ASEG";
    SynchronizeExitAnimationCounts(document);
    result.message = "Created an embedded transition ASEG for EXIT OID " +
                     std::to_string(document.Objects()[static_cast<std::size_t>(exitObjectIndex)].oid) + ".";
    return result;
}

EditorOperationResult EditorDocumentOperations::MoveEmbeddedWarpAseg(BrxDocument &document,
                                                                     const std::int32_t asegObjectIndex,
                                                                     const int direction)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, asegObjectIndex) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(asegObjectIndex)].cid, 131))
    {
        result.message = "Select a valid embedded ASEG first.";
        return result;
    }
    const auto parent = document.Objects()[static_cast<std::size_t>(asegObjectIndex)].parentIndex;
    if (!ValidObjectIndex(document, parent) || (document.Objects()[static_cast<std::size_t>(parent)].cid != 116 &&
                                                document.Objects()[static_cast<std::size_t>(parent)].cid != 125))
    {
        result.message = "The selected ASEG is not embedded in a WARP or EXIT.";
        return result;
    }
    if (std::any_of(document.Objects().begin(), document.Objects().end(),
                    [&](const BrxObjectRecord &candidate) { return candidate.parentIndex == asegObjectIndex; }))
    {
        result.message = "Cannot reorder an embedded ASEG that owns child objects.";
        return result;
    }

    std::vector<std::int32_t> siblings;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
        if (document.Objects()[index].parentIndex == parent && CidDerivesFrom(document.Objects()[index].cid, 131))
            siblings.push_back(static_cast<std::int32_t>(index));
    const auto current = std::find(siblings.begin(), siblings.end(), asegObjectIndex);
    if (current == siblings.end())
    {
        result.message = "Embedded ASEG ordering is inconsistent.";
        return result;
    }
    const auto position = static_cast<std::ptrdiff_t>(current - siblings.begin());
    const auto targetPosition = position + (direction < 0 ? -1 : 1);
    if (targetPosition < 0 || targetPosition >= static_cast<std::ptrdiff_t>(siblings.size()))
    {
        result.message = "The embedded ASEG is already at that end of the list.";
        return result;
    }
    const std::int32_t targetIndex = siblings[static_cast<std::size_t>(targetPosition)];
    std::swap(document.EditableObjects()[static_cast<std::size_t>(asegObjectIndex)],
              document.EditableObjects()[static_cast<std::size_t>(targetIndex)]);
    std::vector<std::int32_t> remap(document.Objects().size());
    for (std::size_t index = 0; index < remap.size(); ++index)
        remap[index] = static_cast<std::int32_t>(index);
    remap[static_cast<std::size_t>(asegObjectIndex)] = targetIndex;
    remap[static_cast<std::size_t>(targetIndex)] = asegObjectIndex;
    RemapDocumentObjectIndices(document, remap);
    SynchronizeWarpAnimationCounts(document);
    SynchronizeExitAnimationCounts(document);
    result.succeeded = true;
    result.primaryObjectIndex = targetIndex;
    result.affectedObjectIndices = {asegObjectIndex, targetIndex};
    result.message = direction < 0 ? "Moved embedded ASEG earlier." : "Moved embedded ASEG later.";
    return result;
}

EditorOperationResult EditorDocumentOperations::DuplicateHierarchy(BrxDocument &document,
                                                                   const std::int32_t rootObjectIndex)
{
    EditorOperationResult result;
    const auto hierarchy = CollectHierarchy(document, rootObjectIndex);
    if (hierarchy.empty())
    {
        result.message = "No valid hierarchy root is selected.";
        return result;
    }
    if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const std::int32_t index)
                    { return document.Objects()[static_cast<std::size_t>(index)].cid == 7; }))
    {
        result.message =
            "JT cannot use ordinary hierarchy duplication because its rig is resolved through hard-coded OIDs. Edit the existing JT hierarchy instead.";
        return result;
    }
    if (std::any_of(hierarchy.begin(), hierarchy.end(),
                    [&](const std::int32_t index)
                    {
                        const auto cid = document.Objects()[static_cast<std::size_t>(index)].cid;
                        return cid == 108 || cid == 109;
                    }))
    {
        result.message = "SW/CM runtime infrastructure cannot be duplicated as authored BRX object hierarchies.";
        return result;
    }
    if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const std::int32_t index)
                    { return document.Objects()[static_cast<std::size_t>(index)].cid == 83; }))
    {
        result.message =
            "A PROXY hierarchy cannot use ordinary duplication because that would strip its "
            "source-pool and nested-root identity. Create a linked PROXY instance or make the root unique first.";
        return result;
    }

    std::unordered_map<std::int32_t, std::int32_t> indexRemap;
    std::unordered_map<std::int16_t, std::int16_t> oidRemap;
    auto &objects = document.EditableObjects();
    const std::size_t originalCount = objects.size();
    for (const auto oldIndex : hierarchy)
    {
        const auto newOid = AllocateOid(document);
        if (newOid < 0)
        {
            objects.resize(originalCount);
            result.message = "The hierarchy could not be duplicated because object IDs are exhausted.";
            return result;
        }
        BrxObjectRecord copy = objects[static_cast<std::size_t>(oldIndex)];
        oidRemap.emplace(copy.oid, newOid);
        copy.oid = newOid;
        copy.offset = copy.dataOffset = copy.endOffset = 0;
        copy.proxyClone = false;
        copy.proxyRecordIndex = copy.proxyRootIndex = copy.proxySourceObjectIndex = -1;
        copy.editorName.clear();
        copy.loaderName = "Editor duplicate";
        objects.push_back(std::move(copy));
        const auto newIndex = static_cast<std::int32_t>(objects.size() - 1);
        indexRemap.emplace(oldIndex, newIndex);
        result.affectedObjectIndices.push_back(newIndex);
    }
    for (std::size_t i = 0; i < hierarchy.size(); ++i)
    {
        auto &copy = objects[static_cast<std::size_t>(result.affectedObjectIndices[i])];
        if (const auto parent = indexRemap.find(copy.parentIndex); parent != indexRemap.end())
            copy.parentIndex = parent->second;
        RemapKnownOidReferences(copy, oidRemap);
    }
    DuplicateOwnedSceneData(document, indexRemap, oidRemap);
    for (const auto &[sourceIndex, duplicateIndex] : indexRemap)
    {
        (void)sourceIndex;
        auto &duplicate = document.EditableObjects()[static_cast<std::size_t>(duplicateIndex)];
        if (duplicate.cid == 125)
        {
            duplicate.properties.erase(std::remove_if(duplicate.properties.begin(), duplicate.properties.end(),
                                                      [](const BrxPropertyRecord &property)
                                                      { return property.eopid == 175; }),
                                       duplicate.properties.end());
            for (auto &property : duplicate.properties)
                if (property.eopid == 168)
                {
                    property.boolValue = false;
                    property.modified = true;
                }
            continue;
        }
        if (duplicate.cid != 116)
            continue;
        for (auto &property : duplicate.properties)
        {
            if (property.eopid == 157 || property.eopid == 158)
            {
                property.boolValue = false;
                property.modified = true;
            }
            else if (property.eopid == 159)
            {
                property.intValue = -1;
                property.modified = true;
            }
        }
    }
    SynchronizeWarpAnimationCounts(document);
    SynchronizeExitAnimationCounts(document);
    result.succeeded = true;
    result.primaryObjectIndex = indexRemap[rootObjectIndex];
    result.message = "Duplicated " + std::to_string(hierarchy.size()) + " object(s) with new OIDs.";
    if (std::any_of(hierarchy.begin(), hierarchy.end(), [&](const std::int32_t index)
                    { return document.Objects()[static_cast<std::size_t>(index)].spliceIndex >= 0; }))
        result.message += " Splice modules remain shared; literal OID lookups keep their original targets."
                          " Use Splice's make-unique/source controls for independent logic.";
    return result;
}

EditorOperationResult EditorDocumentOperations::ImportHierarchy(BrxDocument &destination, const BrxDocument &source,
                                                                const std::int32_t sourceRootObjectIndex,
                                                                const std::int32_t destinationParentObjectIndex,
                                                                const bool preserveNestedProxies)
{
    EditorOperationResult result;
    auto hierarchy = CollectHierarchy(source, sourceRootObjectIndex);
    if (hierarchy.empty())
    {
        result.message = "The hierarchy package has no valid root.";
        return result;
    }
    // PROXY roots describe their source with a physical [objectBegin, objectCount)
    // span.  CollectHierarchy is breadth-first for scene-tree operations, which
    // can reorder grandchildren after later siblings.  Preserve the source BRX
    // object order while appending this package so every imported inline-source
    // span remains contiguous and retains the same member ordering.
    std::sort(hierarchy.begin(), hierarchy.end());
    if (destinationParentObjectIndex < -1 ||
        (destinationParentObjectIndex >= 0 && !ValidObjectIndex(destination, destinationParentObjectIndex)))
    {
        result.message = "The requested import parent no longer exists.";
        return result;
    }
    if (destinationParentObjectIndex >= 0)
    {
        const auto &parent = destination.Objects()[static_cast<std::size_t>(destinationParentObjectIndex)];
        const auto sourceCid = source.Objects()[static_cast<std::size_t>(sourceRootObjectIndex)].cid;
        if (parent.cid == 83 || parent.proxyClone || parent.proxyRootIndex >= 0)
        {
            result.message = "Importing here requires PROXY source-span or attachment bookkeeping. "
                             "Import at the world root or beneath an ordinary ALO instead.";
            return result;
        }
        if (parent.cid == 120 || parent.cid == 136 ||
            ((parent.cid == 116 || parent.cid == 125) && sourceCid != 131) ||
            (!CidDerivesFrom(parent.cid, 2) &&
             !((parent.cid == 116 || parent.cid == 125) && sourceCid == 131)))
        {
            result.message = "The selected parent cannot serialize this imported hierarchy. "
                             "Use an ordinary ALO or the dedicated owner-specific tools.";
            return result;
        }
        std::unordered_set<std::int32_t> ancestors;
        for (auto ancestor = destinationParentObjectIndex; ancestor >= 0;)
        {
            if (!ValidObjectIndex(destination, ancestor) || !ancestors.insert(ancestor).second)
            {
                result.message = "The import parent has an invalid or cyclic hierarchy.";
                return result;
            }
            ancestor = destination.Objects()[static_cast<std::size_t>(ancestor)].parentIndex;
            if (ancestor < -1)
            {
                result.message = "The import parent has an invalid parent identity.";
                return result;
            }
        }
        const glm::mat4 parentWorld = glm::make_mat4(parent.worldTransform);
        const glm::mat4 world =
            glm::make_mat4(source.Objects()[static_cast<std::size_t>(sourceRootObjectIndex)].worldTransform);
        const glm::mat4 local = glm::inverse(parentWorld) * world;
        const auto finiteMatrix = [](const glm::mat4 &matrix)
        {
            const float *values = glm::value_ptr(matrix);
            return std::all_of(values, values + 16, [](const float value) { return std::isfinite(value); });
        };
        if (!finiteMatrix(parentWorld) || !finiteMatrix(world) || !finiteMatrix(local))
        {
            result.message = "The import transform is non-finite or its parent transform cannot be inverted.";
            return result;
        }
    }
    for (const auto index : hierarchy)
    {
        const auto cid = source.Objects()[static_cast<std::size_t>(index)].cid;
        if (cid == 7 || cid == 108 || cid == 109 || (cid == 83 && !preserveNestedProxies))
        {
            result.message =
                "JT, PROXY, SW and CM infrastructure cannot be imported as an ordinary reusable hierarchy.";
            return result;
        }
    }

    // Event symbols 0..29 are a fixed ProjectCane ABI.  Every other SYMID is
    // level-local, so imported modules need a fresh range instead of silently
    // aliasing unrelated destination locals that happen to use the same ID.
    constexpr std::uint32_t kFirstLocalSymbol = 30;
    const std::uint32_t destinationSymbolBase = (std::max)(destination.WorldHeader().symbolCount, kFirstLocalSymbol);
    const std::uint32_t sourceLocalSymbolCount =
        source.WorldHeader().symbolCount > kFirstLocalSymbol ? source.WorldHeader().symbolCount - kFirstLocalSymbol : 0;
    if (destinationSymbolBase > 1'000'000u || sourceLocalSymbolCount > 1'000'000u - destinationSymbolBase)
    {
        result.message = "Imported Splice symbols would exceed the loader's safe limit.";
        return result;
    }

    // Only carry modules used by this package, plus their transitive imports.
    // LoadSwSpliceFromBrx evaluates every serialized module, even when no object
    // attaches it, so copying the source world's complete map executes unrelated
    // level logic in the destination.
    std::unordered_set<std::int32_t> requiredModuleSet;
    std::vector<std::int32_t> pendingModules;
    for (const auto index : hierarchy)
    {
        const auto splice = source.Objects()[static_cast<std::size_t>(index)].spliceIndex;
        if (splice >= 0)
            pendingModules.push_back(splice);
    }
    if (preserveNestedProxies)
        for (const auto &proxy : source.Proxies())
            if (std::find(hierarchy.begin(), hierarchy.end(), proxy.ownerObjectIndex) != hierarchy.end())
                for (const auto &overrideRecord : proxy.overrides)
                {
                    if (overrideRecord.replacementSpliceIndex >= 0)
                        pendingModules.push_back(overrideRecord.replacementSpliceIndex);
                    if (overrideRecord.inheritedSpliceIndex >= 0)
                        pendingModules.push_back(overrideRecord.inheritedSpliceIndex);
                }
    const auto validSourceNode = [&](const std::int32_t node)
    { return node >= 0 && static_cast<std::size_t>(node) < source.SpliceNodes().size(); };
    while (!pendingModules.empty())
    {
        const auto moduleIndex = pendingModules.back();
        pendingModules.pop_back();
        if (moduleIndex < 0 || static_cast<std::size_t>(moduleIndex) >= source.SpliceModules().size())
        {
            result.message = "Imported hierarchy references a missing Splice module.";
            return result;
        }
        if (!requiredModuleSet.insert(moduleIndex).second)
            continue;
        std::vector<std::int32_t> pendingNodes{source.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode};
        std::unordered_set<std::int32_t> visitedNodes;
        while (!pendingNodes.empty())
        {
            const auto nodeIndex = pendingNodes.back();
            pendingNodes.pop_back();
            if (nodeIndex == -1)
                continue;
            if (!validSourceNode(nodeIndex))
            {
                result.message = "Imported Splice contains an invalid expression link. Repair it before importing.";
                return result;
            }
            if (!visitedNodes.insert(nodeIndex).second)
                continue;
            const auto &node = source.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
            if (node.tag == 28 && (node.intValue < 0 ||
                static_cast<std::uint32_t>(node.intValue) >= source.WorldHeader().symbolCount))
            {
                result.message = "Imported Splice references a symbol outside its source symbol table.";
                return result;
            }
            pendingNodes.push_back(node.childNode);
            pendingNodes.push_back(node.nextNode);
            if (node.tag != 30 || !validSourceNode(node.childNode))
                continue;
            const auto &head = source.SpliceNodes()[static_cast<std::size_t>(node.childNode)];
            if (head.tag != 16 || !validSourceNode(head.nextNode))
                continue;
            const auto &imported = source.SpliceNodes()[static_cast<std::size_t>(head.nextNode)];
            if (imported.tag == 20)
                pendingModules.push_back(imported.intValue);
        }
    }
    std::vector<std::int32_t> requiredModules(requiredModuleSet.begin(), requiredModuleSet.end());
    std::sort(requiredModules.begin(), requiredModules.end());
    std::unordered_set<std::int32_t> requiredNodeSet;
    for (const auto moduleIndex : requiredModules)
    {
        std::vector<std::pair<std::int32_t, std::uint32_t>> pendingNodes{
            {source.SpliceModules()[static_cast<std::size_t>(moduleIndex)].rootNode, 0u}};
        while (!pendingNodes.empty())
        {
            const auto [nodeIndex, depth] = pendingNodes.back();
            pendingNodes.pop_back();
            if (nodeIndex == -1)
                continue;
            if (!validSourceNode(nodeIndex) || depth > 4096u || !requiredNodeSet.insert(nodeIndex).second)
            {
                result.message = "Imported Splice has invalid, cyclic, shared or excessively deep expression links.";
                return result;
            }
            const auto &node = source.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
            if (node.tag >= 36u || (node.tag != 30u && node.childNode != -1) ||
                (node.tag == 29u && (node.intValue < 0 || node.intValue >= 125)) ||
                (node.tag == 32u && node.intValue != 0))
            {
                result.message = "Imported Splice contains a tag, built-in or raw pointer payload that cannot be rebuilt safely.";
                return result;
            }
            const std::size_t floatCount = node.tag == 21u ? 1u : node.tag == 23u ? 16u : node.tag == 25u ? 2u :
                (node.tag == 22u || node.tag == 24u || node.tag == 26u) ? 3u : 0u;
            if (!std::all_of(node.floatValue, node.floatValue + floatCount,
                             [](const float value) { return std::isfinite(value); }))
            {
                result.message = "Imported Splice contains a non-finite literal. Repair it before importing.";
                return result;
            }
            pendingNodes.emplace_back(node.childNode, depth + 1u);
            pendingNodes.emplace_back(node.nextNode, depth + 1u);
        }
    }
    if (destination.SpliceNodes().size() > 10'000'000u ||
        requiredNodeSet.size() > 10'000'000u - destination.SpliceNodes().size())
    {
        result.message = "Imported Splice expressions would exceed the loader's node limit.";
        return result;
    }
    if (destination.SpliceModules().size() > 1'000'000u ||
        requiredModules.size() > 1'000'000u - destination.SpliceModules().size())
    {
        result.message = "Imported Splice modules would exceed the loader's module limit.";
        return result;
    }
    if (!requiredModules.empty() &&
        destination.SpliceModules().size() + requiredModules.size() > static_cast<std::size_t>(INT16_MAX) + 1u)
    {
        result.message = "Imported object Splice indices would exceed signed 16-bit storage.";
        return result;
    }

    const std::int32_t bitmapBase = static_cast<std::int32_t>(destination.Bitmaps().size());
    for (auto bitmap : source.Bitmaps())
        destination.AddBitmap(std::move(bitmap));
    const std::int32_t shaderBase = static_cast<std::int32_t>(destination.Shaders().size());
    const auto remapBitmap = [&](std::vector<std::int32_t> &values)
    {
        for (auto &value : values)
            if (value >= 0)
                value += bitmapBase;
    };
    for (auto shader : source.Shaders())
    {
        remapBitmap(shader.ambientBitmapIndices);
        remapBitmap(shader.diffuseBitmapIndices);
        remapBitmap(shader.saturateBitmapIndices);
        for (auto &stage : shader.textureStages)
        {
            remapBitmap(stage.ambientBitmapIndices);
            remapBitmap(stage.diffuseBitmapIndices);
            remapBitmap(stage.saturateBitmapIndices);
        }
        destination.AddShader(std::move(shader));
    }

    std::unordered_map<std::int32_t, std::int32_t> moduleRemap;
    for (const auto sourceModule : requiredModules)
        moduleRemap.emplace(sourceModule,
                            static_cast<std::int32_t>(destination.SpliceModules().size() + moduleRemap.size()));
    std::unordered_map<std::int32_t, std::int32_t> nodeRemap;
    std::vector<std::int32_t> importedNodeIndices;
    std::vector<std::int32_t> orderedNodes(requiredNodeSet.begin(), requiredNodeSet.end());
    std::sort(orderedNodes.begin(), orderedNodes.end());
    for (const auto sourceNode : orderedNodes)
    {
        auto copy = source.SpliceNodes()[static_cast<std::size_t>(sourceNode)];
        copy.offset = copy.serializedSize = 0;
        copy.modified = true;
        copy.editorCreated = true;
        if (copy.tag == 28 && copy.intValue >= static_cast<std::int32_t>(kFirstLocalSymbol))
            copy.intValue = static_cast<std::int32_t>(destinationSymbolBase +
                                                      (static_cast<std::uint32_t>(copy.intValue) - kFirstLocalSymbol));
        const auto targetNode = static_cast<std::int32_t>(destination.SpliceNodes().size());
        nodeRemap.emplace(sourceNode, targetNode);
        importedNodeIndices.push_back(targetNode);
        destination.EditableSpliceNodes().push_back(copy);
    }
    const auto mappedSpliceNode = [&](const std::int32_t index)
        { return index == -1 ? -1 : nodeRemap.at(index); };
    for (const auto sourceNode : orderedNodes)
    {
        const auto &original = source.SpliceNodes()[static_cast<std::size_t>(sourceNode)];
        auto &copy = destination.EditableSpliceNodes()[static_cast<std::size_t>(nodeRemap.at(sourceNode))];
        copy.childNode = mappedSpliceNode(original.childNode);
        copy.nextNode = mappedSpliceNode(original.nextNode);
    }
    for (const auto sourceModule : requiredModules)
    {
        auto module = source.SpliceModules()[static_cast<std::size_t>(sourceModule)];
        module.rootNode = mappedSpliceNode(module.rootNode);
        module.offset = module.serializedSize = 0;
        module.modified = true;
        module.editorCreated = true;
        destination.EditableSpliceModules().push_back(std::move(module));
    }
    for (const auto nodeIndex : importedNodeIndices)
    {
        auto &wrapper = destination.EditableSpliceNodes()[static_cast<std::size_t>(nodeIndex)];
        if (wrapper.tag != 30 || wrapper.childNode < 0)
            continue;
        auto &importHead = destination.EditableSpliceNodes()[static_cast<std::size_t>(wrapper.childNode)];
        if (importHead.tag != 16 || importHead.nextNode < 0)
            continue;
        auto &importIndex = destination.EditableSpliceNodes()[static_cast<std::size_t>(importHead.nextNode)];
        if (importIndex.tag != 20)
            continue;
        const auto replacement = moduleRemap.find(importIndex.intValue);
        if (replacement == moduleRemap.end())
        {
            result.message = "Imported Splice dependency was not included in the module closure.";
            return result;
        }
        importIndex.intValue = replacement->second;
    }
    destination.EditableWorldHeader().symbolCount = destinationSymbolBase + sourceLocalSymbolCount;
    destination.EditableWorldHeader().extraOptionCount =
        (std::max)(destination.WorldHeader().extraOptionCount, source.WorldHeader().extraOptionCount);

    std::unordered_map<std::int32_t, std::int32_t> indexRemap;
    std::unordered_map<std::int16_t, std::int16_t> oidRemap;
    std::unordered_map<std::int32_t, std::int32_t> proxyRemap;
    if (preserveNestedProxies)
        for (std::size_t oldProxy = 0; oldProxy < source.Proxies().size(); ++oldProxy)
            if (std::find(hierarchy.begin(), hierarchy.end(), source.Proxies()[oldProxy].ownerObjectIndex) !=
                hierarchy.end())
                proxyRemap.emplace(static_cast<std::int32_t>(oldProxy),
                                   static_cast<std::int32_t>(destination.Proxies().size() + proxyRemap.size()));
    for (const auto oldIndex : hierarchy)
    {
        auto copy = source.Objects()[static_cast<std::size_t>(oldIndex)];
        // Duplicate OIDs are intentional inside nested PROXY contexts. Retail
        // lookup disambiguates them by the nearest hierarchy/PXR boundary, so
        // assigning each occurrence a different OID destroys that behavior.
        // Allocate one fresh destination OID per distinct non-negative source
        // OID and reuse it for every contextual duplicate. OID_Nil remains
        // anonymous and must never be promoted to a named object.
        if (copy.oid >= 0)
        {
            if (const auto existing = oidRemap.find(copy.oid); existing != oidRemap.end())
                copy.oid = existing->second;
            else
            {
                const auto oid = AllocateOid(destination);
                if (oid < 0)
                {
                    result.message = "Object IDs are exhausted.";
                    return result;
                }
                oidRemap.emplace(copy.oid, oid);
                copy.oid = oid;
            }
        }
        copy.offset = copy.dataOffset = copy.endOffset = 0;
        copy.proxyClone = false;
        if (preserveNestedProxies)
        {
            if (const auto proxy = proxyRemap.find(copy.proxyRecordIndex); proxy != proxyRemap.end())
                copy.proxyRecordIndex = proxy->second;
            else
                copy.proxyRecordIndex = copy.proxyRootIndex = copy.proxySourceObjectIndex = -1;
        }
        else
            copy.proxyRecordIndex = copy.proxyRootIndex = copy.proxySourceObjectIndex = -1;
        if (copy.spliceIndex >= 0)
        {
            const auto splice = moduleRemap.find(copy.spliceIndex);
            if (splice == moduleRemap.end())
            {
                result.message = "Imported object Splice module was not copied.";
                return result;
            }
            copy.spliceIndex = static_cast<std::int16_t>(splice->second);
        }
        if (!copy.editorName.empty() && destination.FindObjectByEditorName(copy.editorName) >= 0)
            copy.editorName.clear();
        copy.loaderName = "Imported hierarchy";
        destination.EditableObjects().push_back(std::move(copy));
        const auto newIndex = static_cast<std::int32_t>(destination.Objects().size() - 1);
        indexRemap.emplace(oldIndex, newIndex);
        result.affectedObjectIndices.push_back(newIndex);
    }
    for (const auto oldIndex : hierarchy)
    {
        auto &copy = destination.EditableObjects()[static_cast<std::size_t>(indexRemap[oldIndex])];
        if (oldIndex == sourceRootObjectIndex)
        {
            copy.parentIndex = destinationParentObjectIndex;
            const glm::mat4 world = glm::make_mat4(copy.worldTransform);
            const glm::mat4 parentWorld =
                destinationParentObjectIndex >= 0
                    ? glm::make_mat4(
                          destination.Objects()[static_cast<std::size_t>(destinationParentObjectIndex)].worldTransform)
                    : glm::mat4(1.0f);
            const glm::mat4 local = glm::inverse(parentWorld) * world;
            std::copy(glm::value_ptr(local), glm::value_ptr(local) + 16, copy.localTransform);
        }
        else if (const auto parent = indexRemap.find(copy.parentIndex); parent != indexRemap.end())
            copy.parentIndex = parent->second;
        else
            copy.parentIndex = destinationParentObjectIndex;
        if (copy.proxySourceObjectIndex >= 0)
        {
            if (const auto sourceObject = indexRemap.find(copy.proxySourceObjectIndex);
                sourceObject != indexRemap.end())
                copy.proxySourceObjectIndex = sourceObject->second;
            else
                copy.proxySourceObjectIndex = -1;
        }
        if (copy.proxyRuntimeParentIndex >= 0)
        {
            if (const auto runtimeParent = indexRemap.find(copy.proxyRuntimeParentIndex);
                runtimeParent != indexRemap.end())
                copy.proxyRuntimeParentIndex = runtimeParent->second;
            else
                copy.proxyRuntimeParentIndex = destinationParentObjectIndex;
        }
        if (copy.alox.editorFosterObjectIndex >= 0)
        {
            if (const auto foster = indexRemap.find(copy.alox.editorFosterObjectIndex); foster != indexRemap.end())
                copy.alox.editorFosterObjectIndex = foster->second;
            else
                copy.alox.editorFosterObjectIndex = -1;
        }
        RemapKnownOidReferences(copy, oidRemap);
    }

    RemapSpliceLookupOids(destination,
                         importedNodeIndices.empty() ? destination.SpliceNodes().size() : importedNodeIndices.front(),
                         oidRemap);

    if (preserveNestedProxies)
    {
        std::vector<std::pair<std::int32_t, std::int32_t>> orderedProxies(proxyRemap.begin(), proxyRemap.end());
        std::sort(orderedProxies.begin(), orderedProxies.end(),
                  [](const auto &left, const auto &right) { return left.second < right.second; });
        for (const auto &[oldProxyIndex, newProxyIndex] : orderedProxies)
        {
            (void)newProxyIndex;
            auto proxy = source.Proxies()[static_cast<std::size_t>(oldProxyIndex)];
            const auto owner = indexRemap.find(proxy.ownerObjectIndex);
            if (owner == indexRemap.end())
            {
                result.message = "A nested PROXY owner was not included in the portable hierarchy.";
                return result;
            }
            proxy.ownerObjectIndex = owner->second;
            for (auto &root : proxy.roots)
            {
                if (root.kind == BrxProxyRootKind::SourceReference)
                {
                    result.message = "A nested PROXY dependency was not materialized before import.";
                    return result;
                }
                const auto instantiated = indexRemap.find(root.instantiatedRootObjectIndex);
                const auto begin = indexRemap.find(static_cast<std::int32_t>(root.objectBegin));
                if (instantiated == indexRemap.end() || begin == indexRemap.end())
                {
                    result.message = "A nested PROXY source range is incomplete in the portable package.";
                    return result;
                }
                for (std::uint32_t offset = 0; offset < root.objectCount; ++offset)
                {
                    const auto sourceMember = static_cast<std::int32_t>(root.objectBegin + offset);
                    const auto mapped = indexRemap.find(sourceMember);
                    if (mapped == indexRemap.end() ||
                        mapped->second != begin->second + static_cast<std::int32_t>(offset))
                    {
                        result.message = "A nested PROXY source range is not contiguous after hierarchy import.";
                        return result;
                    }
                }
                root.instantiatedRootObjectIndex = instantiated->second;
                root.objectBegin = static_cast<std::uint32_t>(begin->second);
                root.sourceProxyRecordIndex = proxyRemap.at(oldProxyIndex);
            }
            for (auto &child : proxy.attachedChildObjectIndices)
            {
                const auto mapped = indexRemap.find(child);
                child = mapped == indexRemap.end() ? -1 : mapped->second;
            }
            proxy.attachedChildObjectIndices.erase(
                std::remove(proxy.attachedChildObjectIndices.begin(), proxy.attachedChildObjectIndices.end(), -1),
                proxy.attachedChildObjectIndices.end());
            for (auto &overrideRecord : proxy.overrides)
            {
                if (const auto target = indexRemap.find(overrideRecord.targetObjectIndex); target != indexRemap.end())
                    overrideRecord.targetObjectIndex = target->second;
                else
                    overrideRecord.targetObjectIndex = -1;
                if (const auto oid = oidRemap.find(overrideRecord.targetOid); oid != oidRemap.end())
                    overrideRecord.targetOid = oid->second;
                RemapKnownOidProperties(overrideRecord.properties, oidRemap);
                RemapKnownOidProperties(overrideRecord.inheritedProperties, oidRemap);
                if (overrideRecord.replacementSpliceIndex >= 0)
                {
                    const auto splice = moduleRemap.find(overrideRecord.replacementSpliceIndex);
                    if (splice == moduleRemap.end())
                    {
                        result.message = "PROXY replacement Splice dependency was not copied.";
                        return result;
                    }
                    overrideRecord.replacementSpliceIndex = static_cast<std::int16_t>(splice->second);
                }
                if (overrideRecord.inheritedSpliceIndex >= 0)
                {
                    const auto splice = moduleRemap.find(overrideRecord.inheritedSpliceIndex);
                    if (splice == moduleRemap.end())
                    {
                        result.message = "PROXY inherited Splice dependency was not copied.";
                        return result;
                    }
                    overrideRecord.inheritedSpliceIndex = static_cast<std::int16_t>(splice->second);
                }
            }
            destination.EditableProxies().push_back(std::move(proxy));
        }
    }

    const auto copyOwned = [&](const auto &sourceRecords, auto &destinationRecords)
    {
        for (auto record : sourceRecords)
        {
            const auto owner = indexRemap.find(record.ownerObjectIndex);
            if (owner == indexRemap.end())
                continue;
            record.ownerObjectIndex = owner->second;
            destinationRecords.push_back(std::move(record));
        }
    };
    copyOwned(source.CollisionTriangles(), destination.EditableCollisionTriangles());
    copyOwned(source.Lights(), destination.EditableLights());
    for (auto camera : source.Cameras())
    {
        const auto owner = indexRemap.find(camera.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        camera.ownerObjectIndex = owner->second;
        if (const auto target = oidRemap.find(camera.targetOid); target != oidRemap.end())
            camera.targetOid = target->second;
        destination.EditableCameras().push_back(std::move(camera));
    }
    for (auto checkpoint : source.Checkpoints())
    {
        const auto owner = indexRemap.find(checkpoint.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        checkpoint.ownerObjectIndex = owner->second;
        if (const auto warp = oidRemap.find(checkpoint.warpOid); warp != oidRemap.end())
            checkpoint.warpOid = warp->second;
        destination.EditableCheckpoints().push_back(std::move(checkpoint));
    }
    for (auto warp : source.Warps())
    {
        const auto owner = indexRemap.find(warp.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        warp.ownerObjectIndex = owner->second;
        for (auto &hidden : warp.hiddenObjectOids)
            if (const auto mapped = oidRemap.find(hidden); mapped != oidRemap.end())
                hidden = mapped->second;
        destination.EditableWarps().push_back(std::move(warp));
    }
    copyOwned(source.Exits(), destination.EditableExits());
    copyOwned(source.Volumes(), destination.EditableVolumes());
    for (auto segment : source.AnimationSegments())
    {
        const auto owner = indexRemap.find(segment.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        segment.ownerObjectIndex = owner->second;
        const auto remapOid = [&](std::int16_t &oid)
        {
            if (const auto mapped = oidRemap.find(oid); mapped != oidRemap.end())
                oid = mapped->second;
        };
        remapOid(segment.rootOid);
        for (auto &oid : segment.searchRootOids)
            remapOid(oid);
        for (auto &channel : segment.channels)
            remapOid(channel.targetOid);
        destination.EditableAnimationSegments().push_back(std::move(segment));
    }
    copyOwned(source.Scans(), destination.EditableScans());
    copyOwned(source.Effects(), destination.EditableEffects());
    copyOwned(source.ShapePaths(), destination.EditableShapePaths());
    copyOwned(source.PathZones(), destination.EditablePathZones());
    for (auto machine : source.StateMachines())
    {
        const auto owner = indexRemap.find(machine.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        machine.ownerObjectIndex = owner->second;
        for (auto &transition : machine.transitions)
            if (transition.animationObjectIndex >= 0)
            {
                const auto animation = indexRemap.find(transition.animationObjectIndex);
                transition.animationObjectIndex = animation == indexRemap.end() ? -1 : animation->second;
                if (transition.animationObjectIndex < 0)
                    transition.ownsAnimation = false;
            }
        destination.EditableStateMachines().push_back(std::move(machine));
    }
    for (auto reach : source.ReachMaps())
    {
        const auto owner = indexRemap.find(reach.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        reach.ownerObjectIndex = owner->second;
        for (auto &oid : reach.animationOids)
            if (const auto mapped = oidRemap.find(oid); mapped != oidRemap.end())
                oid = mapped->second;
        destination.EditableReachMaps().push_back(std::move(reach));
    }
    copyOwned(source.Keyholes(), destination.EditableKeyholes());
    for (auto warp : source.GlobWarps())
    {
        const auto owner = indexRemap.find(warp.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        warp.ownerObjectIndex = owner->second;
        if (const auto controller = oidRemap.find(warp.controllerOid); controller != oidRemap.end())
            warp.controllerOid = controller->second;
        destination.EditableGlobWarps().push_back(std::move(warp));
    }
    for (auto beam : source.LaserBeams())
    {
        const auto owner = indexRemap.find(beam.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        beam.ownerObjectIndex = owner->second;
        if (const auto shape = indexRemap.find(beam.shapeObjectIndex); shape != indexRemap.end())
            beam.shapeObjectIndex = shape->second;
        else
            beam.shapeObjectIndex = -1;
        destination.EditableLaserBeams().push_back(std::move(beam));
    }

    // A proxy instance's GLOBSET can reference geometry records owned by its
    // reusable source rather than by the materialized clone object. Follow the
    // GLOBSET references themselves so portable hierarchies carry their models.
    std::unordered_map<std::size_t, std::int32_t> requiredGlobs;
    for (const auto &globset : source.Globsets())
    {
        const auto owner = indexRemap.find(globset.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        for (const auto globIndex : globset.globIndices)
            if (globIndex < source.Globs().size())
                requiredGlobs.try_emplace(globIndex, owner->second);
    }
    for (std::size_t i = 0; i < source.Globs().size(); ++i)
        if (const auto owner = indexRemap.find(source.Globs()[i].ownerObjectIndex); owner != indexRemap.end())
            requiredGlobs.try_emplace(i, owner->second);

    std::vector<std::pair<std::size_t, std::int32_t>> orderedGlobs(requiredGlobs.begin(), requiredGlobs.end());
    std::sort(orderedGlobs.begin(), orderedGlobs.end(),
              [](const auto &left, const auto &right) { return left.first < right.first; });
    std::unordered_map<std::size_t, std::size_t> globRemap;
    for (const auto &[i, newOwner] : orderedGlobs)
    {
        auto glob = source.Globs()[i];
        glob.ownerObjectIndex = newOwner;
        if (const auto controller = oidRemap.find(glob.warpControllerOid); controller != oidRemap.end())
            glob.warpControllerOid = controller->second;
        for (auto &subglob : glob.sourceSubglobs)
            if (subglob.shaderIndex != 0xffff)
                subglob.shaderIndex = static_cast<std::uint16_t>(subglob.shaderIndex + shaderBase);
        glob.sourceRecordIndex = static_cast<std::size_t>(-1);
        glob.sourceGlobIndex = -1;
        glob.instanced = false;
        glob.editorCreatedInstance = false;
        glob.propertyMask &= ~std::uint16_t{1};
        // Loader caches contain offsets into the source document's flattened
        // vertex/index arrays. They are never portable. Rebuild fresh packets
        // from the authoritative SUBGLOB mesh in the destination document.
        glob.derivedBatches.clear();
        glob.derivedCelPackets.clear();
        const auto added = destination.EditableGlobs().size();
        destination.EditableGlobs().push_back(std::move(glob));
        if (!destination.CommitGlobEdit(added))
        {
            result.message = "A portable model GLOB could not rebuild its destination geometry.";
            return result;
        }
        globRemap.emplace(i, added);
    }
    for (auto globset : source.Globsets())
    {
        const auto owner = indexRemap.find(globset.ownerObjectIndex);
        if (owner == indexRemap.end())
            continue;
        globset.ownerObjectIndex = owner->second;
        std::vector<std::size_t> indices;
        for (auto &boneOid : globset.boneOids)
        {
            if (const auto mapped = oidRemap.find(boneOid); mapped != oidRemap.end())
                boneOid = mapped->second;
            else
                boneOid = -1; // External bones cannot survive a self-contained portable hierarchy.
        }
        for (const auto old : globset.globIndices)
            if (const auto found = globRemap.find(old); found != globRemap.end())
                indices.push_back(found->second);
        globset.globIndices = std::move(indices);
        destination.EditableGlobsets().push_back(std::move(globset));
    }
    destination.RebuildRenderPacketsFromGlobs();
    SynchronizeWarpAnimationCounts(destination);
    SynchronizeExitAnimationCounts(destination);
    result.succeeded = true;
    result.primaryObjectIndex = indexRemap[sourceRootObjectIndex];
    result.message = "Imported " + std::to_string(hierarchy.size()) + " object(s) with new OIDs.";
    if (!requiredModules.empty())
        result.message += " Splice literal object lookups were retargeted; review computed OIDs and external script dependencies.";
    return result;
}

EditorOperationResult EditorDocumentOperations::ReplaceHierarchy(BrxDocument &destination, const BrxDocument &source,
                                                                 const std::int32_t sourceRootObjectIndex,
                                                                 const std::int32_t destinationRootObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(destination, destinationRootObjectIndex))
    {
        result.message = "No valid destination hierarchy is selected.";
        return result;
    }
    const auto destinationCid = destination.Objects()[static_cast<std::size_t>(destinationRootObjectIndex)].cid;
    if (destinationCid == 7 || destinationCid == 83 || destinationCid == 108 || destinationCid == 109)
    {
        result.message = "JT, PROXY, SW and CM infrastructure cannot be replaced as ordinary reusable hierarchies.";
        return result;
    }
    const auto &destinationObject = destination.Objects()[static_cast<std::size_t>(destinationRootObjectIndex)];
    if (destinationObject.proxyRecordIndex >= 0 &&
        static_cast<std::size_t>(destinationObject.proxyRecordIndex) < destination.Proxies().size() &&
        destination.Proxies()[static_cast<std::size_t>(destinationObject.proxyRecordIndex)].ownerObjectIndex !=
            destinationRootObjectIndex)
    {
        result.message = "A member of a PROXY source or linked instance cannot be replaced independently.";
        return result;
    }
    const auto removed = CollectHierarchy(destination, destinationRootObjectIndex);
    std::unordered_set<std::int16_t> removedSpliceTargetOids;
    for (const auto index : removed)
        removedSpliceTargetOids.insert(destination.Objects()[static_cast<std::size_t>(index)].oid);
    const auto oldRoot = destination.Objects()[static_cast<std::size_t>(destinationRootObjectIndex)];
    const glm::mat4 desiredWorld = glm::make_mat4(oldRoot.worldTransform);
    const auto finiteMatrix = [](const glm::mat4 &matrix)
    {
        const float *values = glm::value_ptr(matrix);
        return std::all_of(values, values + 16, [](const float value) { return std::isfinite(value); });
    };
    if (!ValidObjectIndex(source, sourceRootObjectIndex) || oldRoot.parentIndex < -1 ||
        (oldRoot.parentIndex >= 0 && !ValidObjectIndex(destination, oldRoot.parentIndex)))
    {
        result.message = "The package root or destination parent does not exist.";
        return result;
    }
    std::unordered_set<std::int32_t> parentAncestors;
    for (auto ancestor = oldRoot.parentIndex; ancestor >= 0;)
    {
        if (!ValidObjectIndex(destination, ancestor) || ancestor == destinationRootObjectIndex ||
            !parentAncestors.insert(ancestor).second)
        {
            result.message = "The destination hierarchy has an invalid or cyclic parent chain.";
            return result;
        }
        ancestor = destination.Objects()[static_cast<std::size_t>(ancestor)].parentIndex;
        if (ancestor < -1)
        {
            result.message = "The destination hierarchy has an invalid parent identity.";
            return result;
        }
    }
    const glm::mat4 sourceWorld =
        glm::make_mat4(source.Objects()[static_cast<std::size_t>(sourceRootObjectIndex)].worldTransform);
    const auto sourceCid = source.Objects()[static_cast<std::size_t>(sourceRootObjectIndex)].cid;
    const auto parentCid = oldRoot.parentIndex >= 0
        ? destination.Objects()[static_cast<std::size_t>(oldRoot.parentIndex)].cid : -1;
    if (parentCid == 120)
    {
        result.message = "An EXPLG child uses a deferred runtime parent and cannot be replaced as an ordinary hierarchy.";
        return result;
    }
    if ((parentCid == 116 || parentCid == 125 || parentCid == 136) && sourceCid != 131)
    {
        result.message = "An embedded animation must be replaced with an ASEG hierarchy package.";
        return result;
    }
    const glm::mat4 destinationParentWorld = oldRoot.parentIndex >= 0
        ? glm::make_mat4(destination.Objects()[static_cast<std::size_t>(oldRoot.parentIndex)].worldTransform)
        : glm::mat4(1.0f);
    const float sourceDeterminant = glm::determinant(sourceWorld);
    const float parentDeterminant = glm::determinant(destinationParentWorld);
    if (!finiteMatrix(desiredWorld) || !finiteMatrix(sourceWorld) || !finiteMatrix(destinationParentWorld) ||
        !std::isfinite(sourceDeterminant) || !std::isfinite(parentDeterminant) ||
        std::abs(sourceDeterminant) <= 0.000001f || std::abs(parentDeterminant) <= 0.000001f ||
        !finiteMatrix(desiredWorld * glm::inverse(sourceWorld)) ||
        !finiteMatrix(glm::inverse(destinationParentWorld) * desiredWorld))
    {
        result.message = "Cannot replace a hierarchy through non-finite or singular source/parent transforms.";
        return result;
    }
    // Stage at world root. The validated replacement path below restores its
    // parent and explicitly retargets attachment/transition ownership; ordinary
    // hierarchy import must not bypass those ownership checks.
    const auto firstImportedSpliceNode = destination.SpliceNodes().size();
    auto imported = ImportHierarchy(destination, source, sourceRootObjectIndex, -1);
    if (!imported.succeeded)
        return imported;
    const auto importedHierarchy = CollectHierarchy(destination, imported.primaryObjectIndex);
    auto &importedRoot = destination.EditableObjects()[static_cast<std::size_t>(imported.primaryObjectIndex)];
    importedRoot.parentIndex = oldRoot.parentIndex;
    const auto temporaryRootOid = importedRoot.oid;
    const glm::mat4 importedWorld = glm::make_mat4(importedRoot.worldTransform);
    const glm::mat4 delta = desiredWorld * glm::inverse(importedWorld);
    const std::unordered_set<std::int32_t> importedSet(importedHierarchy.begin(), importedHierarchy.end());
    for (const auto index : importedHierarchy)
    {
        auto &object = destination.EditableObjects()[static_cast<std::size_t>(index)];
        const glm::mat4 world = delta * glm::make_mat4(object.worldTransform);
        std::copy(glm::value_ptr(world), glm::value_ptr(world) + 16, object.worldTransform);
        if (object.so.present && object.so.fixedPhysics)
        {
            const float radiusScale = (std::max)(
                {glm::length(glm::vec3(delta[0])), glm::length(glm::vec3(delta[1])), glm::length(glm::vec3(delta[2]))});
            const auto transformGeometry = [&](BrxSoGeometryInfo &geometry)
            {
                if (geometry.positions.empty())
                    return;
                glm::vec3 lower((std::numeric_limits<float>::max)());
                glm::vec3 upper((std::numeric_limits<float>::lowest)());
                for (auto &position : geometry.positions)
                {
                    const glm::vec3 transformed =
                        glm::vec3(delta * glm::vec4(position[0], position[1], position[2], 1.0f));
                    position = {transformed.x, transformed.y, transformed.z};
                    lower = glm::min(lower, transformed);
                    upper = glm::max(upper, transformed);
                }
                for (auto &surface : geometry.surfaces)
                {
                    const glm::vec3 transformed =
                        glm::vec3(delta * glm::vec4(surface.center[0], surface.center[1], surface.center[2], 1.0f));
                    surface.center = {transformed.x, transformed.y, transformed.z};
                    surface.radius *= radiusScale;
                }
                std::copy(&lower.x, &lower.x + 3, geometry.boundsMin);
                std::copy(&upper.x, &upper.x + 3, geometry.boundsMax);
                geometry.radius *= radiusScale;
            };
            transformGeometry(object.so.collisionGeometry);
            transformGeometry(object.so.cameraGeometry);
            object.so.modified = true;
        }
    }
    glm::mat4 parentWorld(1.0f);
    if (oldRoot.parentIndex >= 0)
        parentWorld =
            glm::make_mat4(destination.Objects()[static_cast<std::size_t>(oldRoot.parentIndex)].worldTransform);
    const glm::mat4 rootLocal = glm::inverse(parentWorld) * desiredWorld;
    std::copy(glm::value_ptr(rootLocal), glm::value_ptr(rootLocal) + 16, importedRoot.localTransform);
    for (auto &triangle : destination.EditableCollisionTriangles())
        if (importedSet.contains(triangle.ownerObjectIndex))
            for (auto &point : triangle.positions)
            {
                const glm::vec4 value = delta * glm::vec4(point[0], point[1], point[2], 1);
                point[0] = value.x;
                point[1] = value.y;
                point[2] = value.z;
            }
    for (auto &light : destination.EditableLights())
        if (importedSet.contains(light.ownerObjectIndex))
        {
            const glm::vec4 position = delta * glm::vec4(light.position[0], light.position[1], light.position[2], 1);
            glm::vec3 direction = glm::mat3(delta) * glm::make_vec3(light.direction);
            if (glm::length(direction) > 0.0001f)
                direction = glm::normalize(direction);
            light.position[0] = position.x;
            light.position[1] = position.y;
            light.position[2] = position.z;
            light.direction[0] = direction.x;
            light.direction[1] = direction.y;
            light.direction[2] = direction.z;
        }
    for (auto &glob : destination.EditableGlobs())
        if (importedSet.contains(glob.ownerObjectIndex))
        {
            const glm::mat4 sourceToWorld = delta * glm::make_mat4(glob.sourceToWorld);
            std::copy(glm::value_ptr(sourceToWorld), glm::value_ptr(sourceToWorld) + 16, glob.sourceToWorld);
            const auto &owner = destination.Objects()[static_cast<std::size_t>(glob.ownerObjectIndex)];
            if (owner.so.present && owner.so.fixedPhysics)
            {
                const glm::vec3 center =
                    glm::vec3(delta * glm::vec4(glob.center[0], glob.center[1], glob.center[2], 1.0f));
                glob.center[0] = center.x;
                glob.center[1] = center.y;
                glob.center[2] = center.z;
                glob.radius *= (std::max)({glm::length(glm::vec3(delta[0])), glm::length(glm::vec3(delta[1])),
                                           glm::length(glm::vec3(delta[2]))});
            }
        }
    importedRoot.oid = oldRoot.oid;
    if (!oldRoot.editorName.empty())
        importedRoot.editorName = oldRoot.editorName;
    const std::unordered_map<std::int16_t, std::int16_t> rootOidRemap{{temporaryRootOid, oldRoot.oid}};
    RemapSpliceLookupOids(destination, firstImportedSpliceNode, rootOidRemap);
    for (const auto index : importedHierarchy)
        RemapKnownOidReferences(destination.EditableObjects()[static_cast<std::size_t>(index)], rootOidRemap);
    // Attached children are emitted by the PROXY's explicit list, not by an
    // ordinary parent scan. Preserve the replaced child's slot before deletion
    // compacts object indices; RemapDocumentObjectIndices remaps the new entry.
    for (auto &proxy : destination.EditableProxies())
        for (auto &child : proxy.attachedChildObjectIndices)
            if (child == destinationRootObjectIndex)
                child = imported.primaryObjectIndex;
    for (auto &machine : destination.EditableStateMachines())
        for (auto &transition : machine.transitions)
            if (transition.ownsAnimation && transition.animationObjectIndex == destinationRootObjectIndex)
                transition.animationObjectIndex = imported.primaryObjectIndex;
    const auto deleted = DeleteHierarchy(destination, destinationRootObjectIndex);
    if (!deleted.succeeded)
    {
        result.message = "Replacement imported, but the old hierarchy could not be removed: " + deleted.message;
        return result;
    }
    const auto removedBefore = std::count_if(removed.begin(), removed.end(), [&](const std::int32_t index)
                                             { return index < imported.primaryObjectIndex; });
    result.succeeded = true;
    result.primaryObjectIndex = imported.primaryObjectIndex - static_cast<std::int32_t>(removedBefore);
    result.message = "Replaced " + std::to_string(removed.size()) + " object(s) with " +
                     std::to_string(importedHierarchy.size()) + " object(s), preserving root OID " +
                     std::to_string(oldRoot.oid) + ".";
    result.message += SpliceRemovedTargetNotice(destination, std::move(removedSpliceTargetOids));
    return result;
}

EditorOperationResult EditorDocumentOperations::DeleteHierarchy(BrxDocument &document,
                                                                const std::int32_t rootObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, rootObjectIndex))
    {
        result.message = "No valid hierarchy root is selected.";
        return result;
    }
    const auto &selected = document.Objects()[static_cast<std::size_t>(rootObjectIndex)];
    if (selected.proxyRecordIndex >= 0 &&
        static_cast<std::size_t>(selected.proxyRecordIndex) < document.Proxies().size() &&
        document.Proxies()[static_cast<std::size_t>(selected.proxyRecordIndex)].ownerObjectIndex != rootObjectIndex)
    {
        const auto &containingProxy = document.Proxies()[static_cast<std::size_t>(selected.proxyRecordIndex)];
        if (selected.proxyRootIndex < 0 ||
            static_cast<std::size_t>(selected.proxyRootIndex) >= containingProxy.roots.size())
        {
            result.message = "The selected object has an invalid PROXY root identity.";
            return result;
        }
        const auto &containingRoot = containingProxy.roots[static_cast<std::size_t>(selected.proxyRootIndex)];
        if (containingRoot.kind == BrxProxyRootKind::SourceReference)
        {
            result.message = "A linked PROXY member inherits its hierarchy from the reusable source. "
                             "Make the root unique before deleting members.";
            return result;
        }
        if (containingRoot.instantiatedRootObjectIndex == rootObjectIndex)
        {
            result.message = "A PROXY root is owned by its wrapper. Delete the PROXY wrapper "
                             "instead of deleting the inline root by itself.";
            return result;
        }
        const bool hasLinkedInstances = std::any_of(
            document.Proxies().begin(), document.Proxies().end(),
            [&](const BrxProxyRecord &candidateProxy)
            {
                if (IsMaterializedProxyClone(document, candidateProxy))
                    return false;
                return std::any_of(candidateProxy.roots.begin(), candidateProxy.roots.end(),
                                   [&](const BrxProxyRootRecord &candidateRoot)
                                   {
                                       return candidateRoot.kind == BrxProxyRootKind::SourceReference &&
                                              candidateRoot.sourceProxyRecordIndex == selected.proxyRecordIndex &&
                                              candidateRoot.sourceProxyRootIndex == selected.proxyRootIndex;
                                   });
            });
        if (hasLinkedInstances)
        {
            result.message = "This reusable PROXY source still has linked instances. "
                             "Make those roots unique or delete them before changing source membership.";
            return result;
        }
    }

    const auto hierarchy = CollectHierarchy(document, rootObjectIndex);
    if (hierarchy.empty())
    {
        result.message = "No valid hierarchy root is selected.";
        return result;
    }
    std::unordered_set<std::int32_t> removed(hierarchy.begin(), hierarchy.end());
    for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
    {
        const auto &proxy = document.Proxies()[proxyIndex];
        if (!removed.contains(proxy.ownerObjectIndex))
            continue;
        for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
        {
            if (proxy.roots[rootIndex].kind != BrxProxyRootKind::InlineSource)
                continue;
            const auto sourceObjectIndex = proxy.roots[rootIndex].instantiatedRootObjectIndex;
            const bool stillReferenced = std::any_of(
                document.Proxies().begin(), document.Proxies().end(),
                [&](const BrxProxyRecord &candidateProxy)
                {
                    if (removed.contains(candidateProxy.ownerObjectIndex) ||
                        IsMaterializedProxyClone(document, candidateProxy))
                        return false;
                    return std::any_of(
                        candidateProxy.roots.begin(), candidateProxy.roots.end(),
                        [&](const BrxProxyRootRecord &candidateRoot)
                        {
                            if (candidateRoot.kind != BrxProxyRootKind::SourceReference)
                                return false;
                            if (candidateRoot.sourceProxyRecordIndex == static_cast<std::int32_t>(proxyIndex) &&
                                candidateRoot.sourceProxyRootIndex == static_cast<std::int32_t>(rootIndex))
                                return true;
                            // Record-vector indices are editor bookkeeping and
                            // may have been compacted by an earlier deletion.
                            // The materialized reference root keeps the actual
                            // source-object relationship used to clone the
                            // runtime hierarchy; use it as the authoritative
                            // dependency identity.
                            if (!ValidObjectIndex(document, candidateRoot.instantiatedRootObjectIndex) ||
                                !ValidObjectIndex(document, sourceObjectIndex))
                                return false;
                            const auto &referenceRoot =
                                document.Objects()[static_cast<std::size_t>(candidateRoot.instantiatedRootObjectIndex)];
                            return referenceRoot.proxySourceObjectIndex == sourceObjectIndex;
                        });
                });
            if (stillReferenced)
            {
                result.message = "This PROXY defines a reusable source that still has linked instances. "
                                 "Delete those instances or make them unique before deleting the source.";
                return result;
            }
        }
    }
    std::unordered_set<std::int16_t> removedSpliceTargetOids;
    for (const auto index : hierarchy)
        removedSpliceTargetOids.insert(document.Objects()[static_cast<std::size_t>(index)].oid);
    RemoveDerivedRecordsForDeletedObjects(document, removed);
    std::vector<std::int32_t> remap(document.Objects().size(), -1);
    std::vector<BrxObjectRecord> survivors;
    survivors.reserve(document.Objects().size() - hierarchy.size());
    for (std::size_t oldIndex = 0; oldIndex < document.Objects().size(); ++oldIndex)
    {
        if (removed.contains(static_cast<std::int32_t>(oldIndex)))
            continue;
        remap[oldIndex] = static_cast<std::int32_t>(survivors.size());
        survivors.push_back(document.Objects()[oldIndex]);
    }
    document.EditableObjects() = std::move(survivors);
    RemapDocumentObjectIndices(document, remap);
    RepairProxySourceIdentities(document);
    RebuildProxySourceListIndices(document);
    SynchronizeWarpAnimationCounts(document);
    SynchronizeExitAnimationCounts(document);
    document.RebuildRenderPacketsFromGlobs();
    result.succeeded = true;
    result.affectedObjectIndices = hierarchy;
    result.message = "Deleted " + std::to_string(hierarchy.size()) + " object(s).";
    result.message += SpliceRemovedTargetNotice(document, std::move(removedSpliceTargetOids));
    if (std::any_of(document.SpliceModules().begin(), document.SpliceModules().end(),
                    [](const BrxSpliceModule &module) { return module.rootNode != -1; }))
        result.message += " Splice modules are retained and still initialize in-game; review any lookups targeting deleted objects.";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateProxySourceFromHierarchy(BrxDocument &document,
                                                                               const std::int32_t rootObjectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, rootObjectIndex))
    {
        result.message = "Select a valid hierarchy root first.";
        return result;
    }
    if (document.Objects()[static_cast<std::size_t>(rootObjectIndex)].proxyRecordIndex >= 0)
    {
        result.message = "This hierarchy already belongs to a reusable PROXY object.";
        return result;
    }
    if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(rootObjectIndex)].cid, 2))
    {
        result.message = "A reusable PROXY root must be ALO-derived because ProjectCane applies "
                         "the wrapper transform through the root's ALO virtual methods.";
        return result;
    }

    auto hierarchy = CollectHierarchy(document, rootObjectIndex);
    std::sort(hierarchy.begin(), hierarchy.end());
    if (hierarchy.empty() || hierarchy.front() != rootObjectIndex)
    {
        result.message = "The selected object is not the first record of its hierarchy.";
        return result;
    }
    for (std::size_t member = 0; member < hierarchy.size(); ++member)
        if (hierarchy[member] != rootObjectIndex + static_cast<std::int32_t>(member))
        {
            result.message = "This hierarchy is interleaved with unrelated objects. "
                             "Reorder it into one contiguous hierarchy before creating a reusable object.";
            return result;
        }

    std::int32_t nextSourceListIndex = 0;
    for (const auto &proxy : document.Proxies())
    {
        if (IsMaterializedProxyClone(document, proxy))
            continue;
        for (const auto &root : proxy.roots)
            if (root.kind == BrxProxyRootKind::InlineSource)
                ++nextSourceListIndex;
    }
    if (nextSourceListIndex >= 128)
    {
        result.message = "The Scene World already uses all 128 ProjectCane PROXY source slots.";
        return result;
    }

    const std::int16_t wrapperOid = AllocateOid(document);
    if (wrapperOid < 0)
    {
        result.message = "No free signed 16-bit object ID remains for the PROXY wrapper.";
        return result;
    }

    const auto originalObjects = document.Objects();
    const auto &originalRoot = originalObjects[static_cast<std::size_t>(rootObjectIndex)];
    const std::int32_t originalParent = originalRoot.parentIndex;
    std::vector<std::int32_t> remap(originalObjects.size(), -1);
    for (std::size_t index = 0; index < originalObjects.size(); ++index)
        remap[index] =
            static_cast<std::int32_t>(index >= static_cast<std::size_t>(rootObjectIndex) ? index + 1 : index);

    BrxObjectRecord wrapper;
    wrapper.cid = 83;
    wrapper.oid = wrapperOid;
    wrapper.spliceIndex = -1;
    wrapper.loaderImplemented = true;
    wrapper.loaderName = "Editor-created reusable object";
    wrapper.offset = wrapper.dataOffset = wrapper.endOffset = 0;
    std::memcpy(wrapper.localTransform, kIdentity, sizeof(kIdentity));
    if (originalParent >= 0 && static_cast<std::size_t>(originalParent) < originalObjects.size())
        std::memcpy(wrapper.worldTransform, originalObjects[static_cast<std::size_t>(originalParent)].worldTransform,
                    sizeof(wrapper.worldTransform));
    else
        std::memcpy(wrapper.worldTransform, kIdentity, sizeof(kIdentity));

    auto &objects = document.EditableObjects();
    objects.insert(objects.begin() + rootObjectIndex, wrapper);
    RemapDocumentObjectIndices(document, remap);

    const std::int32_t wrapperIndex = rootObjectIndex;
    const std::int32_t instanceRootIndex = rootObjectIndex + 1;
    auto &insertedWrapper = objects[static_cast<std::size_t>(wrapperIndex)];
    insertedWrapper.parentIndex = originalParent >= 0 ? remap[static_cast<std::size_t>(originalParent)] : -1;
    objects[static_cast<std::size_t>(instanceRootIndex)].parentIndex = wrapperIndex;
    objects[static_cast<std::size_t>(instanceRootIndex)].proxyRuntimeParentIndex = insertedWrapper.parentIndex;

    const std::int32_t proxyRecordIndex = static_cast<std::int32_t>(document.EditableProxies().size());
    BrxProxyRecord proxy;
    proxy.ownerObjectIndex = wrapperIndex;
    std::memcpy(proxy.localTransform, insertedWrapper.localTransform, sizeof(proxy.localTransform));
    BrxProxyRootRecord root;
    root.kind = BrxProxyRootKind::InlineSource;
    root.sourceListIndex = nextSourceListIndex;
    root.sourceProxyRecordIndex = proxyRecordIndex;
    root.sourceProxyRootIndex = 0;
    root.instantiatedRootObjectIndex = instanceRootIndex;
    root.objectBegin = static_cast<std::uint32_t>(instanceRootIndex);
    root.objectCount = static_cast<std::uint32_t>(hierarchy.size());
    root.cloneCapacity = 1;
    proxy.roots.push_back(root);
    document.EditableProxies().push_back(std::move(proxy));
    insertedWrapper.proxyRecordIndex = proxyRecordIndex;

    for (std::size_t member = 0; member < hierarchy.size(); ++member)
    {
        const std::int32_t objectIndex = instanceRootIndex + static_cast<std::int32_t>(member);
        auto &object = objects[static_cast<std::size_t>(objectIndex)];
        if (object.proxyRecordIndex < 0 || member == 0)
        {
            object.proxyRecordIndex = proxyRecordIndex;
            object.proxyRootIndex = 0;
            object.proxySourceObjectIndex = objectIndex;
            object.proxyClone = false;
        }
        result.affectedObjectIndices.push_back(objectIndex);
    }

    document.RebuildRenderPacketsFromGlobs();
    result.succeeded = true;
    result.primaryObjectIndex = wrapperIndex;
    result.message = "Created a reusable object source containing " + std::to_string(hierarchy.size()) + " object(s).";
    return result;
}

EditorOperationResult EditorDocumentOperations::CreateLinkedProxyInstance(BrxDocument &document,
                                                                          const std::int32_t sourceProxyRecordIndex,
                                                                          const std::int32_t sourceRootIndex)
{
    EditorOperationResult result;
    if (sourceProxyRecordIndex < 0 || static_cast<std::size_t>(sourceProxyRecordIndex) >= document.Proxies().size())
    {
        result.message = "The reusable PROXY source no longer exists.";
        return result;
    }
    const auto sourceProxy = document.Proxies()[static_cast<std::size_t>(sourceProxyRecordIndex)];
    if (sourceRootIndex < 0 || static_cast<std::size_t>(sourceRootIndex) >= sourceProxy.roots.size())
    {
        result.message = "The reusable PROXY root no longer exists.";
        return result;
    }
    const auto sourceRoot = sourceProxy.roots[static_cast<std::size_t>(sourceRootIndex)];
    if (sourceRoot.kind != BrxProxyRootKind::InlineSource)
    {
        result.message = "Linked instances must be created from an inline reusable source.";
        return result;
    }
    if (sourceRoot.objectCount == 0 || sourceRoot.objectBegin >= document.Objects().size() ||
        static_cast<std::size_t>(sourceRoot.objectBegin) + sourceRoot.objectCount > document.Objects().size())
    {
        result.message = "The reusable source has an invalid object range.";
        return result;
    }

    std::size_t existingUses = 0;
    for (const auto &proxy : document.Proxies())
    {
        if (IsMaterializedProxyClone(document, proxy))
            continue;
        for (const auto &root : proxy.roots)
            if (root.kind == BrxProxyRootKind::SourceReference &&
                root.sourceProxyRecordIndex == sourceProxyRecordIndex && root.sourceProxyRootIndex == sourceRootIndex)
                ++existingUses;
    }
    if (sourceRoot.cloneCapacity <= 0 || existingUses + 1 >= static_cast<std::size_t>(sourceRoot.cloneCapacity))
    {
        result.message = "This reusable source has no remaining clone capacity. Increase Clone capacity first.";
        return result;
    }

    const auto wrapperOid = AllocateOid(document);
    if (wrapperOid < 0)
    {
        result.message = "No free signed 16-bit object ID remains for the linked PROXY wrapper.";
        return result;
    }
    if (!ValidObjectIndex(document, sourceProxy.ownerObjectIndex))
    {
        result.message = "The reusable source has no valid PROXY wrapper.";
        return result;
    }

    const auto sourceObjects = document.Objects();
    const auto &sourceWrapper = sourceObjects[static_cast<std::size_t>(sourceProxy.ownerObjectIndex)];
    BrxObjectRecord wrapper = sourceWrapper;
    wrapper.oid = wrapperOid;
    wrapper.offset = wrapper.dataOffset = wrapper.endOffset = 0;
    wrapper.proxyRecordIndex = wrapper.proxyRootIndex = wrapper.proxySourceObjectIndex = -1;
    wrapper.proxyRuntimeParentIndex = -1;
    wrapper.proxyClone = false;
    wrapper.editorName.clear();
    wrapper.loaderName = "Editor-created linked PROXY";

    auto &objects = document.EditableObjects();
    const std::int32_t wrapperIndex = static_cast<std::int32_t>(objects.size());
    objects.push_back(std::move(wrapper));
    const std::int32_t clonedObjectBase = static_cast<std::int32_t>(objects.size());
    std::unordered_map<std::int32_t, std::int32_t> objectRemap;
    for (std::uint32_t offset = 0; offset < sourceRoot.objectCount; ++offset)
    {
        const std::int32_t oldIndex = static_cast<std::int32_t>(sourceRoot.objectBegin + offset);
        BrxObjectRecord clone = sourceObjects[static_cast<std::size_t>(oldIndex)];
        clone.offset = clone.dataOffset = clone.endOffset = 0;
        clone.proxyClone = true;
        clone.proxySourceObjectIndex = oldIndex;
        clone.editorName.clear();
        clone.loaderName = "Editor-created linked PROXY member";
        objects.push_back(std::move(clone));
        objectRemap.emplace(oldIndex, clonedObjectBase + static_cast<std::int32_t>(offset));
    }
    for (const auto &[oldIndex, newIndex] : objectRemap)
    {
        auto &clone = objects[static_cast<std::size_t>(newIndex)];
        if (const auto parent = objectRemap.find(clone.parentIndex); parent != objectRemap.end())
            clone.parentIndex = parent->second;
        else
            clone.parentIndex = wrapperIndex;
        if (const auto runtimeParent = objectRemap.find(clone.proxyRuntimeParentIndex);
            runtimeParent != objectRemap.end())
            clone.proxyRuntimeParentIndex = runtimeParent->second;
        if (const auto foster = objectRemap.find(clone.alox.editorFosterObjectIndex); foster != objectRemap.end())
            clone.alox.editorFosterObjectIndex = foster->second;
    }
    objects[static_cast<std::size_t>(clonedObjectBase)].parentIndex = wrapperIndex;
    objects[static_cast<std::size_t>(clonedObjectBase)].proxyRuntimeParentIndex =
        objects[static_cast<std::size_t>(wrapperIndex)].parentIndex;

    const std::size_t proxyCountBeforeClone = document.Proxies().size();
    const std::int32_t newProxyIndex = static_cast<std::int32_t>(proxyCountBeforeClone);
    std::vector<std::int32_t> nestedProxyOrder;
    std::unordered_map<std::int32_t, std::int32_t> nestedProxyRemap;
    for (std::size_t candidate = 0; candidate < proxyCountBeforeClone; ++candidate)
    {
        const auto owner = document.Proxies()[candidate].ownerObjectIndex;
        if (!objectRemap.contains(owner))
            continue;
        nestedProxyRemap.emplace(static_cast<std::int32_t>(candidate),
                                 newProxyIndex + 1 + static_cast<std::int32_t>(nestedProxyOrder.size()));
        nestedProxyOrder.push_back(static_cast<std::int32_t>(candidate));
    }

    BrxProxyRecord linkedProxy;
    linkedProxy.ownerObjectIndex = wrapperIndex;
    std::memcpy(linkedProxy.localTransform, objects[static_cast<std::size_t>(wrapperIndex)].localTransform,
                sizeof(linkedProxy.localTransform));
    BrxProxyRootRecord linkedRoot;
    linkedRoot.kind = BrxProxyRootKind::SourceReference;
    linkedRoot.sourceListIndex = sourceRoot.sourceListIndex;
    linkedRoot.sourceProxyRecordIndex = sourceProxyRecordIndex;
    linkedRoot.sourceProxyRootIndex = sourceRootIndex;
    linkedRoot.instantiatedRootObjectIndex = clonedObjectBase;
    linkedRoot.objectBegin = static_cast<std::uint32_t>(clonedObjectBase);
    linkedRoot.objectCount = sourceRoot.objectCount;
    linkedRoot.cloneCapacity = 0;
    linkedProxy.roots.push_back(linkedRoot);

    // PloGetSwProxySource returns a clone captured by AddSwProxySource before
    // the defining PROXY's instance overrides are applied.  The editor's
    // materialized objects already contain those effective overrides, but the
    // rebuilt runtime instance must receive equivalent override packets or its
    // splice/options/zone/MRD state silently reverts to the raw source.
    // Copy only packets whose resolved target belongs to the selected source
    // root and remap their cached editor identity to the new materialization.
    for (const auto &sourceOverride : sourceProxy.overrides)
    {
        const auto mappedTarget = objectRemap.find(sourceOverride.targetObjectIndex);
        if (mappedTarget == objectRemap.end())
            continue;
        BrxProxyOverrideRecord linkedOverride = sourceOverride;
        linkedOverride.targetObjectIndex = mappedTarget->second;
        linkedProxy.overrides.push_back(std::move(linkedOverride));
    }
    document.EditableProxies().push_back(std::move(linkedProxy));
    objects[static_cast<std::size_t>(wrapperIndex)].proxyRecordIndex = newProxyIndex;

    for (const auto &[oldIndex, newIndex] : objectRemap)
    {
        auto &clone = objects[static_cast<std::size_t>(newIndex)];
        if (const auto nested = nestedProxyRemap.find(clone.proxyRecordIndex); nested != nestedProxyRemap.end())
            clone.proxyRecordIndex = nested->second;
        else
        {
            clone.proxyRecordIndex = newProxyIndex;
            clone.proxyRootIndex = 0;
        }
        result.affectedObjectIndices.push_back(newIndex);
    }

    for (const auto oldNestedIndex : nestedProxyOrder)
    {
        BrxProxyRecord nested = document.Proxies()[static_cast<std::size_t>(oldNestedIndex)];
        if (const auto owner = objectRemap.find(nested.ownerObjectIndex); owner != objectRemap.end())
            nested.ownerObjectIndex = owner->second;
        for (auto &root : nested.roots)
        {
            if (const auto mapped = objectRemap.find(root.instantiatedRootObjectIndex); mapped != objectRemap.end())
                root.instantiatedRootObjectIndex = mapped->second;
            if (const auto mapped = objectRemap.find(static_cast<std::int32_t>(root.objectBegin));
                mapped != objectRemap.end())
                root.objectBegin = static_cast<std::uint32_t>(mapped->second);
            if (const auto mapped = nestedProxyRemap.find(root.sourceProxyRecordIndex);
                mapped != nestedProxyRemap.end())
                root.sourceProxyRecordIndex = mapped->second;
        }
        for (auto &child : nested.attachedChildObjectIndices)
        {
            const auto mapped = objectRemap.find(child);
            child = mapped != objectRemap.end() ? mapped->second : -1;
        }
        nested.attachedChildObjectIndices.erase(
            std::remove(nested.attachedChildObjectIndices.begin(), nested.attachedChildObjectIndices.end(), -1),
            nested.attachedChildObjectIndices.end());
        for (auto &overrideRecord : nested.overrides)
        {
            const auto mapped = objectRemap.find(overrideRecord.targetObjectIndex);
            overrideRecord.targetObjectIndex = mapped != objectRemap.end() ? mapped->second : -1;
        }
        document.EditableProxies().push_back(std::move(nested));
    }

    DuplicateOwnedSceneData(document, objectRemap, {}, true);
    document.RebuildRenderPacketsFromGlobs();
    result.succeeded = true;
    result.primaryObjectIndex = wrapperIndex;
    result.affectedObjectIndices.insert(result.affectedObjectIndices.begin(), wrapperIndex);
    result.message =
        "Created a linked PROXY instance with " + std::to_string(sourceRoot.objectCount) + " inherited object(s).";
    return result;
}

EditorOperationResult EditorDocumentOperations::MakeProxyRootUnique(BrxDocument &document,
                                                                    const std::int32_t objectIndex)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, objectIndex))
    {
        result.message = "No valid PROXY instance object is selected.";
        return result;
    }

    const auto identity = document.Objects()[static_cast<std::size_t>(objectIndex)];
    if (identity.proxyRecordIndex < 0 || identity.proxyRootIndex < 0 ||
        static_cast<std::size_t>(identity.proxyRecordIndex) >= document.EditableProxies().size())
    {
        result.message = "The selected object does not belong to a PROXY instance.";
        return result;
    }

    auto &proxy = document.EditableProxies()[static_cast<std::size_t>(identity.proxyRecordIndex)];
    if (static_cast<std::size_t>(identity.proxyRootIndex) >= proxy.roots.size())
    {
        result.message = "The selected object has an invalid PROXY root identity.";
        return result;
    }
    auto &root = proxy.roots[static_cast<std::size_t>(identity.proxyRootIndex)];
    if (root.kind == BrxProxyRootKind::InlineSource)
    {
        result.message = "This PROXY root already owns a unique inline source.";
        return result;
    }
    if (root.objectCount == 0 || root.objectBegin >= document.Objects().size() ||
        static_cast<std::size_t>(root.objectBegin) + root.objectCount > document.Objects().size())
    {
        result.message = "The PROXY root has an invalid object range.";
        return result;
    }
    std::size_t sourceCount = 0;
    for (const auto &candidateProxy : document.Proxies())
    {
        if (ValidObjectIndex(document, candidateProxy.ownerObjectIndex) &&
            document.Objects()[static_cast<std::size_t>(candidateProxy.ownerObjectIndex)].proxyClone)
            continue;
        sourceCount += static_cast<std::size_t>(std::count_if(
            candidateProxy.roots.begin(), candidateProxy.roots.end(), [](const BrxProxyRootRecord &candidateRoot)
            { return candidateRoot.kind == BrxProxyRootKind::InlineSource; }));
    }
    if (sourceCount >= 128)
    {
        result.message = "The Scene World already uses all 128 ProjectCane PROXY source slots.";
        return result;
    }

    // A retail linked clone may render through GLOBSETs owned by its source
    // records instead of carrying private scene data.  Once this root becomes
    // inline, those source identities are intentionally discarded, so first
    // materialize any missing owned model/scene records onto the clone objects.
    std::unordered_map<std::int32_t, std::int32_t> sourceToUniqueObject;
    const auto uniqueBegin = static_cast<std::int32_t>(root.objectBegin);
    const auto uniqueEnd = uniqueBegin + static_cast<std::int32_t>(root.objectCount);
    for (std::int32_t member = uniqueBegin; member < uniqueEnd; ++member)
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(member)];
        const auto sourceObject = object.proxySourceObjectIndex;
        if (sourceObject < 0 || sourceObject == member || !ValidObjectIndex(document, sourceObject))
            continue;
        const bool alreadyOwnsModel =
            std::any_of(document.Globsets().begin(), document.Globsets().end(),
                        [&](const BrxGlobsetRecord &globset) { return globset.ownerObjectIndex == member; });
        const bool sourceOwnsModel =
            std::any_of(document.Globsets().begin(), document.Globsets().end(),
                        [&](const BrxGlobsetRecord &globset) { return globset.ownerObjectIndex == sourceObject; });
        if (!alreadyOwnsModel && sourceOwnsModel)
            sourceToUniqueObject.emplace(sourceObject, member);
    }
    if (!sourceToUniqueObject.empty())
        DuplicateOwnedSceneData(document, sourceToUniqueObject, {}, true);

    root.kind = BrxProxyRootKind::InlineSource;
    root.sourceProxyRecordIndex = identity.proxyRecordIndex;
    root.sourceProxyRootIndex = identity.proxyRootIndex;
    root.cloneCapacity = 1;

    RebuildProxySourceListIndices(document);

    const auto begin = static_cast<std::int32_t>(root.objectBegin);
    const auto end = begin + static_cast<std::int32_t>(root.objectCount);
    for (std::int32_t member = begin; member < end; ++member)
    {
        auto &object = document.EditableObjects()[static_cast<std::size_t>(member)];
        if (object.proxyRecordIndex != identity.proxyRecordIndex || object.proxyRootIndex != identity.proxyRootIndex)
            continue; // Preserve the nearest identity of nested PROXY roots.
        object.proxyClone = false;
        object.proxySourceObjectIndex = member;
        result.affectedObjectIndices.push_back(member);
    }

    // The materialized records already contain the effective values produced by
    // these overrides.  They now belong to the new inline source, so retaining
    // the commands would apply the same edits twice and misrepresent inheritance.
    proxy.overrides.erase(
        std::remove_if(proxy.overrides.begin(), proxy.overrides.end(),
                       [&](const BrxProxyOverrideRecord &candidate)
                       {
                           if (candidate.targetObjectIndex < begin || candidate.targetObjectIndex >= end ||
                               !ValidObjectIndex(document, candidate.targetObjectIndex))
                               return false;
                           const auto &target =
                               document.Objects()[static_cast<std::size_t>(candidate.targetObjectIndex)];
                           return target.proxyRecordIndex == identity.proxyRecordIndex &&
                                  target.proxyRootIndex == identity.proxyRootIndex;
                       }),
        proxy.overrides.end());

    result.succeeded = true;
    result.primaryObjectIndex = root.instantiatedRootObjectIndex;
    result.message = "Made the selected PROXY root unique with its own inline source hierarchy.";
    return result;
}

EditorOperationResult EditorDocumentOperations::ReparentObject(BrxDocument &document, const std::int32_t objectIndex,
                                                               const std::int32_t newParentIndex,
                                                               const bool preserveWorldTransform)
{
    EditorOperationResult result;
    if (!ValidObjectIndex(document, objectIndex) || newParentIndex < -1 ||
        (newParentIndex >= 0 && !ValidObjectIndex(document, newParentIndex)))
    {
        result.message = "The object or new parent does not exist.";
        return result;
    }
    if (objectIndex == newParentIndex)
    {
        result.message = "An object cannot parent itself.";
        return result;
    }
    const auto &proxyMember = document.Objects()[static_cast<std::size_t>(objectIndex)];
    if (proxyMember.parentIndex < -1 ||
        (proxyMember.parentIndex >= 0 && !ValidObjectIndex(document, proxyMember.parentIndex)))
    {
        result.message = "The object's current parent does not exist. Repair its hierarchy before moving it.";
        return result;
    }
    if (newParentIndex >= 0)
    {
        const auto &parent = document.Objects()[static_cast<std::size_t>(newParentIndex)];
        if (((parent.cid == 116 || parent.cid == 125) && proxyMember.cid != 131) ||
            (!CidDerivesFrom(parent.cid, 2) && parent.cid != 120 &&
             !((parent.cid == 116 || parent.cid == 125 || parent.cid == 136) && proxyMember.cid == 131)))
        {
            result.message = "The destination cannot serialize this child. Use an ALO-derived parent, "
                             "EXPLG, or the dedicated embedded-animation tools.";
            return result;
        }
        // AuthoredChildren excludes members of another serialized PROXY root;
        // PROXY wrappers write attached children from their explicit list.
        // Changing parentIndex alone cannot establish either relationship.
        const bool nestedProxyWrapper = proxyMember.cid == 83 && proxyMember.proxyRootIndex < 0;
        if (parent.cid == 83 || parent.proxyClone ||
            (parent.proxyRootIndex >= 0 && parent.proxyRootIndex != proxyMember.proxyRootIndex && !nestedProxyWrapper))
        {
            result.message = "This move crosses a PROXY source or attachment boundary. "
                             "Move the wrapper, or edit within the object's existing unique source hierarchy.";
            return result;
        }
        if (nestedProxyWrapper && parent.proxyRootIndex >= 0 &&
            parent.proxyRecordIndex >= 0 &&
            static_cast<std::size_t>(parent.proxyRecordIndex) < document.Proxies().size())
        {
            const auto &parentProxy = document.Proxies()[static_cast<std::size_t>(parent.proxyRecordIndex)];
            const auto rootOffset = static_cast<std::size_t>(parent.proxyRootIndex);
            if (rootOffset >= parentProxy.roots.size())
            {
                result.message = "The destination has an invalid PROXY root identity.";
                return result;
            }
            const auto &parentRoot = parentProxy.roots[rootOffset];
            if (objectIndex < static_cast<std::int32_t>(parentRoot.objectBegin) ||
                static_cast<std::size_t>(objectIndex) >=
                    static_cast<std::size_t>(parentRoot.objectBegin) + parentRoot.objectCount)
            {
                result.message = "Moving this wrapper into another source would invalidate its contiguous PROXY span.";
                return result;
            }
        }
    }
    if (proxyMember.cid == 83)
        for (const auto &proxy : document.Proxies())
            for (const auto &root : proxy.roots)
                if (root.kind == BrxProxyRootKind::InlineSource &&
                    static_cast<std::size_t>(objectIndex) >= root.objectBegin &&
                    static_cast<std::size_t>(objectIndex) < static_cast<std::size_t>(root.objectBegin) + root.objectCount &&
                    (newParentIndex < static_cast<std::int32_t>(root.objectBegin) ||
                     static_cast<std::size_t>(newParentIndex) >= static_cast<std::size_t>(root.objectBegin) + root.objectCount))
                {
                    result.message = "A nested PROXY wrapper must remain within its containing source span.";
                    return result;
                }
    if (proxyMember.parentIndex != newParentIndex &&
        ((proxyMember.parentIndex >= 0 &&
          document.Objects()[static_cast<std::size_t>(proxyMember.parentIndex)].cid == 120) ||
         (newParentIndex >= 0 && document.Objects()[static_cast<std::size_t>(newParentIndex)].cid == 120)))
    {
        // LoadExplgFromBrx instantiates deferred children under the EXPLG's
        // ALO parent, not under the EXPLG frame shown in the structural tree.
        result.message = "EXPLG children use a different runtime parent. Generic reparenting would change their placement.";
        return result;
    }
    if (proxyMember.parentIndex != newParentIndex &&
        ((proxyMember.parentIndex >= 0 &&
          document.Objects()[static_cast<std::size_t>(proxyMember.parentIndex)].cid == 136) ||
         (newParentIndex >= 0 && document.Objects()[static_cast<std::size_t>(newParentIndex)].cid == 136)))
    {
        result.message = "Move an SM-owned ASEG through its transition editor so ownership remains valid.";
        return result;
    }
    if (proxyMember.proxyRecordIndex >= 0 &&
        static_cast<std::size_t>(proxyMember.proxyRecordIndex) < document.Proxies().size())
    {
        const auto &proxy = document.Proxies()[static_cast<std::size_t>(proxyMember.proxyRecordIndex)];
        if (proxy.ownerObjectIndex != objectIndex)
        {
            if (proxyMember.proxyRootIndex < 0 ||
                static_cast<std::size_t>(proxyMember.proxyRootIndex) >= proxy.roots.size())
            {
                result.message = "This object has an invalid PROXY root identity.";
                return result;
            }
            const auto &root = proxy.roots[static_cast<std::size_t>(proxyMember.proxyRootIndex)];
            if (root.kind == BrxProxyRootKind::SourceReference)
            {
                result.message = "Linked PROXY members inherit their hierarchy from the reusable source. "
                                 "Make this root unique before reparenting its members.";
                return result;
            }
            if (objectIndex == root.instantiatedRootObjectIndex)
            {
                result.message = "A PROXY root must remain serialized beneath its wrapper. "
                                 "Move or reparent the PROXY wrapper instead.";
                return result;
            }
            const auto rootEnd = static_cast<std::size_t>(root.objectBegin) + root.objectCount;
            if (newParentIndex < static_cast<std::int32_t>(root.objectBegin) ||
                newParentIndex >= static_cast<std::int32_t>(rootEnd))
            {
                result.message = "A reusable PROXY source member must remain inside its source hierarchy.";
                return result;
            }
        }
    }
    const bool isShape = CidDerivesFrom(document.Objects()[static_cast<std::size_t>(objectIndex)].cid, 110);
    const bool isPoint = CidDerivesFrom(document.Objects()[static_cast<std::size_t>(objectIndex)].cid, 126);
    if ((isShape || isPoint) && newParentIndex >= 0 &&
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(newParentIndex)].cid, 2))
    {
        result.message =
            isPoint ? "A PNT parent must be an ALO-derived object." : "A SHAPE parent must be an ALO-derived object.";
        return result;
    }
    std::unordered_set<std::int32_t> visitedAncestors;
    for (auto ancestor = newParentIndex; ancestor >= 0;)
    {
        if (!visitedAncestors.insert(ancestor).second)
        {
            result.message = "The destination parent hierarchy already contains a cycle.";
            return result;
        }
        if (ancestor == objectIndex)
        {
            result.message = "Reparenting would create a hierarchy cycle.";
            return result;
        }
        const auto next = document.Objects()[static_cast<std::size_t>(ancestor)].parentIndex;
        if (next < -1 || (next >= 0 && !ValidObjectIndex(document, next)))
        {
            result.message = "The destination parent hierarchy contains an invalid object index.";
            return result;
        }
        ancestor = next;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(objectIndex)];
    const glm::mat4 oldWorld = glm::make_mat4(object.worldTransform);
    const std::int32_t oldParentIndex = object.parentIndex;
    const glm::mat4 oldParentWorld =
        oldParentIndex >= 0
            ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(oldParentIndex)].worldTransform)
            : glm::mat4(1.0f);
    const glm::mat4 newParentWorld =
        newParentIndex >= 0
            ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(newParentIndex)].worldTransform)
            : glm::mat4(1.0f);
    const auto finiteMatrix = [](const glm::mat4 &matrix)
    {
        const float *values = glm::value_ptr(matrix);
        return std::all_of(values, values + 16, [](const float value) { return std::isfinite(value); });
    };
    const float parentDeterminant = glm::determinant(newParentWorld);
    if (!finiteMatrix(oldWorld) || !finiteMatrix(oldParentWorld) || !finiteMatrix(newParentWorld) ||
        (preserveWorldTransform && (!std::isfinite(parentDeterminant) ||
                                    std::abs(parentDeterminant) <= 0.000001f ||
                                    !finiteMatrix(glm::inverse(newParentWorld) * oldWorld))))
    {
        result.message = "Cannot move through a non-finite or singular parent transform.";
        return result;
    }
    if ((isShape || isPoint) && preserveWorldTransform)
    {
        const float oldParentDeterminant = glm::determinant(oldParentWorld);
        const float newParentDeterminant = glm::determinant(newParentWorld);
        if (!std::isfinite(oldParentDeterminant) || !std::isfinite(newParentDeterminant) ||
            std::abs(oldParentDeterminant) <= 0.000001f || std::abs(newParentDeterminant) <= 0.000001f)
        {
            result.message =
                isPoint
                    ? "Cannot reparent PNT through a singular parent transform; its position conversion would be undefined."
                    : "Cannot reparent SHAPE through a singular parent transform; its curve conversion would be undefined.";
            return result;
        }
    }
    if (isShape && preserveWorldTransform)
    {
        // Match SetShapeParent: points are stored in the parent's local space,
        // while cubic derivatives are vectors and must not receive translation.
        const glm::mat4 conversion = glm::inverse(newParentWorld) * oldParentWorld;
        const glm::mat3 vectorConversion(conversion);
        for (auto &shape : document.EditableShapePaths())
        {
            if (shape.ownerObjectIndex != objectIndex)
                continue;
            for (auto &point : shape.points)
            {
                const glm::vec3 converted = glm::vec3(conversion * glm::vec4(point[0], point[1], point[2], 1.0f));
                point = {converted.x, converted.y, converted.z};
            }
            const auto convertVectors = [&](std::vector<std::array<float, 3>> &vectors)
            {
                for (auto &value : vectors)
                {
                    const glm::vec3 converted = vectorConversion * glm::vec3(value[0], value[1], value[2]);
                    value = {converted.x, converted.y, converted.z};
                }
            };
            convertVectors(shape.tangentsIn);
            convertVectors(shape.tangentsOut);
        }
    }
    // An attached child is emitted from its original PROXY's explicit list,
    // regardless of parentIndex. Detaching it must also release that entry.
    if (object.parentIndex != newParentIndex)
        for (auto &proxy : document.EditableProxies())
        {
            auto &attached = proxy.attachedChildObjectIndices;
            attached.erase(std::remove(attached.begin(), attached.end(), objectIndex), attached.end());
        }
    object.parentIndex = newParentIndex;
    if (preserveWorldTransform)
    {
        glm::mat4 local = glm::inverse(newParentWorld) * oldWorld;
        if (isPoint)
        {
            // Match SetPntParent -> ConvertAloPos exactly. Runtime ALO bases are
            // expected to be orthonormal, so conversion into the new parent is
            // transpose(matWorld) * (worldPosition - parentPosition), not a
            // general inverse. PNT has no serializable orientation or scale.
            const glm::vec3 worldPosition(oldWorld[3]);
            const glm::vec3 parentPosition(newParentWorld[3]);
            const glm::vec3 localPosition =
                glm::transpose(glm::mat3(newParentWorld)) * (worldPosition - parentPosition);
            local = glm::translate(glm::mat4(1.0f), localPosition);
        }
        std::copy(glm::value_ptr(local), glm::value_ptr(local) + 16, object.localTransform);
        if (isPoint)
        {
            const glm::mat4 pointWorld = newParentWorld * local;
            std::copy(glm::value_ptr(pointWorld), glm::value_ptr(pointWorld) + 16, object.worldTransform);
        }
    }
    else
    {
        const glm::mat4 world = newParentWorld * glm::make_mat4(object.localTransform);
        std::copy(glm::value_ptr(world), glm::value_ptr(world) + 16, object.worldTransform);
    }
    SynchronizeWarpAnimationCounts(document);
    SynchronizeExitAnimationCounts(document);
    result.succeeded = true;
    result.primaryObjectIndex = objectIndex;
    result.affectedObjectIndices.push_back(objectIndex);
    result.message = isShape && preserveWorldTransform
                         ? "Reparented SHAPE and converted its curve to preserve the world-space path."
                     : isPoint && preserveWorldTransform
                         ? "Reparented PNT and converted its local position to preserve the world-space point."
                         : "Reparented object without creating a cycle.";
    return result;
}
