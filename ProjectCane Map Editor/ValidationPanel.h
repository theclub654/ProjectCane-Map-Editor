#pragma once
#include <string>
class BrxDocument;
class EditorWorkspace;
class SceneRenderer;
class EditorCamera;
void DrawValidationPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                         EditorCamera &camera, std::string &status);
