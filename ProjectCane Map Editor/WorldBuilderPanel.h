#pragma once

#include <string>
#include <cstddef>

class BrxDocument;
class EditorCamera;
class EditorWorkspace;
class SceneRenderer;
struct GLFWwindow;

void DrawWorldBuilderPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                           EditorCamera &camera, GLFWwindow *window, bool &showAssetBrowser, bool &showValidation,
                           bool &showSpliceEvents, bool &showPaths, bool &showReferenceLinks, bool &showPlacementTools,
                           std::size_t undoCount, std::size_t redoCount, const std::string &undoLabel,
                           const std::string &redoLabel, bool &requestUndo, bool &requestRedo, std::string &status);
