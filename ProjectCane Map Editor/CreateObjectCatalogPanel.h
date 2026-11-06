#pragma once

#include <cstdint>
#include <string>

class BrxDocument;
class EditorWorkspace;
class SceneRenderer;
class ObjectTypeRegistry;
class EditorCamera;
struct EditorOperationResult;

// Shared with automation; identical presets and unavailable-class rules to Add.
EditorOperationResult CreateObjectFromCatalog(BrxDocument &document, std::int16_t cid,
                                              std::int32_t parent, const float *transform);

// Read-only guidance; does not certify dependencies or change native records.
void DrawObjectSetupCard(std::int16_t cid, bool initiallyExpanded = false);
// Returns true when the user requests the existing class-specific editor.
bool DrawObjectRelationships(const BrxDocument &document, const ObjectTypeRegistry &registry,
                             SceneRenderer &renderer, std::int32_t objectIndex);

void DrawCreateObjectCatalogPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace,
                                  SceneRenderer &renderer, std::int32_t selectedObjectIndex, std::string &status,
                                  const EditorCamera &camera);

void DrawObjectUseGuidePanel(bool &open, EditorWorkspace &workspace, bool &openCreateObjectCatalog,
                             bool &openObjectOperations);
