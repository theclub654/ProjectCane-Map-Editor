#include "AssetDatabase.h"

#include "BrxDocument.h"
#include "PrefabLibrary.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace
{
std::uint64_t StableId(const EditorAssetKind kind, const std::uint64_t source, const std::int32_t owner)
{
    std::uint64_t hash = 1469598103934665603ull;
    hash ^= static_cast<std::uint8_t>(kind);
    hash *= 1099511628211ull;
    hash ^= source;
    hash *= 1099511628211ull;
    hash ^= static_cast<std::uint32_t>(owner);
    hash *= 1099511628211ull;
    return hash;
}

std::uint64_t StableStringSource(const std::string &value)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char character : value)
    {
        hash ^= character;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string ObjectName(const BrxDocument &document, const std::int32_t owner)
{
    if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
        return "Unowned";
    const auto &object = document.Objects()[static_cast<std::size_t>(owner)];
    if (!object.editorName.empty())
        return object.editorName;
    const char *name = BrxDocument::ClassName(object.cid);
    return std::string(name ? name : "Object") + " OID " + std::to_string(object.oid);
}
}

void AssetDatabase::Rebuild(const BrxDocument &document)
{
    assets_.clear();
    for (std::size_t index = 0; index < document.Globsets().size(); ++index)
    {
        const auto owner = document.Globsets()[index].ownerObjectIndex;
        EditorAssetRecord asset{StableId(EditorAssetKind::Model, index, owner), EditorAssetKind::Model,
                                ObjectName(document, owner), static_cast<std::int32_t>(index), owner};
        if (owner >= 0)
            asset.objectUsers.push_back(owner);
        for (const auto globIndex : document.Globsets()[index].globIndices)
            if (globIndex < document.Globs().size())
                for (const auto &batch : document.Globs()[globIndex].derivedBatches)
                    asset.primitiveCount += batch.indexCount / 3;
        assets_.push_back(std::move(asset));
    }
    for (std::size_t index = 0; index < document.Bitmaps().size(); ++index)
    {
        const auto &bitmap = document.Bitmaps()[index];
        EditorAssetRecord asset{
            bitmap.editorResourceId != 0 ? bitmap.editorResourceId : StableId(EditorAssetKind::Texture, index, -1),
            EditorAssetKind::Texture, bitmap.editorName.empty() ? "Bitmap " + std::to_string(index) : bitmap.editorName,
            static_cast<std::int32_t>(index), -1};
        asset.width = bitmap.width;
        asset.height = bitmap.height;
        for (const auto &glob : document.Globs())
            for (const auto &batch : glob.derivedBatches)
            {
                if (batch.shaderIndex >= document.Shaders().size())
                    continue;
                const auto &shader = document.Shaders()[batch.shaderIndex];
                const auto uses = [&](const std::vector<std::int32_t> &stages)
                { return std::find(stages.begin(), stages.end(), static_cast<std::int32_t>(index)) != stages.end(); };
                bool referenced = uses(shader.ambientBitmapIndices) || uses(shader.diffuseBitmapIndices) ||
                                  uses(shader.saturateBitmapIndices);
                for (const auto &stage : shader.textureStages)
                    referenced = referenced || uses(stage.ambientBitmapIndices) || uses(stage.diffuseBitmapIndices) ||
                                 uses(stage.saturateBitmapIndices);
                if (referenced && glob.ownerObjectIndex >= 0 &&
                    std::find(asset.objectUsers.begin(), asset.objectUsers.end(), glob.ownerObjectIndex) ==
                        asset.objectUsers.end())
                    asset.objectUsers.push_back(glob.ownerObjectIndex);
            }
        // Font atlases are resources too even though they have no owning world
        // object. Keep them out of the unused-resource cleanup path.
        if (std::any_of(document.Fonts().begin(), document.Fonts().end(), [index](const BrxFontRecord &font)
                        { return font.bitmapIndex == static_cast<std::int32_t>(index); }))
            asset.ownerObjectIndex = -2;
        assets_.push_back(std::move(asset));
    }
    for (std::size_t index = 0; index < document.Shaders().size(); ++index)
    {
        const auto &shader = document.Shaders()[index];
        EditorAssetRecord asset{shader.editorResourceId != 0 ? shader.editorResourceId
                                                             : StableId(EditorAssetKind::Material, index, -1),
                                EditorAssetKind::Material,
                                shader.editorName.empty() ? "Shader " + std::to_string(index) : shader.editorName,
                                static_cast<std::int32_t>(index), -1};
        for (const auto &glob : document.Globs())
        {
            for (const auto &subglob : glob.sourceSubglobs)
                if (subglob.shaderIndex == index && glob.ownerObjectIndex >= 0 &&
                    std::find(asset.objectUsers.begin(), asset.objectUsers.end(), glob.ownerObjectIndex) ==
                        asset.objectUsers.end())
                    asset.objectUsers.push_back(glob.ownerObjectIndex);
            for (const auto &batch : glob.derivedBatches)
                if (batch.shaderIndex == index && glob.ownerObjectIndex >= 0 &&
                    std::find(asset.objectUsers.begin(), asset.objectUsers.end(), glob.ownerObjectIndex) ==
                        asset.objectUsers.end())
                    asset.objectUsers.push_back(glob.ownerObjectIndex);
        }
        assets_.push_back(std::move(asset));
    }
    for (std::size_t index = 0; index < document.AnimationSegments().size(); ++index)
    {
        const auto owner = document.AnimationSegments()[index].ownerObjectIndex;
        EditorAssetRecord asset{StableId(EditorAssetKind::Animation, index, owner), EditorAssetKind::Animation,
                                "ASEG " + std::to_string(index), static_cast<std::int32_t>(index), owner};
        if (owner >= 0)
            asset.objectUsers.push_back(owner);
        assets_.push_back(std::move(asset));
    }
    for (std::size_t index = 0; index < document.Effects().size(); ++index)
    {
        const auto owner = document.Effects()[index].ownerObjectIndex;
        EditorAssetRecord asset{StableId(EditorAssetKind::Effect, index, owner), EditorAssetKind::Effect,
                                ObjectName(document, owner), static_cast<std::int32_t>(index), owner};
        if (owner >= 0)
            asset.objectUsers.push_back(owner);
        assets_.push_back(std::move(asset));
    }

    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        if (!object.so.present ||
            (object.so.collisionGeometry.surfaces.empty() && object.so.cameraGeometry.surfaces.empty()))
            continue;
        EditorAssetRecord asset{
            StableId(EditorAssetKind::Collision, objectIndex, static_cast<std::int32_t>(objectIndex)),
            EditorAssetKind::Collision, ObjectName(document, static_cast<std::int32_t>(objectIndex)) + " Collision",
            static_cast<std::int32_t>(objectIndex), static_cast<std::int32_t>(objectIndex)};
        asset.objectUsers.push_back(static_cast<std::int32_t>(objectIndex));
        asset.primitiveCount = object.so.collisionGeometry.surfaces.size() + object.so.cameraGeometry.surfaces.size();
        assets_.push_back(std::move(asset));
    }

    // Audio is referenced by resource name rather than by an indexed BRX
    // payload. Catalog each unique world/dialog resource and retain every
    // object that uses it; this mirrors the game's named BNK/MUS lookup.
    std::unordered_map<std::string, std::size_t> audioByName;
    const auto addAudio = [&](const std::string &name, const std::int32_t owner)
    {
        if (name.empty())
            return;
        const auto [found, inserted] = audioByName.emplace(name, assets_.size());
        if (inserted)
        {
            EditorAssetRecord asset{StableId(EditorAssetKind::Audio, StableStringSource(name), -1),
                                    EditorAssetKind::Audio, name, -1, owner};
            if (owner >= 0)
                asset.objectUsers.push_back(owner);
            assets_.push_back(std::move(asset));
            return;
        }
        auto &users = assets_[found->second].objectUsers;
        if (owner >= 0 && std::find(users.begin(), users.end(), owner) == users.end())
            users.push_back(owner);
    };
    addAudio(document.WorldHeader().soundBankFile, -1);
    addAudio(document.WorldHeader().commentaryAudioFile, -1);
    addAudio(document.WorldHeader().musicFile, -1);
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &dialog = document.Objects()[objectIndex].dialog;
        if (!dialog.present)
            continue;
        for (const auto &event : dialog.events)
            if (event.kind == BrxDialogEventKind::PreloadAudio || event.kind == BrxDialogEventKind::Talk)
                addAudio(event.resourceName, static_cast<std::int32_t>(objectIndex));
    }
}

