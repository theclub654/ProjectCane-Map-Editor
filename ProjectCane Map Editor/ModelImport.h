#pragma once

#include <filesystem>
#include <cstddef>
#include <string>

class BrxDocument;

struct ModelImportOptions
{
    // Common OBJ/glTF/COLLADA assets are authored in meters. ProjectCane
    // worlds use approximately centimeter-sized coordinates, so 100 is the
    // useful game-space default while remaining explicitly editable.
    float scale = 100.0f;
    bool flipV = true;
    bool importMaterials = true;
};

struct ModelImportResult
{
    bool succeeded = false;
    std::size_t globIndex = static_cast<std::size_t>(-1);
    std::size_t triangleCount = 0;
    std::string message;
};

struct FbxCharacterImportOptions
{
    ModelImportOptions model;
    bool importAnimations = true;
    float sampleRate = 60.0f;
};

[[nodiscard]] ModelImportResult ImportStaticModel(const std::filesystem::path &path, BrxDocument &document,
                                                  int ownerObjectIndex, const ModelImportOptions &options);

// Imports an FBX skin as a retail-compatible GLOBSET palette, creates an
// LBONE rest hierarchy, and converts animation stacks into ASEG channels.
// Gameplay AI/state machines are deliberately outside this visual-rig import.
[[nodiscard]] ModelImportResult ImportFbxCharacter(const std::filesystem::path &path, BrxDocument &document,
                                                   int ownerObjectIndex, const FbxCharacterImportOptions &options);

// Replaces only the render model owned by an existing ALO-derived object. The
// object's identity, hierarchy, transform, properties and Splice bindings are
// deliberately left untouched.
[[nodiscard]] ModelImportResult ReplaceStaticModel(const std::filesystem::path &path, BrxDocument &document,
                                                   int ownerObjectIndex, const ModelImportOptions &options);

// Builds retail SO triangle/BSP records from the owner's authored model. This
// is opt-in because visual detail is often unsuitable for gameplay collision.
[[nodiscard]] bool BuildModelCollision(BrxDocument &document, int ownerObjectIndex, bool cameraCollision,
                                       std::string &message);
// Replaces an SO collision payload from a dedicated model file without
// changing its visible GLOBs. The current exact builder accepts closed convex
// hulls, which map safely to ProjectCane's solid-space BSP representation.
[[nodiscard]] bool ImportModelCollision(const std::filesystem::path &path, BrxDocument &document, int ownerObjectIndex,
                                        const ModelImportOptions &options, bool cameraCollision, std::string &message);
// Rebuilds all derived SO geometry data (surface bounds, adjacency, aggregate
// bounds/counts and mapping lengths) from the authoritative positions and
// triangle indices. Invalid topology is rejected instead of silently emitted.
[[nodiscard]] bool RepairModelCollision(BrxDocument &document, int ownerObjectIndex, bool cameraCollision,
                                        std::string &message);
// Reverses every collision plane while preserving BSP inside/outside behavior.
[[nodiscard]] bool FlipModelCollisionNormals(BrxDocument &document, int ownerObjectIndex, bool cameraCollision,
                                             std::string &message);
[[nodiscard]] bool AlignImportedModelToOwner(BrxDocument &document, int ownerObjectIndex, std::string &message);
