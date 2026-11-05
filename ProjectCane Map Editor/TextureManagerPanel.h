#pragma once

#include <string>

class BrxDocument;
class SceneRenderer;
struct GLFWwindow;

void DrawTextureManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, GLFWwindow *window,
                             std::string &status);
