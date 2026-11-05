#pragma once

#include <string>

class BrxDocument;
class EditorWorkspace;
class SceneRenderer;
struct GLFWwindow;

void DrawSoundManagerPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                           GLFWwindow *window, std::string &status);
