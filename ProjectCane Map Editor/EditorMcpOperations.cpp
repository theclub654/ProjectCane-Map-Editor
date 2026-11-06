#include "EditorMcpOperations.h"

#include "EditorMcpTools.h"
#include "EditorOptions.h"
#include "SpliceRuntime.h"
#include "EditorDocumentOperations.h"
#include "EditorProject.h"
#include "ModelImport.h"
#include "TextureImport.h"
#include "PathZoneRuntime.h"
#include "ShapeEditing.h"
#include "SceneWorldPanel.h"
#include "VisibilityMap.h"
#include <cmath>
#include <limits>
#include <memory>

namespace EditorMcp
{
// One discoverable catalog for the companion and native dispatcher. Arguments
// describe native indices explicitly; no arbitrary C++/shell dispatch exists.
Json Operations()
{
    Json rows = Json::array();
    const auto add = [&](const char *name, const char *parameters, const char *handler) {
        rows.push_back({{"name", name}, {"parameters", parameters}, {"native_handler", handler}});
    };
    add("duplicate_hierarchy", "index", "EditorDocumentOperations::DuplicateHierarchy");
    add("delete_hierarchy", "index", "EditorDocumentOperations::DeleteHierarchy");
    add("import_hierarchy", "path (.canehierarchy), parent_index", "EditorProject::LoadHierarchy");
    add("replace_hierarchy", "path (.canehierarchy), index", "EditorProject::ReplaceHierarchy");
    add("import_proxy", "path, parent_index", "EditorProject::LoadPortableProxy");
    add("proxy_source", "index", "EditorDocumentOperations::CreateProxySourceFromHierarchy");
    add("proxy_instance", "proxy_record, root", "EditorDocumentOperations::CreateLinkedProxyInstance");
    add("proxy_unique", "index", "EditorDocumentOperations::MakeProxyRootUnique");
    add("clue_add_frame", "index, source_frame (optional, -1 default)", "EditorDocumentOperations::AddClueBounceFrame");
    add("clue_remove_frame", "index, frame", "EditorDocumentOperations::RemoveClueBounceFrame");
    add("clue_move_frame", "index, frame, direction (-1 or 1)", "EditorDocumentOperations::MoveClueBounceFrame");
    add("clue_collection_helper", "index", "EditorDocumentOperations::CreateClueCollectionHelper");
    add("checkpoint_animation", "index", "EditorDocumentOperations::CreateCheckpointAseg");
    add("barrier_destination", "index", "EditorDocumentOperations::CreateBarrierDestinationPoint");
    add("water_from_object", "index", "EditorDocumentOperations::CreateWaterFromObject");
    add("warp_animation", "index", "EditorDocumentOperations::CreateEmbeddedWarpAseg");
    add("exit_animation", "index", "EditorDocumentOperations::CreateEmbeddedExitAseg");
    add("move_embedded_animation", "index, direction (-1 or 1)", "EditorDocumentOperations::MoveEmbeddedWarpAseg");
    add("pathzone_from_collision", "index, parent_index", "EditorDocumentOperations::CreatePathZoneFromCollision");
    add("pathzone_polygon", "points (world-space vec3 array), parent_index", "EditorDocumentOperations::CreatePathZone");
    add("pathzone_rebuild", "index (PATHZONE owner)", "RebuildPathZoneEdges / RebuildPathZoneBsp");
    add("pathzone_delete_vertex", "index, vertex", "DeletePathZoneVertex");
    add("pathzone_split_edge", "index, edge", "SplitPathZoneBoundaryEdge");
    add("pathzone_project_vertex", "index, vertex", "ProjectPathZoneVertexToCollision");
    add("splice_create", "index", "CreateSpliceModule");
    add("splice_attach", "index, module", "AttachSpliceModule");
    add("splice_detach", "index", "DetachSpliceModule");
    add("splice_unique", "index", "MakeSpliceModuleUnique");
    add("splice_add_event", "index, event", "AddSpliceEventStub");
    add("splice_remove_event", "index, event", "RemoveSpliceEvent");
    add("splice_source", "module, source, cid (optional, -1 default); affects ALL module users", "ApplySpliceModuleSource");
    add("splice_repair_imports", "module", "RepairSpliceImports");
    add("splice_add_import", "module, imported_module", "AddSpliceImport");
    add("model_import", "index (owner), path, scale=100, flip_v=true, materials=true", "ImportStaticModel");
    add("model_replace", "index (owner), path, scale=100, flip_v=true, materials=true", "ReplaceStaticModel");
    add("character_import", "index (owner), path, scale=100, animations=true, sample_rate=60", "ImportFbxCharacter");
    add("model_align", "index (owner)", "AlignImportedModelToOwner");
    add("collision_build", "index (owner), camera=false", "BuildModelCollision");
    add("collision_import", "index (owner), path, scale=100, camera=false", "ImportModelCollision");
    add("collision_repair", "index (owner), camera=false", "RepairModelCollision");
    add("collision_flip", "index (owner), camera=false", "FlipModelCollisionNormals");
    add("texture_import", "path, format (0 RGBA8, 1 BC1, 2 BC3)", "LoadTextureImage / AddBitmap");
    add("texture_replace", "resource_index, path, format (0 RGBA8, 1 BC1, 2 BC3)", "LoadTextureImage; preserve UI replacement metadata");
    add("texture_duplicate", "resource_index", "BrxDocument::DuplicateBitmap");
    add("texture_delete", "resource_index, replacement (-1 clears references)", "BrxDocument::DeleteBitmapAndRemap");
    add("material_duplicate", "resource_index", "BrxDocument::DuplicateShader");
    add("material_delete", "resource_index, replacement (-1 clears references)", "BrxDocument::DeleteShaderAndRemap");
    add("glob_unique", "resource_index", "BrxDocument::MakeGlobUnique");
    add("glob_instance", "resource_index (source record)", "BrxDocument::DuplicateGlobAsInstance");
    add("property_set", "index, eopid, occurrence=0, value; typed int uses {type,value}; supported scalar/vector descriptors only", "EditorOptions descriptor, CanEnableBaseOption, ValidateBaseOptions, SynchronizeDerivedObjectRecord");
    add("property_remove", "index, eopid, occurrence=0", "Ordered property occurrence removal / SynchronizeDerivedObjectRecord");
    add("world_property_set", "eopid, occurrence=0, value (same types as property_set); SW CID 108", "EditorOptions / SynchronizeSceneWorldOptions");
    add("world_property_remove", "eopid, occurrence=0; SW CID 108", "EditorOptions / SynchronizeSceneWorldOptions");
    add("shape_transform", "index (SHAPE owner), matrix (16 column-major affine values, PARENT-LOCAL delta)", "ShapeEditing::Transform");
    add("batch", "operations (1..32 objects with operation and arguments); no nested batches; indices are sequential", "RunOperation on one staged document / one Undo entry");
    add("visibility_single_zone", "no arguments; replaces tree/rows and all ALO/GLOB membership", "VisibilityMap::SingleZone (same helper as studio)");
    add("visibility_membership", "objects (object indices), globs (record indices), zone (0..31), mode (0 add,1 remove,2 replace)", "VisibilityMap::EditMembership (same helper as studio)");
    add("visibility_zone_name", "zone, name (editor-only)", "BrxDocument::EditableVisibilityZoneNames");
    add("visibility_one_hop", "zone (row index), mask (full U32)", "Existing retail one-hop row storage / VisibilityMap queries");
    return rows;
}

int Integer(const Json &args, const char *key, int minimum, int maximum)
{
    const auto &v = args.at(key);
    if (!v.is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
    if (v.is_number_unsigned() && v.get<std::uint64_t>() > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) throw std::runtime_error(std::string(key) + " out of range");
    const auto n = v.get<std::int64_t>();
    if (n < minimum || n > maximum) throw std::runtime_error(std::string(key) + " out of range");
    return static_cast<int>(n);
}
float Number(const Json &v)
{
    if (!v.is_number()) throw std::runtime_error("Expected a number");
    const float f = v.get<float>();
    if (!std::isfinite(f)) throw std::runtime_error("Expected a finite float");
    return f;
}
std::uint32_t Mask(const Json &value)
{
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) || value.get<std::uint64_t>() > 0xffffffffull) throw std::runtime_error("Expected U32 mask");
    return value.get<std::uint32_t>();
}
std::filesystem::path InputPath(const Json &args)
{
    const auto path = PathFromUtf8(args.at("path").get<std::string>());
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path)) throw std::runtime_error("An existing absolute input file path is required");
    return path;
}
int Owner(const Json &a, const BrxDocument &doc)
{
    const int index = Integer(a, "index");
    if (static_cast<std::size_t>(index) >= doc.Objects().size()) throw std::runtime_error("Object does not exist");
    if (doc.Objects()[index].proxyClone) throw std::runtime_error("Resolved PROXY clones cannot be edited directly");
    return index;
}
inline int Parent(const Json &a, const BrxDocument &doc)
{
    const int index = Integer(a, "parent_index", -1);
    if (index >= 0 && (static_cast<std::size_t>(index) >= doc.Objects().size() || doc.Objects()[index].proxyClone)) throw std::runtime_error("Invalid destination parent");
    return index;
}
inline Json Result(const EditorOperationResult &r)
{
    if (!r.succeeded) throw std::runtime_error(r.message);
    return {{"object_index", r.primaryObjectIndex}, {"affected_indices", r.affectedObjectIndices}, {"message", r.message}};
}
inline Json Result(const SpliceAuthoringResult &r)
{
    if (!r.succeeded) throw std::runtime_error(r.message);
    return {{"module", r.moduleIndex}, {"message", r.message}};
}

