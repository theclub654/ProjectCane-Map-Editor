#pragma once
#include <string>
class BrxDocument;
class SceneRenderer;
void DrawPlacementToolsPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, int &gizmoMode,
                             std::string &status);
