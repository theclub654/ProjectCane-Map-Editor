#pragma once

#include <string>
#include <cstddef>

class BrxDocument;
class SceneRenderer;
class AssetDatabase;
class PrefabLibrary;
class EditorCamera;
class EditorWorkspace;
struct GLFWwindow;

// Shared by model assets, the Inspector and hierarchy menus. A menu label
// draws the export options as a submenu instead of a button/popup.
// This only writes external files.
void DrawModelExportButton(const BrxDocument &document, std::size_t index, bool globset,
                           const std::string &name, GLFWwindow *window, std::string &status,
                           const char *menuLabel = nullptr);

void DrawAssetBrowserPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, AssetDatabase &assets,
                           PrefabLibrary &prefabs, EditorWorkspace &workspace, GLFWwindow *window, EditorCamera &camera,
                           int &gizmoMode, std::string &status);
