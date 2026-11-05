#pragma once

class BrxDocument;

// Data-only PCBRX v17 world loader owned by the Map Editor.  ProjectCane is
// the format authority, but the editor deliberately has no source or binary
// dependency on the game project.
class BrxLoader
{
  public:
    static void ParseWorld(BrxDocument &document);
};
