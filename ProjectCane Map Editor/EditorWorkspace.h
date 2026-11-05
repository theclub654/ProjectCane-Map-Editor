#pragma once

#include "AssetDatabase.h"
#include "BrxCompiler.h"
#include "ObjectTypeRegistry.h"
#include "PrefabLibrary.h"
#include "ResolvedScene.h"
#include "RuntimePreviewWorld.h"
#include "ValidationSystem.h"

#include <cstdint>

class BrxDocument;

class EditorWorkspace
{
  public:
    EditorWorkspace();
    void BindSource(const BrxDocument &document);
    void Refresh(const BrxDocument &document);
    void Close();

    [[nodiscard]] ResolvedScene &Scene()
    {
        return scene_;
    }
    [[nodiscard]] ObjectTypeRegistry &ObjectTypes()
    {
        return objectTypes_;
    }
    [[nodiscard]] AssetDatabase &Assets()
    {
        return assets_;
    }
    [[nodiscard]] PrefabLibrary &Prefabs()
    {
        return prefabs_;
    }
    [[nodiscard]] RuntimePreviewWorld &Preview()
    {
        return preview_;
    }
    [[nodiscard]] ValidationSystem &Validation()
    {
        return validation_;
    }
    [[nodiscard]] BrxCompiler &Compiler()
    {
        return compiler_;
    }
    [[nodiscard]] std::uint64_t Revision() const
    {
        return revision_;
    }

  private:
    ResolvedScene scene_;
    ObjectTypeRegistry objectTypes_;
    AssetDatabase assets_;
    PrefabLibrary prefabs_;
    RuntimePreviewWorld preview_;
    ValidationSystem validation_;
    BrxCompiler compiler_;
    std::uint64_t revision_ = 0;
};
