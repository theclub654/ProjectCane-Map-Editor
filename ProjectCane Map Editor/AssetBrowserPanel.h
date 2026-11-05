#pragma once

#include <string>

class BrxDocument;
class SceneRenderer;
class AssetDatabase;
class PrefabLibrary;
class EditorCamera;
class EditorWorkspace;
struct GLFWwindow;

void DrawAssetBrowserPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, AssetDatabase &assets,
                           PrefabLibrary &prefabs, EditorWorkspace &workspace, GLFWwindow *window, EditorCamera &camera,
                           int &gizmoMode, std::string &status);
