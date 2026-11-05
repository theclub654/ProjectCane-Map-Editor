#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class BrxDocument;

struct BrxSourceObjectIdentity
{
    std::uint64_t offset = 0;
    std::int16_t cid = -1;
    std::int16_t oid = -1;
    std::int16_t spliceIndex = -1;
};

class BrxSourceDocument
{
  public:
    void Capture(const BrxDocument &document);
    void Clear();

    [[nodiscard]] bool IsBound() const
    {
        return !sourcePath_.empty();
    }
    [[nodiscard]] bool Matches(const BrxDocument &document) const;
    [[nodiscard]] const std::filesystem::path &SourcePath() const
    {
        return sourcePath_;
    }
    [[nodiscard]] std::uint64_t Fingerprint() const
    {
        return fingerprint_;
    }
    [[nodiscard]] std::size_t OriginalObjectCount() const
    {
        return objects_.size();
    }
    [[nodiscard]] const std::vector<BrxSourceObjectIdentity> &Objects() const
    {
        return objects_;
    }

  private:
    std::filesystem::path sourcePath_;
    std::uint64_t fileSize_ = 0;
    std::uint32_t version_ = 0;
    std::uint64_t fingerprint_ = 0;
    std::vector<BrxSourceObjectIdentity> objects_;
};
