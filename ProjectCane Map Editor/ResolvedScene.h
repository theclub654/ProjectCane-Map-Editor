#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

class BrxDocument;

struct ResolvedObjectReference
{
    std::int32_t sourceObjectIndex = -1;
    std::int16_t eopid = -1;
    std::int16_t targetOid = -1;
    std::int32_t targetObjectIndex = -1;
    bool compatible = false;
};

class ResolvedScene
{
  public:
    void Rebuild(const BrxDocument &document);
    void Clear();

    [[nodiscard]] const std::vector<std::int32_t> &Roots() const
    {
        return roots_;
    }
    [[nodiscard]] const std::vector<std::int32_t> &Children(std::int32_t objectIndex) const;
    [[nodiscard]] const std::vector<ResolvedObjectReference> &References() const
    {
        return references_;
    }
    [[nodiscard]] std::int32_t FindObject(std::int16_t oid, std::int16_t spliceIndex = -1) const;
    [[nodiscard]] std::int32_t FindObjectFrom(std::int16_t oid, std::int32_t sourceObjectIndex) const;
    [[nodiscard]] std::uint64_t Revision() const
    {
        return revision_;
    }

  private:
    std::vector<std::int32_t> roots_;
    std::vector<std::vector<std::int32_t>> children_;
    std::vector<std::int16_t> spliceIndices_;
    std::vector<std::int32_t> proxyRecordIndices_;
    std::vector<std::int32_t> proxyRootIndices_;
    std::unordered_multimap<std::int16_t, std::int32_t> objectsByOid_;
    std::vector<ResolvedObjectReference> references_;
    std::uint64_t revision_ = 0;
};
