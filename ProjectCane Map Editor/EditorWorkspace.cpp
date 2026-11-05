#include "EditorWorkspace.h"

#include "BrxDocument.h"

EditorWorkspace::EditorWorkspace()
{
    objectTypes_.Rebuild();
}

void EditorWorkspace::BindSource(const BrxDocument &document)
{
    Refresh(document);
}

void EditorWorkspace::Refresh(const BrxDocument &document)
{
    scene_.Rebuild(document);
    assets_.Rebuild(document);
    assets_.SyncPrefabs(prefabs_);
    validation_.Run(document, scene_);
    ++revision_;
}

void EditorWorkspace::Close()
{
    preview_.Stop();
    scene_.Clear();
    assets_.Clear();
    prefabs_.ClearInstances();
    validation_.Clear();
    ++revision_;
}