inline void EditProperty(BrxDocument &doc, const Json &a, bool remove, bool world = false)
{
    const int index = world ? -1 : Owner(a, doc), id = Integer(a, "eopid", 0, 1178);
    BrxObjectRecord worldRecord;
    if (world)
    {
        for (const auto &option : doc.WorldOptions())
            if (option.eopid >= 526 && option.eopid <= 536 && option.kind == BrxPropertyKind::Dynamic)
                throw std::runtime_error("World appearance contains an expression; inspect it in the specialized editor before changing world options");
        worldRecord.cid = 108; worldRecord.properties = doc.WorldOptions();
    }
    auto &object = world ? worldRecord : doc.EditableObjects()[index];
    const auto &legal = CidOptionIds().at(static_cast<std::size_t>(object.cid));
    if (std::find(legal.begin(), legal.end(), id) == legal.end() || !IsEditorWritableOptionForCid(object.cid, static_cast<std::int16_t>(id))) throw std::runtime_error("Property is not writable for this CID");
    if (OptionGroup(static_cast<std::int16_t>(id)) == EditorOptionGroup::Transform) throw std::runtime_error("Use the transform operation for transform settings");
    const int occurrence = a.contains("occurrence") ? Integer(a, "occurrence") : 0;
    int seen = 0;
    auto &values = object.properties;
    auto found = std::find_if(values.begin(), values.end(), [&](const auto &p) { return p.eopid == id && seen++ == occurrence; });
    if (found != values.end() && !found->editable) throw std::runtime_error("Serialized property is read-only");
    const auto oldIssues = ValidateBaseOptions(object.cid, values);
    if (remove)
    {
        if (found == values.end()) throw std::runtime_error("Property occurrence does not exist");
        values.erase(found);
    }
    else
    {
        auto other = values;
        if (found != values.end()) other.erase(other.begin() + std::distance(values.begin(), found));
        std::string reason;
        if (!CanEnableBaseOption(object.cid, other, static_cast<std::int16_t>(id), &reason)) throw std::runtime_error(reason);
        const auto definition = EditorOptionDefinitionForCid(object.cid, static_cast<std::int16_t>(id));
        if (!definition.present) throw std::runtime_error("Missing descriptor");
        BrxPropertyRecord p;
        if (found != values.end())
        {
            p = *found;
            if (p.kind != definition.kind) throw std::runtime_error("Stored property kind differs from its native descriptor; use specialized repair tools");
        }
        else
        {
            if (occurrence != seen) throw std::runtime_error("Occurrence must identify an existing value or the next append position");
            if (seen > 0 && !IsAppendSetUserOption(static_cast<std::int16_t>(id))) throw std::runtime_error("This descriptor does not append");
            p.eopid = static_cast<std::int16_t>(id); p.kind = definition.kind;
            p.flags = definition.flags; p.argumentCount = definition.argumentCount;
            ResetPropertyValue(p);
        }
        const auto &value = a.at("value");
        switch (p.kind)
        {
        case BrxPropertyKind::Bool:
            if (!value.is_boolean()) throw std::runtime_error("Expected boolean");
            p.boolValue = value.get<bool>(); break;
        case BrxPropertyKind::Float: p.floatValue[0] = Number(value); break;
        case BrxPropertyKind::Float2:
        case BrxPropertyKind::Vector3:
        case BrxPropertyKind::Vector4:
        {
            const std::size_t count = p.kind == BrxPropertyKind::Float2 ? 2 : p.kind == BrxPropertyKind::Vector3 ? 3 : 4;
            if (!value.is_array() || value.size() != count) throw std::runtime_error("Incorrect vector component count");
            for (std::size_t i = 0; i < count; ++i) p.floatValue[i] = Number(value[i]);
            break;
        }
        case BrxPropertyKind::Int32: p.intValue = Integer(a, "value", (std::numeric_limits<int>::min)()); break;
        case BrxPropertyKind::Int16: p.intValue = Integer(a, "value", -32768, 32767); break;
        case BrxPropertyKind::TypedInt16:
            if (Integer(value, "type", -32768, 32767) != p.typeValue) throw std::runtime_error("Preserve the native typed-value tag from inspection");
            p.intValue = Integer(value, "value", -32768, 32767); break;
        case BrxPropertyKind::UInt32:
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0 || value.get<std::uint64_t>() > 0xffffffffull) throw std::runtime_error("Expected U32");
            p.uintValue = value.get<std::uint32_t>(); break;
        default: throw std::runtime_error("Dynamic descriptors require their specialized native authoring operation");
        }
        if (IsObjectReferenceOption(p.eopid) && p.intValue != -1)
        {
            const bool accepts = std::any_of(doc.Objects().begin(), doc.Objects().end(), [&](const auto &o) {
                return o.oid == p.intValue && ObjectReferenceAccepts(p.eopid, o.cid);
            });
            if (!accepts) throw std::runtime_error("No compatible object has that native OID");
        }
        p.modified = true;
        if (found != values.end()) *found = std::move(p); else values.push_back(std::move(p));
    }
    for (const auto &issue : ValidateBaseOptions(object.cid, values))
        if (issue.severity == EditorOptionIssueSeverity::Error && std::none_of(oldIssues.begin(), oldIssues.end(), [&](const auto &old) {
            return old.severity == issue.severity && old.option == issue.option && old.relatedOption == issue.relatedOption && old.message == issue.message;
        })) throw std::runtime_error(issue.message);
    if (world)
    {
        doc.EditableWorldOptions() = std::move(worldRecord.properties);
        SynchronizeSceneWorldOptions(doc);
    }
    else
    {
        // Keep the same decoded fields updated by the inspector. Upload reads
        // these caches; changing only the option stream leaves a stale preview.
        if (id == 125)
        {
            object.hidden = false; // InitAlo default; replay ordered overrides.
            for (const auto &p : values)
                if (p.eopid == 125 && p.kind == BrxPropertyKind::Bool) object.hidden = p.boolValue;
        }
        if (id == 24 || id == 25)
            for (const auto &p : values)
                if (p.eopid == id && p.kind == BrxPropertyKind::Float)
                {
                    if (id == 24) { object.maximumRenderDistance = p.floatValue[0]; object.maximumRenderDistanceDefault = false; }
                    else { object.celBorderMaximumDistance = p.floatValue[0]; object.celBorderMaximumDistanceDefault = false; }
                }
        if (id == 366 || id == 421)
        {
            std::int16_t origin = 0; // LoadEmitter/LoadExplo default point origin.
            for (const auto &p : values)
                if (p.eopid == id && p.kind == BrxPropertyKind::Int16) origin = static_cast<std::int16_t>(p.intValue);
            for (auto &effect : doc.EditableEffects())
                if (effect.ownerObjectIndex == index) effect.originKind = origin;
        }
        doc.SynchronizeDerivedObjectRecord(index);
    }
}

