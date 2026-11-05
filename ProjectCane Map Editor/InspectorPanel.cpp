#include "InspectorPanel.h"
#include "BrxDocument.h"
#include "ObjectTypeRegistry.h"
#include "SceneRenderer.h"
#include <algorithm>

InspectorPanelModel BuildInspectorPanelModel(const BrxDocument &document, const ObjectTypeRegistry &registry,
                                             const SceneRenderer &renderer, const std::int32_t selectedObjectIndex)
{
    InspectorPanelModel model;
    if (selectedObjectIndex < 0 || static_cast<std::size_t>(selectedObjectIndex) >= document.Objects().size())
        return model;
    const auto &object = document.Objects()[static_cast<std::size_t>(selectedObjectIndex)];
    const auto *type = registry.Find(object.cid);
    model.propertySections = type ? type->inheritanceSections : PropertySectionsForCid(object.cid);
    model.appearance = object.hasAloPayload || (type && type->hasAppearance);
    model.physics = object.so.present || (type && type->hasPhysics);
    model.model = object.renderGroupCount != 0 ||
                  std::any_of(document.Globs().begin(), document.Globs().end(), [&](const BrxGlobRecord &glob)
                              { return renderer.ResolveLogicalOwner(glob.ownerObjectIndex) == selectedObjectIndex; });
    model.animation =
        object.hasAloPayload || object.alox.flags != 0 || !object.poseCombinations.empty() ||
        std::any_of(document.Globs().begin(), document.Globs().end(),
                    [&](const BrxGlobRecord &glob)
                    {
                        return renderer.ResolveLogicalOwner(glob.ownerObjectIndex) == selectedObjectIndex &&
                               glob.hasShaderAnimation;
                    });
    model.lighting = std::any_of(document.Lights().begin(), document.Lights().end(), [&](const BrxLightRecord &light)
                                 { return light.ownerObjectIndex == selectedObjectIndex; });
    model.gameplay = !object.properties.empty() || (type && type->hasGameplay);
    const bool hasShapePath =
        std::any_of(document.ShapePaths().begin(), document.ShapePaths().end(),
                    [&](const BrxShapePathRecord &shape) { return shape.ownerObjectIndex == selectedObjectIndex; });
    // Every CID with a ProjectCane SetUser lane has a real object-specific
    // authoring surface, even when it does not yet have a handcrafted studio.
    // Keep Object-specific available so the schema-backed fallback can expose
    // those legal BRX values instead of forcing users into read-only Advanced.
    model.specialized = !object.details.empty() || hasShapePath ||
                        (type && (type->hasSpecializedInspector || !type->writableOptions.empty()));
    return model;
}
