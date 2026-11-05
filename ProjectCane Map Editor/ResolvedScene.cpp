#include "ResolvedScene.h"
#include "GuardAuthoring.h"

#include "BrxDocument.h"
#include "EditorOptions.h"
#include "ObjectTypeRegistry.h"

#include <algorithm>

void ResolvedScene::Rebuild(const BrxDocument &document)
{
    const PropertySchema propertySchema;
    roots_.clear();
    objectsByOid_.clear();
    references_.clear();
    children_.assign(document.Objects().size(), {});
    spliceIndices_.resize(document.Objects().size(), -1);
    proxyRecordIndices_.resize(document.Objects().size(), -1);
    proxyRootIndices_.resize(document.Objects().size(), -1);
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        spliceIndices_[index] = object.spliceIndex;
        proxyRecordIndices_[index] = object.proxyRecordIndex;
        proxyRootIndices_[index] = object.proxyRootIndex;
        if (object.oid >= 0)
            objectsByOid_.emplace(object.oid, static_cast<std::int32_t>(index));
        if (object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < children_.size() &&
            object.parentIndex != static_cast<std::int32_t>(index))
            children_[static_cast<std::size_t>(object.parentIndex)].push_back(static_cast<std::int32_t>(index));
        else
            roots_.push_back(static_cast<std::int32_t>(index));
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        for (const auto &property : object.properties)
        {
            const auto schema = propertySchema.Describe(object.cid, property.eopid);
            if (!schema.objectReference || IsObjectNameFilterOption(property.eopid))
                continue;
            const auto targetOid = static_cast<std::int16_t>(property.intValue);
            // OID_Nil is the serialized representation of an intentionally
            // unbound optional reference.  ProjectCane skips the lookup; it
            // is not a missing-object error.
            if (targetOid == -1)
                continue;

            // VOLBTN's push-object list is resolved by PostVolbtnLoad through
            // PloFindSwNearest. OID__CAMERA (4) names the engine-owned CAMERA
            // singleton, not a BRX-authored world record, so retail levels may
            // legally use it even though it is absent from document.Objects().
            // Keep every other EOPID 275 target subject to normal contextual
            // existence and CID validation.
            if (property.eopid == 275 && targetOid == 4)
                continue;
            if (property.eopid == 701 && CidDerivesFrom(object.cid, 9))
            {
                const auto childIndex =
                    FindSmartguardFlashlightChild(document, static_cast<std::int32_t>(index), targetOid);
                references_.push_back(
                    {static_cast<std::int32_t>(index), property.eopid, targetOid, childIndex, childIndex >= 0});
                continue;
            }
            auto targetIndex = FindObjectFrom(targetOid, static_cast<std::int32_t>(index));
            bool compatible =
                targetIndex >= 0 &&
                propertySchema.ReferenceAccepts(schema, document.Objects()[static_cast<std::size_t>(targetIndex)].cid);

            // PloFindSwNearest/PloFindSwObject resolve duplicate OIDs through
            // hierarchy and PXR context.  A PROXY wrapper and its supplied
            // root commonly share an OID, so choosing the highest-scoring OID
            // first and checking its CID afterwards can select the wrapper and
            // falsely reject the compatible instantiated descendant.  Rank
            // compatible candidates in the same context instead.
            if (!compatible)
            {
                const auto range = objectsByOid_.equal_range(targetOid);
                const auto source = static_cast<std::size_t>(index);
                const std::int32_t sourceProxy = proxyRecordIndices_[source];
                const std::int32_t sourceRoot = proxyRootIndices_[source];
                const std::int16_t sourceSplice = spliceIndices_[source];
                std::int32_t compatibleIndex = -1;
                int compatibleScore = -1;
                const auto considerCompatible = [&](const std::int32_t candidateIndex, const int score)
                {
                    if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= document.Objects().size() ||
                        !propertySchema.ReferenceAccepts(
                            schema, document.Objects()[static_cast<std::size_t>(candidateIndex)].cid) ||
                        score <= compatibleScore)
                        return;
                    compatibleScore = score;
                    compatibleIndex = candidateIndex;
                };
                for (auto current = range.first; current != range.second; ++current)
                {
                    const std::int32_t candidateIndex = current->second;
                    if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= document.Objects().size())
                        continue;
                    const auto candidate = static_cast<std::size_t>(candidateIndex);
                    int score = spliceIndices_[candidate] == sourceSplice ? 1 : 0;
                    if (sourceProxy >= 0 && proxyRecordIndices_[candidate] == sourceProxy)
                    {
                        score = 2;
                        if (sourceRoot >= 0 && proxyRootIndices_[candidate] == sourceRoot)
                            score = 3;
                    }
                    else if (sourceProxy < 0 && proxyRecordIndices_[candidate] < 0)
                    {
                        score = spliceIndices_[candidate] == sourceSplice ? 3 : 2;
                    }
                    considerCompatible(candidateIndex, score);

                    // CploFindSwObjects hashes the PROXY wrapper by its OID,
                    // then supplies every PXR root to MatchSwObject.  The root
                    // therefore does not need to carry the wrapper's OID.
                    // Expand that indirection before declaring the wrapper's
                    // target type incompatible.
                    if (document.Objects()[candidate].cid == 83)
                    {
                        for (const auto &proxy : document.Proxies())
                        {
                            if (proxy.ownerObjectIndex != candidateIndex)
                                continue;
                            for (const auto &root : proxy.roots)
                                considerCompatible(root.instantiatedRootObjectIndex, score);
                        }
                    }
                }
                if (compatibleIndex >= 0)
                {
                    targetIndex = compatibleIndex;
                    compatible = true;
                }
            }
            references_.push_back(
                {static_cast<std::int32_t>(index), property.eopid, targetOid, targetIndex, compatible});
        }
    }
    ++revision_;
}

