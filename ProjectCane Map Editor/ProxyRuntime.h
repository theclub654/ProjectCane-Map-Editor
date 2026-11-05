#pragma once

#include "BrxDocument.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Editor-side equivalents of the PROXY-specific part of
// PloFindSwObject(..., 0x101, ...).  An instance override may address a root
// itself or an ordinary descendant of that root, but the search must not cross
// a nested PXR boundary.  A flat object-range search is observably wrong when
// duplicate OIDs exist inside nested reusable objects.
namespace ProxyRuntime
{
inline bool IsInstantiatedProxyRoot(const BrxDocument &document, const std::int32_t objectIndex,
                                    const std::int32_t ignoredProxyIndex, const std::int32_t ignoredRootIndex)
{
    for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
    {
        const auto &proxy = document.Proxies()[proxyIndex];
        for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
        {
            if (static_cast<std::int32_t>(proxyIndex) == ignoredProxyIndex &&
                static_cast<std::int32_t>(rootIndex) == ignoredRootIndex)
                continue;
            if (proxy.roots[rootIndex].instantiatedRootObjectIndex == objectIndex)
                return true;
        }
    }
    return false;
}

inline bool IsOverrideCandidate(const BrxDocument &document, const std::int32_t proxyIndex,
                                const std::int32_t rootIndex, const std::int32_t candidateIndex,
                                const bool proxyMatch = false)
{
    if (proxyIndex < 0 || static_cast<std::size_t>(proxyIndex) >= document.Proxies().size())
        return false;
    const auto &proxy = document.Proxies()[static_cast<std::size_t>(proxyIndex)];
    if (rootIndex < 0 || static_cast<std::size_t>(rootIndex) >= proxy.roots.size())
        return false;
    const auto &root = proxy.roots[static_cast<std::size_t>(rootIndex)];
    if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= document.Objects().size())
        return false;

    if (candidateIndex == root.instantiatedRootObjectIndex)
        return true; // LoadProxyFromBrx checks each supplied root before PloFindSwObject.

    // An ordinary hash match that is itself a PXR root is rejected.  It is
    // eligible only when CploFindSwObjects reached it through its matching
    // PROXY wrapper and supplied fProxyMatch=true.
    if (!proxyMatch && IsInstantiatedProxyRoot(document, candidateIndex, proxyIndex, rootIndex))
        return false;

    // A nested PXR root is not eligible through its ordinary hash entry, but
    // CploFindSwObjects also visits the nested PROXY wrapper and supplies that
    // same root to MatchSwObject with fProxyMatch=true.  In mode 1 the root
    // itself may therefore match an outer source context.  Descendants below
    // it must still be rejected when the parent walk reaches the nested PXR
    // boundary (handled by the loop below).

    // CploFindSwObjects handles a PROXY hash entry through its PXR roots; it
    // does not return the nested PROXY wrapper as an ordinary descendant.
    if (document.Objects()[static_cast<std::size_t>(candidateIndex)].cid == 83)
        return false;

    std::int32_t current = candidateIndex;
    for (std::size_t guard = 0; guard <= document.Objects().size(); ++guard)
    {
        if (current == root.instantiatedRootObjectIndex)
            return true;
        if (current < 0 || static_cast<std::size_t>(current) >= document.Objects().size())
            return false;
        if (current != candidateIndex && IsInstantiatedProxyRoot(document, current, proxyIndex, rootIndex))
            return false;
        const auto parent = document.Objects()[static_cast<std::size_t>(current)].parentIndex;
        if (parent == current)
            return false;
        current = parent;
    }
    return false;
}

inline std::int32_t ResolveOverrideTarget(const BrxDocument &document, const std::int32_t proxyIndex,
                                          const std::int16_t oid)
{
    if (proxyIndex < 0 || static_cast<std::size_t>(proxyIndex) >= document.Proxies().size())
        return -1;
    const auto &proxy = document.Proxies()[static_cast<std::size_t>(proxyIndex)];
    for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
    {
        // LoadProxyFromBrx compares this supplied root directly before calling
        // PloFindSwObject.  Do not let document-table ordering choose a same-OID
        // descendant first after editor insertion/deletion has changed indices.
        const auto directRoot = proxy.roots[rootIndex].instantiatedRootObjectIndex;
        if (directRoot >= 0 && static_cast<std::size_t>(directRoot) < document.Objects().size() &&
            document.Objects()[static_cast<std::size_t>(directRoot)].oid == oid)
            return directRoot;

        // objectBegin/objectCount describe the original decoded stream and are
        // intentionally retained for source/clone correspondence. They cease
        // to be a membership test after editor insertion, deletion, or
        // reparenting. PXR lookup follows the live hierarchy, so inspect the
        // complete object table and let IsOverrideCandidate enforce this root
        // and nested-PROXY boundaries through parent links.
        for (std::size_t candidate = 0; candidate < document.Objects().size(); ++candidate)
        {
            if (document.Objects()[candidate].oid != oid)
                continue;

            // CploFindSwObjects does not return a matching PROXY wrapper from
            // the ordinary hash entry.  It visits that PROXY's PXR list and
            // passes each instantiated root to MatchSwObject instead.  The
            // serialized lookup OID therefore remains the wrapper's OID while
            // the object receiving the override can have a different OID.
            // Eye of the Storm relies on this for the nested emitter PROXYs
            // 1589/1584/1586/1581, all of which resolve to an OID 1582 root.
            if (document.Objects()[candidate].cid == 83)
            {
                for (std::size_t nestedProxyIndex = 0; nestedProxyIndex < document.Proxies().size(); ++nestedProxyIndex)
                {
                    const auto &nestedProxy = document.Proxies()[nestedProxyIndex];
                    if (nestedProxy.ownerObjectIndex != static_cast<std::int32_t>(candidate))
                        continue;
                    for (const auto &nestedRoot : nestedProxy.roots)
                    {
                        const auto nestedRootObject = nestedRoot.instantiatedRootObjectIndex;
                        if (IsOverrideCandidate(document, proxyIndex, static_cast<std::int32_t>(rootIndex),
                                                nestedRootObject, true))
                            return nestedRootObject;
                    }
                }
                continue;
            }
            if (IsOverrideCandidate(document, proxyIndex, static_cast<std::int32_t>(rootIndex),
                                    static_cast<std::int32_t>(candidate)))
                return static_cast<std::int32_t>(candidate);
        }
    }
    return -1;
}
}
