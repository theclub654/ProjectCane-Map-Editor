#pragma once

#include <cstdint>
#include <string>
#include <vector>

class BrxDocument;
class PrefabLibrary;

enum class EditorAssetKind : std::uint8_t
{
    Model,
    Texture,
    Material,
    Animation,
    Effect,
    Audio,
    Collision,
    Prefab
};

struct EditorAssetDragPayload
{
    std::uint64_t stableId = 0;
    std::uint8_t kind = 0;
};

struct EditorAssetRecord
{
    std::uint64_t stableId = 0;
    EditorAssetKind kind = EditorAssetKind::Model;
    std::string name;
    std::int32_t sourceIndex = -1;
    std::int32_t ownerObjectIndex = -1;
    std::vector<std::int32_t> objectUsers;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t primitiveCount = 0;
};

class AssetDatabase
{
  public:
    void Rebuild(const BrxDocument &document);
    void Clear();
    void SyncPrefabs(const PrefabLibrary &prefabs);
    [[nodiscard]] const std::vector<EditorAssetRecord> &Assets() const
    {
        return assets_;
    }
    [[nodiscard]] std::vector<const EditorAssetRecord *> Find(EditorAssetKind kind,
                                                              const std::string &filter = {}) const;
    [[nodiscard]] const EditorAssetRecord *FindById(std::uint64_t stableId) const;

  private:
    std::vector<EditorAssetRecord> assets_;
};
