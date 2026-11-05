#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class BrxDocument;

struct EditorOperationResult
{
    bool succeeded = false;
    std::int32_t primaryObjectIndex = -1;
    std::vector<std::int32_t> affectedObjectIndices;
    std::string message;
};

class EditorDocumentOperations
{
  public:
    enum class TnPreset : std::uint8_t
    {
        StaticFrame,
        LinearPath,
        CubicPath
    };
    enum class ButtonPreset : std::uint8_t
    {
        Push,
        Lever,
        Contact,
        Whack,
        Hang
    };
    static std::int16_t AllocateOid(const BrxDocument &document);
    static EditorOperationResult CreateObject(BrxDocument &document, std::int16_t cid, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateXfm(BrxDocument &document, std::int32_t parentIndex,
                                           const float *worldTransform = nullptr);
    static EditorOperationResult CreatePnt(BrxDocument &document, std::int32_t parentIndex,
                                           const float *worldTransform = nullptr);
    static EditorOperationResult CreateHpnt(BrxDocument &document, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateJmt(BrxDocument &document, std::int32_t parentIndex,
                                           const float *worldTransform = nullptr);
    static EditorOperationResult CreateSpire(BrxDocument &document, std::int32_t parentIndex,
                                             const float *worldTransform = nullptr);
    static EditorOperationResult CreateScan(BrxDocument &document, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateShape(BrxDocument &document, bool cubic, std::int32_t parentIndex,
                                             const float *worldTransform = nullptr);
    static EditorOperationResult CreateLasen(BrxDocument &document, std::int32_t parentIndex,
                                             const float *worldTransform = nullptr);
    static EditorOperationResult CreateCamsen(BrxDocument &document, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateHshape(BrxDocument &document, bool cubic, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreatePipe(BrxDocument &document, bool cubic, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateRail(BrxDocument &document, bool cubic, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateLanding(BrxDocument &document, bool cubic, std::int32_t parentIndex,
                                               const float *worldTransform = nullptr);
    static EditorOperationResult AddClueBounceFrame(BrxDocument &document, std::int32_t clueObjectIndex,
                                                    std::int32_t sourceFrameObjectIndex = -1);
    static EditorOperationResult RemoveClueBounceFrame(BrxDocument &document, std::int32_t clueObjectIndex,
                                                       std::int32_t frameObjectIndex);
    static EditorOperationResult MoveClueBounceFrame(BrxDocument &document, std::int32_t clueObjectIndex,
                                                     std::int32_t frameObjectIndex, int direction);
    static EditorOperationResult CreateClueCollectionHelper(BrxDocument &document, std::int32_t clueObjectIndex);
    static EditorOperationResult CreateTn(BrxDocument &document, TnPreset preset, std::int32_t parentIndex,
                                          const float *worldTransform = nullptr);
    static EditorOperationResult CreateWarp(BrxDocument &document, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateExit(BrxDocument &document, std::int32_t parentIndex,
                                            const float *worldTransform = nullptr);
    static EditorOperationResult CreateVolume(BrxDocument &document, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateCheckpoint(BrxDocument &document, std::int32_t parentIndex,
                                                  const float *worldTransform = nullptr);
    static EditorOperationResult CreateCheckpointAseg(BrxDocument &document, std::int32_t checkpointObjectIndex);
    static EditorOperationResult CreateTzp(BrxDocument &document, std::int32_t parentIndex,
                                           const float *worldTransform = nullptr);
    static EditorOperationResult CreateVolzp(BrxDocument &document, std::int32_t parentIndex,
                                             const float *worldTransform = nullptr);
    static EditorOperationResult CreateVolbtn(BrxDocument &document, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateJlovol(BrxDocument &document, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateBarrier(BrxDocument &document, std::int32_t parentIndex,
                                               const float *worldTransform = nullptr);
    static EditorOperationResult CreateBarrierDestinationPoint(BrxDocument &document, std::int32_t barrierObjectIndex);
    static EditorOperationResult CreateWater(BrxDocument &document, std::int32_t parentIndex,
                                             const float *worldTransform = nullptr);
    static EditorOperationResult CreateWaterFromObject(BrxDocument &document, std::int32_t sourceObjectIndex);
    static EditorOperationResult CreateButton(BrxDocument &document, ButtonPreset preset, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreateDialog(BrxDocument &document, std::int32_t parentIndex,
                                              const float *worldTransform = nullptr);
    static EditorOperationResult CreatePathZone(BrxDocument &document,
                                                const std::vector<std::array<float, 3>> &boundary,
                                                std::int32_t parentIndex);
    static EditorOperationResult CreatePathZoneFromCollision(BrxDocument &document, std::int32_t sourceObjectIndex,
                                                             std::int32_t parentIndex);
    static EditorOperationResult CreateEmbeddedWarpAseg(BrxDocument &document, std::int32_t warpObjectIndex);
    static EditorOperationResult CreateEmbeddedExitAseg(BrxDocument &document, std::int32_t exitObjectIndex);
    static EditorOperationResult MoveEmbeddedWarpAseg(BrxDocument &document, std::int32_t asegObjectIndex,
                                                      int direction);
    static EditorOperationResult DuplicateHierarchy(BrxDocument &document, std::int32_t rootObjectIndex);
    static EditorOperationResult ImportHierarchy(BrxDocument &destination, const BrxDocument &source,
                                                 std::int32_t sourceRootObjectIndex,
                                                 std::int32_t destinationParentObjectIndex,
                                                 bool preserveNestedProxies = false);
    static EditorOperationResult ReplaceHierarchy(BrxDocument &destination, const BrxDocument &source,
                                                  std::int32_t sourceRootObjectIndex,
                                                  std::int32_t destinationRootObjectIndex);
    static EditorOperationResult DeleteHierarchy(BrxDocument &document, std::int32_t rootObjectIndex);
    static EditorOperationResult ReparentObject(BrxDocument &document, std::int32_t objectIndex,
                                                std::int32_t newParentIndex, bool preserveWorldTransform = true);
    static EditorOperationResult CreateProxySourceFromHierarchy(BrxDocument &document, std::int32_t rootObjectIndex);
    static EditorOperationResult CreateLinkedProxyInstance(BrxDocument &document, std::int32_t sourceProxyRecordIndex,
                                                           std::int32_t sourceRootIndex);
    static EditorOperationResult MakeProxyRootUnique(BrxDocument &document, std::int32_t objectIndex);
};
