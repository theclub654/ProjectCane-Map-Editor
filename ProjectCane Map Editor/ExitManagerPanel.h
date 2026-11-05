#pragma once

#include <cstdint>
#include <string>

class BrxDocument;
class EditorCamera;
class EditorWorkspace;
class SceneRenderer;

void DrawExitManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, EditorCamera &camera,
                          EditorWorkspace &workspace, std::int32_t &visualizedExitObjectIndex, std::string &status);
