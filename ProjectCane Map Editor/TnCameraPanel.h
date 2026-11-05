#pragma once

#include <string>

class BrxDocument;
class EditorCamera;
class EditorWorkspace;
class SceneRenderer;

struct TnCameraDisplaySettings
{
    bool showHelpers = true;
    bool showAllTns = false;
    bool showTriggerVolumes = true;
    bool showCameraPaths = true;
    bool showCameraFrames = true;
    bool showCameraIcons = true;
    bool showFrustums = true;
    bool showLabels = true;
    bool debugGeometry = false;
    bool showInvalidOnly = false;
};

void DrawTnCameraPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                       EditorWorkspace &workspace, TnCameraDisplaySettings &display, std::string &status);