const EditorAssetRecord *AssetDatabase::FindById(const std::uint64_t stableId) const
{
    const auto found = std::find_if(assets_.begin(), assets_.end(),
                                    [&](const EditorAssetRecord &asset) { return asset.stableId == stableId; });
    return found == assets_.end() ? nullptr : &*found;
}

void AssetDatabase::Clear()
{
    assets_.clear();
}

void AssetDatabase::SyncPrefabs(const PrefabLibrary &prefabs)
{
    assets_.erase(std::remove_if(assets_.begin(), assets_.end(),
                                 [](const EditorAssetRecord &asset) { return asset.kind == EditorAssetKind::Prefab; }),
                  assets_.end());
    for (std::size_t index = 0; index < prefabs.Prefabs().size(); ++index)
    {
        const auto &prefab = prefabs.Prefabs()[index];
        EditorAssetRecord asset{prefab.stableId, EditorAssetKind::Prefab, prefab.name, static_cast<std::int32_t>(index),
                                -1};
        asset.primitiveCount = prefab.objects.size();
        assets_.push_back(std::move(asset));
    }
}

std::vector<const EditorAssetRecord *> AssetDatabase::Find(const EditorAssetKind kind, const std::string &filter) const
{
    std::string lowered = filter;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::vector<const EditorAssetRecord *> result;
    for (const auto &asset : assets_)
    {
        if (asset.kind != kind)
            continue;
        std::string name = asset.name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (lowered.empty() || name.find(lowered) != std::string::npos)
            result.push_back(&asset);
    }
    return result;
}