Json RunOperation(BrxDocument &doc, const std::string &name, const Json &a)
{
    if (!doc.IsOpen()) throw std::runtime_error("Open a map first");
    const auto owner = [&]() { return Owner(a, doc); };
    const auto parent = [&]() { return Parent(a, doc); };
    const auto direction = [&]() { const int d = Integer(a, "direction", -1, 1); if (d == 0) throw std::runtime_error("direction must be -1 or 1"); return d; };
    if (name == "visibility_single_zone" || name == "visibility_membership" || name == "visibility_zone_name" || name == "visibility_one_hop")
    {
        if (std::none_of(doc.Objects().begin(), doc.Objects().end(), [](const auto &o) { return o.cid == 134; })) throw std::runtime_error("Map has no native VISMAP singleton");
        if (name == "visibility_single_zone") { VisibilityMap::SingleZone(doc); return {{"zones", 1}}; }
        const int zone = Integer(a, "zone", 0, 31);
        if (zone >= doc.WorldHeader().visibilityZoneCount) throw std::runtime_error("Zone is outside the declared zone count");
        if (name == "visibility_zone_name")
        {
            const auto text = a.at("name").get<std::string>();
            if (text.size() > 256) throw std::runtime_error("Zone name exceeds 256 bytes");
            doc.EditableVisibilityZoneNames().resize(doc.WorldHeader().visibilityZoneCount);
            doc.EditableVisibilityZoneNames()[zone] = text;
            return {{"zone", zone}, {"name", text}};
        }
        if (name == "visibility_one_hop")
        {
            doc.EditableVisibilityOneHopMasks().at(zone) = Mask(a.at("mask"));
            return {{"zone", zone}, {"mask", doc.VisibilityOneHopMasks()[zone]}};
        }
        std::vector<std::int32_t> objects; std::vector<std::size_t> globs;
        for (const auto &value : a.at("objects")) objects.push_back(Owner({{"index", value}}, doc));
        for (const auto &value : a.at("globs"))
        {
            const int index = Integer({{"index", value}}, "index");
            const auto &glob = doc.Globs().at(index);
            if (glob.ownerObjectIndex >= 0 && doc.Objects().at(glob.ownerObjectIndex).proxyClone) throw std::runtime_error("Edit the source of a PROXY model instead");
            globs.push_back(index);
        }
        const auto [changedObjects, changedModels] = VisibilityMap::EditMembership(doc, objects, globs, zone, Integer(a, "mode", 0, 2));
        return {{"objects", changedObjects}, {"models", changedModels}};
    }
    if (name == "batch")
    {
        const auto &operations = a.at("operations");
        if (!operations.is_array() || operations.empty() || operations.size() > 32) throw std::runtime_error("Batch must contain 1..32 operations");
        Json results = Json::array();
        for (const auto &operation : operations)
        {
            const auto child = operation.at("operation").get<std::string>();
            if (child == "batch") throw std::runtime_error("Nested batches are not supported");
            results.push_back(RunOperation(doc, child, operation.at("arguments")));
        }
        return {{"results", results}};
    }
    if (name == "shape_transform")
    {
        const int index = owner();
        auto &paths = doc.EditableShapePaths();
        auto path = std::find_if(paths.begin(), paths.end(), [&](const auto &p) { return p.ownerObjectIndex == index; });
        if (path == paths.end()) throw std::runtime_error("Object has no SHAPE payload");
        const auto &values = a.at("matrix");
        if (!values.is_array() || values.size() != 16) throw std::runtime_error("Expected 16 matrix values");
        glm::mat4 matrix(1.0f);
        for (int i = 0; i < 16; ++i) matrix[i / 4][i % 4] = Number(values[i]);
        if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1) throw std::runtime_error("Expected an affine delta");
        if (!ShapeEditing::Transform(*path, matrix)) throw std::runtime_error("SHAPE transform rejected");
        return {{"object_index", index}};
    }
    if (name == "duplicate_hierarchy") return Result(EditorDocumentOperations::DuplicateHierarchy(doc, owner()));
    if (name == "delete_hierarchy") return Result(EditorDocumentOperations::DeleteHierarchy(doc, owner()));
    if (name == "proxy_source") return Result(EditorDocumentOperations::CreateProxySourceFromHierarchy(doc, owner()));
    if (name == "proxy_unique") return Result(EditorDocumentOperations::MakeProxyRootUnique(doc, owner()));
    if (name == "proxy_instance") return Result(EditorDocumentOperations::CreateLinkedProxyInstance(doc, Integer(a, "proxy_record"), Integer(a, "root")));
    if (name == "clue_add_frame") return Result(EditorDocumentOperations::AddClueBounceFrame(doc, owner(), a.contains("source_frame") ? Integer(a, "source_frame", -1) : -1));
    if (name == "clue_remove_frame") return Result(EditorDocumentOperations::RemoveClueBounceFrame(doc, owner(), Integer(a, "frame")));
    if (name == "clue_move_frame") return Result(EditorDocumentOperations::MoveClueBounceFrame(doc, owner(), Integer(a, "frame"), direction()));
    if (name == "clue_collection_helper") return Result(EditorDocumentOperations::CreateClueCollectionHelper(doc, owner()));
    if (name == "checkpoint_animation") return Result(EditorDocumentOperations::CreateCheckpointAseg(doc, owner()));
    if (name == "barrier_destination") return Result(EditorDocumentOperations::CreateBarrierDestinationPoint(doc, owner()));
    if (name == "water_from_object") return Result(EditorDocumentOperations::CreateWaterFromObject(doc, owner()));
    if (name == "warp_animation") return Result(EditorDocumentOperations::CreateEmbeddedWarpAseg(doc, owner()));
    if (name == "exit_animation") return Result(EditorDocumentOperations::CreateEmbeddedExitAseg(doc, owner()));
    if (name == "move_embedded_animation") return Result(EditorDocumentOperations::MoveEmbeddedWarpAseg(doc, owner(), direction()));
    if (name == "pathzone_from_collision") return Result(EditorDocumentOperations::CreatePathZoneFromCollision(doc, owner(), parent()));
    if (name == "pathzone_polygon")
    {
        const auto &input = a.at("points");
        if (!input.is_array() || input.size() > 65535) throw std::runtime_error("Expected at most 65535 polygon points");
        std::vector<std::array<float, 3>> points;
        for (const auto &p : input)
        {
            if (!p.is_array() || p.size() != 3) throw std::runtime_error("Each polygon point must have exactly three components");
            points.push_back({Number(p[0]), Number(p[1]), Number(p[2])});
        }
        return Result(EditorDocumentOperations::CreatePathZone(doc, points, parent()));
    }
    if (name.starts_with("pathzone_"))
    {
        const int index = owner();
        auto &zones = doc.EditablePathZones();
        auto found = std::find_if(zones.begin(), zones.end(), [&](const auto &z) { return z.ownerObjectIndex == index; });
        if (found == zones.end()) throw std::runtime_error("Object has no PATHZONE payload");
        bool ok = false;
        if (name == "pathzone_rebuild") { RebuildPathZoneEdges(*found); ok = RebuildPathZoneBsp(*found); }
        else if (name == "pathzone_delete_vertex") ok = DeletePathZoneVertex(*found, Integer(a, "vertex"));
        else if (name == "pathzone_split_edge") ok = SplitPathZoneBoundaryEdge(*found, Integer(a, "edge"));
        else if (name == "pathzone_project_vertex") ok = ProjectPathZoneVertexToCollision(*found, Integer(a, "vertex"), doc);
        if (!ok) throw std::runtime_error("PATHZONE operation rejected; no live data changed");
        return {{"object_index", index}, {"diagnostics", AuditPathZone(*found)}};
    }
    if (name == "import_hierarchy" || name == "replace_hierarchy" || name == "import_proxy")
    {
        int root = -1; std::string error; const auto path = InputPath(a);
        const bool ok = name == "import_hierarchy" ? EditorProject::LoadHierarchy(path, doc, parent(), root, error)
            : name == "replace_hierarchy" ? EditorProject::ReplaceHierarchy(path, doc, owner(), root, error)
            : EditorProject::LoadPortableProxy(path, doc, parent(), root, error);
        if (!ok) throw std::runtime_error(error);
        return {{"object_index", root}};
    }
    if (name == "splice_create") return Result(CreateSpliceModule(doc, owner()));
    if (name == "splice_attach") return Result(AttachSpliceModule(doc, owner(), Integer(a, "module")));
    if (name == "splice_detach") return Result(DetachSpliceModule(doc, owner()));
    if (name == "splice_unique") return Result(MakeSpliceModuleUnique(doc, owner()));
    if (name == "splice_add_event") return Result(AddSpliceEventStub(doc, owner(), static_cast<std::uint32_t>(Integer(a, "event"))));
    if (name == "splice_remove_event") return Result(RemoveSpliceEvent(doc, owner(), static_cast<std::uint32_t>(Integer(a, "event"))));
    if (name == "splice_repair_imports") return Result(RepairSpliceImports(doc, Integer(a, "module")));
    if (name == "splice_add_import") return Result(AddSpliceImport(doc, Integer(a, "module"), Integer(a, "imported_module")));
    if (name == "splice_source")
    {
        const int module = Integer(a, "module");
        const int cid = a.contains("cid") ? Integer(a, "cid", -1, 147) : -1;
        const auto result = ApplySpliceModuleSource(doc, module, a.at("source").get<std::string>(), static_cast<std::int16_t>(cid));
        if (!result.succeeded) throw std::runtime_error(result.diagnostics.empty() ? "Splice source rejected" : result.diagnostics.front().message);
        return {{"module", module}, {"shared_users", SpliceModuleUserCount(doc, module)}, {"nodes", result.nodeCount}};
    }
    if (name == "model_import" || name == "model_replace" || name == "character_import" || name == "collision_import")
    {
        const int index = owner(); const auto path = InputPath(a);
        ModelImportOptions options;
        if (a.contains("scale")) options.scale = Number(a.at("scale"));
        options.flipV = a.value("flip_v", true); options.importMaterials = a.value("materials", true);
        if (name == "collision_import")
        {
            std::string error;
            if (!ImportModelCollision(path, doc, index, options, a.value("camera", false), error)) throw std::runtime_error(error);
            return {{"object_index", index}, {"message", error}};
        }
        FbxCharacterImportOptions character;
        character.model = options; character.importAnimations = a.value("animations", true);
        if (a.contains("sample_rate")) character.sampleRate = Number(a.at("sample_rate"));
        if (character.sampleRate <= 0) throw std::runtime_error("sample_rate must be positive");
        const auto result = name == "model_import" ? ImportStaticModel(path, doc, index, options)
            : name == "model_replace" ? ReplaceStaticModel(path, doc, index, options) : ImportFbxCharacter(path, doc, index, character);
        if (!result.succeeded) throw std::runtime_error(result.message);
        return {{"object_index", index}, {"glob", result.globIndex}, {"triangles", result.triangleCount}, {"message", result.message}};
    }
    if (name == "model_align" || name == "collision_build" || name == "collision_repair" || name == "collision_flip")
    {
        const int index = owner(); std::string error; const bool camera = a.value("camera", false);
        const bool ok = name == "model_align" ? AlignImportedModelToOwner(doc, index, error)
            : name == "collision_build" ? BuildModelCollision(doc, index, camera, error)
            : name == "collision_repair" ? RepairModelCollision(doc, index, camera, error) : FlipModelCollisionNormals(doc, index, camera, error);
        if (!ok) throw std::runtime_error(error);
        return {{"object_index", index}, {"message", error}};
    }
    if (name == "texture_import" || name == "texture_replace")
    {
        BrxBitmapRecord replacement; std::string error;
        if (!LoadTextureImage(InputPath(a), static_cast<TextureImportFormat>(Integer(a, "format", 0, 2)), replacement, error)) throw std::runtime_error(error);
        if (name == "texture_import") return {{"resource_index", doc.AddBitmap(std::move(replacement))}};
        const int index = Integer(a, "resource_index"); auto &bitmap = doc.EditableBitmaps().at(index);
        replacement.editorResourceId = bitmap.editorResourceId;
        replacement.editorContentRevision = bitmap.editorContentRevision + 1u;
        replacement.editorName = bitmap.editorName;
        replacement.zoneFlags = bitmap.zoneFlags; replacement.textureFlags = bitmap.textureFlags;
        bitmap = std::move(replacement);
        return {{"resource_index", index}};
    }
    if (name == "texture_duplicate" || name == "material_duplicate")
    {
        const int index = Integer(a, "resource_index");
        if (name == "texture_duplicate") { doc.Bitmaps().at(index); return {{"resource_index", doc.DuplicateBitmap(index)}}; }
        doc.Shaders().at(index); return {{"resource_index", doc.DuplicateShader(index)}};
    }
    if (name == "texture_delete" || name == "material_delete")
    {
        const int index = Integer(a, "resource_index"), replacement = Integer(a, "replacement", -1);
        const bool ok = name == "texture_delete" ? doc.DeleteBitmapAndRemap(index, replacement) : doc.DeleteShaderAndRemap(index, replacement);
        if (!ok) throw std::runtime_error("Resource deletion/remap rejected");
        return {{"removed_index", index}};
    }
    if (name == "glob_unique" || name == "glob_instance")
    {
        const int index = Integer(a, "resource_index"); doc.Globs().at(index);
        if (name == "glob_instance")
        {
            const auto instance = doc.DuplicateGlobAsInstance(index);
            if (instance == static_cast<std::size_t>(-1)) throw std::runtime_error("GLOB instance creation rejected");
            return {{"resource_index", instance}};
        }
        std::string error;
        if (!doc.MakeGlobUnique(index, &error)) throw std::runtime_error(error);
        return {{"resource_index", index}};
    }
    if (name == "property_set" || name == "property_remove")
    {
        EditProperty(doc, a, name == "property_remove");
        return {{"object_index", owner()}};
    }
    if (name == "world_property_set" || name == "world_property_remove")
    {
        EditProperty(doc, a, name == "world_property_remove", true);
        return {{"world_updated", true}};
    }
    throw std::runtime_error("Unknown operation; use list_operations");
}

