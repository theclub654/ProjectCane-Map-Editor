#pragma once
#include <string>
class BrxDocument;
class EditorWorkspace;
class SceneRenderer;
class EditorCamera;
bool HandleViewportAssetDrop(BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                             const EditorCamera &camera, int &gizmoMode, std::string &status);
