#pragma once

#include <cstdint>
#include <functional>
#include <string>

class BrxDocument;
class SceneRenderer;

using SpeakerAnimationPreviewRequest =
    std::function<void(std::int32_t speakerObjectIndex, std::int32_t dialogObjectIndex, std::int16_t animationOid,
                       bool explicitAnimation)>;

void DrawSpeakerManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, std::string &status,
                             const SpeakerAnimationPreviewRequest &previewAnimation = {});
