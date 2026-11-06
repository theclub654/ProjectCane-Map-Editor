#include "EditorMcpTools.h"

#include "EditorMcpBridge.h"
#include "EditorWorkspace.h"
#include "EditorOptions.h"
#include "CidAuthoringGuide.h"
#include "SpliceRuntime.h"
#include <algorithm>
#include <bit>

// Inspection uses the same document and descriptor tables as the inspector.
// Array positions are object indices, never native OIDs. Return both explicitly.
namespace EditorMcp
{
std::string Text(const char *value) { return value ? value : ""; }
std::string Utf8Path(const std::filesystem::path &path)
{
    const auto value = path.generic_u8string();
    return value.empty() ? std::string() : std::string(reinterpret_cast<const char *>(value.data()), value.size());
}
std::filesystem::path PathFromUtf8(const std::string &text)
{
    // C++20 path constructors recognize char8_t as UTF-8 on both platforms.
    std::u8string value;
    value.reserve(text.size());
    for (const unsigned char byte : text) value.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(value);
}
Json FloatBits(const float *values, std::size_t count)
{
    Json bits = Json::array();
    for (std::size_t i = 0; i < count; ++i) bits.push_back(std::bit_cast<std::uint32_t>(values[i]));
    return bits;
}
Json Property(const BrxPropertyRecord &p, std::int16_t cid)
{
    Json args = Json::array();
    for (const auto &a : p.arguments)
        args.push_back({{"type", a.type}, {"integer", a.intValue}, {"floats", a.floatValue}, {"float_bits", FloatBits(a.floatValue, 4)}});
    return {{"eopid", p.eopid}, {"name", Text(PropertyNameForCid(cid, p.eopid))},
        {"token", SplicePropertyToken(cid, p.eopid)}, {"kind", PropertyKindName(p.kind)},
        {"flags", p.flags}, {"argument_count", p.argumentCount}, {"editable", p.editable},
        {"modified", p.modified}, {"bool", p.boolValue}, {"integer", p.intValue},
        {"unsigned", p.uintValue}, {"type", p.typeValue}, {"floats", p.floatValue},
        {"float_bits", FloatBits(p.floatValue, 4)}, {"arguments", args}};
}
Json Object(const BrxDocument &doc, std::size_t index, bool full)
{
    const auto &o = doc.Objects().at(index);
    Json result = {{"index", index}, {"oid", o.oid}, {"cid", o.cid},
        {"class", BrxDocument::ClassName(o.cid)}, {"name", doc.ObjectEditorLabel(static_cast<std::int32_t>(index))},
        {"parent_index", o.parentIndex}, {"splice_index", o.spliceIndex},
        {"proxy_clone", o.proxyClone}, {"has_alo_payload", o.hasAloPayload},
        {"zone_mask", o.zoneMask}, {"visibility_mode", o.visibilityMode}, {"zone_mode", o.zoneMode}};
    if (!full) return result;
    result["world_matrix_column_major"] = o.worldTransform;
    result["local_matrix_column_major"] = o.localTransform;
    result["loader"] = o.loaderName;
    result["properties"] = Json::array();
    for (const auto &p : o.properties) result["properties"].push_back(Property(p, o.cid));
    result["details"] = Json::array();
    for (const auto &d : o.details) result["details"].push_back({{"name", d.name}, {"value", d.value}});
    result["children"] = Json::array();
    for (std::size_t i = 0; i < doc.Objects().size(); ++i)
        if (doc.Objects()[i].parentIndex == static_cast<std::int32_t>(index)) result["children"].push_back(i);
    result["shape_paths"] = Json::array();
    for (const auto &p : doc.ShapePaths()) if (p.ownerObjectIndex == static_cast<std::int32_t>(index))
        result["shape_paths"].push_back({{"closed", p.closed}, {"hermite", p.hermite},
            {"coordinate_space", "parent-local"}, {"points", p.points}, {"parameters", p.parameters},
            {"tangents_in", p.tangentsIn}, {"tangents_out", p.tangentsOut}});
    result["effects"] = Json::array();
    for (const auto &e : doc.Effects()) if (e.ownerObjectIndex == static_cast<std::int32_t>(index))
        result["effects"].push_back({{"origin_kind", e.originKind}, {"curve_kind", e.curveKind},
            {"closed", e.curveClosed}, {"colors_are_ranges", e.colorsAreRanges}, {"colors", e.colors},
            {"mesh_point_count", e.meshPositions.size()}, {"curve_point_count", e.curvePoints.size()}});
    return result;
}
Json Inspect(const Json &request, const BrxDocument &doc, EditorWorkspace &workspace)
{
    const auto command = request.at("command").get<std::string>();
    if (command == "cids")
    {
        Json result = Json::array();
        for (std::size_t i = 0; i < kCidAuthoringGuides.size(); ++i)
            result.push_back({{"cid", i}, {"class", BrxDocument::ClassName(static_cast<std::int16_t>(i))},
                {"name", Text(kCidAuthoringGuides[i].name)}});
        return result;
    }
    if (command == "schema")
    {
        const int cid = request.at("cid").get<int>();
        if (cid < 0 || cid >= static_cast<int>(kCidAuthoringGuides.size())) throw std::runtime_error("Invalid CID");
        const auto &g = kCidAuthoringGuides[cid];
        Json properties = Json::array();
        for (const auto id : CidOptionIds()[cid])
        {
            const auto d = EditorOptionDefinitionForCid(static_cast<std::int16_t>(cid), id);
            properties.push_back({{"eopid", id}, {"name", Text(PropertyNameForCid(static_cast<std::int16_t>(cid), id))},
                {"token", SplicePropertyToken(static_cast<std::int16_t>(cid), id)},
                {"kind", PropertyKindName(d.kind)}, {"descriptor_present", d.present}, {"flags", d.flags},
                {"argument_count", d.argumentCount}, {"binding", SplicePropertyBinding(id)},
                {"description", SplicePropertyDetails(id)}, {"group", OptionGroupName(OptionGroup(id))},
                {"behavior", OptionBehaviorName(OptionBehavior(id))},
                {"writable", IsEditorWritableOptionForCid(static_cast<std::int16_t>(cid), id)},
                {"append_semantics", IsAppendSetUserOption(id)}, {"object_reference", IsObjectReferenceOption(id)},
                {"reference_type", Text(ObjectReferenceTypeName(id))}, {"has_runtime_default", d.runtimeDefaultable},
                {"runtime_default_bits", d.runtimeDefaultBits}});
        }
        return {{"cid", cid}, {"name", g.name}, {"next_step", g.nextStep}, {"caution", g.caution},
            {"source", g.source}, {"properties", properties}};
    }
    if (!doc.IsOpen()) throw std::runtime_error("Open a map first");
    if (command == "objects")
    {
        const int start = request.value("offset", 0), limit = request.value("limit", 100);
        if (start < 0 || limit < 1 || limit > 200) throw std::runtime_error("Invalid page; limit is 1..200");
        const auto filter = request.value("filter", std::string());
        const int cid = request.value("cid", -1);
        Json rows = Json::array(); std::size_t matched = 0;
        for (std::size_t i = 0; i < doc.Objects().size(); ++i)
        {
            const auto &o = doc.Objects()[i];
            if ((cid >= 0 && o.cid != cid) || (!filter.empty() && doc.ObjectEditorLabel(static_cast<std::int32_t>(i)).find(filter) == std::string::npos)) continue;
            if (matched++ >= static_cast<std::size_t>(start) && rows.size() < static_cast<std::size_t>(limit)) rows.push_back(Object(doc, i, false));
        }
        return {{"total", matched}, {"offset", start}, {"objects", rows}};
    }
    if (command == "object") return Object(doc, request.at("index").get<std::size_t>(), true);
    if (command == "world")
    {
        Json options = Json::array();
        for (const auto &p : doc.WorldOptions()) options.push_back(Property(p, -1));
        return {{"options", options}, {"object_count", doc.Objects().size()},
            {"collision_triangle_count", doc.CollisionTriangles().size()}, {"glob_count", doc.Globs().size()},
            {"splice_module_count", doc.SpliceModules().size()}, {"shape_count", doc.ShapePaths().size()}};
    }
    if (command == "assets")
    {
        const int start = request.value("offset", 0), limit = request.value("limit", 100);
        if (start < 0 || limit < 1 || limit > 200) throw std::runtime_error("Invalid page");
        Json rows = Json::array(); const auto &assets = workspace.Assets().Assets();
        for (std::size_t i = static_cast<std::size_t>(start); i < assets.size() && rows.size() < static_cast<std::size_t>(limit); ++i)
        {
            const auto &a = assets[i];
            rows.push_back({{"id", std::to_string(a.stableId)}, {"kind", static_cast<int>(a.kind)}, {"name", a.name},
                {"source_index", a.sourceIndex}, {"owner_index", a.ownerObjectIndex}, {"users", a.objectUsers},
                {"width", a.width}, {"height", a.height}, {"primitive_count", a.primitiveCount}});
        }
        return {{"total", assets.size()}, {"assets", rows}};
    }
    if (command == "splice")
    {
        const int index = request.at("index").get<int>();
        if (index < 0 || static_cast<std::size_t>(index) >= doc.SpliceModules().size()) throw std::runtime_error("Invalid module index");
        const auto a = AnalyzeSpliceModule(doc, index);
        return {{"index", index}, {"source", FormatSpliceModuleSource(doc, index)}, {"events", a.eventIds},
            {"imports", a.imports}, {"builtins", a.builtinCalls}, {"property_reads", a.optionReads},
            {"property_writes", a.optionWrites}, {"diagnostics", a.diagnostics}, {"users", SpliceModuleUserCount(doc, index)}};
    }
    if (command == "visibility")
    {
        Json nodes = Json::array();
        for (const auto &n : doc.VisibilityNodes()) nodes.push_back({{"normal", n.normal}, {"distance", n.planeDistance},
            {"negative_child", n.negativeChild}, {"negative_mask", n.negativeZoneMask},
            {"positive_child", n.positiveChild}, {"positive_mask", n.positiveZoneMask}});
        return {{"zone_names", doc.VisibilityZoneNames()}, {"one_hop_masks", doc.VisibilityOneHopMasks()}, {"nodes", nodes}};
    }
    if (command == "validate")
    {
        workspace.Refresh(doc);
        workspace.Validation().Run(doc, workspace.Scene());
        const auto plan = workspace.Compiler().Analyze(doc, workspace.Validation());
        Json issues = Json::array();
        for (const auto &i : workspace.Validation().Issues()) issues.push_back({{"severity", static_cast<int>(i.severity)},
            {"object_index", i.objectIndex}, {"eopid", i.eopid}, {"category", i.category}, {"message", i.message}});
        return {{"issues", issues}, {"export_blockers", plan.blockers}, {"writer_available", plan.writerAvailable},
            {"validation_passed", plan.validationPassed}, {"cid_layouts_audited", plan.cidLayoutsAudited}};
    }
    throw std::runtime_error("Unknown inspection command");
}
}
