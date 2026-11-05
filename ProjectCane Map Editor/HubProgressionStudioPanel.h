#pragma once

#include <string>

class BrxDocument;
class EditorCamera;
class SceneRenderer;

void DrawHubProgressionStudioPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                                   std::string &status);
