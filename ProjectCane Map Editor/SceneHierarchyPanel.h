#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include <imgui.h>

#include "ModelImport.h"

class BrxDocument;
class SceneRenderer;
class ResolvedScene;
struct GLFWwindow;

// Structural edits are requested while drawing and applied by the application
// afterward, when no hierarchy rows retain references into the document.
struct SceneHierarchyAction
{
    enum class Kind
    {
        None,
        MoveToParent,
        MoveInViewport,
        DeleteHierarchy,
        ReplaceHierarchy,
        ReplaceModel,
        MakeUnique,
        CreateTransformMarker,
        OpenGuide,
        ReviewDuplicate,
        CopyGlobToObject,
        DeleteGlob
    };

    Kind kind = Kind::None;
    std::int32_t objectIndex = -1;
    std::int32_t parentIndex = -1;
    ModelImportOptions modelOptions;
    std::size_t globIndex = static_cast<std::size_t>(-1);
};

void DrawSceneHierarchyPanel(const ImVec2 &position, const ImVec2 &size, ImGuiWindowFlags flags, BrxDocument &document,
                             const ResolvedScene &resolvedScene, SceneRenderer &renderer, ImGuiTextFilter &filter,
                             std::int32_t &lastSelectedObject, std::string &status, SceneHierarchyAction &action,
                             bool dockable = false, GLFWwindow *window = nullptr);