Json PreviewChanges(const BrxDocument &before, const BrxDocument &after)
{
    const auto summarize = [](const BrxDocument &doc, std::size_t index) -> Json {
        if (index >= doc.Objects().size()) return nullptr;
        const auto &object = doc.Objects()[index];
        auto row = Object(doc, index, false);
        row["world_matrix_column_major"] = object.worldTransform;
        row["properties"] = Json::array();
        for (const auto &p : object.properties) row["properties"].push_back(Property(p, object.cid));
        return row;
    };
    Json changes = Json::array(); std::size_t count = 0;
    for (std::size_t i = 0; i < (std::max)(before.Objects().size(), after.Objects().size()); ++i)
    {
        auto old = summarize(before, i), current = summarize(after, i);
        const auto oldText = old.dump(-1, ' ', false, Json::error_handler_t::replace);
        const auto currentText = current.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (oldText == currentText) continue;
        ++count;
        if (changes.size() < 20)
        {
            if (oldText.size() + currentText.size() > 16384)
            {
                for (auto *row : {&old, &current}) if (row->is_object())
                {
                    (*row)["property_count"] = row->at("properties").size();
                    row->erase("properties"); (*row)["properties_omitted_from_summary"] = true;
                }
            }
            changes.push_back({{"index", i}, {"before", std::move(old)}, {"after", std::move(current)}});
        }
    }
    return {{"object_rows_changed", count}, {"first_object_changes", changes},
        {"objects", {before.Objects().size(), after.Objects().size()}}, {"globs", {before.Globs().size(), after.Globs().size()}},
        {"bitmaps", {before.Bitmaps().size(), after.Bitmaps().size()}}, {"shaders", {before.Shaders().size(), after.Shaders().size()}},
        {"splice_modules", {before.SpliceModules().size(), after.SpliceModules().size()}},
        {"note", "Hierarchy edits may remap many row indices. Specialized payload/resource edits may keep these counts unchanged; inspect the staged record before applying."}};
}
}