void ResolvedScene::Clear()
{
    roots_.clear();
    children_.clear();
    spliceIndices_.clear();
    proxyRecordIndices_.clear();
    proxyRootIndices_.clear();
    objectsByOid_.clear();
    references_.clear();
    ++revision_;
}

const std::vector<std::int32_t> &ResolvedScene::Children(const std::int32_t objectIndex) const
{
    static const std::vector<std::int32_t> empty;
    return objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < children_.size()
               ? children_[static_cast<std::size_t>(objectIndex)]
               : empty;
}

std::int32_t ResolvedScene::FindObject(const std::int16_t oid, const std::int16_t spliceIndex) const
{
    const auto range = objectsByOid_.equal_range(oid);
    if (range.first == range.second)
        return -1;
    if (spliceIndex < 0)
        return range.first->second;
    // Prefer same-splice lookup. The caller can still fall back to the first
    // global match when the game permits cross-splice references.
    for (auto current = range.first; current != range.second; ++current)
        if (current->second >= 0 && static_cast<std::size_t>(current->second) < spliceIndices_.size() &&
            spliceIndices_[static_cast<std::size_t>(current->second)] == spliceIndex)
            return current->second;
    return range.first->second;
}

std::int32_t ResolvedScene::FindObjectFrom(const std::int16_t oid, const std::int32_t sourceObjectIndex) const
{
    const auto range = objectsByOid_.equal_range(oid);
    if (range.first == range.second)
        return -1;
    if (sourceObjectIndex < 0 || static_cast<std::size_t>(sourceObjectIndex) >= spliceIndices_.size())
        return range.first->second;

    const auto source = static_cast<std::size_t>(sourceObjectIndex);
    const std::int32_t sourceProxy = proxyRecordIndices_[source];
    const std::int32_t sourceRoot = proxyRootIndices_[source];
    const std::int16_t sourceSplice = spliceIndices_[source];
    std::int32_t bestIndex = -1;
    int bestScore = -1;
    for (auto current = range.first; current != range.second; ++current)
    {
        const std::int32_t candidateIndex = current->second;
        if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= spliceIndices_.size())
            continue;
        const auto candidate = static_cast<std::size_t>(candidateIndex);
        int score = spliceIndices_[candidate] == sourceSplice ? 1 : 0;
        if (sourceProxy >= 0 && proxyRecordIndices_[candidate] == sourceProxy)
        {
            score = 2;
            if (sourceRoot >= 0 && proxyRootIndices_[candidate] == sourceRoot)
                score = 3;
        }
        else if (sourceProxy < 0 && proxyRecordIndices_[candidate] < 0)
        {
            score = spliceIndices_[candidate] == sourceSplice ? 3 : 2;
        }
        if (score > bestScore)
        {
            bestScore = score;
            bestIndex = candidateIndex;
        }
    }
    return bestIndex;
}
