#pragma once

#include "BrxDocument.h"
#include "EditorDocumentOperations.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct EditorPrefab
{
    struct ReferenceBinding
    {
        enum class Kind : std::uint8_t
        {
            Property,
            RotationSource,
            Foster,
            IkTarget,
            Elbow,
            JointParent,
            JointChild,
            Pose
        };
        std::int32_t sourceLocalIndex = -1;
        std::int32_t targetLocalIndex = -1;
        Kind kind = Kind::Property;
        std::uint32_t slot = 0;
    };
    std::uint64_t stableId = 0;
    std::string name;
    std::vector<BrxObjectRecord> objects;
    std::vector<BrxGlobRecord> globs;
    std::vector<BrxGlobsetRecord> globsets;
    std::vector<BrxCollisionTriangle> collision;
    std::vector<BrxLightRecord> lights;
    std::vector<BrxCameraRecord> cameras;
    std::vector<BrxCheckpointRecord> checkpoints;
    std::vector<BrxWarpRecord> warps;
    std::vector<BrxExitRecord> exits;
    std::vector<BrxScanRecord> scans;
    std::vector<BrxAsegRecord> animationSegments;
    std::vector<BrxGlobWarpRecord> globWarps;
    std::vector<BrxLaserBeamRecord> laserBeams;
    std::vector<BrxShapePathRecord> shapePaths;
    std::vector<BrxPathZoneRecord> pathZones;
    std::vector<BrxSmRecord> stateMachines;
    std::vector<BrxRchmRecord> reachMaps;
    std::vector<BrxKeyholeRecord> keyholes;
    std::vector<BrxEffectRecord> effects;
    std::vector<BrxVolumeRecord> volumes;
    std::vector<ReferenceBinding> internalReferences;
    std::int32_t rootLocalIndex = -1;
};

class PrefabLibrary
{
  public:
    bool Capture(const BrxDocument &document, std::int32_t rootObjectIndex, const std::string &name,
                 std::string &error);
    EditorOperationResult Instantiate(BrxDocument &document, std::uint64_t prefabId,
                                      std::int32_t parentObjectIndex = -1, const float *worldTransform = nullptr,
                                      bool linked = true);
    bool MakeUnique(std::int32_t instanceRoot);
    bool Rename(std::uint64_t prefabId, const std::string &name);
    bool Remove(std::uint64_t prefabId);
    [[nodiscard]] const EditorPrefab *Find(std::uint64_t stableId) const;
    [[nodiscard]] const std::vector<EditorPrefab> &Prefabs() const
    {
        return prefabs_;
    }
    [[nodiscard]] std::uint64_t LinkedPrefab(std::int32_t instanceRoot) const;
    void ClearInstances()
    {
        linkedInstances_.clear();
    }

  private:
    std::vector<EditorPrefab> prefabs_;
    std::unordered_map<std::int32_t, std::uint64_t> linkedInstances_;
};
