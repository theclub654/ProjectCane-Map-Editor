#pragma once

#include <cstdint>
#include <string>

class BrxDocument;

enum class RuntimePreviewKind : std::uint8_t
{
    None,
    Animation,
    Physics,
    Particles,
    Camera,
    Tunnel,
    Warp,
    StateMachine
};

class RuntimePreviewWorld
{
  public:
    bool Begin(RuntimePreviewKind kind, std::int32_t objectIndex, const BrxDocument &document, std::string &error);
    void Stop();
    void Pause(bool paused)
    {
        paused_ = paused;
    }
    void Update(float deltaSeconds);

    [[nodiscard]] bool Active() const
    {
        return kind_ != RuntimePreviewKind::None;
    }
    [[nodiscard]] bool Paused() const
    {
        return paused_;
    }
    [[nodiscard]] RuntimePreviewKind Kind() const
    {
        return kind_;
    }
    [[nodiscard]] std::int32_t ObjectIndex() const
    {
        return objectIndex_;
    }
    [[nodiscard]] float Time() const
    {
        return time_;
    }

  private:
    RuntimePreviewKind kind_ = RuntimePreviewKind::None;
    std::int32_t objectIndex_ = -1;
    bool paused_ = false;
    float time_ = 0.0f;
};
