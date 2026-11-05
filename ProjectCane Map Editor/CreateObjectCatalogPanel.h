#pragma once

#include <cstdint>
#include <string>

class BrxDocument;
class EditorWorkspace;
class SceneRenderer;

void DrawCreateObjectCatalogPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace,
                                  SceneRenderer &renderer, std::int32_t selectedObjectIndex, std::string &status);

void DrawObjectUseGuidePanel(bool &open, EditorWorkspace &workspace, bool &openCreateObjectCatalog,
                             bool &openObjectOperations);
