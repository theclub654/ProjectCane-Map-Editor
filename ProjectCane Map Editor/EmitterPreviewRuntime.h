#pragma once

#include "BrxDocument.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

struct EmitterPreviewParticle
{
    std::int32_t ownerObjectIndex = -1;
    std::int32_t renderObjectIndex = -1;
    std::int32_t nextRenderObjectIndex = -1;
    glm::vec3 position{};
    glm::vec3 previousPosition{};
    glm::vec3 velocity{};
    glm::mat3 orientation{1.0f};
    glm::vec3 angularVelocity{};
    glm::vec3 splineTargetPosition{};
    glm::vec3 splineTargetVelocity{};
    glm::vec4 color{1.0f};
    glm::vec4 colorStart{1.0f};
    glm::vec4 colorEnd{1.0f};
    float age = 0.0f;
    float lifetime = 1.0f;
    float roll = 0.0f;
    float angularSpeed = 0.0f;
    float flyingScale = 0.0f;
    float velocityDamping = -1.0f;
    float renderHeightOffset = 0.0f;
    float lifetimeScale = 1.0f;
    float scale = 1.0f;
    float baseScale = 1.0f;
    float baseAlpha = 1.0f;
    std::int16_t shaderOid = -1;
    std::int8_t ripType = -1;
    std::int8_t trailType = -1;
    std::uint16_t textureFrame = 0;
    std::uint16_t initialTextureFrame = 0;
    bool additive = false;
    bool interpolateColor = false;
    bool hasSplineTarget = false;
    bool modelParticle = false;
    std::uint8_t movementKind = 0;
};

enum class EmitterPreviewScope : std::uint8_t
{
    Selected,
    Visible,
    All
};

enum class EmitterPreviewActivation : std::uint8_t
{
    Authored,
    Enabled,
    Disabled,
    Paused
};

// Editor-only equivalent of ProjectCane's EMITTER -> BLIPG update path. It
// consumes authored values but never writes runtime state back into the BRX.
class EmitterPreviewRuntime
{
  public:
    void SetPlaying(bool playing)
    {
        playing_ = playing;
    }
    void SetLooping(bool looping)
    {
        looping_ = looping;
    }
    void SetSpeed(float speed)
    {
        speed_ = speed;
    }
    void SetScope(EmitterPreviewScope scope);
    void SetSelectedEmitter(std::int32_t objectIndex)
    {
        selectedEmitter_ = objectIndex;
    }
    void SetSeed(std::uint32_t seed);
    void Restart();
    void RestartAll();
    void ResetSession();
    // Authored particles are frozen while playback is stopped. Rebuild that
    // snapshot once after a document/render upload instead of rescanning and
    // warming every EMITTER on every viewport frame.
    void InvalidateSnapshot();
    void Restart(std::int32_t objectIndex);
    void SetActivation(std::int32_t objectIndex, EmitterPreviewActivation activation);
    void PauseFor(std::int32_t objectIndex, float seconds);
    [[nodiscard]] EmitterPreviewActivation Activation(std::int32_t objectIndex) const;
    [[nodiscard]] float TimedPauseRemaining(std::int32_t objectIndex) const;
    void Stop();
    void Step(const BrxDocument &document, std::int32_t objectIndex, float seconds);
    void Update(const BrxDocument &document, const std::vector<std::int32_t> &objectIndices, float seconds);
    void SetSpatialContext(std::vector<glm::mat4> resolvedWorld, std::vector<glm::vec3> linearVelocity,
                           std::vector<glm::vec3> angularVelocity, const glm::mat4 &cameraWorld);

    [[nodiscard]] bool Playing() const
    {
        return playing_;
    }
    [[nodiscard]] bool NeedsUpdate() const
    {
        return playing_ || snapshotDirty_;
    }
    [[nodiscard]] bool Looping() const
    {
        return looping_;
    }
    [[nodiscard]] float Speed() const
    {
        return speed_;
    }
    [[nodiscard]] EmitterPreviewScope Scope() const
    {
        return scope_;
    }
    [[nodiscard]] std::int32_t SelectedEmitter() const
    {
        return selectedEmitter_;
    }
    [[nodiscard]] std::uint32_t Seed() const
    {
        return seed_;
    }
    [[nodiscard]] const std::vector<EmitterPreviewParticle> &Particles() const
    {
        return particles_;
    }
    [[nodiscard]] std::size_t ActiveEmitterCount() const
    {
        return groups_.size();
    }

  private:
    struct GroupState
    {
        std::vector<EmitterPreviewParticle> particles;
        std::mt19937 random{1};
        float spawnAccumulator = 0.0f;
        float rateRecalcRemaining = 0.0f;
        float currentRate = 0.0f;
        float pauseRemaining = 0.0f;
        std::int32_t emittedCount = 0;
        std::int16_t shaderOid = 0;
        glm::vec4 shaderColor{1.0f};
        bool additive = false;
        std::uint16_t textureFrameCount = 1;
        std::vector<std::uint16_t> textureFrameMap{0};
        bool initialized = false;
        bool staticSnapshotBuilt = false;
    };

    void Simulate(const BrxDocument &document, std::int32_t objectIndex, float seconds, GroupState &group,
                  std::int32_t configurationObjectIndex = -1);
    void RebuildParticleView();

    std::vector<EmitterPreviewParticle> particles_;
    std::unordered_map<std::int32_t, GroupState> groups_;
    std::unordered_map<std::int32_t, EmitterPreviewActivation> activation_;
    std::unordered_map<std::int32_t, float> timedPauseRemaining_;
    std::vector<glm::mat4> resolvedWorld_;
    std::vector<glm::vec3> linearVelocity_;
    std::vector<glm::vec3> angularVelocity_;
    glm::mat4 cameraWorld_{1.0f};
    std::uint32_t seed_ = 1;
    float speed_ = 1.0f;
    bool playing_ = false;
    bool looping_ = true;
    EmitterPreviewScope scope_ = EmitterPreviewScope::Visible;
    std::int32_t selectedEmitter_ = -1;
    bool snapshotDirty_ = true;
    std::size_t particleLimitPerEmitter_ = 4096;
};
