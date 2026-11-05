#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

class BrxDocument;
class EditorCamera;
class SceneRenderer;
struct BrxObjectRecord;

// CM is runtime camera state. This panel simulates its commands without
// serializing runtime-only CM EOPIDs into the level.
class CameraRuntimePanel
{
  public:
    void Draw(const BrxObjectRecord &object, const BrxDocument &document, EditorCamera &camera, SceneRenderer &renderer,
              std::string &status);
    void Update(const BrxDocument &document, EditorCamera &camera, SceneRenderer &renderer, float deltaSeconds,
                float aspectRatio);
    void Stop(const BrxDocument &document, SceneRenderer &renderer);

  private:
    void ResetFromWorld(const BrxDocument &document);
    bool initialized_ = false;
    std::string documentKey_;
    bool enabled_ = false;
    bool paused_ = false;
    float fov_ = 1.0f;
    float targetFov_ = 1.0f;
    float fovResponse_ = 0.5f;
    float nearClip_ = 100.0f;
    float farClip_ = 20000.0f;
    float nearFog_ = 500.0f;
    float farFog_ = 20000.0f;
    float fogMaximum_ = 0.5f;
    float mrdRatio_ = 1.0f;
    std::array<int, 4> lookStack_{0, -1, -1, -1};
    int lookCount_ = 1;
    int lookToPush_ = 0;
    std::int32_t focusObject_ = -1;
    float sniperScreenRadius_ = 0.2f;
    float sniperWorldRadius_ = 50.0f;
    float sniperSuggestedFov_ = 1.0f;
    float joltRemaining_ = 0.0f;
    float joltStrength_ = 2.0f;
    glm::vec3 visibilityCenter_{0.0f};
    float visibilityRadius_ = 50.0f;
    float visibilityMrd_ = 20000.0f;
    bool visibilityResult_ = false;
    float visibilityAlpha_ = 0.0f;
};
