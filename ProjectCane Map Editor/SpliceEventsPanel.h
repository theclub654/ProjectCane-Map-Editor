#pragma once

#include <string>

class BrxDocument;
class EditorWorkspace;
class SceneRenderer;

void DrawSpliceEventsPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                           std::string &status);
