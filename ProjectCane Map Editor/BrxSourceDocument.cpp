#include "BrxSourceDocument.h"

#include "BrxDocument.h"

namespace
{
std::uint64_t ComputeSourceFingerprint(const BrxDocument &document)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : document.Payload())
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    hash ^= document.FileSize();
    hash *= 1099511628211ull;
    hash ^= document.Version();
    hash *= 1099511628211ull;
    return hash;
}
}

void BrxSourceDocument::Capture(const BrxDocument &document)
{
    Clear();
    if (!document.IsOpen())
        return;
    sourcePath_ = document.Path();
    fileSize_ = document.FileSize();
    version_ = document.Version();
    fingerprint_ = ComputeSourceFingerprint(document);
    objects_.reserve(document.Objects().size());
    for (const auto &object : document.Objects())
        objects_.push_back({object.offset, object.cid, object.oid, object.spliceIndex});
}

void BrxSourceDocument::Clear()
{
    sourcePath_.clear();
    fileSize_ = 0;
    version_ = 0;
    fingerprint_ = 0;
    objects_.clear();
}

bool BrxSourceDocument::Matches(const BrxDocument &document) const
{
    return IsBound() && document.IsOpen() && sourcePath_.filename() == document.Path().filename() &&
           fileSize_ == document.FileSize() && version_ == document.Version() &&
           fingerprint_ == ComputeSourceFingerprint(document);
}
