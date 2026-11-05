#pragma once

#include "BrxDocument.h"
#include "EditorOptions.h"

// Authoring-side eligibility for PostSmartguardLoadFlashlight's class lookup
// (513 = all results, mode 1, active objects). A PXR root is not returned by
// this class lookup, and mode 1 cannot cross an unrelated PXR boundary.
inline bool IsGuardProxyRoot(const BrxDocument &document, const std::int32_t index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= document.Objects().size())
        return false;
    const auto &object = document.Objects()[static_cast<std::size_t>(index)];
    if (object.proxyRecordIndex < 0 || object.proxyRootIndex < 0 ||
        static_cast<std::size_t>(object.proxyRecordIndex) >= document.Proxies().size())
        return false;
    const auto &roots = document.Proxies()[static_cast<std::size_t>(object.proxyRecordIndex)].roots;
    return static_cast<std::size_t>(object.proxyRootIndex) < roots.size() &&
           roots[static_cast<std::size_t>(object.proxyRootIndex)].instantiatedRootObjectIndex == index;
}

inline bool IsSmartguardFlashlightChild(const BrxDocument &document, const std::int32_t candidateIndex,
                                        const std::int32_t guardIndex)
{
    if (candidateIndex < 0 || guardIndex < 0 || candidateIndex == guardIndex ||
        static_cast<std::size_t>(candidateIndex) >= document.Objects().size() ||
        static_cast<std::size_t>(guardIndex) >= document.Objects().size() ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(candidateIndex)].cid, 96) ||
        !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(guardIndex)].cid, 9) ||
        IsGuardProxyRoot(document, candidateIndex))
        return false;
    const auto parentOf = [&](const std::int32_t index)
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(index)];
        return object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
    };
    std::int32_t parent = parentOf(candidateIndex);
    for (std::size_t depth = 0; parent >= 0 && static_cast<std::size_t>(parent) < document.Objects().size() &&
                                depth < document.Objects().size();
         ++depth)
    {
        if (parent == guardIndex)
            return true;
        if (IsGuardProxyRoot(document, parent))
            return false;
        parent = parentOf(parent);
    }
    return false;
}

// Returns an eligible matching child for diagnostics, not the runtime's final
// pointer when multiple FLASH children share a name (that depends on CID-list order).
inline std::int32_t FindSmartguardFlashlightChild(const BrxDocument &document, const std::int32_t guardIndex,
                                                  const std::int16_t oid)
{
    if (oid == -1)
        return -1;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
        if (document.Objects()[index].oid == oid &&
            IsSmartguardFlashlightChild(document, static_cast<std::int32_t>(index), guardIndex))
            return static_cast<std::int32_t>(index);
    return -1;
}
