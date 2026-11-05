#pragma once

#include "BrxDocument.h"

#include <cstdint>
#include <string>
#include <vector>

struct SmPreviewSnapshot
{
    std::int32_t machineObjectIndex = -1;
    std::int32_t boundRootObjectIndex = -1;
    std::int16_t currentStateOid = -1;
    std::int16_t nextStateOid = -1;
    std::int16_t goalStateOid = -1;
    std::int32_t activeTransitionIndex = -1;
    std::int32_t animationObjectIndex = -1;
    bool reverse = false;
    bool waitingForAnimation = false;
    float speed = 1.0f;
};

class SmPreviewRuntime
{
  public:
    void Bind(std::int32_t machineObjectIndex, const BrxSmRecord &machine, std::int32_t rootObjectIndex = -1);
    void Reset(const BrxSmRecord &machine);
    bool SetGoal(const BrxSmRecord &machine, std::int16_t goalOid);
    bool Seek(const BrxSmRecord &machine, std::int16_t stateOid);
    bool CompleteAnimatedTransition(const BrxSmRecord &machine);
    bool Advance(const BrxSmRecord &machine);
    void SetSpeed(float speed)
    {
        snapshot_.speed = speed;
    }
    void SetBoundRoot(std::int32_t objectIndex)
    {
        snapshot_.boundRootObjectIndex = objectIndex;
    }
    void SetSeed(std::uint32_t seed)
    {
        randomState_ = seed != 0 ? seed : 1u;
    }
    [[nodiscard]] std::uint32_t Seed() const
    {
        return randomState_;
    }
    [[nodiscard]] const SmPreviewSnapshot &Snapshot() const
    {
        return snapshot_;
    }
    [[nodiscard]] const std::vector<std::string> &Trace() const
    {
        return trace_;
    }

  private:
    [[nodiscard]] static int StateIndex(const BrxSmRecord &machine, std::int16_t oid);
    bool ChooseTransition(const BrxSmRecord &machine);
    void FinishWithoutTransition();
    float Random01();
    void Log(std::string message);

    SmPreviewSnapshot snapshot_;
    std::uint32_t randomState_ = 0x6d2b79f5u;
    std::vector<std::string> trace_;
};
