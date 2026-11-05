#pragma once

#include <functional>
#include <string>

class BrxDocument;

struct SceneWorldPreviewCallbacks
{
    std::function<void(float, float, const float *)> updateWorldAppearance;
    std::function<void(float, float, float)> updateFog;
    std::function<void(const float *)> updateFogColor;
    std::function<void(float, float, float)> updateProjection;
    std::function<void(float)> updateMrdRatio;
};

void DrawSceneWorldPanel(bool &open, BrxDocument &document, std::string &status,
                         const SceneWorldPreviewCallbacks &preview);
