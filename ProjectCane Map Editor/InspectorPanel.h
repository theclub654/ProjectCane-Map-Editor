#pragma once
#include "EditorOptions.h"
#include <cstdint>
#include <vector>
class BrxDocument;
class ObjectTypeRegistry;
class SceneRenderer;
struct InspectorPanelModel
{
    std::vector<EditorPropertySection> propertySections;
    bool appearance = false, physics = false, model = false, animation = false;
    bool lighting = false, gameplay = false, specialized = false;
};
InspectorPanelModel BuildInspectorPanelModel(const BrxDocument &document, const ObjectTypeRegistry &registry,
                                             const SceneRenderer &renderer, std::int32_t selectedObjectIndex);
