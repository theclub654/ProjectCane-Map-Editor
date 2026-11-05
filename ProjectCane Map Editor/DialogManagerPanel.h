#pragma once

#include <cstdint>
#include <functional>
#include <string>

class BrxDocument;
class SceneRenderer;

// Default lipsync (OID 1354) is resolved below the active SPEAKER. Explicit
// lipsync is resolved from the owning DIALOG context, matching dialog.cpp.
using DialogLipsyncPreviewRequest = std::function<void(std::int32_t dialogObjectIndex, std::int32_t speakerOid,
                                                       std::int16_t animationOid, bool explicitAnimation)>;

void DrawDialogManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, std::string &status,
                            const DialogLipsyncPreviewRequest &previewLipsync = {});
