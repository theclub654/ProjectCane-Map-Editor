#include "ValidationReport.h"
#include "PathZoneRuntime.h"

#include "BrxDocument.h"
#include "GuardAuthoring.h"
#include "EditorOptions.h"
#include "ProxyRuntime.h"
#include "SpliceRuntime.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
void WriteProperty(std::ostream &out, const BrxPropertyRecord &property)
{
    out << "    EOPID " << property.eopid << " (" << PropertyKindName(property.kind) << ") = ";
    switch (property.kind)
    {
    case BrxPropertyKind::Bool:
        out << (property.boolValue ? "true" : "false");
        break;
    case BrxPropertyKind::Float:
        out << property.floatValue[0];
        break;
    case BrxPropertyKind::Vector3:
        out << property.floatValue[0] << ", " << property.floatValue[1] << ", " << property.floatValue[2];
        break;
    case BrxPropertyKind::Vector4:
        out << property.floatValue[0] << ", " << property.floatValue[1] << ", " << property.floatValue[2] << ", "
            << property.floatValue[3];
        break;
    case BrxPropertyKind::Float2:
        out << property.floatValue[0] << ", " << property.floatValue[1];
        break;
    case BrxPropertyKind::UInt32:
        out << property.uintValue;
        break;
    case BrxPropertyKind::TypedInt16:
        out << property.typeValue << ":" << property.intValue;
        break;
    case BrxPropertyKind::Dynamic:
        out << "dynamic/expression";
        break;
    default:
        out << property.intValue;
        break;
    }
    if (property.modified)
        out << " [modified]";
    out << '\n';
}
}

std::string BuildLevelDiagnosticDump(const BrxDocument &document)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    const auto &header = document.WorldHeader();
    out << "ProjectCane Map Editor diagnostic dump\n"
        << "Source: " << document.Path().string() << '\n'
        << "Container version: " << document.Version() << '\n'
        << "Payload version: " << header.version << '\n'
        << "File bytes: " << document.FileSize() << '\n'
        << "Objects: " << document.Objects().size() << '\n'
        << "PROXY records: " << document.Proxies().size() << '\n'
        << "GLOBSETs: " << document.Globsets().size() << '\n'
        << "Authoritative GLOBs: " << document.Globs().size() << '\n'
        << "Render batches: " << document.RenderBatches().size() << '\n'
        << "Collision triangles: " << document.CollisionTriangles().size() << '\n'
        << "VISMAP nodes/zones: " << document.VisibilityNodes().size() << "/" << header.visibilityZoneCount << '\n'
        << "Camera FOV/MRD ratio: " << header.cameraFov << '/' << header.cameraMrdRatio << "\n\n";

    std::unordered_map<std::int16_t, std::size_t> oidCounts;
    for (const auto &object : document.Objects())
        if (object.oid >= 0)
            ++oidCounts[object.oid];
    out << "=== VALIDATION ===\n";
    std::size_t issues = 0;
    const auto issue = [&](const std::string &message)
    {
        ++issues;
        out << "[ISSUE] " << message << '\n';
    };
    std::unordered_map<std::int16_t, std::vector<std::size_t>> shadersByOid;
    std::size_t globalShaderAnimationCount = 0;
    for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
    {
        const auto &shader = document.Shaders()[shaderIndex];
        shadersByOid[shader.oid].push_back(shaderIndex);
        if (shader.animation.kind != BrxShaderAnimationKind::None)
            ++globalShaderAnimationCount;
    }
    if (globalShaderAnimationCount != header.declaredShaderAnimationCount)
        issue("Decoded global shader-animation count does not match the PCBRX world header");

    const auto validateShaderAnimation =
        [&](const BrxShaderAnimationRecord &animation, const std::string &identity, const bool requireTarget)
    {
        const int kind = static_cast<int>(animation.kind);
        if (kind < static_cast<int>(BrxShaderAnimationKind::Loop) ||
            kind > static_cast<int>(BrxShaderAnimationKind::Looker))
        {
            issue(identity + " has an unsupported shader-animation kind " + std::to_string(kind));
            return;
        }
        const auto target = shadersByOid.find(animation.shaderOid);
        if (requireTarget && target == shadersByOid.end())
            issue(identity + " targets missing SHD OID " + std::to_string(animation.shaderOid));
        // PshdFindShader is a forward linear search. Duplicate shader OIDs are
        // deterministic (the first record wins), not an invalid reference.
        const auto finite = [](float value) { return std::isfinite(value); };
        const auto finiteValues = [&](std::size_t count)
        {
            for (std::size_t index = 0; index < count; ++index)
                if (!finite(animation.values[index]))
                    return false;
            return true;
        };
        const auto orderedNonnegative = [&](std::size_t first)
        {
            return finite(animation.values[first]) && finite(animation.values[first + 1]) &&
                   animation.values[first] >= 0.0f && animation.values[first] <= animation.values[first + 1];
        };
        if (animation.kind == BrxShaderAnimationKind::Loop || animation.kind == BrxShaderAnimationKind::PingPong)
        {
            if (!orderedNonnegative(0) || animation.values[1] <= 0.0f)
                issue(identity + " has an invalid cycle-duration range");
            if (!orderedNonnegative(2))
                issue(identity + " has an invalid pause-duration range");
            if ((animation.kind == BrxShaderAnimationKind::Loop &&
                 (animation.initialFrame < (std::numeric_limits<std::int16_t>::min)() ||
                  animation.initialFrame > (std::numeric_limits<std::int16_t>::max)())) ||
                (animation.kind == BrxShaderAnimationKind::PingPong &&
                 (animation.initialFrame < 0 || animation.initialFrame > (std::numeric_limits<std::uint16_t>::max)())))
                issue(identity + " has an initial frame outside its serialized 16-bit range");
            if (target != shadersByOid.end() && !target->second.empty())
            {
                const auto &shader = document.Shaders()[target->second.front()];
                const std::size_t frames =
                    (std::max)({shader.ambientBitmapIndices.size(), shader.diffuseBitmapIndices.size(),
                                shader.saturateBitmapIndices.size()});
                // Retail stores LOOP's initial frame as signed S16 and wraps it
                // through GModPositive during UpdateLoop. PINGPONG likewise
                // consumes its full U16 field without a BRX-time frame-count
                // restriction. Only an empty target stage is structurally
                // unusable; rejecting out-of-range starts is stricter than the
                // game and blocks valid authored data.
                if (frames == 0)
                    issue(identity + " targets a material with no texture frames");
            }
        }
        else if (animation.kind == BrxShaderAnimationKind::Shuffle)
        {
            // Zero is legal: UpdateShuffle then selects a new frame on every
            // game update.
            if (!orderedNonnegative(0))
                issue(identity + " has an invalid shuffle-pause range");
        }
        else if (animation.kind == BrxShaderAnimationKind::Hologram)
        {
            if ((!finite(animation.values[0]) && animation.values[0] != (std::numeric_limits<float>::max)()) ||
                animation.symmetryCount == 0)
                issue(identity + " has invalid hologram angle/symmetry data");
        }
        else if (animation.kind == BrxShaderAnimationKind::Eyes)
        {
            if (!finiteValues(4) || animation.values[0] <= 0.0f || !orderedNonnegative(1) ||
                animation.values[3] < 0.0f || animation.values[3] > 1.0f)
                issue(identity + " has invalid eye blink timing/probability");
            // LoadEyesFromBrx stores a signed OID and PostEyesLoad explicitly
            // accepts PshdFindShader returning null.  OID -1 therefore means
            // that this eye animation has no secondary material.
            // A missing secondary shader is legal: PshdFindShader returns
            // null and the primary eye material continues to animate alone.
        }
        else if (animation.kind == BrxShaderAnimationKind::Scroller)
        {
            if (!finiteValues(4) || animation.values[2] == 0.0f || animation.values[3] == 0.0f)
                issue(identity + " has non-finite data or a zero UV modulus");
        }
        else if (animation.kind == BrxShaderAnimationKind::Circler && !finiteValues(4))
            issue(identity + " has non-finite circular-motion data");
        else if (animation.kind == BrxShaderAnimationKind::Looker)
        {
            if (!finiteValues(6) || animation.values[2] > animation.values[3] ||
                animation.values[4] > animation.values[5])
                issue(identity + " has invalid looker UV bounds");
        }
    };
    for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
        if (document.Shaders()[shaderIndex].animation.kind != BrxShaderAnimationKind::None)
        {
            validateShaderAnimation(document.Shaders()[shaderIndex].animation,
                                    "SHD[" + std::to_string(shaderIndex) + "] OID " +
                                        std::to_string(document.Shaders()[shaderIndex].oid),
                                    true);
            const auto &shader = document.Shaders()[shaderIndex];
            if (shader.animation.shaderOid != shader.oid && !(shader.animation.kind == BrxShaderAnimationKind::Eyes &&
                                                              shader.animation.otherShaderOid == shader.oid))
                issue("SHD[" + std::to_string(shaderIndex) + "] owns an animation that cannot bind to itself");
        }
    for (std::size_t globIndex = 0; globIndex < document.Globs().size(); ++globIndex)
        if (document.Globs()[globIndex].hasShaderAnimation &&
            document.Globs()[globIndex].shaderAnimation.kind != BrxShaderAnimationKind::None)
        {
            validateShaderAnimation(document.Globs()[globIndex].shaderAnimation,
                                    "GLOB[" + std::to_string(globIndex) + "] shader animation", false);
            // Retail accepts an SAA whose target shader is not present in this
            // GLOB.  PostSaaLoad may leave its SAI unbound, and SUBGLOB setup
            // simply receives no local animation state.  Shipped levels use
            // this legal no-op form, so it must not block a rebuild.
        }
    const bool hasAuthoredSpliceModules =
        std::any_of(document.SpliceModules().begin(), document.SpliceModules().end(),
                    [](const BrxSpliceModule &module) { return module.editorCreated; });
    const bool hasAuthoredSpliceNodes = std::any_of(document.SpliceNodes().begin(), document.SpliceNodes().end(),
                                                    [](const BrxSpliceNode &node) { return node.editorCreated; });
    if (!hasAuthoredSpliceModules && document.SpliceModules().size() != header.spliceCount)
        issue("Decoded splice-module count does not match the PCBRX splice header");
    if (!hasAuthoredSpliceNodes && document.SpliceNodes().size() != header.expressionNodeCount)
        issue("Decoded splice-node count does not match the PCBRX expression count");
    std::unordered_set<std::int32_t> analyzedSplices;
    for (const auto &object : document.Objects())
    {
        if (object.spliceIndex < -1 || (object.spliceIndex >= 0 && static_cast<std::size_t>(object.spliceIndex) >=
                                                                       document.SpliceModules().size()))
            issue(std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid) +
                  " references missing splice " + std::to_string(object.spliceIndex));
        else if (object.spliceIndex >= 0 && analyzedSplices.insert(object.spliceIndex).second)
        {
            const auto analysis = AnalyzeSpliceModule(document, object.spliceIndex);
            for (const auto &diagnostic : analysis.diagnostics)
                issue("Splice " + std::to_string(object.spliceIndex) + ": " + diagnostic);
        }
    }
    // Unreferenced modules still belong to the decoded splice map and can be
    // attached later in the editor. Validate them too instead of silently
    // limiting diagnostics to modules currently owned by a world object.
    for (std::size_t spliceIndex = 0; spliceIndex < document.SpliceModules().size(); ++spliceIndex)
    {
        if (!analyzedSplices.insert(static_cast<std::int32_t>(spliceIndex)).second)
            continue;
        const auto analysis = AnalyzeSpliceModule(document, static_cast<std::int32_t>(spliceIndex));
        for (const auto &diagnostic : analysis.diagnostics)
            issue("Splice " + std::to_string(spliceIndex) + ": " + diagnostic);
    }
    for (std::size_t spliceIndex = 0; spliceIndex < document.SpliceModules().size(); ++spliceIndex)
    {
        const auto &module = document.SpliceModules()[spliceIndex];
        if (!module.modified && !module.editorCreated)
            continue;
        const auto roundTrip = VerifySpliceModuleSourceRoundTrip(document, static_cast<std::int32_t>(spliceIndex));
        if (!roundTrip.succeeded)
            issue("Splice " + std::to_string(spliceIndex) + " does not round-trip through the source editor exactly");
    }
    std::vector<std::uint8_t> reachableSpliceNodes(document.SpliceNodes().size(), 0);
    std::vector<std::int32_t> pendingSpliceNodes;
    pendingSpliceNodes.reserve(document.SpliceModules().size());
    for (const auto &module : document.SpliceModules())
        pendingSpliceNodes.push_back(module.rootNode);
    while (!pendingSpliceNodes.empty())
    {
        const auto nodeIndex = pendingSpliceNodes.back();
        pendingSpliceNodes.pop_back();
        if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= document.SpliceNodes().size() ||
            reachableSpliceNodes[static_cast<std::size_t>(nodeIndex)])
            continue;
        reachableSpliceNodes[static_cast<std::size_t>(nodeIndex)] = 1;
        const auto &node = document.SpliceNodes()[static_cast<std::size_t>(nodeIndex)];
        pendingSpliceNodes.push_back(node.childNode);
        pendingSpliceNodes.push_back(node.nextNode);
    }
    const auto unreachableSpliceNodes = static_cast<std::size_t>(
        std::count(reachableSpliceNodes.begin(), reachableSpliceNodes.end(), static_cast<std::uint8_t>(0)));
    if (unreachableSpliceNodes != 0)
        issue("Splice graph contains " + std::to_string(unreachableSpliceNodes) +
              " node(s) unreachable from every module; the rebuilt writer rejects this state");

    // These are not optional editor metadata.  Their retail loaders consume
    // one inline trailing record immediately after the owning CID payload, so
    // a missing, duplicate, orphaned, or wrong-class record changes the byte
    // boundary of every object that follows it.
    std::vector<std::size_t> effectOwners(document.Objects().size(), 0);
    for (std::size_t payloadIndex = 0; payloadIndex < document.Effects().size(); ++payloadIndex)
    {
        const auto owner = document.Effects()[payloadIndex].ownerObjectIndex;
        if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
        {
            issue("Effect payload " + std::to_string(payloadIndex) + " has invalid owner index " +
                  std::to_string(owner));
            continue;
        }
        ++effectOwners[static_cast<std::size_t>(owner)];
        const auto cid = document.Objects()[static_cast<std::size_t>(owner)].cid;
        if (cid != 80 && cid != 121 && cid != 122)
            issue("Effect payload " + std::to_string(payloadIndex) + " is owned by " +
                  std::string(BrxDocument::ClassName(cid)) + " instead of EMITTER, EXPLO, or EXPLS");
    }
    std::vector<std::size_t> volumeOwners(document.Objects().size(), 0);
    for (std::size_t payloadIndex = 0; payloadIndex < document.Volumes().size(); ++payloadIndex)
    {
        const auto owner = document.Volumes()[payloadIndex].ownerObjectIndex;
        if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
        {
            issue("TBSP/volume payload " + std::to_string(payloadIndex) + " has invalid owner index " +
                  std::to_string(owner));
            continue;
        }
        ++volumeOwners[static_cast<std::size_t>(owner)];
        const auto cid = document.Objects()[static_cast<std::size_t>(owner)].cid;
        if (cid != 96 && cid != 123 && cid != 124 && cid != 125)
            issue("TBSP/volume payload " + std::to_string(payloadIndex) + " is owned by " +
                  std::string(BrxDocument::ClassName(cid)) + " instead of FLASH, VOL, RATHOLE, or EXIT");
    }
    std::vector<std::size_t> shapeOwners(document.Objects().size(), 0);
    for (std::size_t payloadIndex = 0; payloadIndex < document.ShapePaths().size(); ++payloadIndex)
    {
        const auto owner = document.ShapePaths()[payloadIndex].ownerObjectIndex;
        if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
        {
            issue("SHAPE path payload " + std::to_string(payloadIndex) + " has invalid owner index " +
                  std::to_string(owner));
            continue;
        }
        ++shapeOwners[static_cast<std::size_t>(owner)];
        const auto cid = document.Objects()[static_cast<std::size_t>(owner)].cid;
        if (cid < 110 || cid > 114)
            issue("SHAPE path payload " + std::to_string(payloadIndex) + " is owned by " +
                  std::string(BrxDocument::ClassName(cid)) + " instead of SHAPE/HSHAPE/PIPE/RAIL/LANDING");
    }
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        // Materialized PROXY clones are viewport/runtime projections of their
        // reusable source and are not emitted as independent inline payloads.
        if (object.proxyClone)
            continue;
        const std::string identity =
            std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid);
        if (object.cid == 80 || object.cid == 121 || object.cid == 122)
        {
            if (effectOwners[objectIndex] != 1)
                issue(identity + " must own exactly one inline effect payload; found " +
                      std::to_string(effectOwners[objectIndex]));
        }
        if (object.cid == 96 || object.cid == 123 || object.cid == 124 || object.cid == 125)
        {
            if (volumeOwners[objectIndex] != 1)
                issue(identity + " must own exactly one inline TBSP/volume payload; found " +
                      std::to_string(volumeOwners[objectIndex]));
        }
        if (object.cid >= 110 && object.cid <= 114 && shapeOwners[objectIndex] != 1)
            issue(identity + " must own exactly one inline SHAPE path payload; found " +
                  std::to_string(shapeOwners[objectIndex]));
    }
    const auto validateInlineOwnerTable =
        [&](const auto &records, const std::int16_t expectedCid, const char *label, const bool enforceOwnerCount = true)
    {
        std::vector<std::size_t> owners(document.Objects().size(), 0);
        for (std::size_t payloadIndex = 0; payloadIndex < records.size(); ++payloadIndex)
        {
            const auto owner = records[payloadIndex].ownerObjectIndex;
            if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
            {
                issue(std::string(label) + " payload " + std::to_string(payloadIndex) + " has invalid owner index " +
                      std::to_string(owner));
                continue;
            }
            ++owners[static_cast<std::size_t>(owner)];
            const auto &object = document.Objects()[static_cast<std::size_t>(owner)];
            if (object.cid != expectedCid)
                issue(std::string(label) + " payload " + std::to_string(payloadIndex) + " is owned by " +
                      BrxDocument::ClassName(object.cid) + " instead of CID " + std::to_string(expectedCid));
        }
        for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
        {
            const auto &object = document.Objects()[objectIndex];
            if (!enforceOwnerCount || object.cid != expectedCid || object.proxyClone)
                continue;
            if (owners[objectIndex] != 1)
                issue(std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid) +
                      " must own exactly one inline " + label + " payload; found " +
                      std::to_string(owners[objectIndex]));
        }
    };
    validateInlineOwnerTable(document.Warps(), 116, "WARP");
    validateInlineOwnerTable(document.Exits(), 125, "EXIT");
    // ASEG and PATHZONE already perform richer per-object count validation in
    // their CID-specific audits below; this pass still catches orphaned and
    // wrong-class records without reporting the same defect twice.
    validateInlineOwnerTable(document.AnimationSegments(), 131, "ASEG", false);
    validateInlineOwnerTable(document.StateMachines(), 136, "SM");
    validateInlineOwnerTable(document.PathZones(), 138, "PATHZONE", false);
    validateInlineOwnerTable(document.ReachMaps(), 139, "RCHM");
    validateInlineOwnerTable(document.Keyholes(), 142, "KEYHOLE");
    std::vector<std::size_t> scanOwners(document.Objects().size(), 0);
    for (std::size_t scanIndex = 0; scanIndex < document.Scans().size(); ++scanIndex)
    {
        const auto &scan = document.Scans()[scanIndex];
        if (scan.ownerObjectIndex < 0 || static_cast<std::size_t>(scan.ownerObjectIndex) >= document.Objects().size())
        {
            issue("SCAN payload " + std::to_string(scanIndex) + " has invalid owner index " +
                  std::to_string(scan.ownerObjectIndex));
            continue;
        }
        ++scanOwners[static_cast<std::size_t>(scan.ownerObjectIndex)];
        if (document.Objects()[static_cast<std::size_t>(scan.ownerObjectIndex)].cid != 130)
            issue("SCAN payload " + std::to_string(scanIndex) + " is owned by " +
                  std::string(
                      BrxDocument::ClassName(document.Objects()[static_cast<std::size_t>(scan.ownerObjectIndex)].cid)) +
                  " instead of CID 130");
        if (scan.text.size() > 65535)
            issue("SCAN payload " + std::to_string(scanIndex) + " exceeds the unsigned-16-bit serialized string limit");
    }
    if (document.Lights().size() > 256)
        issue("Level contains " + std::to_string(document.Lights().size()) +
              " LIGHT records; ProjectCane's active table preserves world order and ignores records after slot 256");
    std::vector<std::size_t> lightOwners(document.Objects().size(), 0);
    for (const auto &light : document.Lights())
    {
        if (light.ownerObjectIndex < 0 || static_cast<std::size_t>(light.ownerObjectIndex) >= document.Objects().size())
        {
            issue("LIGHT record has an invalid owner index " + std::to_string(light.ownerObjectIndex));
            continue;
        }
        ++lightOwners[static_cast<std::size_t>(light.ownerObjectIndex)];
        if (document.Objects()[static_cast<std::size_t>(light.ownerObjectIndex)].cid != 81)
            issue("LIGHT record owner index " + std::to_string(light.ownerObjectIndex) + " is not CID 81");
    }
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        if (document.Objects()[objectIndex].cid != 81)
            continue;
        if (lightOwners[objectIndex] == 0)
            issue("LIGHT OID " + std::to_string(document.Objects()[objectIndex].oid) + " has no decoded LIGHT record");
        else if (lightOwners[objectIndex] > 1)
            issue("LIGHT OID " + std::to_string(document.Objects()[objectIndex].oid) + " owns multiple LIGHT records");
    }
    const std::size_t skyCount =
        static_cast<std::size_t>(std::count_if(document.Objects().begin(), document.Objects().end(),
                                               [](const BrxObjectRecord &object) { return object.cid == 84; }));
    if (skyCount > 1)
        issue("Level contains " + std::to_string(skyCount) +
              " SKY roots; ProjectCane updates and renders every active SKY, which can create overlapping backgrounds");
    const auto &worldOptions = document.WorldOptions();
    std::vector<std::int16_t> seenWorldOptions;
    const auto &legalWorldOptions = CidOptionIds()[108];
    for (const auto &option : worldOptions)
    {
        if (std::find(legalWorldOptions.begin(), legalWorldOptions.end(), option.eopid) == legalWorldOptions.end())
            issue("Scene World contains illegal EOPID " + std::to_string(option.eopid));
        const bool repeatableWorldRecord = option.eopid == 549 || option.eopid == 550 || option.eopid == 551 ||
                                           option.eopid == 580 || option.eopid == 581;
        if (!repeatableWorldRecord &&
            std::find(seenWorldOptions.begin(), seenWorldOptions.end(), option.eopid) != seenWorldOptions.end())
            issue("Scene World repeats EOPID " + std::to_string(option.eopid));
        seenWorldOptions.push_back(option.eopid);
        if (!IsEditorWritableOption(option.eopid))
            issue("Scene World serializes non-SetUser/runtime EOPID " + std::to_string(option.eopid));
        if (option.eopid == 525 &&
            (!std::isfinite(option.floatValue[0]) || option.floatValue[0] < 0.0f || option.floatValue[0] > 1.0f))
            issue("Scene World music volume is outside 0..1");
    }
    const auto worldProperty = [&](const std::int16_t eopid) -> const BrxOptionRecord *
    {
        const auto found = std::find_if(worldOptions.begin(), worldOptions.end(),
                                        [&](const BrxOptionRecord &option) { return option.eopid == eopid; });
        return found == worldOptions.end() ? nullptr : &*found;
    };
    for (const auto [id, label] :
         {std::pair<std::int16_t, const char *>{537, "gold enable delay"}, {538, "commentary duration"}})
        if (const auto *property = worldProperty(id);
            property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
            issue(std::string("Scene World ") + label + " cannot be negative");
    for (const auto [id, label] :
         {std::pair<std::int16_t, const char *>{526, "midtone illumination"}, {527, "shadow illumination"}})
        if (const auto *property = worldProperty(id);
            property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f ||
                         property->floatValue[0] > 1.0f))
            issue(std::string("Scene World ") + label + " is outside 0..1");
    for (const auto [id, label] : {std::pair<std::int16_t, const char *>{528, "sky color"}, {535, "fog color"}})
        if (const auto *property = worldProperty(id))
            for (float component : property->floatValue)
                if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                {
                    issue(std::string("Scene World ") + label + " contains a component outside 0..1");
                    break;
                }
    if (const auto *reverb = worldProperty(552))
    {
        if (reverb->arguments.size() != 2)
            issue("Scene World default reverb must contain type and depth arguments");
        else
        {
            if (reverb->arguments[0].intValue < 0 || reverb->arguments[0].intValue > 5)
                issue("Scene World default reverb kind is outside None..Room");
            if (reverb->arguments[1].intValue < 0 || reverb->arguments[1].intValue > 127)
                issue("Scene World default reverb depth is outside 0..127");
        }
    }
    int intermittentCount = 0;
    bool hasIntermittentSound = false;
    bool hasVolumeForCurrentSound = false;
    bool hasRepeatForCurrentSound = false;
    std::unordered_map<int, int> authoredVaultStates;
    for (const auto &option : worldOptions)
    {
        if (option.eopid == 549)
        {
            ++intermittentCount;
            hasIntermittentSound = true;
            hasVolumeForCurrentSound = false;
            hasRepeatForCurrentSound = false;
            if (option.intValue < 0)
                issue("Scene World intermittent sound has no valid sound-effect ID");
        }
        else if (option.eopid == 550 || option.eopid == 551)
        {
            if (!hasIntermittentSound)
                issue("Scene World intermittent sound modifier appears before any sound-effect record");
            bool &alreadyPresent = option.eopid == 550 ? hasVolumeForCurrentSound : hasRepeatForCurrentSound;
            if (alreadyPresent)
                issue(std::string("Scene World intermittent sound repeats its ") +
                      (option.eopid == 550 ? "volume" : "delay") + " modifier");
            alreadyPresent = true;
            if (!std::isfinite(option.floatValue[0]) || !std::isfinite(option.floatValue[1]) ||
                option.floatValue[0] < 0.0f || option.floatValue[1] < option.floatValue[0])
                issue(std::string("Scene World intermittent sound has invalid ") +
                      (option.eopid == 550 ? "volume" : "repeat-delay") + " range");
            if (option.eopid == 550 && option.floatValue[1] > 100.0f)
                issue("Scene World intermittent volume percentage exceeds 100");
        }
        if (option.eopid == 580 || option.eopid == 581)
        {
            if (option.arguments.size() != 1)
                issue("Scene World vault-state command must contain one vault index");
            else if (option.arguments[0].intValue < 0 || option.arguments[0].intValue > 31)
                issue("Scene World vault index is outside 0..31");
            else
            {
                const int vault = option.arguments[0].intValue;
                const int state = option.eopid == 580 ? 0 : 1;
                authoredVaultStates[vault] = state;
            }
        }
    }
    if (intermittentCount > 16)
        issue("Scene World exceeds the runtime limit of sixteen intermittent sounds");
    if (!std::isfinite(header.cameraFov) || header.cameraFov <= 0.0f || header.cameraFov >= 3.1415927f)
        issue("Scene World camera FOV must be between 0 and pi radians");
    if (!std::isfinite(header.cameraNearClip) || header.cameraNearClip <= 0.0f)
        issue("Scene World near clip must be greater than zero");
    if (!std::isfinite(header.cameraFarClip) || header.cameraFarClip <= header.cameraNearClip)
        issue("Scene World far clip must be greater than near clip");
    if (!std::isfinite(header.cameraMrdRatio) || header.cameraMrdRatio < 0.75f || header.cameraMrdRatio > 3.0f)
        issue("Scene World MRD ratio is outside ProjectCane's supported 0.75x..3.0x range");
    const bool hasFogDistance = std::any_of(worldOptions.begin(), worldOptions.end(), [](const BrxOptionRecord &option)
                                            { return option.eopid == 532 || option.eopid == 533; });
    const bool hasFogMaximum = std::any_of(worldOptions.begin(), worldOptions.end(),
                                           [](const BrxOptionRecord &option) { return option.eopid == 534; });
    if (hasFogDistance && (!std::isfinite(header.fogNear) || !std::isfinite(header.fogFar) || header.fogNear < 0.0f ||
                           header.fogFar <= header.fogNear))
        issue("Scene World fog distances must be finite, non-negative, and ordered near < far");
    if (hasFogMaximum && (!std::isfinite(header.fogMaximum) || header.fogMaximum < 0.0f || header.fogMaximum > 1.0f))
        issue("Scene World fog maximum is outside 0..1");
    for (const auto &asset : {std::pair<const char *, const std::string &>{"sound bank", header.soundBankFile},
                              {"music", header.musicFile},
                              {"commentary audio", header.commentaryAudioFile}})
    {
        if (asset.second.size() > 31)
            issue(std::string("Scene World ") + asset.first + " filename exceeds its 31-character BRX limit");
        if (asset.second.find('\0') != std::string::npos)
            issue(std::string("Scene World ") + asset.first + " filename contains an embedded null byte");
    }
    if (!header.soundBankFile.empty())
    {
        static constexpr std::array<std::string_view, 7> acceptedBanks{
            "Common.bnk", "Intro.bnk", "Underwater.bnk", "Muggshot.bnk", "Voodoo.bnk", "Snow.bnk", "Clockwerk.bnk"};
        if (std::find(acceptedBanks.begin(), acceptedBanks.end(), std::string_view(header.soundBankFile)) ==
            acceptedBanks.end())
            issue("Scene World sound bank is not one of the seven names accepted by LoadSwFromBrx");
    }
    if (!header.musicFile.empty())
    {
        constexpr std::string_view musicPrefix = "MUSID_";
        constexpr std::string_view musicSuffix = ".mus";
        bool valid = header.musicFile.size() > musicPrefix.size() + musicSuffix.size() &&
                     header.musicFile.compare(0, musicPrefix.size(), musicPrefix.data(), musicPrefix.size()) == 0 &&
                     header.musicFile.compare(header.musicFile.size() - musicSuffix.size(), musicSuffix.size(),
                                              musicSuffix.data(), musicSuffix.size()) == 0;
        if (valid)
            for (std::size_t character = musicPrefix.size(); character < header.musicFile.size() - musicSuffix.size();
                 ++character)
                if (!std::isxdigit(static_cast<unsigned char>(header.musicFile[character])))
                {
                    valid = false;
                    break;
                }
        if (!valid)
            issue("Scene World music filename must use the exact MUSID_<hex>.mus format");
    }
    if (header.commentarySpeakers.size() > (std::numeric_limits<std::uint16_t>::max)())
        issue("Scene World commentary-speaker text exceeds the serialized U16 byte limit");
    const auto worldInt = [&](const std::int16_t eopid, int &value)
    {
        const auto found = std::find_if(worldOptions.begin(), worldOptions.end(),
                                        [&](const BrxOptionRecord &option) { return option.eopid == eopid; });
        if (found == worldOptions.end())
            return false;
        value = found->intValue;
        return true;
    };
    for (const auto [minimumId, maximumId, label] :
         {std::tuple<std::int16_t, std::int16_t, const char *>{542, 543, "rich-world coin"},
          {544, 545, "poor-world coin"}})
    {
        int minimum = 0, maximum = 0;
        if (worldInt(minimumId, minimum) && worldInt(maximumId, maximum) && minimum > maximum)
            issue(std::string("Scene World ") + label + " minimum exceeds maximum");
    }
    std::unordered_map<std::int32_t, std::pair<std::uint16_t, std::size_t>> proxySources;
    std::unordered_map<std::int32_t, std::size_t> proxySourceUses;
    for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
    {
        const auto owner = document.Proxies()[proxyIndex].ownerObjectIndex;
        const bool materializedClone = owner >= 0 && static_cast<std::size_t>(owner) < document.Objects().size() &&
                                       document.Objects()[static_cast<std::size_t>(owner)].proxyClone;
        if (materializedClone)
            continue;
        for (const auto &root : document.Proxies()[proxyIndex].roots)
            if (root.kind == BrxProxyRootKind::InlineSource && root.sourceListIndex >= 0)
            {
                // AddSwProxySource assigns the runtime pool index in physical
                // stream order.  The structured writer deliberately rebuilds
                // that order as it emits PROXY roots, so a decoded/editor-side
                // sourceListIndex is an identity hint rather than a required
                // contiguous ordering of document.Proxies().
                if (root.sourceListIndex >= 128)
                    issue("PROXY source-list entry " + std::to_string(root.sourceListIndex) +
                          " exceeds ProjectCane's 128-entry Scene World pool");
                if (!proxySources.emplace(root.sourceListIndex, std::make_pair(root.cloneCapacity, proxyIndex)).second)
                    issue("PROXY source-list entry " + std::to_string(root.sourceListIndex) +
                          " is defined more than once");
            }
    }
    if (proxySources.size() > 128)
        issue("Scene World defines " + std::to_string(proxySources.size()) +
              " PROXY sources; ProjectCane's pool holds only 128");
    std::unordered_map<std::int32_t, std::size_t> attachedProxyChildOwners;
    for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
    {
        const auto &proxy = document.Proxies()[proxyIndex];
        const std::string prefix = "PROXY record " + std::to_string(proxyIndex);
        const bool materializedProxyClone =
            proxy.ownerObjectIndex >= 0 &&
            static_cast<std::size_t>(proxy.ownerObjectIndex) < document.Objects().size() &&
            document.Objects()[static_cast<std::size_t>(proxy.ownerObjectIndex)].proxyClone;
        if (proxy.ownerObjectIndex < 0 ||
            static_cast<std::size_t>(proxy.ownerObjectIndex) >= document.Objects().size() ||
            document.Objects()[static_cast<std::size_t>(proxy.ownerObjectIndex)].cid != 83)
            issue(prefix + " has an invalid CID 83 owner");
        if (proxy.roots.empty())
            issue(prefix + " has no proxy roots");
        if (proxy.roots.size() > (std::numeric_limits<std::uint8_t>::max)())
            issue(prefix + " has more than the serialized U8 root limit");
        if (proxy.overrides.size() > (std::numeric_limits<std::uint8_t>::max)())
            issue(prefix + " has more than the serialized U8 override limit");
        if (proxy.attachedChildObjectIndices.size() > (std::numeric_limits<std::uint16_t>::max)())
            issue(prefix + " has more than the serialized U16 attached-child limit");
        if (proxy.roots.size() != 1 && !proxy.attachedChildObjectIndices.empty())
            issue(prefix + " has attached children despite having multiple roots");
        for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
        {
            const auto &root = proxy.roots[rootIndex];
            const std::string rootPrefix = prefix + " root " + std::to_string(rootIndex);
            if (!materializedProxyClone && root.sourceListIndex < 0)
                issue(rootPrefix + " has an invalid source-list index");
            if (root.instantiatedRootObjectIndex < 0 ||
                static_cast<std::size_t>(root.instantiatedRootObjectIndex) >= document.Objects().size())
                issue(rootPrefix + " has no instantiated root object");
            else
            {
                const auto &rootObject = document.Objects()[static_cast<std::size_t>(root.instantiatedRootObjectIndex)];
                if (!CidDerivesFrom(rootObject.cid, 2))
                    issue(rootPrefix + " is not ALO-derived; ProjectCane applies the PROXY "
                                       "transform and parent through the ALO virtual table");
                if (rootObject.proxyRecordIndex != static_cast<std::int32_t>(proxyIndex) ||
                    rootObject.proxyRootIndex != static_cast<std::int32_t>(rootIndex))
                    issue(rootPrefix + " disagrees with its root object's proxy identity");
            }
            if (root.objectCount == 0 || root.objectBegin >= document.Objects().size() ||
                static_cast<std::size_t>(root.objectBegin) + root.objectCount > document.Objects().size())
                issue(rootPrefix + " has an invalid object-membership range");
            else if (root.instantiatedRootObjectIndex != static_cast<std::int32_t>(root.objectBegin))
                issue(rootPrefix + " root object does not begin its membership range");
            if (root.objectCount != 0 && root.objectBegin < document.Objects().size())
            {
                const std::size_t objectEnd = (std::min)(document.Objects().size(),
                                                         static_cast<std::size_t>(root.objectBegin) + root.objectCount);
                for (std::size_t memberIndex = root.objectBegin; memberIndex < objectEnd; ++memberIndex)
                {
                    std::int32_t candidate = static_cast<std::int32_t>(memberIndex);
                    bool belongsToRootHierarchy = false;
                    for (std::size_t guard = 0; candidate >= 0 &&
                                                static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                                guard++ <= document.Objects().size();)
                    {
                        if (candidate == root.instantiatedRootObjectIndex)
                        {
                            belongsToRootHierarchy = true;
                            break;
                        }
                        const auto next = document.Objects()[static_cast<std::size_t>(candidate)].parentIndex;
                        if (next == candidate)
                            break;
                        candidate = next;
                    }
                    if (!belongsToRootHierarchy)
                    {
                        issue(rootPrefix + " membership range contains unrelated object " +
                              std::to_string(memberIndex));
                        break;
                    }
                }
            }
            if (root.instantiatedRootObjectIndex >= 0 &&
                static_cast<std::size_t>(root.instantiatedRootObjectIndex) < document.Objects().size())
            {
                const auto &rootObject = document.Objects()[static_cast<std::size_t>(root.instantiatedRootObjectIndex)];
                if (rootObject.parentIndex != proxy.ownerObjectIndex)
                    issue(rootPrefix + " is not serialized beneath its PROXY wrapper");
                if (proxy.ownerObjectIndex >= 0 &&
                    static_cast<std::size_t>(proxy.ownerObjectIndex) < document.Objects().size() &&
                    rootObject.proxyRuntimeParentIndex !=
                        document.Objects()[static_cast<std::size_t>(proxy.ownerObjectIndex)].parentIndex)
                    issue(rootPrefix + " has an incorrect runtime parent");
            }
            if (root.kind == BrxProxyRootKind::InlineSource)
            {
                if (!materializedProxyClone && root.cloneCapacity < 1)
                    issue(rootPrefix + " has clone capacity below one");
            }
            else if (!materializedProxyClone)
            {
                ++proxySourceUses[root.sourceListIndex];
                const auto source = proxySources.find(root.sourceListIndex);
                if (source == proxySources.end())
                    issue(rootPrefix + " has a missing source reference");
                if (root.sourceProxyRecordIndex < 0 || root.sourceProxyRootIndex < 0)
                    issue(rootPrefix + " has no source PROXY/root identity");
                else if (static_cast<std::size_t>(root.sourceProxyRecordIndex) >= document.Proxies().size() ||
                         static_cast<std::size_t>(root.sourceProxyRootIndex) >=
                             document.Proxies()[static_cast<std::size_t>(root.sourceProxyRecordIndex)].roots.size())
                    issue(rootPrefix + " points to an invalid source PROXY/root identity");
                else
                {
                    const auto &sourceRoot = document.Proxies()[static_cast<std::size_t>(root.sourceProxyRecordIndex)]
                                                 .roots[static_cast<std::size_t>(root.sourceProxyRootIndex)];
                    if (sourceRoot.kind != BrxProxyRootKind::InlineSource ||
                        sourceRoot.sourceListIndex != root.sourceListIndex)
                        issue(rootPrefix + " source identity does not match its source-list entry");
                    if (sourceRoot.objectCount != root.objectCount)
                        issue(rootPrefix + " clone membership count differs from its reusable source");
                    const std::size_t cloneEnd = (std::min)(
                        document.Objects().size(), static_cast<std::size_t>(root.objectBegin) + root.objectCount);
                    for (std::size_t cloneIndex = root.objectBegin; cloneIndex < cloneEnd; ++cloneIndex)
                    {
                        const auto &cloneObject = document.Objects()[cloneIndex];
                        const std::size_t expectedSource = static_cast<std::size_t>(sourceRoot.objectBegin) +
                                                           (cloneIndex - static_cast<std::size_t>(root.objectBegin));
                        if (expectedSource >= document.Objects().size() ||
                            cloneObject.proxySourceObjectIndex != static_cast<std::int32_t>(expectedSource))
                        {
                            issue(rootPrefix + " has a broken source counterpart at object " +
                                  std::to_string(cloneIndex));
                            break;
                        }
                    }
                }
            }
        }
        for (std::size_t overrideIndex = 0; overrideIndex < proxy.overrides.size(); ++overrideIndex)
        {
            const auto &overrideRecord = proxy.overrides[overrideIndex];
            const std::string overridePrefix = prefix + " override " + std::to_string(overrideIndex);
            const auto runtimeResolvedTarget = ProxyRuntime::ResolveOverrideTarget(
                document, static_cast<std::int32_t>(proxyIndex), overrideRecord.targetOid);
            if (runtimeResolvedTarget >= 0 && overrideRecord.targetObjectIndex != runtimeResolvedTarget)
                issue(overridePrefix + " does not match ProjectCane's context-aware target " +
                      std::to_string(runtimeResolvedTarget) + " for OID " + std::to_string(overrideRecord.targetOid));
            if (runtimeResolvedTarget < 0)
                issue(overridePrefix + " cannot resolve OID " + std::to_string(overrideRecord.targetOid) +
                      " without crossing a nested PROXY boundary");
            if (overrideRecord.targetObjectIndex < 0 ||
                static_cast<std::size_t>(overrideRecord.targetObjectIndex) >= document.Objects().size())
                issue(overridePrefix + " does not resolve target OID " + std::to_string(overrideRecord.targetOid));
            else
            {
                const auto &target = document.Objects()[static_cast<std::size_t>(overrideRecord.targetObjectIndex)];
                // The context-aware resolver has already applied the retail
                // nested-PROXY/PXR substitution rule.  Its returned identity
                // is the authoritative membership result.
                if (runtimeResolvedTarget != overrideRecord.targetObjectIndex)
                    issue(overridePrefix + " resolves outside its PROXY instance");
                // A nested PROXY lookup is keyed by the wrapper OID but
                // ProjectCane applies the override to its instantiated PXR
                // root.  That root is allowed (and expected) to have another
                // OID, so runtimeResolvedTarget is the identity check above.
                for (const auto &property : overrideRecord.properties)
                {
                    if (!IsEditorWritableOptionForCid(target.cid, property.eopid))
                        issue(overridePrefix + " contains non-SetUser EOPID " + std::to_string(property.eopid));
                }

                // Inline-source objects are serialized as the reusable source,
                // before this instance packet is replayed by LoadProxyFromBrx.
                // The decoder captures their pre-override property list for that
                // reason.  Editor-project versions predating that snapshot can
                // still be opened, but exporting them without a capture would
                // bake the effective instance properties into the source and
                // apply the packet a second time.  Materialized reference clones
                // are not source records and therefore do not require a capture.
                if (!target.proxyClone && !overrideRecord.properties.empty() &&
                    !overrideRecord.inheritedPropertiesCaptured)
                    issue(overridePrefix + " targets a serialized reusable-source object but "
                                           "has no captured pre-override property state; reopen the original PCBRX "
                                           "or recreate this override before export");
            }
            if (overrideRecord.mrdMode == 2 && !std::isfinite(overrideRecord.maximumRenderDistance))
                issue(overridePrefix + " has a non-finite maximum render distance");
        }
        for (const auto childIndex : proxy.attachedChildObjectIndices)
        {
            if (childIndex < 0 || static_cast<std::size_t>(childIndex) >= document.Objects().size())
            {
                issue(prefix + " contains an invalid attached-child index");
                continue;
            }
            if (document.Objects()[static_cast<std::size_t>(childIndex)].proxyClone)
                issue(prefix + " attached child " + std::to_string(childIndex) +
                      " is a materialized linked-source clone");
            if (proxy.roots.size() == 1 && document.Objects()[static_cast<std::size_t>(childIndex)].parentIndex !=
                                               proxy.roots.front().instantiatedRootObjectIndex)
                issue(prefix + " attached child " + std::to_string(childIndex) +
                      " was not reparented to its sole runtime root");
            if (std::count(proxy.attachedChildObjectIndices.begin(), proxy.attachedChildObjectIndices.end(),
                           childIndex) > 1)
                issue(prefix + " repeats attached-child object " + std::to_string(childIndex));
            const auto [owner, inserted] = attachedProxyChildOwners.emplace(childIndex, proxyIndex);
            if (!inserted && owner->second != proxyIndex)
                issue("PROXY attached child " + std::to_string(childIndex) + " is serialized by more than one wrapper");
        }

        for (std::size_t nestedProxyIndex = 0; nestedProxyIndex < document.Proxies().size(); ++nestedProxyIndex)
        {
            if (nestedProxyIndex == proxyIndex)
                continue;
            const auto nestedOwner = document.Proxies()[nestedProxyIndex].ownerObjectIndex;
            const bool ownerInside =
                std::any_of(proxy.roots.begin(), proxy.roots.end(),
                            [&](const BrxProxyRootRecord &root)
                            {
                                std::int32_t current = nestedOwner;
                                for (std::size_t guard = 0; guard <= document.Objects().size(); ++guard)
                                {
                                    if (current == root.instantiatedRootObjectIndex)
                                        return true;
                                    if (current < 0 || static_cast<std::size_t>(current) >= document.Objects().size())
                                        return false;
                                    const auto parent =
                                        document.Objects()[static_cast<std::size_t>(current)].parentIndex;
                                    if (parent == current)
                                        return false;
                                    current = parent;
                                }
                                return false;
                            });
            if (!ownerInside)
                continue;
            if (nestedOwner < 0 || static_cast<std::size_t>(nestedOwner) >= document.Objects().size() ||
                document.Objects()[static_cast<std::size_t>(nestedOwner)].proxyRecordIndex !=
                    static_cast<std::int32_t>(nestedProxyIndex))
                issue(prefix + " contains nested PROXY " + std::to_string(nestedProxyIndex) +
                      " whose nearest proxy identity was not rebuilt");
        }
    }
    for (const auto &[sourceIndex, source] : proxySources)
    {
        const std::size_t availableReferences = source.first > 0 ? static_cast<std::size_t>(source.first - 1) : 0;
        if (proxySourceUses[sourceIndex] > availableReferences)
            issue("PROXY source-list entry " + std::to_string(sourceIndex) + " is referenced " +
                  std::to_string(proxySourceUses[sourceIndex]) + " times but permits only " +
                  std::to_string(availableReferences));
    }
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        if (object.cid == 83)
        {
            const auto ownerCount =
                std::count_if(document.Proxies().begin(), document.Proxies().end(), [&](const BrxProxyRecord &proxy)
                              { return proxy.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
            if (ownerCount != 1)
                issue("PROXY object " + std::to_string(objectIndex) + " owns " + std::to_string(ownerCount) +
                      " decoded PROXY records instead of one");
        }
        if (object.proxyRecordIndex < 0 || object.proxySourceObjectIndex < 0)
            continue;
        if (static_cast<std::size_t>(object.proxySourceObjectIndex) >= document.Objects().size())
            continue;
        const auto &source = document.Objects()[static_cast<std::size_t>(object.proxySourceObjectIndex)];
        if (source.cid != object.cid || source.oid != object.oid)
            issue("PROXY object " + std::to_string(objectIndex) +
                  " has a source counterpart with a different CID or OID");
    }
    std::size_t derivedSubglobCount = 0;
    std::size_t derivedCelPacketCount = 0;
    std::size_t modifiedGlobCount = 0;
    std::unordered_set<std::uint32_t> renderGroups;
    std::unordered_map<std::int16_t, std::size_t> globOidCounts;
    for (std::size_t globIndex = 0; globIndex < document.Globs().size(); ++globIndex)
    {
        const auto &glob = document.Globs()[globIndex];
        derivedSubglobCount += glob.derivedBatches.size();
        derivedCelPacketCount += glob.derivedCelPackets.size();
        if (glob.modified)
            ++modifiedGlobCount;
        if (glob.oid != -1)
            ++globOidCounts[glob.oid];
        if (glob.instanced && glob.sourceGlobIndex < 0)
            issue("Instanced GLOB " + std::to_string(glob.renderGroup) + " has no source index");
        // renderGroup is editor/runtime bookkeeping and is not globally unique
        // across object-owned GLOBSETs or materialized PROXY instances.
        renderGroups.insert(glob.renderGroup);
        if (glob.ownerObjectIndex < 0 || static_cast<std::size_t>(glob.ownerObjectIndex) >= document.Objects().size())
            issue("GLOB " + std::to_string(glob.renderGroup) + " has an invalid owner object");
        // propertyMask is decoded input bookkeeping.  The from-scratch writer
        // rebuilds all ten retail presence bits from the semantic fields, so
        // stale or unknown source bits are neither serialized nor an error.
        const float emptyBoundsSentinel = (std::numeric_limits<float>::lowest)();
        const bool emptyBounds = glob.radius == emptyBoundsSentinel && glob.center[0] == emptyBoundsSentinel &&
                                 glob.center[1] == emptyBoundsSentinel && glob.center[2] == emptyBoundsSentinel;
        if (!std::isfinite(glob.center[0]) || !std::isfinite(glob.center[1]) || !std::isfinite(glob.center[2]) ||
            !std::isfinite(glob.radius) || (glob.radius < 0.0f && !emptyBounds))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has invalid retail bounds");
        if (glob.hasSubglobRadiusScale && (!std::isfinite(glob.subglobRadiusScale) || glob.subglobRadiusScale < 0.0f))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has an invalid SUBGLOB radius scale");
        if (glob.hasExplicitSortZ && !std::isfinite(glob.authoredSortZ) &&
            glob.authoredSortZ != (std::numeric_limits<float>::max)())
            issue("GLOB " + std::to_string(glob.renderGroup) + " has an invalid sort value");
        if (glob.hasFogScale && !std::isfinite(glob.fogScale))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has a non-finite fog scale");
        if (glob.hasMaximumRenderDistance && !glob.maximumRenderDistanceInfinite &&
            !std::isfinite(glob.authoredMaximumRenderDistance))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has a non-finite maximum render distance");
        if (glob.hasCelMaximumRenderDistance && !glob.celMaximumRenderDistanceInfinite &&
            !std::isfinite(glob.authoredCelMaximumRenderDistance))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has a non-finite cel-border render distance");
        if (glob.hasGleam && (!std::isfinite(glob.gleamNormal[0]) || !std::isfinite(glob.gleamNormal[1]) ||
                              !std::isfinite(glob.gleamNormal[2]) ||
                              std::any_of(std::begin(glob.gleamCurve), std::end(glob.gleamCurve),
                                          [](const float value) { return !std::isfinite(value); })))
            issue("GLOB " + std::to_string(glob.renderGroup) + " has a non-finite gleam payload");
        const auto ownedWarpCount = std::count_if(
            document.GlobWarps().begin(), document.GlobWarps().end(), [&](const BrxGlobWarpRecord &warp)
            { return warp.ownerObjectIndex == glob.ownerObjectIndex && warp.renderGroup == glob.renderGroup; });
        if (glob.hasWarp && ownedWarpCount != 1)
            issue("Warped GLOB " + std::to_string(glob.renderGroup) + " must own exactly one WRBG record");
        if (!glob.hasWarp && ownedWarpCount != 0)
            issue("Non-warped GLOB " + std::to_string(glob.renderGroup) + " owns an unexpected WRBG record");
        if (glob.hasWarp && glob.warpMatrixCount > 4)
            issue("Warped GLOB " + std::to_string(glob.renderGroup) +
                  " exceeds ProjectCane's four WR deformation matrices");
        if (glob.sourceSubglobs.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            glob.sourceCelRecords.size() > (std::numeric_limits<std::uint16_t>::max)())
            issue("GLOB " + std::to_string(glob.renderGroup) + " exceeds the retail U16 SUBGLOB/SUBCEL count");
        // sourceRecordIndex is a derived editor cache.  The serialized source
        // identity and its acyclic ordering are validated below against the
        // owning GLOBSET's sourceGlobIndex slots.
        if (glob.instanced)
        {
            constexpr std::array<std::size_t, 12> serializedMatrixIndices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            if (std::any_of(serializedMatrixIndices.begin(), serializedMatrixIndices.end(),
                            [&](const std::size_t component)
                            { return !std::isfinite(glob.instanceTransform[component]); }))
                issue("Instanced GLOB " + std::to_string(glob.renderGroup) + " has a non-finite retail transform");
        }
        if (glob.rotationTracking >= 6 && glob.rotationTracking != 0xff)
            issue("GLOB " + std::to_string(glob.renderGroup) + " has invalid RTCK " +
                  std::to_string(glob.rotationTracking));
        if (glob.renderPhase >= 18 && glob.renderPhase != 0xff)
            issue("GLOB " + std::to_string(glob.renderGroup) + " has invalid render phase " +
                  std::to_string(glob.renderPhase));
        const bool proxyOwned = glob.ownerObjectIndex >= 0 &&
                                static_cast<std::size_t>(glob.ownerObjectIndex) < document.Objects().size() &&
                                document.Objects()[static_cast<std::size_t>(glob.ownerObjectIndex)].proxyClone;
        if ((!glob.instanced || proxyOwned) && glob.sourceSubglobs.size() != glob.derivedBatches.size())
            issue("GLOB " + std::to_string(glob.renderGroup) + " source SUBGLOB/generated batch count mismatch");
        if (proxyOwned && glob.sourceCelRecords.size() != glob.derivedCelPackets.size())
            issue("PROXY GLOB " + std::to_string(glob.renderGroup) + " source CEL/generated packet count mismatch");
        for (std::size_t subglobIndex = 0; subglobIndex < glob.sourceSubglobs.size(); ++subglobIndex)
        {
            const auto &subglob = glob.sourceSubglobs[subglobIndex];
            const std::size_t vertexCount = subglob.vertices.size();
            if (subglob.vertices.size() > (std::numeric_limits<std::uint16_t>::max)() ||
                subglob.triangles.size() > (std::numeric_limits<std::uint16_t>::max)())
                issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                      " exceeds a retail U16 geometry count");
            if (!std::isfinite(subglob.center[0]) || !std::isfinite(subglob.center[1]) ||
                !std::isfinite(subglob.center[2]) || !std::isfinite(subglob.radius) || subglob.radius < 0.0f ||
                !std::isfinite(subglob.selfIllumination))
                issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                      " has invalid bounds/material values");
            for (const auto &vertex : subglob.vertices)
            {
                const bool finiteVertex = std::all_of(std::begin(vertex.position), std::end(vertex.position),
                                                      [](const float value) { return std::isfinite(value); }) &&
                                          std::all_of(std::begin(vertex.normal), std::end(vertex.normal),
                                                      [](const float value) { return std::isfinite(value); }) &&
                                          std::all_of(std::begin(vertex.color), std::end(vertex.color),
                                                      [](const float value) { return std::isfinite(value); }) &&
                                          std::all_of(std::begin(vertex.uv), std::end(vertex.uv),
                                                      [](const float value) { return std::isfinite(value); }) &&
                                          std::all_of(std::begin(vertex.boneWeights), std::end(vertex.boneWeights),
                                                      [](const float value) { return std::isfinite(value); });
                if (!finiteVertex)
                {
                    issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                          " contains a non-finite vertex");
                    break;
                }
            }
            const auto finiteDelta = [](const std::array<float, 4> &delta)
            { return std::all_of(delta.begin(), delta.end(), [](const float value) { return std::isfinite(value); }); };
            if (std::any_of(subglob.posePositionDeltas.begin(), subglob.posePositionDeltas.end(),
                            [&](const auto &delta) { return !finiteDelta(delta); }) ||
                std::any_of(subglob.poseNormalDeltas.begin(), subglob.poseNormalDeltas.end(),
                            [&](const auto &delta) { return !finiteDelta(delta); }))
                issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                      " contains a non-finite pose delta");
            for (const auto &triangle : subglob.triangles)
                if (triangle[0] >= vertexCount || triangle[1] >= vertexCount || triangle[2] >= vertexCount)
                    issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                          " has an out-of-range triangle");
            if (proxyOwned && subglobIndex < glob.derivedBatches.size() &&
                glob.derivedBatches[subglobIndex].indexCount != subglob.triangles.size() * 3u)
                issue("PROXY GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                      " is paired with the wrong render packet");
        }
    }
    for (const auto &globset : document.Globsets())
    {
        if (globset.ownerObjectIndex < 0 ||
            static_cast<std::size_t>(globset.ownerObjectIndex) >= document.Objects().size())
        {
            issue("GLOBSET has an invalid owner object");
            continue;
        }
        // A materialized PROXY clone carries cloned render records for the
        // viewport, but the writer emits only its reusable source/reference.
        // Its source-record bookkeeping is intentionally not an authored
        // local GLOBSET slot table.
        if (document.Objects()[static_cast<std::size_t>(globset.ownerObjectIndex)].proxyClone)
            continue;
        if (std::unordered_set<std::size_t>(globset.globIndices.begin(), globset.globIndices.end()).size() !=
            globset.globIndices.size())
            issue("GLOBSET owner " + std::to_string(globset.ownerObjectIndex) + " contains duplicate GLOB records");
        if (globset.boneOids.size() > (std::numeric_limits<std::uint8_t>::max)() ||
            globset.poseWeights.size() > (std::numeric_limits<std::uint8_t>::max)() ||
            globset.globIndices.size() > (std::numeric_limits<std::uint16_t>::max)())
            issue("GLOBSET owner " + std::to_string(globset.ownerObjectIndex) +
                  " exceeds its retail bone/pose/GLOB field width");
        if (std::any_of(globset.poseWeights.begin(), globset.poseWeights.end(),
                        [](const float value) { return !std::isfinite(value); }))
            issue("GLOBSET owner " + std::to_string(globset.ownerObjectIndex) + " contains a non-finite pose weight");
        // BindGlobset uses PloFindSwNearest and explicitly leaves BND::palo
        // null when an authored bone OID is absent.  That is a supported
        // unbound influence, not malformed BRX.  Validate the local influence
        // indices below, but do not reject the nullable runtime binding.
        for (std::size_t localGlobIndex = 0; localGlobIndex < globset.globIndices.size(); ++localGlobIndex)
        {
            const auto globIndex = globset.globIndices[localGlobIndex];
            if (globIndex >= document.Globs().size())
            {
                issue("GLOBSET owner " + std::to_string(globset.ownerObjectIndex) +
                      " references an invalid GLOB record");
                continue;
            }
            const auto &glob = document.Globs()[globIndex];
            if (glob.ownerObjectIndex != globset.ownerObjectIndex)
                issue("GLOBSET owner " + std::to_string(globset.ownerObjectIndex) +
                      " contains a GLOB owned by another object");
            if (glob.instanced)
            {
                if (glob.sourceGlobIndex < 0 || static_cast<std::size_t>(glob.sourceGlobIndex) >= localGlobIndex)
                    issue("Instanced GLOB " + std::to_string(glob.renderGroup) +
                          " does not reference an earlier GLOBSET slot");
            }
            for (std::size_t subglobIndex = 0; subglobIndex < glob.sourceSubglobs.size(); ++subglobIndex)
            {
                const auto &subglob = glob.sourceSubglobs[subglobIndex];
                if (subglob.shaderIndex == 0xffffu || subglob.shaderIndex >= document.Shaders().size())
                    issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                          " has no valid runtime shader");
                if (subglob.boneCount > globset.boneOids.size())
                    issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                          " bone count exceeds its GLOBSET table");
                const std::size_t expectedPoseDeltas = subglob.vertices.size() * globset.poseWeights.size();
                if (subglob.posePositionDeltas.size() != expectedPoseDeltas ||
                    subglob.poseNormalDeltas.size() != expectedPoseDeltas)
                    issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " + std::to_string(subglobIndex) +
                          " pose-delta count mismatch");
                for (const auto &vertex : subglob.vertices)
                    for (std::size_t influence = 0; influence < 4; ++influence)
                        if (vertex.boneWeights[influence] != 0.0f &&
                            vertex.boneIndices[influence] >= globset.boneOids.size())
                            issue("GLOB " + std::to_string(glob.renderGroup) + " SUBGLOB " +
                                  std::to_string(subglobIndex) + " has an invalid bone index");
            }
            for (std::size_t celIndex = 0; celIndex < glob.sourceCelRecords.size(); ++celIndex)
            {
                const auto &cel = glob.sourceCelRecords[celIndex];
                if ((cel.points.size() & 3u) != 0 ||
                    cel.posePositionDeltas.size() != cel.points.size() * globset.poseWeights.size())
                    issue("GLOB " + std::to_string(glob.renderGroup) + " CEL " + std::to_string(celIndex) +
                          " source payload mismatch");
                const bool finitePoints =
                    std::all_of(cel.points.begin(), cel.points.end(),
                                [](const BrxSourceCelPoint &point)
                                {
                                    return std::all_of(std::begin(point.position), std::end(point.position),
                                                       [](const float value) { return std::isfinite(value); }) &&
                                           std::all_of(std::begin(point.boneWeights), std::end(point.boneWeights),
                                                       [](const float value) { return std::isfinite(value); });
                                });
                if (!finitePoints || std::any_of(cel.posePositionDeltas.begin(), cel.posePositionDeltas.end(),
                                                 [](const std::array<float, 4> &delta)
                                                 {
                                                     return std::any_of(delta.begin(), delta.end(),
                                                                        [](const float value)
                                                                        { return !std::isfinite(value); });
                                                 }))
                    issue("GLOB " + std::to_string(glob.renderGroup) + " CEL " + std::to_string(celIndex) +
                          " contains a non-finite point or pose delta");
                for (const auto &point : cel.points)
                    for (std::size_t influence = 0; influence < 4; ++influence)
                        if (point.boneWeights[influence] != 0.0f &&
                            point.boneIndices[influence] >= globset.boneOids.size())
                            issue("GLOB " + std::to_string(glob.renderGroup) + " CEL " + std::to_string(celIndex) +
                                  " has an invalid bone index");
            }
        }
    }
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        if (!CidDerivesFrom(object.cid, 2))
            continue;
        // Materialized PROXY clones are editor/runtime views of a reusable
        // source.  They are not serialized as independent ALO payloads and
        // therefore do not own independent decoded GLOBSET records.
        if (object.proxyClone)
            continue;
        const auto ownedGlobsets =
            std::count_if(document.Globsets().begin(), document.Globsets().end(), [&](const BrxGlobsetRecord &globset)
                          { return globset.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (ownedGlobsets != 1)
            issue("ALO-derived object " + std::to_string(objectIndex) + " owns " + std::to_string(ownedGlobsets) +
                  " GLOBSET records instead of one");
        else
        {
            const auto &globset = *std::find_if(
                document.Globsets().begin(), document.Globsets().end(), [&](const BrxGlobsetRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
            if (object.globPoseCount != globset.poseWeights.size())
                issue("ALO-derived object " + std::to_string(objectIndex) + " pose count disagrees with its GLOBSET");
        }
    }
    if (derivedSubglobCount != document.RenderBatches().size())
        issue("GLOB model/render batch count mismatch");
    if (derivedCelPacketCount != document.CelBatches().size())
        issue("GLOB model/cel packet count mismatch");
    for (const auto &batch : document.RenderBatches())
        if (!renderGroups.contains(batch.renderGroup))
            issue("Render batch references stale GLOB group " + std::to_string(batch.renderGroup));
    for (const auto &binding : document.GlobWarps())
    {
        if (!renderGroups.contains(binding.renderGroup))
            issue("WRBG binding references stale GLOB group " + std::to_string(binding.renderGroup));
        if (static_cast<std::size_t>(binding.firstVertex) + binding.vertexCount > document.RenderVertices().size())
            issue("WRBG binding for GLOB " + std::to_string(binding.renderGroup) + " has an invalid vertex range");
        if (static_cast<std::size_t>(binding.firstCelEdge) + binding.celEdgeCount > document.CelEdges().size())
            issue("WRBG binding for GLOB " + std::to_string(binding.renderGroup) + " has an invalid cel-edge range");
        if (binding.envelopeKind < -1 || binding.envelopeKind >= 7)
            issue("WRBG binding for GLOB " + std::to_string(binding.renderGroup) + " has an invalid envelope kind");
        if (binding.matrixCount > 4)
            issue("WRBG binding for GLOB " + std::to_string(binding.renderGroup) +
                  " exceeds ProjectCane's four deformation matrices");
        if (binding.envelopeKind != -1 &&
            (!std::isfinite(binding.innerDistance) || !std::isfinite(binding.innerWeight) ||
             !std::isfinite(binding.outerDistance) || !std::isfinite(binding.outerWeight)))
            issue("WRBG binding for GLOB " + std::to_string(binding.renderGroup) + " has a non-finite envelope");
    }
    out << "Modified GLOBs: " << modifiedGlobCount << '\n';
    for (const auto &[oid, count] : globOidCounts)
        if (count > 1)
            out << "[INFO] Contextual GLOB OID " << oid << " occurs " << count << " times\n";
    for (const auto &[oid, count] : oidCounts)
        if (count > 1)
            out << "[INFO] Contextual object OID " << oid << " occurs " << count << " times\n";
    std::vector<std::size_t> bitmapUses(document.Bitmaps().size(), 0);
    std::vector<std::size_t> shaderUses(document.Shaders().size(), 0);
    for (std::size_t bitmapIndex = 0; bitmapIndex < document.Bitmaps().size(); ++bitmapIndex)
    {
        const auto &bitmap = document.Bitmaps()[bitmapIndex];
        if (bitmap.width == 0 || bitmap.height == 0 || bitmap.width > 32768u || bitmap.height > 32768u)
            issue("Bitmap " + std::to_string(bitmapIndex) + " has dimensions outside ProjectCane's accepted range");
        if (bitmap.mipCount == 0 || bitmap.mipCount > 32u)
            issue("Bitmap " + std::to_string(bitmapIndex) + " has an invalid mip count");
        if (bitmap.textureFormat > 2u)
            issue("Bitmap " + std::to_string(bitmapIndex) + " has an unsupported texture format");
        if ((bitmap.textureFlags & (1u << 2u)) != 0u && bitmap.mipCount != 1u)
            issue("Bitmap " + std::to_string(bitmapIndex) +
                  " requests runtime mip generation but contains authored mip levels");

        if (bitmap.width != 0 && bitmap.height != 0 && bitmap.width <= 32768u && bitmap.height <= 32768u &&
            bitmap.mipCount != 0 && bitmap.mipCount <= 32u && bitmap.textureFormat <= 2u)
        {
            std::size_t expectedBytes = 0;
            std::uint32_t width = bitmap.width;
            std::uint32_t height = bitmap.height;
            for (std::uint32_t mip = 0; mip < bitmap.mipCount; ++mip)
            {
                const std::size_t levelBytes = bitmap.textureFormat == 0u
                                                   ? static_cast<std::size_t>(width) * height * 4u
                                                   : static_cast<std::size_t>((width + 3u) / 4u) *
                                                         ((height + 3u) / 4u) * (bitmap.textureFormat == 1u ? 8u : 16u);
                expectedBytes += levelBytes;
                width = (std::max)(1u, width >> 1u);
                height = (std::max)(1u, height >> 1u);
            }
            if (bitmap.pixelBytes != expectedBytes || bitmap.pixels.size() != expectedBytes)
                issue("Bitmap " + std::to_string(bitmapIndex) +
                      " pixel payload does not match its format and mip chain");
        }
    }
    for (std::size_t shaderIndex = 0; shaderIndex < document.Shaders().size(); ++shaderIndex)
    {
        const auto &shader = document.Shaders()[shaderIndex];
        const auto validateChannel = [&](const char *channelName, const std::vector<std::int32_t> &channel)
        {
            if (channel.size() > (std::numeric_limits<std::uint16_t>::max)())
                issue("Shader " + std::to_string(shaderIndex) + " " + channelName +
                      " channel exceeds its serialized U16 frame count");
            for (const auto bitmapIndex : channel)
            {
                // ProjectCane's LoadTexFromBrx reads a signed bitmap index.  -1 is the
                // canonical "no map" sentinel; values below -1 are malformed.
                if (bitmapIndex < -1 ||
                    (bitmapIndex >= 0 && static_cast<std::size_t>(bitmapIndex) >= document.Bitmaps().size()))
                    issue("Shader " + std::to_string(shaderIndex) + " " + channelName +
                          " channel references invalid bitmap " + std::to_string(bitmapIndex));
                else if (bitmapIndex >= 0)
                    ++bitmapUses[static_cast<std::size_t>(bitmapIndex)];
            }
        };
        if (shader.textureStages.size() != shader.textureStageCount)
            issue("Shader " + std::to_string(shaderIndex) +
                  " texture-stage count disagrees with its serialized stage records");
        if (shader.kind >= 11u)
            issue("Shader " + std::to_string(shaderIndex) + " has a kind outside ProjectCane's retail SHDK range");
        if (shader.renderPhase >= 18u)
            issue("Shader " + std::to_string(shaderIndex) + " has a render phase outside ProjectCane's retail range");
        if (!std::all_of(std::begin(shader.rgba), std::end(shader.rgba),
                         [](const float value) { return std::isfinite(value); }) ||
            !std::all_of(std::begin(shader.volumeRgba), std::end(shader.volumeRgba),
                         [](const float value) { return std::isfinite(value); }))
            issue("Shader " + std::to_string(shaderIndex) + " contains a non-finite material color");
        if (shader.animationKind != static_cast<std::int16_t>(shader.animation.kind))
            issue("Shader " + std::to_string(shaderIndex) +
                  " animation discriminator disagrees with its animation payload");
        for (std::size_t stageIndex = 0; stageIndex < shader.textureStages.size(); ++stageIndex)
        {
            const auto &stage = shader.textureStages[stageIndex];
            const std::string stageName = " stage " + std::to_string(stageIndex) + " ";
            validateChannel((stageName + "ambient").c_str(), stage.ambientBitmapIndices);
            validateChannel((stageName + "diffuse").c_str(), stage.diffuseBitmapIndices);
            validateChannel((stageName + "saturate").c_str(), stage.saturateBitmapIndices);
            if (stage.diffuseBitmapIndices.size() != stage.ambientBitmapIndices.size() ||
                stage.saturateBitmapIndices.size() != stage.ambientBitmapIndices.size())
                issue("Shader " + std::to_string(shaderIndex) + stageName +
                      "does not contain aligned ambient/diffuse/saturate frame triplets");
        }
        if (!shader.textureStages.empty())
        {
            const auto &primary = shader.textureStages.front();
            if (shader.ambientBitmapIndices != primary.ambientBitmapIndices ||
                shader.diffuseBitmapIndices != primary.diffuseBitmapIndices ||
                shader.saturateBitmapIndices != primary.saturateBitmapIndices)
                issue("Shader " + std::to_string(shaderIndex) +
                      " stage-zero preview cache disagrees with its serialized texture stage");
        }
        else if (!shader.ambientBitmapIndices.empty() || !shader.diffuseBitmapIndices.empty() ||
                 !shader.saturateBitmapIndices.empty())
            issue("Shader " + std::to_string(shaderIndex) +
                  " has stage-zero texture frames but no serialized texture stage");
    }
    for (std::size_t fontIndex = 0; fontIndex < document.Fonts().size(); ++fontIndex)
    {
        const auto &font = document.Fonts()[fontIndex];
        if (font.bitmapIndex < -1 ||
            (font.bitmapIndex >= 0 && static_cast<std::size_t>(font.bitmapIndex) >= bitmapUses.size()))
            issue("Font " + std::to_string(fontIndex) + " references invalid bitmap " +
                  std::to_string(font.bitmapIndex));
        else if (font.bitmapIndex >= 0)
            ++bitmapUses[static_cast<std::size_t>(font.bitmapIndex)];
        if (!std::isfinite(font.scale))
            issue("Font " + std::to_string(fontIndex) + " has a non-finite scale");
        if (font.glyphs.size() > (std::numeric_limits<std::uint16_t>::max)())
            issue("Font " + std::to_string(fontIndex) + " exceeds ProjectCane's serialized glyph-count limit");
    }
    for (const auto &glob : document.Globs())
        for (const auto &subglob : glob.sourceSubglobs)
            if (subglob.shaderIndex != 0xffffu)
            {
                if (subglob.shaderIndex >= document.Shaders().size())
                    issue("GLOB " + std::to_string(glob.renderGroup) + " references invalid shader " +
                          std::to_string(subglob.shaderIndex));
                else
                    ++shaderUses[subglob.shaderIndex];
            }
    for (std::size_t bitmapIndex = 0; bitmapIndex < bitmapUses.size(); ++bitmapIndex)
        if (bitmapUses[bitmapIndex] == 0)
            out << "[INFO] Orphan bitmap " << bitmapIndex << " (stable ID "
                << document.Bitmaps()[bitmapIndex].editorResourceId << ")\n";
    for (std::size_t shaderIndex = 0; shaderIndex < shaderUses.size(); ++shaderIndex)
        if (shaderUses[shaderIndex] == 0)
            out << "[INFO] Orphan material " << shaderIndex << " (stable ID "
                << document.Shaders()[shaderIndex].editorResourceId << ")\n";
    std::size_t playableRootCount = 0;
    const auto poRuntimeParent = [&](const BrxObjectRecord &candidate)
    {
        const bool serializedUnderProxy =
            candidate.parentIndex >= 0 && static_cast<std::size_t>(candidate.parentIndex) < document.Objects().size() &&
            document.Objects()[static_cast<std::size_t>(candidate.parentIndex)].cid == 83 &&
            candidate.proxyRecordIndex >= 0 && candidate.proxyRootIndex >= 0;
        return serializedUnderProxy ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
    };
    for (const auto &candidate : document.Objects())
    {
        if (!CidDerivesFrom(candidate.cid, 5) || poRuntimeParent(candidate) >= 0)
            continue;
        const auto playable = std::find_if(candidate.properties.begin(), candidate.properties.end(),
                                           [](const BrxPropertyRecord &property) { return property.eopid == 281; });
        const bool effectivePlayable =
            playable == candidate.properties.end() ? PoClassDefaultPlayable(candidate.cid) : playable->boolValue;
        if (effectivePlayable)
            ++playableRootCount;
    }
    if (playableRootCount > 16)
        issue("Playable root roster exceeds ProjectCane's 16-object PO list (" + std::to_string(playableRootCount) +
              " authored)");
    std::size_t projectedShadowCount = 0;
    for (const auto &candidate : document.Objects())
    {
        if (!CidDerivesFrom(candidate.cid, 2))
            continue;
        bool active = false;
        for (const auto &property : candidate.properties)
        {
            if (property.eopid == 43)
                active = property.boolValue;
            else if (property.eopid >= 44 && property.eopid <= 51)
                active = true;
        }
        if (active)
            ++projectedShadowCount;
    }
    if (projectedShadowCount > 64)
        issue("Projected-shadow roster exceeds ProjectCane's 64 texture-layer limit (" +
              std::to_string(projectedShadowCount) + " authored)");
    for (std::size_t zoneIndex = 0; zoneIndex < document.PathZones().size(); ++zoneIndex)
    {
        const auto owner = document.PathZones()[zoneIndex].ownerObjectIndex;
        if (owner < 0 || static_cast<std::size_t>(owner) >= document.Objects().size())
            issue("PATHZONE payload " + std::to_string(zoneIndex) + " has an invalid owner");
        else if (document.Objects()[static_cast<std::size_t>(owner)].cid != 138)
            issue("PATHZONE payload " + std::to_string(zoneIndex) + " is owned by a non-PATHZONE object");
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        const std::string identity =
            std::string(BrxDocument::ClassName(object.cid)) + " OID " + std::to_string(object.oid);
        if (!object.loaderImplemented)
            issue(identity + " has an incomplete loader");
        const bool derivesLo = CidDerivesFrom(object.cid, 1);
        const bool derivesAlo = CidDerivesFrom(object.cid, 2);
        const bool derivesSo = CidDerivesFrom(object.cid, 3);
        const bool derivesPo = CidDerivesFrom(object.cid, 5);
        const bool derivesStep = CidDerivesFrom(object.cid, 6);
        const bool derivesJt = CidDerivesFrom(object.cid, 7);
        // PloFindSwNearest/PloFindSwObject resolve duplicate OIDs relative to
        // the caller's hierarchy and proxy root. Character packages commonly
        // reuse their fixed child OIDs, so global first-match validation gives
        // the wrong answer as soon as more than one package is instantiated.
        const auto findEarlyContextualCompatible = [&](const std::int16_t oid, const std::int16_t requiredCid)
        {
            auto best = document.Objects().end();
            std::size_t bestDepth = std::numeric_limits<std::size_t>::max();
            std::vector<std::pair<std::int32_t, std::size_t>> contextAncestors;
            std::int32_t contextAncestor = static_cast<std::int32_t>(index);
            if (!CidDerivesFrom(object.cid, 2))
                contextAncestor =
                    object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
            for (std::size_t depth = 1;
                 contextAncestor >= 0 && static_cast<std::size_t>(contextAncestor) < document.Objects().size() &&
                 depth <= document.Objects().size();
                 ++depth)
            {
                contextAncestors.emplace_back(contextAncestor, depth);
                const auto &ancestor = document.Objects()[static_cast<std::size_t>(contextAncestor)];
                const std::int32_t parent =
                    ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                if (parent == contextAncestor)
                    break;
                contextAncestor = parent;
            }
            for (auto candidate = document.Objects().begin(); candidate != document.Objects().end(); ++candidate)
            {
                if (candidate->oid != oid)
                    continue;
                std::size_t depth = std::numeric_limits<std::size_t>::max();
                std::int32_t candidateAncestor =
                    static_cast<std::int32_t>(std::distance(document.Objects().begin(), candidate));
                if (!CidDerivesFrom(candidate->cid, 2))
                    candidateAncestor = candidate->proxyRuntimeParentIndex >= 0 ? candidate->proxyRuntimeParentIndex
                                                                                : candidate->parentIndex;
                for (std::size_t guard = 0; candidateAncestor >= 0 &&
                                            static_cast<std::size_t>(candidateAncestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto shared =
                        std::find_if(contextAncestors.begin(), contextAncestors.end(),
                                     [&](const auto &entry) { return entry.first == candidateAncestor; });
                    if (shared != contextAncestors.end())
                    {
                        depth = shared->second;
                        break;
                    }
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(candidateAncestor)];
                    const std::int32_t parent =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (parent == candidateAncestor)
                        break;
                    candidateAncestor = parent;
                }
                if (depth < bestDepth)
                {
                    best = candidate;
                    bestDepth = depth;
                }
            }
            return best != document.Objects().end() && CidDerivesFrom(best->cid, requiredCid)
                       ? best
                       : document.Objects().end();
        };
        if (object.cid == 0)
            issue(
                identity +
                " is an authored BASIC record; ProjectCane cannot instantiate CID_BASIC because BRX objects require a VTLO constructor");
        if (derivesAlo && !object.hasAloPayload && object.cid != 83)
            issue(identity + " derives from ALO but has no decoded ALO payload");
        if (derivesSo && !object.so.present)
            issue(identity + " derives from SO but has no decoded SO payload");
        if (object.cid == 4 && object.so.present && !object.so.fixedPhysics)
            issue(
                identity +
                " is an MS merged-static object with dynamic physics; RenderMsGlobset treats its GLOB bounds as static world-space bounds");
        if (!derivesAlo && object.hasAloPayload)
            issue(identity + " has an ALO payload but does not derive from ALO");
        if (!derivesSo && object.so.present)
            issue(identity + " has an SO payload but does not derive from SO");
        if (derivesPo && !derivesSo)
            issue(identity + " derives from PO without the required SO foundation");
        if (derivesStep && !derivesPo)
            issue(identity + " derives from STEP without the required PO foundation");
        if (derivesJt && !derivesStep)
            issue(identity + " derives from JT without the required STEP foundation");
        if (derivesLo)
        {
            if (const auto suckHide =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [](const BrxPropertyRecord &property) { return property.eopid == 3; });
                suckHide != object.properties.end() &&
                (!std::isfinite(suckHide->floatValue[0]) || !std::isfinite(suckHide->floatValue[1]) ||
                 suckHide->floatValue[0] > suckHide->floatValue[1]))
                issue(identity + " has inverted or non-finite LO suck/hide limits");
            std::unordered_map<std::int32_t, std::int16_t> customTypes;
            static constexpr std::int16_t expectedValueTypes[]{0, 5, 1, 9, 10, 6, 2};
            for (const auto &property : object.properties)
            {
                if (property.eopid < 14 || property.eopid > 20)
                    continue;
                if (property.kind != BrxPropertyKind::Dynamic || property.argumentCount != 2 ||
                    property.arguments.size() != 2 || property.arguments[0].type != 20 ||
                    property.arguments[1].type != expectedValueTypes[property.eopid - 14])
                {
                    issue(identity + " has malformed LO custom sidebag property EOPID " +
                          std::to_string(property.eopid));
                    continue;
                }
                const std::int32_t optionId = property.arguments[0].intValue;
                if (optionId < -32768 || optionId > 32767)
                    issue(identity + " has a custom sidebag option ID outside the runtime 16-bit range");
                if (optionId == -1)
                    issue(identity + " has a custom sidebag property with OPTID_Nil; it cannot be addressed usefully");
                const auto [found, inserted] = customTypes.emplace(optionId, property.eopid);
                if (!inserted)
                    issue(identity + " repeats custom sidebag option ID " + std::to_string(optionId) +
                          (found->second == property.eopid ? "; the later record overwrites the earlier value"
                                                           : "; the later record also changes its runtime type"));
                if (property.eopid == 18 && (!std::isfinite(property.arguments[1].floatValue[0]) ||
                                             !std::isfinite(property.arguments[1].floatValue[1]) ||
                                             property.arguments[1].floatValue[0] > property.arguments[1].floatValue[1]))
                    issue(identity + " has inverted or non-finite custom limits for option " +
                          std::to_string(optionId));
                if ((property.eopid == 16 || property.eopid == 17 || property.eopid == 20) &&
                    std::any_of(std::begin(property.arguments[1].floatValue),
                                std::begin(property.arguments[1].floatValue) + (property.eopid == 16 ? 1 : 3),
                                [](float value) { return !std::isfinite(value); }))
                    issue(identity + " has a non-finite custom sidebag value for option " + std::to_string(optionId));
            }
        }
        if (derivesLo && object.cid >= 0 && BrxDocument::AllowedPropertyCount(object.cid) == 0)
            issue(identity + " derives from LO but has no authoritative EOPID table");
        if (object.parentIndex < -1 || object.parentIndex == static_cast<std::int32_t>(index) ||
            object.parentIndex >= static_cast<std::int32_t>(document.Objects().size()))
            issue(identity + " has invalid parent " + std::to_string(object.parentIndex));
        else if (object.parentIndex >= 0)
        {
            const auto parentCid = document.Objects()[static_cast<std::size_t>(object.parentIndex)].cid;
            const bool legalCustomTail =
                parentCid == 120 || ((parentCid == 116 || parentCid == 125 || parentCid == 136) && object.cid == 131);
            if (!CidDerivesFrom(parentCid, 2) && !legalCustomTail)
                issue(
                    identity +
                    " has a parent that cannot serialize this child; only ALO child lists, EXPLG records, and WARP/EXIT/SM ASEGs are legal");
        }
        if (object.proxyRecordIndex >= 0)
        {
            if (static_cast<std::size_t>(object.proxyRecordIndex) >= document.Proxies().size())
                issue(identity + " has an invalid PROXY record index");
            else if (object.proxyRootIndex >= 0 &&
                     static_cast<std::size_t>(object.proxyRootIndex) >=
                         document.Proxies()[static_cast<std::size_t>(object.proxyRecordIndex)].roots.size())
                issue(identity + " has an invalid PROXY root index");
            if (object.proxySourceObjectIndex >= static_cast<std::int32_t>(document.Objects().size()))
                issue(identity + " has an invalid PROXY source counterpart");
            if (object.proxyRuntimeParentIndex >= static_cast<std::int32_t>(document.Objects().size()))
                issue(identity + " has an invalid PROXY runtime parent");
        }
        const bool finiteLocal = std::all_of(std::begin(object.localTransform), std::end(object.localTransform),
                                             [](const float value) { return std::isfinite(value); });
        const bool finiteWorld = std::all_of(std::begin(object.worldTransform), std::end(object.worldTransform),
                                             [](const float value) { return std::isfinite(value); });
        if (!finiteLocal || !finiteWorld)
            issue(identity + " has a non-finite ALO transform");
        if (CidDerivesFrom(object.cid, 126))
        {
            if (object.parentIndex >= 0 &&
                (static_cast<std::size_t>(object.parentIndex) >= document.Objects().size() ||
                 !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(object.parentIndex)].cid, 2)))
                issue(identity + " has a non-ALO parent; PNT::paloParent must be null or ALO-derived");
            if (!std::isfinite(object.localTransform[12]) || !std::isfinite(object.localTransform[13]) ||
                !std::isfinite(object.localTransform[14]))
                issue(identity + " has a non-finite local point position");
            constexpr float identityBasis[12]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
            bool positionOnlyTransform = true;
            for (std::size_t component = 0; component < 12; ++component)
                if (std::abs(object.localTransform[component] - identityBasis[component]) > 0.0001f)
                {
                    positionOnlyTransform = false;
                    break;
                }
            if (!positionOnlyTransform)
                issue(identity + " contains local rotation or scale that PNT cannot serialize");
            if (object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < document.Objects().size())
            {
                const float *parent = document.Objects()[static_cast<std::size_t>(object.parentIndex)].worldTransform;
                const float determinant = parent[0] * (parent[5] * parent[10] - parent[6] * parent[9]) -
                                          parent[4] * (parent[1] * parent[10] - parent[2] * parent[9]) +
                                          parent[8] * (parent[1] * parent[6] - parent[2] * parent[5]);
                if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
                    issue(identity + " has a singular ALO parent; PNT coordinate conversion is undefined");
                else
                {
                    const auto dot3 = [](const float *a, const float *b)
                    { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
                    const float x[3]{parent[0], parent[1], parent[2]};
                    const float y[3]{parent[4], parent[5], parent[6]};
                    const float z[3]{parent[8], parent[9], parent[10]};
                    const bool orthonormal = std::abs(dot3(x, x) - 1.0f) <= 0.001f &&
                                             std::abs(dot3(y, y) - 1.0f) <= 0.001f &&
                                             std::abs(dot3(z, z) - 1.0f) <= 0.001f && std::abs(dot3(x, y)) <= 0.001f &&
                                             std::abs(dot3(x, z)) <= 0.001f && std::abs(dot3(y, z)) <= 0.001f;
                    if (!orthonormal)
                        issue(
                            identity +
                            " has a scaled or sheared ALO parent; runtime ConvertAloPos uses the parent-basis transpose and assumes an orthonormal basis");
                }
            }
            for (const auto &property : object.properties)
            {
                if (property.eopid == 176)
                    issue(
                        identity +
                        " serializes script-writable but non-SetUser PNT local-position EOPID 176; authored local position belongs in the PNT payload");
                else if (property.eopid == 177)
                    issue(identity + " serializes runtime-derived PNT world-position query EOPID 177");
            }
        }
        if (object.cid >= 0 && static_cast<std::size_t>(object.cid) < CidOptionIds().size())
        {
            const auto &legal = CidOptionIds()[static_cast<std::size_t>(object.cid)];
            for (const auto &property : object.properties)
            {
                if (std::find(legal.begin(), legal.end(), property.eopid) == legal.end())
                    issue(identity + " contains illegal EOPID " + std::to_string(property.eopid));
            }
        }
        if (object.cid == 108)
            issue(
                identity +
                " serializes an SW object record; SW is the BRX world container and is not part of the authored object list");
        if (object.cid == 109)
        {
            issue(
                identity +
                " serializes a CM object record; ProjectCane creates the sole runtime CM as OID 4 during world loading");
            for (const auto &property : object.properties)
                if (property.eopid >= 584 && property.eopid <= 601)
                    issue(identity + " serializes runtime-only CM EOPID " + std::to_string(property.eopid) +
                          "; authored camera defaults belong to Scene World");
        }
        for (const auto &optionIssue : ValidateBaseOptions(object.cid, object.properties))
        {
            const char *severity = optionIssue.severity == EditorOptionIssueSeverity::Error     ? "error"
                                   : optionIssue.severity == EditorOptionIssueSeverity::Warning ? "warning"
                                                                                                : "info";
            issue(identity + " property " + severity + ": " + optionIssue.message);
        }
        for (const auto &property : object.properties)
        {
            const bool objectReference = property.eopid == 23 || property.eopid == 51 || property.eopid == 85;
            if (property.eopid == 124 && !property.arguments.empty())
            {
                const auto referencedOid = property.arguments.front().intValue;
                if (referencedOid != -1 &&
                    std::none_of(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate) { return candidate.oid == referencedOid; }))
                    issue(identity + " MATCH_OTHER_OBJECT references missing object OID " +
                          std::to_string(referencedOid));
            }
            if (!objectReference || property.intValue == -1)
                continue;
            if (std::none_of(document.Objects().begin(), document.Objects().end(),
                             [&](const BrxObjectRecord &candidate) { return candidate.oid == property.intValue; }))
                issue(identity + " property " + std::to_string(property.eopid) + " references missing object OID " +
                      std::to_string(property.intValue));
        }
        const auto validateTypedOid = [&](const std::int16_t oid, const std::int16_t requiredCid,
                                          const std::string &role, const bool optional = true,
                                          const bool contextual = false)
        {
            if (optional && oid == -1)
                return;
            auto found = document.Objects().end();
            std::size_t bestDepth = std::numeric_limits<std::size_t>::max();
            std::size_t bestCount = 0;
            std::vector<std::pair<std::int32_t, std::size_t>> contextAncestors;
            if (contextual)
            {
                std::int32_t ancestor = static_cast<std::int32_t>(index);
                if (!CidDerivesFrom(object.cid, 2))
                    ancestor =
                        object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
                for (std::size_t depth = 1;
                     ancestor >= 0 && static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                     depth <= document.Objects().size();
                     ++depth)
                {
                    contextAncestors.emplace_back(ancestor, depth);
                    const auto &current = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent =
                        current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex : current.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
            }
            for (auto candidate = document.Objects().begin(); candidate != document.Objects().end(); ++candidate)
            {
                if (candidate->oid != oid)
                    continue;
                std::size_t depth = contextual ? std::numeric_limits<std::size_t>::max() : 0u;
                if (contextual)
                {
                    std::int32_t ancestor =
                        static_cast<std::int32_t>(std::distance(document.Objects().begin(), candidate));
                    if (!CidDerivesFrom(candidate->cid, 2))
                        ancestor = candidate->proxyRuntimeParentIndex >= 0 ? candidate->proxyRuntimeParentIndex
                                                                           : candidate->parentIndex;
                    for (std::size_t guard = 0; ancestor >= 0 &&
                                                static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                                guard++ <= document.Objects().size();)
                    {
                        const auto shared = std::find_if(contextAncestors.begin(), contextAncestors.end(),
                                                         [&](const auto &entry) { return entry.first == ancestor; });
                        if (shared != contextAncestors.end())
                        {
                            depth = shared->second;
                            break;
                        }
                        const auto &current = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t parent = current.proxyRuntimeParentIndex >= 0
                                                        ? current.proxyRuntimeParentIndex
                                                        : current.parentIndex;
                        if (parent == ancestor)
                            break;
                        ancestor = parent;
                    }
                }
                if (depth < bestDepth)
                {
                    found = candidate;
                    bestDepth = depth;
                    bestCount = 1;
                }
                else if (depth == bestDepth)
                    ++bestCount;
            }
            if (found == document.Objects().end())
                issue(identity + " " + role + " references missing OID " + std::to_string(oid));
            else
            {
                if (contextual && bestCount > 1)
                    issue(identity + " " + role + " OID " + std::to_string(oid) + " has multiple equally-near matches");
                if (requiredCid >= 0 && found->cid != requiredCid && !CidDerivesFrom(found->cid, requiredCid))
                    issue(identity + " " + role + " references incompatible " +
                          std::string(BrxDocument::ClassName(found->cid)) + " OID " + std::to_string(oid));
            }
        };
        if (CidDerivesFrom(object.cid, 2))
        {
            const auto shadowProperty = [&](const std::int16_t eopid) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            // ProjectCane passes the authored fast-shadow radius/depth straight
            // into RenderFastShadow's scale.  Preserve retail's valid
            // non-positive-radius "disabled" behavior, but reject NaN/Inf so a
            // malformed option cannot poison the shadow transform.
            for (const auto [eopid, name] : {std::pair<std::int16_t, const char *>{41, "fast-shadow radius"},
                                             std::pair<std::int16_t, const char *>{42, "fast-shadow depth"}})
            {
                const auto *property = shadowProperty(eopid);
                if (property != nullptr && !std::isfinite(property->floatValue[0]))
                    issue(identity + " has non-finite " + name);
            }
            bool hasShadowOptions = false;
            for (const auto &property : object.properties)
                hasShadowOptions |= property.eopid >= 43 && property.eopid <= 51;
            if (hasShadowOptions)
            {
                const auto castIterator =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [](const BrxPropertyRecord &property) { return property.eopid == 43; });
                const bool setterAfterDisabledCast =
                    castIterator != object.properties.end() && !castIterator->boolValue &&
                    std::any_of(std::next(castIterator), object.properties.end(), [](const BrxPropertyRecord &property)
                                { return property.eopid >= 44 && property.eopid <= 51; });
                if (setterAfterDisabledCast)
                    issue(
                        identity +
                        " disables Cast Shadow before later SHADOW options; those setters recreate the runtime SHADOW record");

                const auto finiteScalar = [&](const std::int16_t eopid, const char *name)
                {
                    const auto *property = shadowProperty(eopid);
                    if (property != nullptr && !std::isfinite(property->floatValue[0]))
                        issue(identity + " has non-finite shadow " + name);
                };
                finiteScalar(45, "near radius");
                finiteScalar(46, "far radius");
                finiteScalar(47, "near cast distance");
                finiteScalar(48, "far cast distance");
                finiteScalar(49, "cone angle");
                const auto *up = shadowProperty(50);
                if (up != nullptr)
                {
                    const float lengthSquared = up->floatValue[0] * up->floatValue[0] +
                                                up->floatValue[1] * up->floatValue[1] +
                                                up->floatValue[2] * up->floatValue[2];
                    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.00000001f)
                        issue(identity + " has an invalid zero/non-finite shadow frustum-up vector");
                }

                const auto *shaderOption = shadowProperty(44);
                const std::int32_t shaderOid =
                    shaderOption == nullptr || shaderOption->intValue == -1 ? 0x2c : shaderOption->intValue;
                const auto shader =
                    std::find_if(document.Shaders().begin(), document.Shaders().end(),
                                 [&](const BrxShaderRecord &candidate) { return candidate.oid == shaderOid; });
                if (shader == document.Shaders().end())
                    issue(identity + " shadow shader OID " + std::to_string(shaderOid) + " does not exist");
                else if (shader->kind != 2 && shader->kind != 3)
                    issue(identity + " shadow shader OID " + std::to_string(shaderOid) +
                          " is not SHDK 2 or 3, so no GLOB receiver flag can accept it");
                else
                {
                    const std::uint16_t receiverBit = shader->kind == 2 ? 1u : 2u;
                    if (std::none_of(document.RenderBatches().begin(), document.RenderBatches().end(),
                                     [&](const BrxRenderBatch &batch)
                                     { return (batch.shadowReceiverMask & receiverBit) != 0; }))
                        issue(identity + " uses SHDK " + std::to_string(shader->kind) +
                              " but the level has no GLOB enabled to receive that shadow kind");
                }

                const auto *dynamicHelper = shadowProperty(51);
                if (dynamicHelper != nullptr && dynamicHelper->intValue != -1)
                    validateTypedOid(static_cast<std::int16_t>(dynamicHelper->intValue), 97,
                                     "dynamic shadow silhouette helper", false, true);
                else if (shader != document.Shaders().end() && shader->diffuseBitmapIndices.empty())
                    issue(identity + " static projected shadow shader has no diffuse texture");
            }
        }
        if (object.cid == 97)
        {
            if (object.renderGroupCount == 0)
                issue(identity + " DYSH has no GLOBSET silhouette geometry to render into the dynamic shadow texture");
            const bool referenced =
                std::any_of(document.Objects().begin(), document.Objects().end(),
                            [&](const BrxObjectRecord &candidate)
                            {
                                return std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                                   [&](const BrxPropertyRecord &property)
                                                   { return property.eopid == 51 && property.intValue == object.oid; });
                            });
            if (!referenced)
                issue(identity + " DYSH is not referenced by any ALO Dynamic Shadow Object OID (EOPID 51)");
        }
        if (object.cid == 101)
        {
            if (!object.dialog.present)
                issue(identity + " has no decoded DIALOG event payload");
            if (object.dialog.events.size() > 127)
                issue(identity + " exceeds the signed U8 limit of 127 dialog events");
            bool letterboxShown = false;
            std::int16_t leftSpeakerOid = -1;
            std::int16_t rightSpeakerOid = -1;
            for (std::size_t eventIndex = 0; eventIndex < object.dialog.events.size(); ++eventIndex)
            {
                const auto &event = object.dialog.events[eventIndex];
                const std::string eventIdentity = identity + " event " + std::to_string(eventIndex + 1);
                const int kind = static_cast<int>(event.kind);
                if (kind < 0 || kind >= 8)
                {
                    issue(eventIdentity + " has an invalid event kind");
                    continue;
                }
                if ((kind == 0 || kind == 7) && event.resourceName.empty())
                    issue(eventIdentity + " has an empty audio/movie resource name");
                if ((kind == 0 || kind == 7) && event.resourceName.size() > 32)
                    issue(eventIdentity + " resource name exceeds the 32-byte BRX limit");
                if (kind == 1 || kind == 2)
                {
                    validateTypedOid(event.primaryOid, 102, "event speaker", true, true);
                    validateTypedOid(event.replacementOid, 2, "event confrontation staging ALO", true, true);
                    if (kind == 1)
                        leftSpeakerOid = event.primaryOid;
                    else
                        rightSpeakerOid = event.primaryOid;
                }
                if (kind == 3)
                {
                    // Negative values are authored sentinels: keep the current zoom and
                    // do not impose a minimum pause, respectively.
                    if (!std::isfinite(event.zoom))
                        issue(eventIdentity + " has invalid zoom");
                    if (!std::isfinite(event.pauseSeconds))
                        issue(eventIdentity + " has invalid pause duration");
                    if (event.promptKind >= 15)
                        issue(eventIdentity + " has an invalid input prompt");
                    if (!event.waitForInput && event.promptKind != 0)
                        issue(eventIdentity + " selects an input prompt while player-input waiting is disabled");
                    if (event.subtitle.size() > 65535)
                        issue(eventIdentity + " subtitle exceeds the U16 serialized length limit");
                    validateTypedOid(event.speakerOid, 102, "talk speaker", true, true);
                    validateTypedOid(event.focusOid, 2, "camera focus", true, true);
                    // Retail uses PloFindSwNearest for dialog anchors, not the
                    // dialog-root-relative PloFindSwObject lookup used by the
                    // speaker, replacement actor, and focus object.
                    validateTypedOid(event.anchorPointOid, 126, "anchor point", true, false);
                    if (event.speakerOid != -1 && event.speakerOid != leftSpeakerOid &&
                        event.speakerOid != rightSpeakerOid)
                        issue(eventIdentity + " talks through a speaker not assigned to either presentation side");
                    if (event.lipsyncAnimationOid != -1)
                    {
                        std::size_t directMatchCount = 0;
                        const BrxObjectRecord *directLipsync = nullptr;
                        for (const auto &candidate : document.Objects())
                        {
                            if (candidate.oid != event.lipsyncAnimationOid)
                                continue;
                            const auto runtimeParent = candidate.proxyRuntimeParentIndex >= 0
                                                           ? candidate.proxyRuntimeParentIndex
                                                           : candidate.parentIndex;
                            if (runtimeParent != static_cast<std::int32_t>(index))
                                continue;
                            ++directMatchCount;
                            if (directLipsync == nullptr)
                                directLipsync = &candidate;
                        }
                        if (directLipsync == nullptr)
                            issue(eventIdentity +
                                  " explicit lipsync ASEG is not a direct DIALOG child (GRFFSO 2 lookup)");
                        else if (directLipsync->cid != 131)
                            issue(eventIdentity + " explicit lipsync child is not an ASEG");
                        if (directMatchCount > 1)
                            issue(eventIdentity + " explicit lipsync OID has multiple direct DIALOG children");
                    }
                }
                // Call-splice stores a label OID, not a scene-object reference.
                // HandleLoSpliceEvent forwards it directly to the splice system.
                if (kind == 5)
                {
                    if (letterboxShown)
                        issue(eventIdentity + " shows an already-visible letterbox");
                    letterboxShown = true;
                }
                if (kind == 6)
                {
                    if (!letterboxShown)
                        issue(eventIdentity + " hides a letterbox that is not visible");
                    letterboxShown = false;
                }
            }
            if (letterboxShown)
                issue(identity + " finishes with the letterbox still visible");
            for (const auto runtimeOnly : {std::int16_t{966}, std::int16_t{974}, std::int16_t{975}, std::int16_t{977}})
                if (std::any_of(object.properties.begin(), object.properties.end(),
                                [&](const BrxPropertyRecord &property) { return property.eopid == runtimeOnly; }))
                    issue(identity + " serializes runtime-only DIALOG option " + std::to_string(runtimeOnly));
            if (const auto cameraAnchor =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [](const BrxPropertyRecord &property) { return property.eopid == 967; });
                cameraAnchor != object.properties.end())
                validateTypedOid(static_cast<std::int16_t>(cameraAnchor->intValue), 126, "default camera anchor", true,
                                 false);
            // Option 976 is a persistence-table key. PostDialog passes it to
            // PfLookupDialog and does not resolve it as a scene object.
            std::size_t equivalenceCount = 0;
            for (const auto &equivalence : object.properties)
            {
                if (equivalence.eopid != 973)
                    continue;
                ++equivalenceCount;
                if (equivalence.arguments.size() != 2)
                    issue(identity + " has malformed dialog-equivalence arguments");
                else
                {
                    if (equivalence.arguments[0].intValue < 0)
                        issue(identity + " has a dialog equivalence with an invalid world ID");
                    const auto equivalentOid = static_cast<std::int16_t>(equivalence.arguments[1].intValue);
                    if (equivalentOid == -1)
                        issue(identity + " has a dialog equivalence with no dialog OID");
                }
            }
            if (equivalenceCount > 4)
                issue(identity + " has more than four dialog-equivalence links");
            const bool hasInstruction =
                std::any_of(object.properties.begin(), object.properties.end(),
                            [](const BrxPropertyRecord &property) { return property.eopid == 971; });
            const bool hasConfrontation =
                std::any_of(object.properties.begin(), object.properties.end(),
                            [](const BrxPropertyRecord &property) { return property.eopid == 972; });
            if (hasInstruction && hasConfrontation)
                issue(identity + " enables both instruction and confrontation presentation modes");
            const bool confrontationAudio =
                std::any_of(object.properties.begin(), object.properties.end(), [](const BrxPropertyRecord &property)
                            { return property.eopid == 978 && property.boolValue; });
            if (confrontationAudio && !hasConfrontation)
                issue(identity + " enables confrontation audio outside confrontation mode");
            const bool isDefaultDialog =
                std::any_of(object.properties.begin(), object.properties.end(), [](const BrxPropertyRecord &property)
                            { return property.eopid == 968 && property.boolValue; });
            if (isDefaultDialog)
            {
                const auto defaultCount =
                    std::count_if(document.Objects().begin(), document.Objects().end(),
                                  [](const BrxObjectRecord &candidate)
                                  {
                                      return candidate.cid == 101 &&
                                             std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                                         [](const BrxPropertyRecord &property)
                                                         { return property.eopid == 968 && property.boolValue; });
                                  });
                if (defaultCount > 1)
                    issue(identity + " competes with another default DIALOG for automatic playback");
            }
        }
        if (object.cid == 102)
        {
            const auto speakerProperty = [&](const std::int16_t id) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            if (const auto *value = speakerProperty(988))
                for (float component : value->floatValue)
                    if (!std::isfinite(component) || component < 0.0f || component > 1.0f)
                    {
                        issue(identity + " subtitle color is outside 0..1");
                        break;
                    }
            if (const auto *value = speakerProperty(989);
                value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] <= 0.0f ||
                          value->floatValue[0] >= 3.1415927f))
                issue(identity + " camera FOV must be between 0 and pi radians");
            if (const auto *value = speakerProperty(990);
                value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] <= 0.0f))
                issue(identity + " camera distance must be positive");
            for (const auto id : {std::int16_t{991}, std::int16_t{992}, std::int16_t{995}, std::int16_t{996}})
                if (const auto *value = speakerProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " screen-position option " + std::to_string(id) + " is not finite");
            if (const auto *idle = speakerProperty(997))
                validateTypedOid(static_cast<std::int16_t>(idle->intValue), 136, "idle state machine", true, true);
            for (const auto id : {std::int16_t{993}, std::int16_t{994}, std::int16_t{998}})
                if (const auto *value = speakerProperty(id);
                    value && (!std::isfinite(value->floatValue[0]) || !std::isfinite(value->floatValue[1]) ||
                              !std::isfinite(value->floatValue[2])))
                    issue(identity + " vector option " + std::to_string(id) + " contains a non-finite component");

            const auto isSpeakerDescendant = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0; candidate >= 0 &&
                                            static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                    const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                    const auto parent =
                        current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex : current.parentIndex;
                    if (parent == candidate)
                        break;
                    candidate = parent;
                }
                return false;
            };
            if (const auto *idle = speakerProperty(997); idle && idle->intValue >= 0)
            {
                const auto contextualIdle = std::find_if(
                    document.Objects().begin(), document.Objects().end(),
                    [&](const BrxObjectRecord &candidate)
                    {
                        if (candidate.oid != idle->intValue || candidate.cid != 136)
                            return false;
                        return isSpeakerDescendant(static_cast<std::int32_t>(&candidate - document.Objects().data()));
                    });
                if (contextualIdle == document.Objects().end())
                    issue(identity + " idle SM cannot resolve from the SPEAKER context");
            }
            const bool hasRenderableHierarchy =
                std::any_of(document.Objects().begin(), document.Objects().end(),
                            [&](const BrxObjectRecord &candidate)
                            {
                                const auto candidateIndex =
                                    static_cast<std::int32_t>(&candidate - document.Objects().data());
                                return candidate.renderGroupCount > 0 && isSpeakerDescendant(candidateIndex);
                            });
            if (!hasRenderableHierarchy)
                issue(identity + " has no renderable model in its hierarchy");

            const bool needsDefaultLipsync =
                std::any_of(document.Objects().begin(), document.Objects().end(),
                            [&](const BrxObjectRecord &candidate)
                            {
                                if (candidate.cid != 101 || !candidate.dialog.present)
                                    return false;
                                return std::any_of(candidate.dialog.events.begin(), candidate.dialog.events.end(),
                                                   [&](const BrxDialogEvent &event)
                                                   {
                                                       return event.kind == BrxDialogEventKind::Talk &&
                                                              event.speakerOid == object.oid &&
                                                              event.lipsyncAnimationOid == -1;
                                                   });
                            });
            if (needsDefaultLipsync)
            {
                const bool hasDefaultLipsync = std::any_of(
                    document.AnimationSegments().begin(), document.AnimationSegments().end(),
                    [&](const BrxAsegRecord &segment)
                    {
                        return segment.ownerObjectIndex >= 0 &&
                               static_cast<std::size_t>(segment.ownerObjectIndex) < document.Objects().size() &&
                               document.Objects()[static_cast<std::size_t>(segment.ownerObjectIndex)].oid == 1354 &&
                               isSpeakerDescendant(segment.ownerObjectIndex);
                    });
                if (!hasDefaultLipsync)
                    issue(identity +
                          " is used by a Talk event without explicit lipsync but has no contextual ASEG OID 1354");
            }
            if (const auto *roll = speakerProperty(999); roll && !std::isfinite(roll->floatValue[0]))
                issue(identity + " confrontation portrait roll must be finite");
        }
        if (object.cid == 103)
        {
            const auto validateWmChild = [&](const std::int16_t oid, const std::int16_t requiredCid, const char *role)
            {
                std::size_t matches = 0;
                bool compatible = false;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid)
                        continue;
                    bool belongs = false;
                    std::int32_t ancestorIndex = static_cast<std::int32_t>(candidateIndex);
                    for (std::size_t guard = 0; guard <= document.Objects().size() && ancestorIndex >= 0 &&
                                                static_cast<std::size_t>(ancestorIndex) < document.Objects().size();
                         ++guard)
                    {
                        if (ancestorIndex == static_cast<std::int32_t>(index))
                        {
                            belongs = true;
                            break;
                        }
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                        const auto parent = ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex
                                                                                  : ancestor.parentIndex;
                        if (parent == ancestorIndex)
                            break;
                        ancestorIndex = parent;
                    }
                    if (!belongs)
                        continue;
                    ++matches;
                    compatible =
                        compatible || candidate.cid == requiredCid || CidDerivesFrom(candidate.cid, requiredCid);
                }
                // Every WM pointer is null-checked by the retail controller:
                // maps may intentionally omit a layer, animation, or anchor.
                // Validate authored fixed links without inventing a required
                // payload that the game itself does not require.
                if (matches != 0 && !compatible)
                    issue(identity + " fixed world-map " + role + " OID " + std::to_string(oid) +
                          " has an incompatible class");
                if (matches > 1)
                    issue(identity + " has multiple contextual descendants at fixed world-map OID " +
                          std::to_string(oid));
            };
            for (const auto oid : {0x1cb, 0x1cc, 0x1cd, 0x1ce, 0x1cf, 0x1d0, 0x1d1, 0x1d2, 0x1d3, 0x1d4, 0x1d5, 0x1d6})
                validateWmChild(static_cast<std::int16_t>(oid), 131, "animation");
            for (int oid = 0x1d7; oid <= 0x1e1; ++oid)
                validateWmChild(static_cast<std::int16_t>(oid), 2, "primary model");
            for (int oid = 0x1e2; oid <= 0x1ec; ++oid)
                validateWmChild(static_cast<std::int16_t>(oid), 2, "secondary model");
            for (const auto oid : {0x1f8, 0x1f9, 0x1fa, 0x1fb, 0x1fc, 0x1fd, 0x1fe, 0x1ff})
                validateWmChild(static_cast<std::int16_t>(oid), 126, "secondary camera point");
            for (const auto oid : {0x1ed, 0x1ee, 0x1ef, 0x1f0, 0x1f1, 0x1f2, 0x1f3, 0x1f4, 0x1f5, 0x1f7})
                validateWmChild(static_cast<std::int16_t>(oid), 126, "primary camera point");
        }
        if (object.cid == 104)
        {
            const auto value = [&](const std::int16_t id, const float fallback)
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? fallback : found->floatValue[0];
            };
            if (const auto kills =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [](const BrxPropertyRecord &property) { return property.eopid == 1057; });
                kills != object.properties.end() && kills->intValue < 0)
                issue(identity + " kill goal must be non-negative");
            if (value(1058, 90.0f) <= 0.0f)
                issue(identity + " maximum game time must be positive");
            if (value(1060, 0.0f) > value(1061, 0.0f))
                issue(identity + " active enemy minimum exceeds maximum");
            if (value(1064, 0.0f) < 0.0f || value(1065, 0.0f) < 0.0f || value(1067, 0.0f) < 0.0f ||
                value(1068, 0.0f) < 0.0f)
                issue(identity + " has a negative spawn/respawn delay");
            int slotCount = 0;
            for (const auto &property : object.properties)
                if (property.eopid == 1059)
                {
                    ++slotCount;
                    // PostCrfodkSpawnerLoad uses PloFindSwNearest(..., pspawn).
                    // Despite its name, that search chooses the match whose shared
                    // ancestor is nearest to the context hierarchy; it is not the
                    // first global OID match and it is not a spatial-distance test.
                    std::vector<std::pair<std::int32_t, std::size_t>> contextAncestors;
                    std::int32_t contextAncestor = static_cast<std::int32_t>(index);
                    for (std::size_t depth = 1; contextAncestor >= 0 &&
                                                static_cast<std::size_t>(contextAncestor) < document.Objects().size() &&
                                                depth <= document.Objects().size();
                         ++depth)
                    {
                        contextAncestors.emplace_back(contextAncestor, depth);
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(contextAncestor)];
                        const std::int32_t parent = ancestor.proxyRuntimeParentIndex >= 0
                                                        ? ancestor.proxyRuntimeParentIndex
                                                        : ancestor.parentIndex;
                        if (parent == contextAncestor)
                            break;
                        contextAncestor = parent;
                    }
                    auto slot = document.Objects().end();
                    std::size_t bestContextDepth = std::numeric_limits<std::size_t>::max();
                    std::size_t equallyNearSlots = 0;
                    for (auto candidate = document.Objects().begin(); candidate != document.Objects().end();
                         ++candidate)
                    {
                        if (candidate->oid != property.intValue)
                            continue;
                        std::size_t candidateDepth = std::numeric_limits<std::size_t>::max();
                        std::int32_t ancestorIndex =
                            static_cast<std::int32_t>(std::distance(document.Objects().begin(), candidate));
                        for (std::size_t guard = 0;
                             ancestorIndex >= 0 &&
                             static_cast<std::size_t>(ancestorIndex) < document.Objects().size() &&
                             guard++ <= document.Objects().size();)
                        {
                            const auto shared =
                                std::find_if(contextAncestors.begin(), contextAncestors.end(),
                                             [&](const auto &entry) { return entry.first == ancestorIndex; });
                            if (shared != contextAncestors.end())
                            {
                                candidateDepth = shared->second;
                                break;
                            }
                            const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                            const std::int32_t parent = ancestor.proxyRuntimeParentIndex >= 0
                                                            ? ancestor.proxyRuntimeParentIndex
                                                            : ancestor.parentIndex;
                            if (parent == ancestorIndex)
                                break;
                            ancestorIndex = parent;
                        }
                        if (candidateDepth < bestContextDepth)
                        {
                            slot = candidate;
                            bestContextDepth = candidateDepth;
                            equallyNearSlots = 1;
                        }
                        else if (candidateDepth == bestContextDepth)
                            ++equallyNearSlots;
                    }
                    if (slot == document.Objects().end())
                        issue(identity + " spawn slot references missing OID " + std::to_string(property.intValue));
                    else if (!ObjectReferenceAccepts(1059, slot->cid))
                        issue(identity + " spawn slot must reference an ALO-derived slot root");
                    else
                    {
                        if (equallyNearSlots > 1)
                            issue(
                                identity + " spawn slot OID " + std::to_string(property.intValue) +
                                " has multiple equally-near hierarchy matches; retail selection depends on OID-list order");
                        const auto slotIndex =
                            static_cast<std::int32_t>(std::distance(document.Objects().begin(), slot));
                        const auto hasDescendant = [&](const std::int16_t oid, const std::int16_t cid)
                        {
                            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size();
                                 ++candidateIndex)
                            {
                                const auto &candidate = document.Objects()[candidateIndex];
                                if (candidate.oid != oid ||
                                    (candidate.cid != cid && !CidDerivesFrom(candidate.cid, cid)))
                                    continue;
                                std::int32_t ancestorIndex = static_cast<std::int32_t>(candidateIndex);
                                for (std::size_t guard = 0;
                                     guard <= document.Objects().size() && ancestorIndex >= 0 &&
                                     static_cast<std::size_t>(ancestorIndex) < document.Objects().size();
                                     ++guard)
                                {
                                    if (ancestorIndex == slotIndex)
                                        return true;
                                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                                    const auto parent = ancestor.proxyRuntimeParentIndex >= 0
                                                            ? ancestor.proxyRuntimeParentIndex
                                                            : ancestor.parentIndex;
                                    if (parent == ancestorIndex)
                                        break;
                                    ancestorIndex = parent;
                                }
                            }
                            return false;
                        };
                        if (!hasDescendant(1014, 115))
                            issue(identity + " spawn slot OID " + std::to_string(property.intValue) +
                                  " has no descendant XFM OID 1014 spawn transform");
                        if (!hasDescendant(1015, 131))
                            issue(identity + " spawn slot OID " + std::to_string(property.intValue) +
                                  " has no descendant ASEG OID 1015 spawn animation");
                    }
                }
            if (slotCount == 0)
                issue(identity + " has no spawn slots");
            if (slotCount > 16)
                issue(identity + " exceeds the runtime limit of sixteen spawn slots");
            if (const auto difficultySet =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [](const BrxPropertyRecord &property) { return property.eopid == 1076; });
                difficultySet != object.properties.end() &&
                (difficultySet->intValue < 0 || difficultySet->intValue > 1))
                issue(identity + " difficulty profile index must select Normal (0) or Assist (1)");
            if (value(1063, 0.3f) < 0.0f || value(1063, 0.3f) > 1.0f)
                issue(identity + " respawn-enable progress threshold must be between 0 and 1");
            if (value(1070, 0.8f) > value(1071, 1.0f))
                issue(identity + " initial difficulty minimum exceeds maximum");
            if (value(1073, 0.0f) > value(1074, 0.0f))
                issue(identity + " secondary difficulty minimum exceeds maximum");
            if (std::any_of(object.properties.begin(), object.properties.end(),
                            [](const BrxPropertyRecord &property) { return property.eopid == 1078; }))
                issue(identity + " serializes runtime-owned CRFODKSPAWN state-machine handle EOPID 1078");
            // SnipAloObjects resolves all three with the spawner as context
            // (GRFFSO 5), so duplicate OIDs elsewhere in the world are not
            // interchangeable with the nearest hierarchy match.
            validateTypedOid(0x3f8, 140, "fixed respawn RWM", false, true);
            validateTypedOid(0x3f9, 140, "fixed spawn RWM", false, true);
            validateTypedOid(0x3fa, 136, "fixed state machine", false, true);
        }
        if (object.cid == 105)
        {
            const auto validateHubselDescendant =
                [&](const std::int16_t oid, const std::int16_t requiredCid, const char *role)
            {
                std::size_t matches = 0;
                bool compatible = false;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid)
                        continue;
                    std::int32_t ancestorIndex = static_cast<std::int32_t>(candidateIndex);
                    bool belongs = false;
                    for (std::size_t guard = 0; guard <= document.Objects().size() && ancestorIndex >= 0 &&
                                                static_cast<std::size_t>(ancestorIndex) < document.Objects().size();
                         ++guard)
                    {
                        if (ancestorIndex == static_cast<std::int32_t>(index))
                        {
                            belongs = true;
                            break;
                        }
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                        const auto parent = ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex
                                                                                  : ancestor.parentIndex;
                        if (parent == ancestorIndex)
                            break;
                        ancestorIndex = parent;
                    }
                    if (!belongs)
                        continue;
                    ++matches;
                    compatible =
                        compatible || candidate.cid == requiredCid || CidDerivesFrom(candidate.cid, requiredCid);
                }
                if (matches == 0)
                    issue(identity + " is missing contextual HUBSEL " + role + " OID " + std::to_string(oid));
                else if (!compatible)
                    issue(identity + " contextual HUBSEL " + role + " OID " + std::to_string(oid) +
                          " has an incompatible class");
                if (matches > 1)
                    issue(identity + " has multiple contextual HUBSEL " + role + " matches at OID " +
                          std::to_string(oid));
            };
            for (const auto oid : {0x4f9, 0x4fa, 0x4fb})
                validateHubselDescendant(static_cast<std::int16_t>(oid), 136, "state machine");
            validateTypedOid(0x4fc, 136, "global HUBSEL display state machine", false);
            for (const auto oid : {0x50b, 0x50c, 0x50d, 0x50e, 0x50f})
                validateHubselDescendant(static_cast<std::int16_t>(oid), 131, "world animation");
            for (const auto oid : {0x510, 0x511, 0x512, 0x513, 0x514})
                validateTypedOid(static_cast<std::int16_t>(oid), 2, "global HUBSEL world model", false);
            for (const auto oid : {0x33b, 0x33c, 0x33d, 0x33e})
                validateTypedOid(static_cast<std::int16_t>(oid), 101, "global HUBSEL progress dialog", false);
        }
        if (object.cid == 106)
        {
            if (object.oid < 1301 || object.oid > 1305)
                issue(identity +
                      " HUBPROG OID must be 1301..1305; ProjectCane derives the displayed world from this OID");
            for (std::int16_t oid = 0x51a; oid <= 0x522; ++oid)
            {
                std::size_t matches = 0;
                bool compatible = false;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid)
                        continue;
                    std::int32_t ancestorIndex = static_cast<std::int32_t>(candidateIndex);
                    bool belongs = false;
                    for (std::size_t guard = 0; guard <= document.Objects().size() && ancestorIndex >= 0 &&
                                                static_cast<std::size_t>(ancestorIndex) < document.Objects().size();
                         ++guard)
                    {
                        if (ancestorIndex == static_cast<std::int32_t>(index))
                        {
                            belongs = true;
                            break;
                        }
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                        const auto parent = ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex
                                                                                  : ancestor.parentIndex;
                        if (parent == ancestorIndex)
                            break;
                        ancestorIndex = parent;
                    }
                    if (!belongs)
                        continue;
                    ++matches;
                    compatible = compatible || candidate.cid == 2 || CidDerivesFrom(candidate.cid, 2);
                }
                // Retail explicitly skips null progress entries, allowing a
                // HUBPROG hierarchy to omit indicators it does not use.
                if (matches != 0 && !compatible)
                    issue(identity + " contextual HUBPROG progress OID " + std::to_string(oid) + " is not ALO-derived");
                if (matches > 1)
                    issue(identity + " has multiple contextual HUBPROG progress matches at OID " + std::to_string(oid));
            }
        }
        if (derivesPo)
        {
            const auto playable = std::find_if(object.properties.begin(), object.properties.end(),
                                               [](const BrxPropertyRecord &property) { return property.eopid == 281; });
            const bool effectivePlayable =
                playable == object.properties.end() ? PoClassDefaultPlayable(object.cid) : playable->boolValue;
            if (effectivePlayable && poRuntimeParent(object) >= 0)
                issue(identity + " is marked playable but is parented; AddPoToList only registers root POs");

            for (const auto runtimeOption : {std::int16_t{282}, std::int16_t{283}, std::int16_t{284}})
                if (std::any_of(object.properties.begin(), object.properties.end(),
                                [&](const BrxPropertyRecord &property) { return property.eopid == runtimeOption; }))
                    issue(identity + " serializes runtime-only PO option " + std::to_string(runtimeOption) +
                          " instead of authored PO configuration");
        }
        if (derivesStep)
        {
            const auto stepFloat = [&](const std::int16_t eopid, const float fallback)
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
                return found == object.properties.end() ? fallback : found->floatValue[0];
            };
            const float baseOffset = stepFloat(709, 35.0f);
            const float spinRatio = stepFloat(710, 1.0f);
            const float gravityRatio = stepFloat(711, CidDerivesFrom(object.cid, 8) ? 1.5f : 1.0f);
            if (!std::isfinite(baseOffset))
                issue(identity + " has an invalid STEP base contact offset");
            if (!std::isfinite(spinRatio) || spinRatio <= 0.0f)
                issue(identity + " has an invalid STEP turning speed ratio");
            if (!std::isfinite(gravityRatio))
                issue(identity + " has a non-finite STEP gravity ratio");

            for (const auto &property : object.properties)
            {
                const bool runtimeCommand = (property.eopid >= 732 && property.eopid <= 734) || property.eopid == 736 ||
                                            (property.eopid >= 737 && property.eopid <= 740) ||
                                            (property.eopid >= 743 && property.eopid <= 745);
                if (runtimeCommand)
                    issue(identity + " serializes runtime-only STEP/JT command " + std::to_string(property.eopid) +
                          " instead of authored STEP configuration");
            }
        }
        if (derivesJt)
        {
            const auto jtHierarchyObject = [&](const std::int16_t oid,
                                               const std::int16_t requiredBaseCid) -> const BrxObjectRecord *
            {
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid || !CidDerivesFrom(candidate.cid, requiredBaseCid))
                        continue;
                    std::int32_t ancestor = static_cast<std::int32_t>(candidateIndex);
                    for (std::size_t guard = 0; ancestor >= 0 &&
                                                static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                                guard++ <= document.Objects().size();)
                    {
                        if (ancestor == static_cast<std::int32_t>(index))
                            return &candidate;
                        const auto next = document.Objects()[static_cast<std::size_t>(ancestor)].parentIndex;
                        if (next == ancestor)
                            break;
                        ancestor = next;
                    }
                }
                return nullptr;
            };
            const auto requireJtBinding =
                [&](const std::int16_t oid, const std::int16_t requiredBaseCid, const char *label)
            {
                if (jtHierarchyObject(oid, requiredBaseCid) == nullptr)
                    issue(identity + " is missing required JT " + label + " binding OID " + std::to_string(oid));
            };
            requireJtBinding(0x60, 2, "gut ALO");
            requireJtBinding(0x35, 2, "left hand ALO");
            requireJtBinding(0x36, 2, "right hand ALO");
            requireJtBinding(0x16f, 2, "cane tool ALO");
            requireJtBinding(0x170, 2, "cane hook ALO");
            requireJtBinding(0x64, 3, "primary physics SO");
            if (jtHierarchyObject(0x37, 2) == nullptr && jtHierarchyObject(0x38, 2) == nullptr)
                issue(identity + " is missing both JT left-foot ball OID 55 and heel OID 56");
            if (jtHierarchyObject(0x3a, 2) == nullptr && jtHierarchyObject(0x3b, 2) == nullptr)
                issue(identity + " is missing both JT right-foot ball OID 58 and heel OID 59");
            if (std::any_of(object.properties.begin(), object.properties.end(),
                            [](const BrxPropertyRecord &property) { return property.eopid == 711; }))
                issue(identity +
                      " has an inherited STEP gravity override that PostJtLoad replaces with JT's fixed 1.3 ratio");
            const auto pipeDirection =
                std::find_if(object.properties.begin(), object.properties.end(),
                             [](const BrxPropertyRecord &property) { return property.eopid == 736; });
            if (pipeDirection != object.properties.end() &&
                (pipeDirection->intValue < -1 || pipeDirection->intValue > 1))
                issue(identity + " has an invalid JT pipe-travel direction; expected automatic, up, or down");
        }
        if (object.hasAloPayload)
        {
            const auto propertyById = [&](const std::int16_t eopid) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            if (object.zoneMode >= 3 || object.visibilityMode >= 3 || object.mrdMode >= 3)
                issue(identity + " has an invalid ALO zone/visibility/MRD mode");
            for (const auto [value, label] :
                 {std::pair<float, const char *>{object.maximumRenderDistance, "maximum render distance"},
                  {object.celBorderMaximumDistance, "cel-border maximum distance"},
                  {object.renderRadiusSelf, "self render radius"},
                  {object.renderRadiusAll, "hierarchy render radius"}})
                if (!std::isfinite(value) || value < 0.0f)
                    issue(identity + " has invalid ALO " + label);
            if (object.renderRadiusAll < object.renderRadiusSelf)
                issue(identity + " has an ALO hierarchy radius smaller than its self radius");
            if (object.poseCombinations.size() > 255u)
                issue(identity + " has more than the U8 pose-combination count can serialize");
            std::unordered_set<std::int16_t> poseOids;
            for (std::size_t poseIndex = 0; poseIndex < object.poseCombinations.size(); ++poseIndex)
            {
                const auto &pose = object.poseCombinations[poseIndex];
                if (pose.weights.size() != object.globPoseCount)
                    issue(identity + " pose combination " + std::to_string(poseIndex) +
                          " weight count does not match the GLOBSET pose count");
                if (!poseOids.insert(pose.oid).second)
                    issue(identity + " has duplicate pose-combination OID " + std::to_string(pose.oid) +
                          "; SetAloPoseCombo selects the first match");
                if (pose.oid == -1)
                    issue(identity + " pose combination " + std::to_string(poseIndex) +
                          " uses OID -1, which SetAloPoseCombo reserves for disabling the blend");
                for (const float weight : pose.weights)
                    if (!std::isfinite(weight))
                    {
                        issue(identity + " pose combination " + std::to_string(poseIndex) +
                              " contains a non-finite weight");
                        break;
                    }
            }
            if ((object.alox.flags & ~0xffu) != 0)
                issue(identity + " serializes unsupported ALOX flag bits");
            if ((object.alox.flags & 0x80u) != 0 && (object.alox.flags & 0x40u) == 0)
                issue(identity + " enables ALOX segment-scale compensation without a joint link");
            const auto validateAloxNearestOid =
                [&](const char *label, const std::int16_t oid, const std::int16_t requiredBaseCid)
            {
                if (oid == -1)
                    return;
                const auto resolved = findEarlyContextualCompatible(oid, requiredBaseCid);
                if (resolved == document.Objects().end())
                {
                    const bool exists =
                        std::any_of(document.Objects().begin(), document.Objects().end(),
                                    [&](const BrxObjectRecord &candidate) { return candidate.oid == oid; });
                    if (exists)
                        issue(identity + " ALOX " + label + " references OID " + std::to_string(oid) +
                              " with an incompatible class or context");
                    else
                        issue(identity + " ALOX " + label + " references missing OID " + std::to_string(oid));
                }
            };
            const auto validateAloxAncestorOid = [&](const char *label, const std::int16_t oid)
            {
                if (oid == -1)
                    return;
                std::int32_t ancestorIndex = static_cast<std::int32_t>(index);
                for (std::size_t guard = 0; ancestorIndex >= 0 &&
                                            static_cast<std::size_t>(ancestorIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                    if (ancestor.oid == oid && CidDerivesFrom(ancestor.cid, 2))
                        return;
                    const std::int32_t parent =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (parent == ancestorIndex)
                        break;
                    ancestorIndex = parent;
                }
                if (std::any_of(document.Objects().begin(), document.Objects().end(),
                                [&](const BrxObjectRecord &candidate) { return candidate.oid == oid; }))
                    issue(identity + " ALOX " + label + " OID " + std::to_string(oid) +
                          " is not this ALO or one of its ALO ancestors (retail mode 3)");
                else
                    issue(identity + " ALOX " + label + " references missing OID " + std::to_string(oid));
            };
            // LoadAloAloxFromBrx resolves the SCJ rotation source immediately
            // with mode 3. MatchSwObject accepts only marked ALOs on the
            // current object's ancestor chain. The remaining links are bound
            // later by BindAloAlox with nearest mode 4.
            if ((object.alox.flags & 0x0cu) != 0)
                validateAloxAncestorOid("rotation source", object.alox.rotationSourceOid);
            if (object.alox.editorFosterOid != -1 &&
                std::none_of(document.Objects().begin(), document.Objects().end(),
                             [&](const BrxObjectRecord &candidate)
                             {
                                 return candidate.offset == object.alox.editorFosterObjectOffset &&
                                        candidate.oid == object.alox.editorFosterOid &&
                                        candidate.spliceIndex == object.alox.editorFosterSpliceIndex;
                             }))
                issue(identity + " has a stale editor foster-basis reference");
            if ((object.alox.flags & 0x10u) != 0)
                validateAloxNearestOid("IK host", object.alox.ikTargetOid, 2);
            if ((object.alox.flags & 0x20u) != 0)
                validateAloxNearestOid("elbow", object.alox.elbowOid, 2);
            if ((object.alox.flags & 0x40u) != 0)
            {
                validateAloxNearestOid("joint parent", object.alox.jointParentOid, 2);
                validateAloxNearestOid("joint child", object.alox.jointChildOid, 2);
            }
            if ((object.alox.flags & 0x01u) != 0)
                for (const float value : object.alox.preRotation)
                    if (!std::isfinite(value))
                    {
                        issue(identity + " has a non-finite ALOX pre-rotation matrix");
                        break;
                    }
            if ((object.alox.flags & 0x02u) != 0)
                for (const float value : object.alox.postRotation)
                    if (!std::isfinite(value))
                    {
                        issue(identity + " has a non-finite ALOX post-rotation matrix");
                        break;
                    }
            if ((object.alox.flags & 0x20u) != 0 &&
                (!std::isfinite(object.alox.ikPosition[0]) || !std::isfinite(object.alox.ikPosition[1]) ||
                 !std::isfinite(object.alox.ikPosition[2]) || !std::isfinite(object.alox.wristPosition[0]) ||
                 !std::isfinite(object.alox.wristPosition[1]) || !std::isfinite(object.alox.wristPosition[2]) ||
                 !std::isfinite(object.alox.twistRadians)))
                issue(identity + " has a non-finite ALOX IK payload");
            if ((object.alox.flags & 0x20u) != 0 && object.alox.elbowOid == object.oid)
                issue(identity + " uses itself as its ALOX elbow");
            if ((object.alox.flags & 0x40u) != 0 &&
                (object.alox.jointParentOid == object.oid || object.alox.jointChildOid == object.oid))
                issue(identity + " contains a self-referential ALOX joint link");
            if ((object.alox.flags & 0x40u) != 0 && object.alox.jointParentOid != -1 &&
                object.alox.jointParentOid == object.alox.jointChildOid)
                issue(identity + " uses the same ALOX object as both joint parent and child");
            const auto validateAloEnum =
                [&](const std::int16_t eopid, const int minimum, const int maximumExclusive, const char *label)
            {
                if (const auto *property = propertyById(eopid);
                    property && (property->intValue < minimum || property->intValue >= maximumExclusive))
                    issue(identity + " has an invalid " + label);
            };
            validateAloEnum(26, -1, 3, "ASEGD interpolation kind");
            validateAloEnum(53, 0, 3, "freeze mode");
            for (const std::int16_t eopid : {55, 61, 73, 74, 76})
                validateAloEnum(eopid, -1, 10, "ALO acknowledgement mode");
            validateAloEnum(97, -1, 6, "throb kind");
            if (const auto *lookAtMode = propertyById(76))
            {
                // Despite the generated OTYP_Mtlk tag, SetAloLookAt consumes
                // this value as ACK and normalizes ACK_Smooth to
                // ACK_SmoothNoLock. It is not an object reference.
                if (lookAtMode->intValue < -1 || lookAtMode->intValue >= 10)
                    issue(identity + " has an invalid look-at ACK mode");
                const auto belongsToHierarchy = [&](std::int32_t candidate)
                {
                    for (std::size_t guard = 0; candidate >= 0 &&
                                                static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                                guard++ <= document.Objects().size();)
                    {
                        if (candidate == static_cast<std::int32_t>(index))
                            return true;
                        candidate = document.Objects()[static_cast<std::size_t>(candidate)].parentIndex;
                    }
                    return false;
                };
                const bool hasRtck =
                    std::any_of(document.Globs().begin(), document.Globs().end(), [&](const BrxGlobRecord &glob)
                                { return belongsToHierarchy(glob.ownerObjectIndex) && glob.rotationTracking != 0; });
                if (hasRtck)
                    issue(identity + " combines ALO look-at with GLOB rotation tracking; verify the intended priority");
            }
            if (const auto *animationRate = propertyById(25);
                animationRate && (!std::isfinite(animationRate->floatValue[0]) || animationRate->floatValue[0] < 0.0f))
                issue(identity + " has an invalid local animation playback rate");
            if (const auto *ignoreRadius = propertyById(77);
                ignoreRadius != nullptr &&
                (!std::isfinite(ignoreRadius->floatValue[0]) || ignoreRadius->floatValue[0] < 0.0f))
                issue(identity + " has an invalid look-at ignore radius");
        }
        if (object.so.present)
        {
            const auto &so = object.so;
            if (!std::isfinite(so.mass) || so.mass < 0.0f)
                issue(identity + " has invalid SO mass");
            if (!so.fixedPhysics && so.mass <= 0.0f)
                issue(identity + " is dynamic with zero SO mass");
            bool finiteInertia = true;
            for (const float value : so.inertia)
                finiteInertia &= std::isfinite(value);
            if (!finiteInertia)
                issue(identity + " has a non-finite SO inertia tensor");
            if (std::any_of(std::begin(so.centerOfMass), std::end(so.centerOfMass),
                            [](const float value) { return !std::isfinite(value); }))
                issue(identity + " has a non-finite SO center of mass");
            if (std::abs(so.inertia[1] - so.inertia[3]) > 0.001f || std::abs(so.inertia[2] - so.inertia[6]) > 0.001f ||
                std::abs(so.inertia[5] - so.inertia[7]) > 0.001f)
                issue(identity + " has a non-symmetric SO inertia tensor");
            const float inertiaDeterminant =
                so.inertia[0] * (so.inertia[4] * so.inertia[8] - so.inertia[5] * so.inertia[7]) -
                so.inertia[1] * (so.inertia[3] * so.inertia[8] - so.inertia[5] * so.inertia[6]) +
                so.inertia[2] * (so.inertia[3] * so.inertia[7] - so.inertia[4] * so.inertia[6]);
            if (!so.fixedPhysics && finiteInertia && std::abs(inertiaDeterminant) <= 0.000001f)
                issue(identity + " is dynamic with a singular SO inertia tensor");
            if (!so.fixedPhysics && finiteInertia &&
                (so.inertia[0] <= 0.0f || so.inertia[0] * so.inertia[4] - so.inertia[1] * so.inertia[3] <= 0.0f ||
                 inertiaDeterminant <= 0.0f))
                issue(identity + " is dynamic with a non-positive-definite SO inertia tensor");
            const auto sphere = std::find_if(object.properties.begin(), object.properties.end(),
                                             [](const BrxPropertyRecord &property) { return property.eopid == 133; });
            if (sphere != object.properties.end() &&
                (!std::isfinite(sphere->floatValue[0]) || sphere->floatValue[0] <= 0.0f))
                issue(identity + " has an invalid sphere-collision radius");
            const auto soProperty = [&](const std::int16_t eopid) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == eopid; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            if (const auto *edgeGrab = soProperty(146))
            {
                if (edgeGrab->kind != BrxPropertyKind::Dynamic || edgeGrab->arguments.size() != 1 ||
                    edgeGrab->arguments[0].type != 5)
                    issue(identity + " has a malformed SO edge-grab authoring call");
                else if (edgeGrab->arguments[0].intValue < -1 || edgeGrab->arguments[0].intValue >= 3)
                    issue(identity + " has an invalid SO edge-grab policy");
            }
            const auto validateSoEnum =
                [&](const std::int16_t eopid, const int minimum, const int maximumExclusive, const char *label)
            {
                if (const auto *property = soProperty(eopid);
                    property && (property->intValue < minimum || property->intValue >= maximumExclusive))
                    issue(identity + " has an invalid SO " + label);
            };
            // Retail BRX data authors the terminal MTLK value 12 on hidden
            // trigger SOs (for example The Lair of the Beast, OID 1899).
            // SetSoMtlk stores it verbatim and performs no exclusive-MTLK_Max
            // rejection, so treating 12 as corrupt makes an original level
            // fail editor validation. Values above the terminal value remain
            // invalid.
            validateSoEnum(128, -1, 13, "material kind");
            // ConstrFromCnstr indexes an eight-entry table directly; CNSTR_Nil
            // is an enum sentinel but is unsafe as an authored setter value.
            validateSoEnum(131, 0, 8, "force constraint");
            validateSoEnum(132, 0, 8, "torque constraint");
            validateSoEnum(143, -1, 5, "collision-zone policy");
            validateSoEnum(145, -1, 3, "camera collision style");
            for (const std::int16_t eopid : {134, 135})
                if (const auto *property = soProperty(eopid);
                    property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                    issue(identity + " has an invalid SO fluid coefficient");
            if (const auto *noInteraction = soProperty(130); noInteraction && !noInteraction->boolValue)
                issue(identity + " authors Disable interaction as false, but SetSoNoInteract ignores "
                                 "the value and locks the object whenever the property is present");
            const auto validateBsp = [&](const char *kind, const BrxSoGeometryInfo &geometry)
            {
                std::unordered_set<std::uint32_t> physicalEdges;
                std::size_t serializedEdgeCount = 0;
                if (geometry.positions.size() >
                        static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
                    geometry.surfaces.size() >
                        static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u)
                    issue(identity + " " + kind + " geometry exceeds the retail signed index range");
                if (geometry.positionCount != geometry.positions.size())
                    issue(identity + " " + kind + " cached position count does not match its geometry records");
                if (geometry.surfaceCount != geometry.surfaces.size())
                    issue(identity + " " + kind + " cached surface count does not match its geometry records");
                for (const auto &position : geometry.positions)
                    if (!std::all_of(position.begin(), position.end(),
                                     [](const float value) { return std::isfinite(value); }))
                    {
                        issue(identity + " " + kind + " geometry contains a non-finite position");
                        break;
                    }
                for (std::size_t surfaceIndex = 0; surfaceIndex < geometry.surfaces.size(); ++surfaceIndex)
                {
                    const auto &surface = geometry.surfaces[surfaceIndex];
                    if (surface.edges.size() > static_cast<std::size_t>((std::numeric_limits<std::int8_t>::max)()))
                        issue(identity + " " + kind + " surface exceeds the retail signed edge-count range");
                    if (!std::isfinite(surface.radius) || surface.radius < 0.0f ||
                        !std::all_of(
                            surface.center.begin(), surface.center.end(),
                            [](const float value) { return std::isfinite(value); }))
                        issue(identity + " " + kind + " surface " + std::to_string(surfaceIndex) +
                              " has invalid bounds data");
                    for (const auto position : surface.positionIndices)
                        if (position >= geometry.positions.size())
                        {
                            issue(identity + " " + kind + " surface has an invalid position index");
                            break;
                        }
                    for (const auto &edge : surface.edges)
                    {
                        ++serializedEdgeCount;
                        if (edge.firstPosition >= geometry.positions.size() ||
                            edge.secondPosition >= geometry.positions.size() ||
                            edge.adjacentSurface >= geometry.surfaces.size() ||
                            edge.adjacentSurface >
                                static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))
                        {
                            issue(identity + " " + kind + " surface has an invalid edge record");
                            break;
                        }
                        const auto low = (std::min)(edge.firstPosition, edge.secondPosition);
                        const auto high = (std::max)(edge.firstPosition, edge.secondPosition);
                        const std::uint32_t key = (static_cast<std::uint32_t>(low) << 16u) | high;
                        if (!physicalEdges.insert(key).second)
                            issue(identity + " " + kind + " geometry serializes the same physical edge more than once");
                    }
                }
                if (serializedEdgeCount != geometry.edgeCount)
                    issue(identity + " " + kind + " geometry edge count does not match its serialized edge records");
                if (geometry.fullBspNodeCount < geometry.bspNodes.size())
                    issue(identity + " " + kind + " BSP full-node capacity is smaller than its active node count");
                if (!std::isfinite(geometry.radius) || geometry.radius < 0.0f)
                    issue(identity + " " + kind + " geometry has an invalid radius");
                for (std::size_t axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(geometry.boundsMin[axis]) || !std::isfinite(geometry.boundsMax[axis]) ||
                        geometry.boundsMin[axis] > geometry.boundsMax[axis])
                    {
                        issue(identity + " " + kind + " geometry has invalid bounds");
                        break;
                    }
                for (std::size_t nodeIndex = 0; nodeIndex < geometry.bspNodes.size(); ++nodeIndex)
                {
                    const auto &node = geometry.bspNodes[nodeIndex];
                    if (node.surfaceIndex >= geometry.surfaces.size())
                        issue(identity + " " + kind + " BSP node " + std::to_string(nodeIndex) +
                              " has invalid surface " + std::to_string(node.surfaceIndex));
                    for (const auto child : {node.negativeChild, node.positiveChild})
                        if (child != 0xffff && child >= geometry.bspNodes.size())
                            issue(identity + " " + kind + " BSP node " + std::to_string(nodeIndex) +
                                  " has invalid child " + std::to_string(child));
                }
                if (!geometry.bspNodes.empty())
                {
                    std::vector<std::uint8_t> state(geometry.bspNodes.size(), 0u);
                    bool cycle = false;
                    std::vector<std::pair<std::uint16_t, bool>> work;
                    work.emplace_back(0u, false);
                    while (!work.empty())
                    {
                        const auto [node, leaving] = work.back();
                        work.pop_back();
                        if (node == 0xffff || node >= geometry.bspNodes.size())
                            continue;
                        if (leaving)
                        {
                            state[node] = 2u;
                            continue;
                        }
                        if (state[node] == 1u)
                        {
                            cycle = true;
                            continue;
                        }
                        if (state[node] == 2u)
                            continue;
                        state[node] = 1u;
                        work.emplace_back(node, true);
                        work.emplace_back(geometry.bspNodes[node].positiveChild, false);
                        work.emplace_back(geometry.bspNodes[node].negativeChild, false);
                    }
                    if (cycle)
                        issue(identity + " " + kind + " BSP contains a cycle");
                    if (std::any_of(state.begin(), state.end(), [](const std::uint8_t value) { return value == 0u; }))
                        issue(identity + " " + kind + " BSP contains nodes unreachable from root node 0");
                }
            };
            validateBsp("gameplay", so.collisionGeometry);
            validateBsp("camera", so.cameraGeometry);
            if (so.cameraGroups.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()))
                issue(identity + " has more camera groups than the signed BSP-node mapping can address");
            if (so.hitSurfaceGroups.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()))
                issue(identity + " has more hit-surface groups than the signed surface range table can address");
            if (so.bspNodeToCameraGroup.size() != so.collisionGeometry.bspNodes.size())
                issue(identity + " BSP camera-group map length does not match gameplay BSP nodes");
            if (so.hitSurfaceGroups.empty() && !so.surfaceToHitSurfaceGroup.empty())
                issue(identity + " has a hit-surface map even though the runtime payload has no hit-surface groups");
            if (!so.hitSurfaceGroups.empty())
            {
                if (so.surfaceToHitSurfaceGroup.size() != so.collisionGeometry.surfaces.size() + 1u)
                    issue(identity +
                          " hit-surface range table length does not match gameplay surface count plus sentinel");
                else
                {
                    // UpdateShadow treats entries [surface] and [surface+1]
                    // as a half-open range into ahsg, not as one group ID per
                    // surface. It therefore requires monotonic endpoints from
                    // zero through the complete HSG array.
                    if (so.surfaceToHitSurfaceGroup.front() != 0 ||
                        so.surfaceToHitSurfaceGroup.back() != static_cast<std::int16_t>(so.hitSurfaceGroups.size()) ||
                        !std::is_sorted(so.surfaceToHitSurfaceGroup.begin(), so.surfaceToHitSurfaceGroup.end()))
                        issue(identity + " hit-surface range table is not monotonic from zero to the HSG count");
                    for (const auto endpoint : so.surfaceToHitSurfaceGroup)
                        if (endpoint < 0 || static_cast<std::size_t>(endpoint) > so.hitSurfaceGroups.size())
                        {
                            issue(identity + " hit-surface range table contains an invalid endpoint");
                            break;
                        }
                }
            }
            const auto globset = std::find_if(
                document.Globsets().begin(), document.Globsets().end(), [&](const BrxGlobsetRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            const std::size_t globCount = globset == document.Globsets().end() ? 0 : globset->globIndices.size();
            for (std::size_t groupIndex = 0; groupIndex < so.cameraGroups.size(); ++groupIndex)
            {
                if (so.cameraGroups[groupIndex].cameraStyle < -1 || so.cameraGroups[groupIndex].cameraStyle > 2)
                    issue(identity + " camera group " + std::to_string(groupIndex) +
                          " has an invalid CMK camera style");
                if (so.cameraGroups[groupIndex].globIndex >= globCount)
                    issue(identity + " camera group " + std::to_string(groupIndex) +
                          " references an invalid local GLOB index");
            }
            for (std::size_t groupIndex = 0; groupIndex < so.hitSurfaceGroups.size(); ++groupIndex)
            {
                const auto &group = so.hitSurfaceGroups[groupIndex];
                if (group.globIndex < 0 || static_cast<std::size_t>(group.globIndex) >= globCount)
                    issue(identity + " hit-surface group " + std::to_string(groupIndex) +
                          " references an invalid local GLOB index");
                else if (globset != document.Globsets().end())
                {
                    const std::size_t globRecordIndex = globset->globIndices[static_cast<std::size_t>(group.globIndex)];
                    if (globRecordIndex >= document.Globs().size() || group.subglobIndex < 0 ||
                        static_cast<std::size_t>(group.subglobIndex) >=
                            document.Globs()[globRecordIndex].sourceSubglobs.size())
                        issue(identity + " hit-surface group " + std::to_string(groupIndex) +
                              " references an invalid SUBGLOB index");
                }
            }
        }
        if (CidDerivesFrom(object.cid, 8))
        {
            const auto stepguardProperty = [&](const std::int16_t id) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            const auto validateRange = [&](const std::int16_t id, const char *label)
            {
                const auto *property = stepguardProperty(id);
                if (property && (!std::isfinite(property->floatValue[0]) || !std::isfinite(property->floatValue[1]) ||
                                 property->floatValue[0] > property->floatValue[1]))
                    issue(identity + " has invalid STEPGUARD " + label + " limits");
            };
            validateRange(653, "attack-distance");
            validateRange(654, "attack-timing");
            validateRange(668, "freefall-duration");
            validateRange(689, "taunt-duration");
            for (const auto [id, label] :
                 {std::pair<std::int16_t, const char *>{659, "dying fade delay"}, {691, "contact friction"}})
            {
                const auto *property = stepguardProperty(id);
                const float value = property ? property->floatValue[0] : 0.0f;
                if (property && !std::isfinite(value))
                    issue(identity + " has invalid STEPGUARD " + label);
            }
            if (const auto *property = stepguardProperty(683); property && !std::isfinite(property->floatValue[0]))
                issue(identity + " has a non-finite STEPGUARD idle animation phase");
            if (const auto *property = stepguardProperty(655); property && !std::isfinite(property->floatValue[0]))
                issue(identity + " has a non-finite STEPGUARD maximum attack angle");
            if (const auto *property = stepguardProperty(676);
                property && (property->intValue < -1 || property->intValue > 1))
                issue(identity + " has invalid STEPGUARD patrol-route selection");
            for (const std::int16_t id :
                 {std::int16_t{652}, std::int16_t{670}, std::int16_t{671}, std::int16_t{681}, std::int16_t{682}})
            {
                const auto *property = stepguardProperty(id);
                if (property && property->intValue != -1 &&
                    std::none_of(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate) { return candidate.oid == property->intValue; }))
                    issue(identity + " STEPGUARD " +
                          std::string(PropertyName(id) ? PropertyName(id) : "object reference") +
                          " references missing OID " + std::to_string(property->intValue));
            }
            // These setters store an OID and later cast the resolved object
            // directly to the indicated runtime structure.  Existence alone
            // is insufficient: an OID collision with another CID survives
            // loading and is then accessed through the wrong layout.
            for (const auto [id, requiredCid, label] :
                 {std::tuple<std::int16_t, std::int16_t, const char *>{652, 138, "path zone"},
                  {671, 3, "enemy physics object"},
                  {681, 119, "explosion effect"},
                  {682, 140, "reach/world-memory object"}})
            {
                const auto *property = stepguardProperty(id);
                if (!property || property->intValue == -1)
                    continue;
                const auto compatible =
                    findEarlyContextualCompatible(static_cast<std::int16_t>(property->intValue), requiredCid);
                if (compatible == document.Objects().end())
                    issue(identity + " STEPGUARD " + label + " OID " + std::to_string(property->intValue) +
                          " does not resolve to the required CID");
            }
            struct DynamicGuardSchema
            {
                std::int16_t eopid;
                std::initializer_list<std::int16_t> types;
                std::initializer_list<std::int16_t> referenceCids;
                std::size_t maximumCount;
            };
            const DynamicGuardSchema dynamicSchemas[]{
                {656, {4132, 6}, {-1, 131}, 17}, {660, {6, 6, 6, 6}, {131, 131, 131, 131}, 8},
                {661, {6, 4123}, {3, -1}, 4},    {662, {4126, 6}, {-1, 131}, 4},
                {664, {4132, 6}, {-1, 3}, 17},
            };
            for (const auto &schema : dynamicSchemas)
            {
                std::size_t count = 0;
                bool usedStates[17]{};
                for (const auto &property : object.properties)
                {
                    if (property.eopid != schema.eopid)
                        continue;
                    ++count;
                    if (property.kind != BrxPropertyKind::Dynamic || property.argumentCount != schema.types.size() ||
                        property.arguments.size() != schema.types.size())
                    {
                        issue(identity + " has malformed " + std::string(PropertyName(schema.eopid)) +
                              " SetUser record");
                        continue;
                    }
                    auto type = schema.types.begin();
                    auto referenceCid = schema.referenceCids.begin();
                    for (std::size_t argumentIndex = 0; argumentIndex < property.arguments.size();
                         ++argumentIndex, ++type, ++referenceCid)
                    {
                        const auto &argument = property.arguments[argumentIndex];
                        if (argument.type != *type)
                            issue(identity + " has " + std::string(PropertyName(schema.eopid)) + " argument " +
                                  std::to_string(argumentIndex + 1) + " with the wrong serialized type");
                        if (*referenceCid < 0 || argument.intValue == -1)
                            continue;
                        const auto referenced = std::find_if(document.Objects().begin(), document.Objects().end(),
                                                             [&](const BrxObjectRecord &candidate)
                                                             { return candidate.oid == argument.intValue; });
                        if (referenced == document.Objects().end())
                            issue(identity + " " + std::string(PropertyName(schema.eopid)) +
                                  " references missing OID " + std::to_string(argument.intValue));
                        else if (!CidDerivesFrom(referenced->cid, *referenceCid))
                            issue(identity + " " + std::string(PropertyName(schema.eopid)) + " references " +
                                  BrxDocument::ClassName(referenced->cid) + ", expected " +
                                  BrxDocument::ClassName(*referenceCid));
                    }
                    if ((schema.eopid == 656 || schema.eopid == 664) && !property.arguments.empty() &&
                        (property.arguments[0].intValue < 0 || property.arguments[0].intValue > 16))
                        issue(identity + " has a per-state guard record outside state 0..16");
                    else if ((schema.eopid == 656 || schema.eopid == 664) && !property.arguments.empty())
                    {
                        const int state = property.arguments[0].intValue;
                        if (usedStates[state])
                            issue(identity + " has more than one " + std::string(PropertyName(schema.eopid)) +
                                  " record for guard state " + std::to_string(state) +
                                  "; ProjectCane keeps only the last one");
                        usedStates[state] = true;
                    }
                }
                if (count > schema.maximumCount)
                    issue(identity + " has too many " + std::string(PropertyName(schema.eopid)) + " records");
            }
        }
        if (CidDerivesFrom(object.cid, 9))
        {
            const auto smartguardProperty = [&](const std::int16_t id) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            for (const auto [id, label] : {std::pair<std::int16_t, const char *>{696, "near detection radius"},
                                           {697, "near detection height"},
                                           {698, "sneak radius"},
                                           {699, "sneak height"}})
            {
                const auto *property = smartguardProperty(id);
                if (property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                    issue(identity + " has invalid SMARTGUARD " + label);
            }
            for (const std::int16_t id : {std::int16_t{700}, std::int16_t{707}, std::int16_t{708}})
            {
                for (const auto &property : object.properties)
                {
                    if (property.eopid != id || property.intValue == -1)
                        continue;
                    if (std::none_of(document.Objects().begin(), document.Objects().end(),
                                     [&](const BrxObjectRecord &candidate)
                                     { return candidate.oid == property.intValue; }))
                        issue(identity + " SMARTGUARD " +
                              std::string(PropertyName(id) ? PropertyName(id) : "object reference") +
                              " references missing OID " + std::to_string(property.intValue));
                }
            }
            const auto validateSmartguardReferenceType =
                [&](const std::int16_t id, const std::int16_t expectedCid, const char *label)
            {
                for (const auto &property : object.properties)
                {
                    if (property.eopid != id || property.intValue == -1)
                        continue;
                    const auto referenced =
                        findEarlyContextualCompatible(static_cast<std::int16_t>(property.intValue), expectedCid);
                    if (referenced == document.Objects().end())
                        issue(identity + " SMARTGUARD " + label + " OID " + std::to_string(property.intValue) +
                              " has no compatible " + BrxDocument::ClassName(expectedCid) + " object");
                }
            };
            validateSmartguardReferenceType(700, 123, "sneak volume");
            validateSmartguardReferenceType(707, 2, "animation-override target");
            validateSmartguardReferenceType(708, 52, "detection barrier");
            const auto barrierCount =
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 708; });
            if (barrierCount > 4)
                issue(identity + " has more than four SMARTGUARD barriers");
            std::unordered_set<std::int32_t> smartguardBarrierOids;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 708)
                    continue;
                if (property.intValue == -1)
                    issue(identity + " has an empty SMARTGUARD barrier entry; remove the entry instead");
                else if (!smartguardBarrierOids.insert(property.intValue).second)
                    issue(identity + " repeats SMARTGUARD detection barrier OID " + std::to_string(property.intValue));
            }
            const auto *forceDetection = smartguardProperty(704);
            const auto *disableDetection = smartguardProperty(705);
            if (forceDetection && forceDetection->boolValue && disableDetection && disableDetection->boolValue)
                issue(
                    identity +
                    " enables both SMARTGUARD force-detection and disable-detection; disable-detection wins at runtime");
            if (const auto *detectionFlags = smartguardProperty(706);
                detectionFlags && (detectionFlags->intValue & ~0x3) != 0)
                issue(identity + " has unknown SMARTGUARD detection flag bits outside 0x3");
            const auto *flashlightName = smartguardProperty(701);
            if ((flashlightName == nullptr || flashlightName->intValue == -1) &&
                !(forceDetection && forceDetection->boolValue) && !(disableDetection && disableDetection->boolValue))
                issue(identity + " has no FLASH child selected, so normal SMARTGUARD detection cannot succeed");
            if (flashlightName != nullptr && flashlightName->intValue != -1)
            {
                if (FindSmartguardFlashlightChild(document, static_cast<std::int32_t>(index),
                                                  static_cast<std::int16_t>(flashlightName->intValue)) < 0)
                    issue(identity + " flashlight OID " + std::to_string(flashlightName->intValue) +
                          " has no eligible FLASH child in this SMARTGUARD hierarchy");
            }
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (!IsSmartguardFlashlightChild(document, static_cast<std::int32_t>(candidateIndex),
                                                 static_cast<std::int32_t>(index)))
                    continue;
                if (flashlightName == nullptr || flashlightName->intValue == -1 ||
                    candidate.oid != flashlightName->intValue)
                    issue(identity + " has nonmatching FLASH child OID " + std::to_string(candidate.oid) +
                          " that PostSmartguardLoadFlashlight will remove");
            }
            std::size_t flashlightTargetCount = 0;
            bool flashlightTargetStates[17]{};
            for (const auto &property : object.properties)
            {
                if (property.eopid != 703)
                    continue;
                ++flashlightTargetCount;
                if (property.kind != BrxPropertyKind::Dynamic || property.argumentCount != 2 ||
                    property.arguments.size() != 2 || property.arguments[0].type != 4132 ||
                    property.arguments[1].type != 6)
                {
                    issue(identity + " has malformed SMARTGUARD per-state flashlight target");
                    continue;
                }
                if (property.arguments[0].intValue < 0 || property.arguments[0].intValue > 16)
                    issue(identity + " has a SMARTGUARD flashlight target outside state 0..16");
                else if (flashlightTargetStates[property.arguments[0].intValue])
                    issue(identity + " has more than one SMARTGUARD flashlight target for state " +
                          std::to_string(property.arguments[0].intValue));
                else
                    flashlightTargetStates[property.arguments[0].intValue] = true;
                const int targetOid = property.arguments[1].intValue;
                if (targetOid == -1)
                    continue;
                const auto referenced = findEarlyContextualCompatible(static_cast<std::int16_t>(targetOid), 2);
                if (referenced == document.Objects().end())
                    issue(identity + " SMARTGUARD flashlight target references missing OID " +
                          std::to_string(targetOid));
            }
            if (flashlightTargetCount > 17)
                issue(identity + " has more than 17 SMARTGUARD per-state flashlight targets");
        }
        if (CidDerivesFrom(object.cid, 10))
        {
            const auto gomerProperty = [&](const std::int16_t id) -> const BrxPropertyRecord *
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? nullptr : &*found;
            };
            for (const auto [id, label] :
                 {std::pair<std::int16_t, const char *>{841, "abandon radius"}, {842, "abandon vertical tolerance"}})
            {
                const auto *property = gomerProperty(id);
                if (property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                    issue(identity + " has invalid GOMER " + label);
            }
            for (const auto [id, label] :
                 {std::pair<std::int16_t, const char *>{845, "detection-distance"}, {846, "detection-angle"}})
            {
                const auto *property = gomerProperty(id);
                if (property && (!std::isfinite(property->floatValue[0]) || !std::isfinite(property->floatValue[1]) ||
                                 property->floatValue[0] > property->floatValue[1]))
                    issue(identity + " has invalid GOMER " + label + " limits");
            }
            if (const auto *property = gomerProperty(845);
                property && (property->floatValue[0] < 0.0f || property->floatValue[1] < 0.0f))
                issue(identity + " has negative GOMER detection-distance limits");
            if (const auto *property = gomerProperty(846); property && property->floatValue[0] < 0.0f)
                issue(identity + " has negative GOMER detection-angle limits");
            for (const std::int16_t id : {std::int16_t{843}, std::int16_t{847}})
            {
                const auto *property = gomerProperty(id);
                if (property && property->intValue != -1)
                {
                    const auto referenced =
                        findEarlyContextualCompatible(static_cast<std::int16_t>(property->intValue), 123);
                    if (referenced == document.Objects().end())
                        issue(identity + " GOMER " +
                              std::string(PropertyName(id) ? PropertyName(id) : "volume reference") +
                              " has no compatible VOL at OID " + std::to_string(property->intValue));
                }
            }
            const auto *holdDetection = gomerProperty(849);
            const auto *forceAbandon = gomerProperty(850);
            if (holdDetection && holdDetection->boolValue && forceAbandon && forceAbandon->boolValue)
                issue(identity + " enables both GOMER hold-detection and force-abandon; force-abandon wins at runtime");
        }
        if (CidDerivesFrom(object.cid, 11))
        {
            struct UbgBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                const char *label;
            };
            const UbgBinding bindings[]{
                {725, 136, "world state machine"}, {743, 115, "middle transform"}, {737, 3, "arena pad 1"},
                {738, 3, "arena pad 2"},           {739, 3, "arena pad 3"},        {740, 3, "arena pad 4"},
                {741, 3, "arena pad 5"},           {742, 3, "arena pad 6"},
            };
            for (const auto &binding : bindings)
            {
                std::vector<const BrxObjectRecord *> matches;
                for (const auto &candidate : document.Objects())
                    if (candidate.oid == binding.oid)
                        matches.push_back(&candidate);
                if (matches.empty())
                    issue(identity + " UBG fixed " + binding.label + " binding OID " + std::to_string(binding.oid) +
                          " has no compatible " + BrxDocument::ClassName(binding.cid) + " object");
                else
                {
                    if (matches.size() > 1)
                        issue(identity + " UBG fixed " + binding.label + " OID " + std::to_string(binding.oid) +
                              " is ambiguous for SnipAloObjects global mode 5");
                    if (!std::all_of(matches.begin(), matches.end(), [&](const BrxObjectRecord *candidate)
                                     { return CidDerivesFrom(candidate->cid, binding.cid); }))
                        issue(identity + " UBG fixed " + binding.label + " OID " + std::to_string(binding.oid) +
                              " has an incompatible class for its unchecked retail cast");
                }
            }
        }
        if (CidDerivesFrom(object.cid, 12))
        {
            const auto targetTransform =
                std::find_if(object.properties.begin(), object.properties.end(),
                             [](const BrxPropertyRecord &property) { return property.eopid == 1025; });
            if (targetTransform != object.properties.end() && targetTransform->intValue != -1)
            {
                const auto referenced =
                    findEarlyContextualCompatible(static_cast<std::int16_t>(targetTransform->intValue), 115);
                if (referenced == document.Objects().end())
                    issue(identity + " MBG return-to-patrol transform references missing OID " +
                          std::to_string(targetTransform->intValue));
            }

            struct MbgBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr MbgBinding bindings[]{
                {130, 131, "run animation"},    {131, 131, "run animation"},  {1088, 140, "left-fire RWM"},
                {1089, 140, "right-fire RWM"},  {1094, 131, "gun animation"}, {1095, 131, "grenade animation"},
                {1096, 131, "smash animation"}, {53, 79, "burn-halo bone"},   {54, 79, "burn-halo bone"},
                {56, 79, "burn-halo bone"},     {59, 79, "burn-halo bone"},   {96, 79, "burn-halo bone"},
                {80, 79, "burn-halo bone"},
            };
            const auto underMbg = [&](std::size_t candidateIndex)
            {
                for (std::size_t guard = 0;
                     candidateIndex < document.Objects().size() && guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == index)
                        return true;
                    const auto &candidate = document.Objects()[candidateIndex];
                    const auto parent = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                               : candidate.parentIndex;
                    if (parent < 0 || parent == static_cast<std::int32_t>(candidateIndex))
                        break;
                    candidateIndex = static_cast<std::size_t>(parent);
                }
                return false;
            };
            for (const MbgBinding &binding : bindings)
            {
                std::vector<const BrxObjectRecord *> matches;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid == binding.oid && underMbg(candidateIndex))
                        matches.push_back(&candidate);
                }
                if (matches.empty())
                    issue(identity + " MBG fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (!std::all_of(matches.begin(), matches.end(), [&](const BrxObjectRecord *candidate)
                                      { return CidDerivesFrom(candidate->cid, binding.baseCid); }))
                    issue(identity + " MBG fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " has an incompatible class for its unchecked retail cast");
                else if (matches.size() > 1)
                    issue(identity + " MBG fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " is ambiguous in its hierarchy");
            }
            std::vector<const BrxObjectRecord *> stateMachines;
            for (const auto &candidate : document.Objects())
                if (candidate.oid == 1079)
                    stateMachines.push_back(&candidate);
            if (stateMachines.empty())
                issue(identity + " MBG fixed state-machine binding references missing global OID 1079");
            else if (stateMachines.size() > 1 ||
                     !std::all_of(stateMachines.begin(), stateMachines.end(),
                                  [](const BrxObjectRecord *candidate) { return CidDerivesFrom(candidate->cid, 136); }))
                issue(identity + " MBG global state-machine OID 1079 is ambiguous or has an incompatible class");
        }
        if (CidDerivesFrom(object.cid, 14))
        {
            struct MurrayBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr MurrayBinding bindings[]{
                {359, 2, "held collectible"},           {360, 2, "held collectible"},
                {239, 131, "celebrate-key animation"},  {362, 131, "alternate death animation"},
                {363, 131, "alternate stun animation"}, {364, 131, "death animation"},
                {365, 131, "stun animation"},
            };
            const auto underMurray = [&](std::size_t candidateIndex)
            {
                for (std::size_t guard = 0;
                     candidateIndex < document.Objects().size() && guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == index)
                        return true;
                    const auto &candidate = document.Objects()[candidateIndex];
                    const auto parent = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                               : candidate.parentIndex;
                    if (parent < 0 || parent == static_cast<std::int32_t>(candidateIndex))
                        break;
                    candidateIndex = static_cast<std::size_t>(parent);
                }
                return false;
            };
            for (const MurrayBinding &binding : bindings)
            {
                std::vector<const BrxObjectRecord *> matches;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid == binding.oid && underMurray(candidateIndex))
                        matches.push_back(&candidate);
                }
                if (matches.empty())
                    issue(identity + " MURRAY fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (!std::all_of(matches.begin(), matches.end(), [&](const BrxObjectRecord *candidate)
                                      { return CidDerivesFrom(candidate->cid, binding.baseCid); }))
                    issue(identity + " MURRAY fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " has an incompatible class for its unchecked retail cast");
                else if (matches.size() > 1)
                    issue(identity + " MURRAY fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " is ambiguous in its hierarchy");
            }
        }
        if (CidDerivesFrom(object.cid, 19))
        {
            const auto propertyById = [&](const std::int16_t id)
            {
                return std::find_if(object.properties.begin(), object.properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == id; });
            };
            const auto framesPerBeat = propertyById(1150);
            if (framesPerBeat != object.properties.end() && framesPerBeat->intValue <= 0)
                issue(identity + " RUBY frames-per-beat must be positive");
            const auto beatsPerMeasure = propertyById(1151);
            if (beatsPerMeasure != object.properties.end() && beatsPerMeasure->intValue <= 0)
                issue(identity + " RUBY beats-per-measure must be positive");
            struct RubyBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr RubyBinding bindings[]{
                {53, 1, "timed event"},     {54, 1, "timed event"},     {56, 1, "timed event"},
                {59, 1, "timed event"},     {80, 1, "timed event"},     {82, 1, "timed event"},
                {83, 1, "timed event"},     {84, 1, "timed event"},     {96, 1, "timed event"},
                {1247, 131, "attack ASEG"}, {1248, 131, "attack ASEG"}, {1249, 131, "attack ASEG"},
                {1250, 131, "attack ASEG"}, {1255, 140, "attack RWM"},  {1256, 140, "attack RWM"},
                {1257, 140, "attack RWM"},  {1258, 140, "attack RWM"},
            };
            const auto underRuby = [&](std::size_t candidateIndex)
            {
                for (std::size_t guard = 0;
                     candidateIndex < document.Objects().size() && guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == index)
                        return true;
                    const auto &candidate = document.Objects()[candidateIndex];
                    const auto parent = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                               : candidate.parentIndex;
                    if (parent < 0 || parent == static_cast<std::int32_t>(candidateIndex))
                        break;
                    candidateIndex = static_cast<std::size_t>(parent);
                }
                return false;
            };
            for (const RubyBinding &binding : bindings)
            {
                std::vector<const BrxObjectRecord *> matches;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid == binding.oid && underRuby(candidateIndex))
                        matches.push_back(&candidate);
                }
                if (matches.empty())
                    issue(identity + " RUBY fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (!std::all_of(matches.begin(), matches.end(), [&](const BrxObjectRecord *candidate)
                                      { return CidDerivesFrom(candidate->cid, binding.baseCid); }))
                    issue(identity + " RUBY fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " has an incompatible class for its unchecked retail cast");
                else if (matches.size() > 1)
                    issue(identity + " RUBY fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " is ambiguous in its hierarchy");
            }
            std::vector<const BrxObjectRecord *> rubyStateMachines;
            for (const auto &candidate : document.Objects())
                if (candidate.oid == 1259)
                    rubyStateMachines.push_back(&candidate);
            if (rubyStateMachines.empty())
                issue(identity + " RUBY global state-machine binding references missing OID 1259");
            else if (rubyStateMachines.size() > 1 ||
                     !std::all_of(rubyStateMachines.begin(), rubyStateMachines.end(),
                                  [](const BrxObjectRecord *candidate) { return CidDerivesFrom(candidate->cid, 136); }))
                issue(identity + " RUBY global state-machine OID 1259 is ambiguous or has an incompatible class");
        }
        if (CidDerivesFrom(object.cid, 20))
        {
            const auto tankState =
                std::find_if(object.properties.begin(), object.properties.end(),
                             [](const BrxPropertyRecord &property) { return property.eopid == 1028; });
            if (tankState != object.properties.end())
                issue(
                    identity +
                    " serializes runtime-only TANK state EOPID 1028; PostTankLoad replaces it, so remove this override");
            struct TankBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr TankBinding bindings[]{
                {845, 3, "physics shell"}, {848, 136, "state machine"},  {80, 2, "head component"},
                {96, 2, "gut component"},  {846, 2, "player component"}, {352, 2, "charm target"},
                {353, 2, "charm target"},  {354, 2, "charm target"},     {355, 2, "charm target"},
            };
            const auto tankDescendant = [&](const BrxObjectRecord &candidate)
            {
                std::int32_t ancestor = static_cast<std::int32_t>(std::distance(document.Objects().data(), &candidate));
                for (std::size_t guard = 0; guard <= document.Objects().size(); ++guard)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                        return true;
                    if (ancestor < 0 || static_cast<std::size_t>(ancestor) >= document.Objects().size())
                        break;
                    const auto &current = document.Objects()[static_cast<std::size_t>(ancestor)];
                    ancestor =
                        current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex : current.parentIndex;
                }
                return false;
            };
            for (const TankBinding &binding : bindings)
            {
                std::vector<const BrxObjectRecord *> matches;
                for (const auto &candidate : document.Objects())
                    if (candidate.oid == binding.oid && tankDescendant(candidate))
                        matches.push_back(&candidate);
                if (matches.empty())
                    issue(identity + " TANK fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (!std::all_of(matches.begin(), matches.end(), [&](const BrxObjectRecord *candidate)
                                      { return CidDerivesFrom(candidate->cid, binding.baseCid); }))
                    issue(identity + " TANK fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " has an incompatible class for its unchecked retail cast");
                else if (matches.size() > 1)
                    issue(identity + " TANK fixed " + binding.role + " OID " + std::to_string(binding.oid) +
                          " is ambiguous in its hierarchy");
            }
            const auto stateMachineObject = std::find_if(
                document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                { return candidate.oid == 848 && tankDescendant(candidate) && CidDerivesFrom(candidate.cid, 136); });
            if (stateMachineObject != document.Objects().end())
            {
                const auto stateMachineIndex =
                    static_cast<std::int32_t>(std::distance(document.Objects().begin(), stateMachineObject));
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == stateMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " TANK state-machine object OID 848 has no decoded state-machine payload");
                else
                {
                    // SetTankTanks and UpdateTankActive pass these OIDs to
                    // SetSmaGoal/SeekSma.  They name SM states, not standalone
                    // ASEG world objects.
                    for (const std::int16_t requiredState : {std::int16_t{849}, std::int16_t{850}, std::int16_t{851},
                                                             std::int16_t{852}, std::int16_t{853}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                            issue(identity + " TANK state machine OID 848 is missing required state OID " +
                                  std::to_string(requiredState));
                }
            }
        }
        if (CidDerivesFrom(object.cid, 21))
        {
            const auto maximumSpeed =
                std::find_if(object.properties.begin(), object.properties.end(),
                             [](const BrxPropertyRecord &property) { return property.eopid == 804; });
            if (maximumSpeed != object.properties.end() &&
                (!std::isfinite(maximumSpeed->floatValue[0]) || maximumSpeed->floatValue[0] <= 0.0f))
                issue(identity + " ROV maximum speed must be greater than zero");
            struct RovBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr RovBinding bindings[]{
                {654, 136, "body state machine"}, {657, 2, "turret component"},  {658, 136, "turret state machine"},
                {665, 2, "prop component"},       {663, 80, "left-gun emitter"}, {664, 80, "right-gun emitter"},
                {666, 131, "descend ASEG"},       {667, 131, "ascend ASEG"},
            };
            const auto rovDescendant = [&](const std::size_t candidateIndex)
            {
                std::int32_t ancestor = static_cast<std::int32_t>(candidateIndex);
                for (std::size_t guard = 0; guard <= document.Objects().size(); ++guard)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                        return true;
                    if (ancestor < 0 || static_cast<std::size_t>(ancestor) >= document.Objects().size())
                        break;
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const auto next = ancestorObject.proxyRuntimeParentIndex >= 0
                                          ? ancestorObject.proxyRuntimeParentIndex
                                          : ancestorObject.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
                return false;
            };
            for (const RovBinding &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid && rovDescendant(candidateIndex))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " ROV fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " ROV has multiple hierarchy objects for fixed " + std::string(binding.role) +
                          " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.baseCid))
                    issue(identity + " ROV fixed " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.baseCid));
            }
            const auto validateRovStateMachine = [&](const std::int16_t machineOid,
                                                     const std::initializer_list<std::int16_t> requiredStates,
                                                     const char *role) -> const BrxSmRecord *
            {
                std::int32_t machineObjectIndex = -1;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == machineOid && rovDescendant(candidateIndex) &&
                        CidDerivesFrom(document.Objects()[candidateIndex].cid, 136))
                    {
                        machineObjectIndex = static_cast<std::int32_t>(candidateIndex);
                        break;
                    }
                if (machineObjectIndex < 0)
                    return nullptr; // The fixed-binding diagnostic above owns this error.
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineObjectIndex; });
                if (machine == document.StateMachines().end())
                {
                    issue(identity + " ROV " + role + " OID " + std::to_string(machineOid) +
                          " has no decoded state-machine payload");
                    return nullptr;
                }
                for (const std::int16_t requiredState : requiredStates)
                    if (std::none_of(machine->states.begin(), machine->states.end(),
                                     [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                        issue(identity + " ROV " + role + " OID " + std::to_string(machineOid) +
                              " is missing required state OID " + std::to_string(requiredState));
                return &*machine;
            };
            validateRovStateMachine(654, {655, 656}, "body state machine");
            const BrxSmRecord *turretMachine = validateRovStateMachine(658, {659, 660}, "turret state machine");
            std::vector<std::size_t> owningRobs;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.cid != 95)
                    continue;
                if (std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                [&](const BrxPropertyRecord &property)
                                { return property.eopid == 773 && property.intValue == object.oid; }))
                    owningRobs.push_back(candidateIndex);
            }
            if (owningRobs.empty())
                issue(identity + " is not referenced by any ROB Vehicle OID; PostRovLoad will remove it");
            else if (owningRobs.size() > 1)
                issue(identity + " is referenced as the vehicle by multiple ROB controllers");

            const auto propulsion = std::find_if(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    const auto candidateIndex = static_cast<std::size_t>(&candidate - document.Objects().data());
                    return candidate.oid == 665 && rovDescendant(candidateIndex) && CidDerivesFrom(candidate.cid, 2);
                });
            if (propulsion != document.Objects().end())
            {
                const auto propulsionIndex = static_cast<std::int32_t>(propulsion - document.Objects().begin());
                std::size_t propEmitterCount = 0;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    if (!CidDerivesFrom(document.Objects()[candidateIndex].cid, 80))
                        continue;
                    std::int32_t ancestor = static_cast<std::int32_t>(candidateIndex);
                    for (std::size_t guard = 0; guard <= document.Objects().size(); ++guard)
                    {
                        if (ancestor == propulsionIndex)
                        {
                            ++propEmitterCount;
                            break;
                        }
                        if (ancestor < 0 || static_cast<std::size_t>(ancestor) >= document.Objects().size())
                            break;
                        const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const auto next = ancestorObject.proxyRuntimeParentIndex >= 0
                                              ? ancestorObject.proxyRuntimeParentIndex
                                              : ancestorObject.parentIndex;
                        if (next == ancestor)
                            break;
                        ancestor = next;
                    }
                }
                if (propEmitterCount > 3)
                    issue(identity + " propulsion hierarchy contains " + std::to_string(propEmitterCount) +
                          " emitters; the runtime keeps only the first three");
            }
            if (turretMachine != nullptr)
            {
                bool leftGunLabel = false;
                bool rightGunLabel = false;
                for (const BrxSmTransitionRecord &transition : turretMachine->transitions)
                {
                    if (transition.animationObjectIndex < 0)
                        continue;
                    for (const auto &segment : document.AnimationSegments())
                    {
                        if (segment.ownerObjectIndex != transition.animationObjectIndex)
                            continue;
                        for (const auto &event : segment.events)
                        {
                            if (event.type != 39)
                                continue;
                            leftGunLabel |= event.shorts[0] == 661;
                            rightGunLabel |= event.shorts[0] == 662;
                        }
                    }
                }
                if (!leftGunLabel)
                    issue(identity + " ROV turret state machine has no left-gun label 661");
                if (!rightGunLabel)
                    issue(identity + " ROV turret state machine has no right-gun label 662");
            }
        }
        if (CidDerivesFrom(object.cid, 22))
        {
            const auto lookMode =
                std::find_if(object.properties.begin(), object.properties.end(),
                             [](const BrxPropertyRecord &property) { return property.eopid == 1041; });
            if (lookMode != object.properties.end() && (lookMode->intValue < -1 || lookMode->intValue > 4))
                issue(identity + " has an invalid TURRET camera look mode " + std::to_string(lookMode->intValue));
            const auto targetCount =
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 1043; });
            if (targetCount > 16)
                issue(identity + " has more than 16 TURRET target OIDs");
            std::unordered_set<std::int32_t> turretTargetOids;
            bool targetResolutionBroken = false;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 1043 || property.intValue == -1)
                    continue;
                if (!turretTargetOids.insert(property.intValue).second)
                    issue(identity + " repeats TURRET target OID " + std::to_string(property.intValue) +
                          " and wastes one of sixteen slots");
                // PostTurretLoad resolves each roster entry with
                // PloFindSwNearest(..., pturret), not by global stream order.
                const auto target = findEarlyContextualCompatible(static_cast<std::int16_t>(property.intValue), 3);
                if (target == document.Objects().end())
                {
                    issue(identity + " TURRET target references missing OID " + std::to_string(property.intValue));
                    targetResolutionBroken = true;
                }
                else if (targetResolutionBroken)
                    issue(identity + " TURRET target OID " + std::to_string(property.intValue) +
                          " will not resolve because an earlier roster entry is invalid");
            }
            for (const std::int16_t id : {std::int16_t{1044}, std::int16_t{1045}})
            {
                const auto limits =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                if (limits != object.properties.end() &&
                    (!std::isfinite(limits->floatValue[0]) || !std::isfinite(limits->floatValue[1])))
                    issue(identity + " TURRET " + std::string(PropertyName(id) ? PropertyName(id) : "aim limits") +
                          " contains a non-finite angle");
                else if (limits != object.properties.end() && limits->floatValue[0] > limits->floatValue[1])
                    issue(identity + " TURRET " + std::string(PropertyName(id) ? PropertyName(id) : "aim limits") +
                          " minimum exceeds maximum");
            }
            struct TurretBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr TurretBinding bindings[]{
                {53, 126, "camera anchor"},
                {854, 140, "weapon RWM"},
                {855, 136, "state machine"},
            };
            const std::int32_t turretIndex = static_cast<std::int32_t>(index);
            const auto underTurret = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == turretIndex)
                        return true;
                return false;
            };
            for (const TurretBinding &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        underTurret(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " TURRET fixed " + binding.role + " binding requires descendant OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " TURRET has multiple hierarchy objects for fixed " + std::string(binding.role) +
                          " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.baseCid))
                    issue(identity + " TURRET fixed " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.baseCid));
            }
            std::int32_t stateMachineIndex = -1;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                if (document.Objects()[candidateIndex].oid == 855 &&
                    underTurret(static_cast<std::int32_t>(candidateIndex)) &&
                    CidDerivesFrom(document.Objects()[candidateIndex].cid, 136))
                {
                    stateMachineIndex = static_cast<std::int32_t>(candidateIndex);
                    break;
                }
            if (stateMachineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == stateMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " TURRET state-machine object OID 855 has no decoded state-machine payload");
                else
                {
                    for (const std::int16_t requiredState : {std::int16_t{856}, std::int16_t{857}, std::int16_t{858}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                            issue(identity + " TURRET state machine OID 855 is missing required state OID " +
                                  std::to_string(requiredState));
                    bool hasFireLabel = false;
                    for (const BrxSmTransitionRecord &transition : machine->transitions)
                    {
                        if (transition.animationObjectIndex < 0)
                            continue;
                        for (const auto &segment : document.AnimationSegments())
                        {
                            if (segment.ownerObjectIndex != transition.animationObjectIndex)
                                continue;
                            for (const auto &event : segment.events)
                                if (event.type == 39 && event.shorts[0] == 859)
                                    hasFireLabel = true;
                        }
                    }
                    if (!hasFireLabel)
                        issue(
                            identity +
                            " TURRET state machine has no animation label 859; firing input will never launch the RWM projectile");
                }
            }
        }
        const auto classProperty = [&](const std::int16_t id) -> const BrxPropertyRecord *
        {
            const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                            [&](const BrxPropertyRecord &property) { return property.eopid == id; });
            return found == object.properties.end() ? nullptr : &*found;
        };
        const auto findHierarchyNearestObjects = [&](const std::int16_t oid, const std::int32_t contextIndex = -1)
        {
            std::vector<std::int32_t> matches;
            const std::int32_t effectiveContext = contextIndex >= 0 ? contextIndex : static_cast<std::int32_t>(index);
            std::vector<std::pair<std::int32_t, std::size_t>> contextAncestors;
            std::int32_t ancestorIndex = effectiveContext;
            if (ancestorIndex >= 0 && static_cast<std::size_t>(ancestorIndex) < document.Objects().size() &&
                !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(ancestorIndex)].cid, 2))
            {
                const auto &context = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                ancestorIndex =
                    context.proxyRuntimeParentIndex >= 0 ? context.proxyRuntimeParentIndex : context.parentIndex;
            }
            for (std::size_t depth = 1;
                 ancestorIndex >= 0 && static_cast<std::size_t>(ancestorIndex) < document.Objects().size() &&
                 depth <= document.Objects().size();
                 ++depth)
            {
                contextAncestors.emplace_back(ancestorIndex, depth);
                const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                const std::int32_t parent =
                    ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                if (parent == ancestorIndex)
                    break;
                ancestorIndex = parent;
            }

            std::size_t bestDepth = std::numeric_limits<std::size_t>::max();
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid != oid)
                    continue;
                std::size_t candidateDepth = std::numeric_limits<std::size_t>::max();
                std::int32_t candidateAncestor = static_cast<std::int32_t>(candidateIndex);
                if (!CidDerivesFrom(candidate.cid, 2))
                    candidateAncestor = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                               : candidate.parentIndex;
                for (std::size_t guard = 0; candidateAncestor >= 0 &&
                                            static_cast<std::size_t>(candidateAncestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto shared =
                        std::find_if(contextAncestors.begin(), contextAncestors.end(),
                                     [&](const auto &entry) { return entry.first == candidateAncestor; });
                    if (shared != contextAncestors.end())
                    {
                        candidateDepth = shared->second;
                        break;
                    }
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(candidateAncestor)];
                    const std::int32_t parent =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (parent == candidateAncestor)
                        break;
                    candidateAncestor = parent;
                }
                if (candidateDepth < bestDepth)
                {
                    bestDepth = candidateDepth;
                    matches.clear();
                    matches.push_back(static_cast<std::int32_t>(candidateIndex));
                }
                else if (candidateDepth == bestDepth)
                    matches.push_back(static_cast<std::int32_t>(candidateIndex));
            }
            return matches;
        };
        const auto findContextDescendants = [&](const std::int16_t oid, const std::int32_t contextIndex)
        {
            std::vector<std::int32_t> matches;
            if (contextIndex < 0 || static_cast<std::size_t>(contextIndex) >= document.Objects().size())
                return matches;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid != oid)
                    continue;
                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == contextIndex)
                    {
                        matches.push_back(static_cast<std::int32_t>(candidateIndex));
                        break;
                    }
                    const auto &parentObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent = parentObject.proxyRuntimeParentIndex >= 0
                                                    ? parentObject.proxyRuntimeParentIndex
                                                    : parentObject.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
            }
            return matches;
        };
        const auto findContextualObject = [&](const std::int16_t oid)
        {
            const auto matches = findHierarchyNearestObjects(oid);
            return matches.empty() ? document.Objects().end() : document.Objects().begin() + matches.front();
        };
        const auto findContextualCompatibleObject = [&](const std::int16_t oid, const std::int16_t eopid)
        {
            const auto matches = findHierarchyNearestObjects(oid);
            for (const std::int32_t candidateIndex : matches)
            {
                if (candidateIndex < 0 || static_cast<std::size_t>(candidateIndex) >= document.Objects().size())
                    continue;
                auto candidate = document.Objects().begin() + candidateIndex;
                if (ObjectReferenceAccepts(eopid, candidate->cid))
                    return candidate;
            }
            return document.Objects().end();
        };
        const auto validateObjectReference = [&](const std::int16_t id, const char *owner)
        {
            (void)id;
            (void)owner;
            // All object references are checked once by the authoritative
            // contract pass below. Keep this local helper so class validators
            // remain readable without producing duplicate diagnostics.
        };
        const auto validateAllObjectReferences = [&](const std::int16_t id, const char *owner)
        {
            (void)id;
            (void)owner;
        };
        for (const auto &property : object.properties)
        {
            if (!IsObjectReferenceOption(property.eopid) || IsObjectNameFilterOption(property.eopid) ||
                property.intValue == -1)
                continue;
            // SMARTGUARD chooses FLASH only from its eligible child set above.
            if (property.eopid == 701 && CidDerivesFrom(object.cid, 9))
                continue;
            if (property.eopid == 955 && CidDerivesFrom(object.cid, 64))
            {
                const auto *flyKind = classProperty(948);
                if (!flyKind || flyKind->intValue != 1)
                    continue; // Insect FLYs never resolve or consume oidSoar.
            }
            const auto missileNameMatch = [&](const BrxObjectRecord &candidate)
            {
                if (property.eopid != 827 || !CidDerivesFrom(candidate.cid, 3) || candidate.proxyRecordIndex < 0 ||
                    static_cast<std::size_t>(candidate.proxyRecordIndex) >= document.Proxies().size())
                    return false;
                const auto ownerIndex =
                    document.Proxies()[static_cast<std::size_t>(candidate.proxyRecordIndex)].ownerObjectIndex;
                return ownerIndex >= 0 && static_cast<std::size_t>(ownerIndex) < document.Objects().size() &&
                       document.Objects()[static_cast<std::size_t>(ownerIndex)].oid == property.intValue;
            };
            auto referenced = findContextualObject(static_cast<std::int16_t>(property.intValue));
            if (property.eopid == 827 &&
                (referenced == document.Objects().end() || !CidDerivesFrom(referenced->cid, 3)))
            {
                referenced = std::find_if(document.Objects().begin(), document.Objects().end(), missileNameMatch);
            }
            const std::string propertyName = PropertyNameForCid(object.cid, property.eopid)
                                                 ? PropertyNameForCid(object.cid, property.eopid)
                                                 : "object reference";
            if (referenced == document.Objects().end())
            {
                issue(identity + " " + propertyName + " references missing OID " + std::to_string(property.intValue));
                continue;
            }
            auto compatible =
                findContextualCompatibleObject(static_cast<std::int16_t>(property.intValue), property.eopid);
            if (property.eopid == 827 && compatible == document.Objects().end())
                compatible = std::find_if(document.Objects().begin(), document.Objects().end(), missileNameMatch);
            if (compatible == document.Objects().end())
                issue(identity + " " + propertyName + " requires " + ObjectReferenceTypeName(property.eopid) +
                      ", but OID " + std::to_string(property.intValue) + " is " +
                      BrxDocument::ClassName(referenced->cid));
        }
        if (CidDerivesFrom(object.cid, 23))
        {
            validateObjectReference(1048, "VAULT");
            validateObjectReference(1049, "VAULT");
            if (const auto *combination = classProperty(1046);
                combination && (combination->intValue < 0 || combination->intValue > 999))
                issue(identity + " VAULT combination must be a three-digit value from 000 to 999");

            std::array<int, 6> goadCounts{};
            std::array<int, 6> resolvedGoadCounts{};
            for (const auto &property : object.properties)
            {
                if (property.eopid != 1047)
                    continue;
                if (property.arguments.size() != 2)
                {
                    issue(identity + " VAULT goad-dialog entry must contain GOADK and DIALOG OID arguments");
                    continue;
                }
                if (property.arguments[0].type != 4179 || property.arguments[1].type != 6)
                    issue(identity + " VAULT goad-dialog entry has incorrect serialized argument types");
                const int category = property.arguments[0].intValue;
                if (category < 0 || category >= static_cast<int>(goadCounts.size()))
                {
                    issue(identity + " VAULT goad-dialog entry has invalid category " + std::to_string(category));
                    continue;
                }
                if (++goadCounts[static_cast<std::size_t>(category)] > 4)
                    issue(identity + " VAULT goad category " + std::to_string(category) +
                          " exceeds the four-dialog runtime limit");
                const int dialogOid = property.arguments[1].intValue;
                if (dialogOid == -1)
                {
                    issue(identity + " VAULT goad category " + std::to_string(category) +
                          " contains an empty DIALOG reference");
                    continue;
                }
                // PostVaultLoad resolves every authored goad dialog with
                // PloFindSwNearest(..., pvault).  Duplicate OIDs in reusable
                // packages must therefore be checked in VAULT context.
                const auto dialog = findContextualCompatibleObject(static_cast<std::int16_t>(dialogOid), 1049);
                if (dialog == document.Objects().end())
                    issue(identity + " VAULT goad entry references missing DIALOG OID " + std::to_string(dialogOid));
                else
                    ++resolvedGoadCounts[static_cast<std::size_t>(category)];
            }
            for (std::size_t category = 0; category < goadCounts.size(); ++category)
                if (resolvedGoadCounts[category] == 0)
                    issue(identity + " VAULT goad category " + std::to_string(category) +
                          " has no dialogs; runtime selection can index and modulo an empty list");

            struct VaultBinding
            {
                std::int16_t oid;
                std::int16_t baseCid;
                const char *role;
            };
            constexpr VaultBinding bindings[]{
                {886, 136, "state machine"}, {887, 2, "left tumbler"},    {888, 2, "middle tumbler"},
                {889, 2, "right tumbler"},   {892, 115, "Sly transform"},
            };
            const auto underVault = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                return false;
            };
            for (const VaultBinding &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        underVault(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " VAULT fixed " + binding.role + " binding references missing OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " VAULT has multiple hierarchy objects for fixed " + std::string(binding.role) +
                          " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.baseCid))
                    issue(identity + " VAULT fixed " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.baseCid));
            }
            const auto validateStateTable = [&](const std::int32_t machineObjectIndex, const std::int16_t firstState,
                                                const std::int16_t lastState, const std::string &role)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineObjectIndex; });
                if (machine == document.StateMachines().end())
                {
                    issue(identity + " VAULT " + role + " has no decoded state-machine payload");
                    return;
                }
                for (std::int16_t stateOid = firstState; stateOid <= lastState; ++stateOid)
                    if (std::none_of(machine->states.begin(), machine->states.end(),
                                     [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                        issue(identity + " VAULT " + role + " is missing required state OID " +
                              std::to_string(stateOid));
            };
            std::int32_t mainMachineIndex = -1;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                if (document.Objects()[candidateIndex].oid == 886 &&
                    underVault(static_cast<std::int32_t>(candidateIndex)) &&
                    CidDerivesFrom(document.Objects()[candidateIndex].cid, 136))
                {
                    mainMachineIndex = static_cast<std::int32_t>(candidateIndex);
                    break;
                }
            if (mainMachineIndex >= 0)
                validateStateTable(mainMachineIndex, 900, 911, "main state machine OID 886");

            constexpr std::int16_t instructionDialogOids[]{930, 932, 933, 934, 935, 936, 937, 939, 940, 941, 942,
                                                           943, 944, 945, 946, 947, 948, 949, 950, 951, 984};
            for (const std::int16_t dialogOid : instructionDialogOids)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == dialogOid &&
                        underVault(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " VAULT is missing fixed instruction DIALOG OID " + std::to_string(dialogOid));
                else if (matching.size() > 1)
                    issue(identity + " VAULT has multiple hierarchy objects for fixed instruction DIALOG OID " +
                          std::to_string(dialogOid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, 101))
                    issue(identity + " VAULT fixed instruction OID " + std::to_string(dialogOid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected DIALOG");
            }

            for (const std::int16_t tumblerOid : {std::int16_t{887}, std::int16_t{888}, std::int16_t{889}})
            {
                std::int32_t tumblerIndex = -1;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == tumblerOid &&
                        underVault(static_cast<std::int32_t>(candidateIndex)) &&
                        CidDerivesFrom(document.Objects()[candidateIndex].cid, 2))
                    {
                        tumblerIndex = static_cast<std::int32_t>(candidateIndex);
                        break;
                    }
                if (tumblerIndex < 0)
                    continue;
                const auto underTumbler = [&](std::int32_t candidate)
                {
                    for (std::size_t guard = 0;
                         candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                         guard++ <= document.Objects().size();
                         candidate =
                             [&]()
                         {
                             const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                             return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                         : current.parentIndex;
                         }())
                        if (candidate == tumblerIndex)
                            return true;
                    return false;
                };
                std::vector<std::int32_t> dialMachineIndices;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == 890 &&
                        underTumbler(static_cast<std::int32_t>(candidateIndex)) &&
                        CidDerivesFrom(document.Objects()[candidateIndex].cid, 136))
                        dialMachineIndices.push_back(static_cast<std::int32_t>(candidateIndex));
                if (dialMachineIndices.empty())
                    issue(identity + " VAULT tumbler OID " + std::to_string(tumblerOid) +
                          " has no contextual dial state machine OID 890");
                else if (dialMachineIndices.size() > 1)
                    issue(identity + " VAULT tumbler OID " + std::to_string(tumblerOid) +
                          " has multiple contextual dial state machines OID 890");
                else
                    validateStateTable(dialMachineIndices.front(), 912, 921,
                                       "tumbler OID " + std::to_string(tumblerOid) + " dial state machine OID 890");
            }
        }
        if (CidDerivesFrom(object.cid, 15))
        {
            const auto *pathProperty = classProperty(652);
            if (pathProperty == nullptr || pathProperty->intValue == -1)
                issue(identity + " PUFFC has no patrol PATHZONE; patrol and flee goal selection require one");
            else
            {
                // PostStepguardLoad resolves this inherited PATHZONE pointer
                // through PloFindSwNearest, so duplicate OIDs must be judged in
                // the PUFFC's hierarchy/proxy context rather than by the first
                // global record in stream order.
                const auto pathObject =
                    findContextualCompatibleObject(static_cast<std::int16_t>(pathProperty->intValue), 652);
                // The authoritative object-reference pass above reports a
                // missing or type-incompatible contextual target. Only run
                // PATHZONE geometry checks after that target resolves.
                if (pathObject != document.Objects().end())
                {
                    const auto pathIndex =
                        static_cast<std::int32_t>(std::distance(document.Objects().begin(), pathObject));
                    const auto zone = std::find_if(document.PathZones().begin(), document.PathZones().end(),
                                                   [&](const BrxPathZoneRecord &candidate)
                                                   { return candidate.ownerObjectIndex == pathIndex; });
                    if (zone == document.PathZones().end() || zone->vertices.empty())
                        issue(identity + " PUFFC patrol PATHZONE has no decoded navigation geometry");
                    else
                    {
                        float minimumX = zone->vertices.front()[0], maximumX = minimumX;
                        float minimumY = zone->vertices.front()[1], maximumY = minimumY;
                        const float puffcX = object.worldTransform[12];
                        const float puffcY = object.worldTransform[13];
                        float farthestDistanceSquared = 0.0f;
                        for (const auto &vertex : zone->vertices)
                        {
                            minimumX = std::min(minimumX, vertex[0]);
                            maximumX = std::max(maximumX, vertex[0]);
                            minimumY = std::min(minimumY, vertex[1]);
                            maximumY = std::max(maximumY, vertex[1]);
                            const float dx = vertex[0] - puffcX;
                            const float dy = vertex[1] - puffcY;
                            farthestDistanceSquared = std::max(farthestDistanceSquared, dx * dx + dy * dy);
                        }
                        const float diagonal = std::hypot(maximumX - minimumX, maximumY - minimumY);
                        if (!std::isfinite(diagonal) || diagonal <= 500.0f)
                            issue(
                                identity +
                                " PUFFC patrol PATHZONE is not wide enough for its greater-than-500-unit random-goal search");
                        else if (!std::isfinite(farthestDistanceSquared) || farthestDistanceSquared <= 500.0f * 500.0f)
                            issue(
                                identity +
                                " PUFFC patrol PATHZONE has no point more than 500 units from this creature; the runtime random-goal loop cannot terminate");
                    }
                }
            }

            const auto countClass = [&](const std::int16_t cid)
            {
                return std::count_if(document.Objects().begin(), document.Objects().end(),
                                     [&](const BrxObjectRecord &candidate)
                                     { return CidDerivesFrom(candidate.cid, cid); });
            };
            const auto pufferCount = countClass(24);
            const auto rostCount = countClass(68);
            if (pufferCount == 0)
                issue(identity + " PUFFC has no PUFFER controller to detect, flee from, or be collected by");
            else if (pufferCount > 1)
                issue(identity +
                      " PUFFC has multiple possible PUFFER controllers; the runtime uses global current-player state");
            if (rostCount == 0)
                issue(identity + " PUFFC has no ROST tunnel available for collection respawn");
            else if (rostCount > 32)
                issue(identity + " PUFFC encounter exceeds the 32-entry runtime ROST roster");

            for (const std::int16_t unusedAttackOption :
                 {std::int16_t{653}, std::int16_t{654}, std::int16_t{655}, std::int16_t{661}})
                if (classProperty(unusedAttackOption) != nullptr)
                    issue(identity + " PUFFC authors inherited attack option " + std::to_string(unusedAttackOption) +
                          ", but PUFFC has no custom attack callback and will not use it as a normal guard attack");
        }
        if (CidDerivesFrom(object.cid, 24))
        {
            if (const auto *timeLimit = classProperty(1051);
                timeLimit && (!std::isfinite(timeLimit->floatValue[0]) || timeLimit->floatValue[0] <= 0.0f))
                issue(identity + " PUFFER game time limit must be positive");

            if (classProperty(1052) != nullptr || classProperty(1053) != nullptr)
                issue(identity + " serializes a runtime-only PUFFER state-machine query (1052/1053)");

            const auto countClass = [&](const std::int16_t cid)
            {
                return std::count_if(document.Objects().begin(), document.Objects().end(),
                                     [&](const BrxObjectRecord &candidate)
                                     { return CidDerivesFrom(candidate.cid, cid); });
            };
            if (countClass(24) > 1)
                issue(identity +
                      " shares the world with another PUFFER; PUFFT ownership and global runtime state are ambiguous");
            if (countClass(72) == 0)
                issue(identity + " has no PUFFT targets; completion and difficulty calculations require at least one");
            if (countClass(15) == 0)
                issue(identity + " has no PUFFC charge creatures");
            if (countClass(68) == 0)
                issue(identity + " has no ROST respawn tunnels for the PUFFC charge loop");

            struct PufferBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                bool global;
                const char *role;
            };
            constexpr PufferBinding bindings[]{
                {985, 136, false, "shooting state machine"},
                {989, 123, false, "collection volume"},
                {990, 123, false, "fire volume"},
                {999, 126, false, "front-left floor probe"},
                {1000, 126, false, "front-right floor probe"},
                {1001, 126, false, "rear-left floor probe"},
                {1002, 126, false, "rear-right floor probe"},
                {1003, 121, false, "target explosion"},
                {1004, 126, false, "natural fire point"},
                {1005, 2, false, "fire gun"},
                {1006, 80, false, "fire emitter"},
                {1007, 2, false, "gun aim target"},
                {1008, 80, false, "spray emitter"},
                {1009, 136, true, "main challenge state machine"},
            };
            const auto underPuffer = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                return false;
            };
            for (const auto &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        (binding.global || underPuffer(static_cast<std::int32_t>(candidateIndex))))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required PUFFER " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple " + std::string(binding.global ? "global" : "hierarchy") +
                          " objects for required PUFFER " + binding.role + " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.cid))
                    issue(identity + " PUFFER " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.cid));
            }

            const auto validatePufferMachine =
                [&](const std::int16_t machineOid, const std::initializer_list<std::int16_t> states, const char *role)
            {
                std::int32_t machineIndex = -1;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == machineOid &&
                        (machineOid == 1009 || underPuffer(static_cast<std::int32_t>(candidateIndex))) &&
                        CidDerivesFrom(document.Objects()[candidateIndex].cid, 136))
                    {
                        machineIndex = static_cast<std::int32_t>(candidateIndex);
                        break;
                    }
                if (machineIndex < 0)
                    return;
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                {
                    issue(identity + " PUFFER " + role + " OID " + std::to_string(machineOid) +
                          " has no decoded state-machine payload");
                    return;
                }
                for (const std::int16_t stateOid : states)
                    if (std::none_of(machine->states.begin(), machine->states.end(),
                                     [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                        issue(identity + " PUFFER " + role + " OID " + std::to_string(machineOid) +
                              " is missing required state OID " + std::to_string(stateOid));
            };
            validatePufferMachine(985, {987, 988}, "shooting state machine");
            validatePufferMachine(1009, {1011, 1012, 1013}, "challenge state machine");

            for (const auto &candidate : document.Objects())
            {
                if (!CidDerivesFrom(candidate.cid, 72))
                    continue;
                for (const auto &required : std::array<std::pair<std::int16_t, std::int16_t>, 3>{
                         std::pair<std::int16_t, std::int16_t>{996, 80}, {997, 126}, {998, 121}})
                {
                    const auto child = std::find_if(
                        document.Objects().begin(), document.Objects().end(),
                        [&](const BrxObjectRecord &nested)
                        {
                            if (nested.oid != required.first || !CidDerivesFrom(nested.cid, required.second))
                                return false;
                            std::int32_t parent = nested.proxyRuntimeParentIndex >= 0 ? nested.proxyRuntimeParentIndex
                                                                                      : nested.parentIndex;
                            while (parent >= 0 && static_cast<std::size_t>(parent) < document.Objects().size())
                            {
                                if (&document.Objects()[static_cast<std::size_t>(parent)] == &candidate)
                                    return true;
                                const auto &parentObject = document.Objects()[static_cast<std::size_t>(parent)];
                                parent = parentObject.proxyRuntimeParentIndex >= 0
                                             ? parentObject.proxyRuntimeParentIndex
                                             : parentObject.parentIndex;
                            }
                            return false;
                        });
                    if (child == document.Objects().end())
                        issue(identity + " uses PUFFT OID " + std::to_string(candidate.oid) +
                              " without required descendant OID " + std::to_string(required.first));
                }
            }
        }
        if (CidDerivesFrom(object.cid, 25))
        {
            for (const std::int16_t id :
                 {std::int16_t{1080}, std::int16_t{1081}, std::int16_t{1082}, std::int16_t{1083}, std::int16_t{1097}})
            {
                const auto *property = classProperty(id);
                if (property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                    issue(identity + " SUV " + std::string(PropertyName(id) ? PropertyName(id) : "numeric property") +
                          " must be non-negative");
            }
            for (const std::int16_t id : {std::int16_t{1086}, std::int16_t{1093}, std::int16_t{1098}})
                if (const auto *property = classProperty(id); property && property->intValue < 0)
                    issue(identity + " SUV " + std::string(PropertyName(id) ? PropertyName(id) : "count") +
                          " must be non-negative");
            if (const auto *state = classProperty(1090); state && (state->intValue < -1 || state->intValue > 3))
                issue(identity + " has invalid SUV state " + std::to_string(state->intValue));
            const auto *gameKind = classProperty(1092);
            if (gameKind && (gameKind->intValue < -1 || gameKind->intValue > 1))
                issue(identity + " has invalid SUV game kind " + std::to_string(gameKind->intValue));
            if (classProperty(1086) != nullptr && classProperty(1093) != nullptr)
                issue(identity + " serializes both SUV lap-count setters (1086 and 1093); "
                                 "both write the same runtime field and stream order determines the result");
            for (const std::int16_t id :
                 {std::int16_t{1084}, std::int16_t{1085}, std::int16_t{1087}, std::int16_t{1088}, std::int16_t{1089},
                  std::int16_t{1091}, std::int16_t{1094}, std::int16_t{1095}, std::int16_t{1096}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only SUV option " + std::to_string(id) +
                          " instead of SetUser configuration");

            struct SuvBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                const char *role;
            };
            constexpr SuvBinding suvBindings[]{
                {1046, 2, "front-left wheel"},
                {1047, 2, "front-right wheel"},
                {1048, 2, "rear-left wheel"},
                {1049, 2, "rear-right wheel"},
                {1050, 126, "front-left suspension probe"},
                {1051, 126, "front-right suspension probe"},
                {1052, 126, "rear-left suspension probe"},
                {1053, 126, "rear-right suspension probe"},
                {1054, 2, "front-left hub"},
                {1055, 2, "front-right hub"},
                {1056, 2, "rear-left hub"},
                {1057, 2, "rear-right hub"},
                {1058, 2, "shadow component"},
                {1064, 2, "front axle"},
                {1065, 2, "rear axle"},
                {1066, 136, "puncher state machine"},
                {1067, 3, "puncher collision object"},
                {1059, 80, "boost emitter"},
                {1060, 121, "dirt explosion"},
                {1061, 121, "dust explosion"},
            };
            const auto underSuv = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                return false;
            };
            std::int32_t puncherMachineIndex = -1;
            for (const SuvBinding &binding : suvBindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        underSuv(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required SUV " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple hierarchy objects for required SUV " + std::string(binding.role) +
                          " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.cid))
                    issue(identity + " SUV " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.cid));
                else if (binding.oid == 1066)
                    puncherMachineIndex = static_cast<std::int32_t>(matching.front());
            }
            if (puncherMachineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == puncherMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " SUV puncher state machine OID 1066 has no decoded payload");
                else
                {
                    const std::int16_t requiredState = gameKind != nullptr && gameKind->intValue == 1 ? 1156 : 900;
                    if (std::none_of(machine->states.begin(), machine->states.end(),
                                     [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                        issue(identity + " SUV puncher state machine OID 1066 is missing startup state OID " +
                              std::to_string(requiredState));
                }
            }
        }
        if (CidDerivesFrom(object.cid, 26))
        {
            if (classProperty(1114) != nullptr)
                issue(identity + " serializes runtime-only LGN state option 1114");
            struct LgnBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                bool global;
                const char *role;
            };
            constexpr LgnBinding bindings[]{
                {861, 136, false, "state machine"}, {867, 2, false, "turret"},
                {868, 2, false, "platform"},        {96, 2, false, "gut suspension"},
                {869, 2, false, "mounted player"},  {352, 2, false, "charm target 0"},
                {353, 2, false, "charm target 1"},  {354, 2, false, "charm target 2"},
                {355, 2, false, "charm target 3"},  {870, 2, true, "auxiliary component"},
            };
            const auto underLgn = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                return false;
            };
            std::int32_t machineIndex = -1;
            for (const LgnBinding &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        (binding.global || underLgn(static_cast<std::int32_t>(candidateIndex))))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required LGN " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple " + std::string(binding.global ? "global" : "hierarchy") +
                          " objects for required LGN " + binding.role + " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.cid))
                    issue(identity + " LGN " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.cid));
                else if (binding.oid == 861)
                    machineIndex = static_cast<std::int32_t>(matching.front());
            }
            if (machineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " LGN state machine OID 861 has no decoded payload");
                else
                    for (std::int16_t stateOid = 862; stateOid <= 866; ++stateOid)
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " LGN state machine OID 861 is missing required state OID " +
                                  std::to_string(stateOid));
            }
        }
        if (CidDerivesFrom(object.cid, 28))
        {
            for (const std::int16_t id : {std::int16_t{1159}, std::int16_t{1160}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only RYTHM option " + std::to_string(id));

            const auto underRythm = [&](std::int32_t candidateIndex)
            {
                for (std::int32_t cursor = candidateIndex; cursor >= 0;)
                {
                    if (cursor == static_cast<std::int32_t>(index))
                        return true;
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(cursor)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent < 0 || parent == cursor)
                        break;
                    cursor = parent;
                }
                return false;
            };
            std::int32_t machineIndex = -1;
            for (std::int16_t requiredOid = 1225; requiredOid <= 1235; ++requiredOid)
            {
                if (requiredOid >= 1230 && requiredOid <= 1231)
                    continue; // SM states, not hierarchy objects.
                if (requiredOid >= 1226 && requiredOid <= 1229)
                    continue; // SM states, not hierarchy objects.
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == requiredOid &&
                        underRythm(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required RYTHM " +
                          std::string(requiredOid == 1225 ? "state machine" : "button marker") + " OID " +
                          std::to_string(requiredOid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple hierarchy objects for required RYTHM " +
                          std::string(requiredOid == 1225 ? "state machine" : "button marker") + " OID " +
                          std::to_string(requiredOid));
                else
                {
                    const std::int16_t expectedCid = requiredOid == 1225 ? 136 : 115;
                    const auto &found = document.Objects()[matching.front()];
                    if (!CidDerivesFrom(found.cid, expectedCid))
                        issue(identity + " RYTHM OID " + std::to_string(requiredOid) + " is " +
                              BrxDocument::ClassName(found.cid) + ", expected " + BrxDocument::ClassName(expectedCid));
                    else if (requiredOid == 1225)
                        machineIndex = static_cast<std::int32_t>(matching.front());
                }
            }
            if (machineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " RYTHM state machine OID 1225 has no decoded payload");
                else
                    for (std::int16_t stateOid = 1226; stateOid <= 1231; ++stateOid)
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " RYTHM state machine OID 1225 is missing required state OID " +
                                  std::to_string(stateOid));
            }
        }
        if (CidDerivesFrom(object.cid, 29))
        {
            for (const std::int16_t id : {std::int16_t{1172}, std::int16_t{1173}})
                if (const auto *property = classProperty(id); property && property->intValue < 0)
                    issue(identity + " MGV " + std::string(PropertyName(id) ? PropertyName(id) : "counter") +
                          " must be non-negative");
            for (const std::int16_t id :
                 {std::int16_t{1170}, std::int16_t{1171}, std::int16_t{1174}, std::int16_t{1175}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only MGV option " + std::to_string(id) +
                          " instead of SetUser configuration");
            struct MgvBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                const char *role;
            };
            constexpr MgvBinding bindings[] = {
                {1317, 3, "aim target"},           {1320, 77, "aim controller"},    {354, 2, "charm target 2"},
                {355, 2, "charm target 3"},        {352, 2, "charm target 0"},      {1319, 136, "aim state machine"},
                {1321, 131, "inactive animation"}, {1322, 131, "active animation"}, {1323, 131, "charm animation"},
                {1324, 131, "dead animation"},     {1325, 131, "impact animation"}, {1326, 131, "damage animation"}};
            const auto underMgv = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0; candidate >= 0 &&
                                            static_cast<std::size_t>(candidate) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                    const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                    const std::int32_t parent =
                        current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex : current.parentIndex;
                    if (parent == candidate)
                        break;
                    candidate = parent;
                }
                return false;
            };
            std::int32_t aimMachineIndex = -1;
            for (const auto &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        underMgv(static_cast<std::int32_t>(candidateIndex)))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required MGV " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple hierarchy objects for required MGV " + std::string(binding.role) +
                          " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.cid))
                    issue(identity + " MGV " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.cid));
                else if (binding.oid == 1319)
                    aimMachineIndex = static_cast<std::int32_t>(matching.front());
            }
            if (aimMachineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == aimMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " MGV aim state machine OID 1319 has no decoded payload");
                else
                    for (const std::int16_t stateOid : {std::int16_t{1156}, std::int16_t{1157}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " MGV aim state machine OID 1319 is missing required state OID " +
                                  std::to_string(stateOid));
            }
        }
        if (CidDerivesFrom(object.cid, 30))
        {
            issue(identity +
                  " is a level-authored RIPG, but the game creates its ten reusable RIPG "
                  "pool objects while loading SW. A BRX RIPG is not part of that pool and has no "
                  "class-specific particle definition; author RIP/model particles through an EMITTER instead");
        }
        if (CidDerivesFrom(object.cid, 31))
        {
            const auto waterCollisionCount = std::count_if(
                document.CollisionTriangles().begin(), document.CollisionTriangles().end(),
                [&](const BrxCollisionTriangle &triangle)
                { return !triangle.cameraGeometry && triangle.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (waterCollisionCount == 0)
                issue(identity + " WATER has no gameplay collision volume and cannot detect submerged objects");
            if (!object.so.present)
                issue(identity + " WATER is missing its required SO payload");
            else
            {
                if (!object.so.fixedPhysics)
                    issue(identity + " WATER physics is not fixed; InitWater locks force and torque constraints");
                const auto finiteBounds = [&](const float *values)
                { return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]); };
                if (!finiteBounds(object.so.collisionGeometry.boundsMin) ||
                    !finiteBounds(object.so.collisionGeometry.boundsMax))
                    issue(identity + " WATER collision bounds contain a non-finite value");
                for (int axis = 0; axis < 3; ++axis)
                    if (object.so.collisionGeometry.boundsMin[axis] > object.so.collisionGeometry.boundsMax[axis])
                        issue(identity + " WATER collision bounds are inverted on axis " + std::to_string(axis));
            }
            for (const auto &warp : document.GlobWarps())
            {
                if (warp.ownerObjectIndex != static_cast<std::int32_t>(index))
                    continue;
                if (warp.controllerOid < 0)
                    issue(identity + " WATER surface warp has no WR controller OID");
                else
                {
                    // PostGlobsetLoad uses PloFindSwObject(..., 0x104, ..., palo),
                    // so duplicated WR OIDs must resolve in this ALO/proxy context.
                    const auto controller = findEarlyContextualCompatible(warp.controllerOid, 141);
                    if (controller == document.Objects().end())
                        issue(identity + " WATER surface warp references missing WR controller OID " +
                              std::to_string(warp.controllerOid));
                }
                if (!std::isfinite(warp.innerDistance) || !std::isfinite(warp.outerDistance) ||
                    warp.innerDistance < 0.0f || warp.outerDistance < warp.innerDistance)
                    issue(identity + " WATER surface warp has invalid inner/outer distances");
            }
            if (const auto *current = classProperty(203);
                current && (!std::isfinite(current->floatValue[0]) || !std::isfinite(current->floatValue[1]) ||
                            !std::isfinite(current->floatValue[2])))
                issue(identity + " WATER current contains a non-finite component");
            for (const auto coefficient : {std::int16_t{134}, std::int16_t{135}})
                if (const auto *value = classProperty(coefficient);
                    value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] < 0.0f))
                    issue(identity + " WATER " +
                          std::string(PropertyName(coefficient) ? PropertyName(coefficient) : "coefficient") +
                          " must be finite and non-negative");
            if (const auto *throwHeight = classProperty(208);
                throwHeight && (!std::isfinite(throwHeight->floatValue[0]) || throwHeight->floatValue[0] < 0.0f))
                issue(identity + " WATER throw height must be non-negative");
            validateAllObjectReferences(204, "WATER");
            const auto zapTargetCount =
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 204; });
            if (zapTargetCount > 32)
                issue(identity + " WATER serializes " + std::to_string(zapTargetCount) +
                      " zap targets, but the runtime capacity is 32");
            for (const std::int16_t id : {std::int16_t{205}, std::int16_t{206}, std::int16_t{207}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only WATER option " + std::to_string(id) +
                          " instead of SetUser configuration");
        }
        if (CidDerivesFrom(object.cid, 32))
        {
            for (const std::int16_t id : {std::int16_t{228}, std::int16_t{229}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only BRK option " + std::to_string(id) +
                          " instead of SetUser configuration");
            for (const std::int16_t id : {std::int16_t{217}, std::int16_t{220}, std::int16_t{221}, std::int16_t{232},
                                          std::int16_t{237}, std::int16_t{238}, std::int16_t{239}})
            {
                const auto *property = classProperty(id);
                if (property && !std::isfinite(property->floatValue[0]))
                    issue(identity + " BRK " + std::string(PropertyName(id) ? PropertyName(id) : "numeric property") +
                          " is not finite");
            }
            if (const auto *limits = classProperty(219);
                limits && (!std::isfinite(limits->floatValue[0]) || !std::isfinite(limits->floatValue[1]) ||
                           limits->floatValue[0] > limits->floatValue[1]))
                issue(identity + " has invalid BRK break-position velocity limits");
            if (const auto *hits = classProperty(227); hits && hits->intValue < 1)
                issue(identity + " BRK hits required to break must be at least one");
            if (const auto *reach = classProperty(231); reach && (reach->intValue < -1 || reach->intValue > 1))
                issue(identity + " has invalid BRK explosion reach kind " + std::to_string(reach->intValue));
            for (const std::int16_t id :
                 {std::int16_t{212}, std::int16_t{213}, std::int16_t{214}, std::int16_t{215}, std::int16_t{222}})
                validateAllObjectReferences(id, "BRK");
            validateObjectReference(218, "BRK");
            validateObjectReference(240, "BRK");
            const auto countOption = [&](const std::int16_t id)
            {
                return std::count_if(object.properties.begin(), object.properties.end(),
                                     [&](const BrxPropertyRecord &property) { return property.eopid == id; });
            };
            const auto ignoredBreakHelper = [&](const BrxObjectRecord &candidate)
            {
                return CidDerivesFrom(candidate.cid, 118) || CidDerivesFrom(candidate.cid, 122) ||
                       CidDerivesFrom(candidate.cid, 83);
            };
            const auto directChildForOid = [&](const std::int16_t oid) -> const BrxObjectRecord *
            {
                const auto found =
                    std::find_if(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate)
                                 {
                                     const std::int32_t runtimeParent = candidate.proxyRuntimeParentIndex >= 0
                                                                            ? candidate.proxyRuntimeParentIndex
                                                                            : candidate.parentIndex;
                                     return runtimeParent == static_cast<std::int32_t>(index) && candidate.oid == oid;
                                 });
                return found == document.Objects().end() ? nullptr : &*found;
            };
            std::unordered_map<std::int16_t, std::int16_t> assignedPartRoles;
            std::size_t explicitPartRules = 0;
            for (const auto &property : object.properties)
            {
                if (property.eopid < 212 || property.eopid > 215)
                    continue;
                ++explicitPartRules;
                const auto [role, inserted] =
                    assignedPartRoles.emplace(static_cast<std::int16_t>(property.intValue), property.eopid);
                if (!inserted)
                    issue(identity + " assigns child OID " + std::to_string(property.intValue) +
                          " to multiple BRK part roles");
                const auto *child = directChildForOid(static_cast<std::int16_t>(property.intValue));
                if (child == nullptr)
                    issue(identity + " BRK part OID " + std::to_string(property.intValue) + " is not a direct child");
                else if (ignoredBreakHelper(*child))
                    issue(identity + " assigns helper " + std::string(BrxDocument::ClassName(child->cid)) + " OID " +
                          std::to_string(child->oid) + " to a BRK part role, but the runtime ignores it");
            }
            std::size_t automaticAppearParts = 0;
            for (const auto &child : document.Objects())
            {
                const std::int32_t runtimeParent =
                    child.proxyRuntimeParentIndex >= 0 ? child.proxyRuntimeParentIndex : child.parentIndex;
                if (runtimeParent == static_cast<std::int32_t>(index) && !ignoredBreakHelper(child) &&
                    assignedPartRoles.find(child.oid) == assignedPartRoles.end())
                    ++automaticAppearParts;
            }
            const std::size_t runtimePartEntries = explicitPartRules + automaticAppearParts;
            if (runtimePartEntries > 16)
                issue(identity + " requires " + std::to_string(runtimePartEntries) +
                      " BRK runtime part entries, but the hard capacity is 16");
            if (const auto *priority = classProperty(218); priority != nullptr && priority->intValue != -1)
            {
                const auto *child = directChildForOid(static_cast<std::int16_t>(priority->intValue));
                if (child == nullptr)
                    issue(identity + " BRK high-priority OID " + std::to_string(priority->intValue) +
                          " is not a direct child");
                else if (ignoredBreakHelper(*child))
                    issue(identity + " BRK high-priority child is a helper ignored by part processing");
            }
            if (countOption(222) > 4)
                issue(identity + " has more than four BRK touch-object rules");
            if (countOption(223) > 4)
                issue(identity + " has more than four BRK touch-class rules");
            if (countOption(224) > 3)
                issue(identity + " has more than three BRK absorb-class rules");
            for (const auto &property : object.properties)
                if ((property.eopid == 223 || property.eopid == 224) &&
                    (property.intValue < 0 || static_cast<std::size_t>(property.intValue) >= CidOptionIds().size() ||
                     !SoClassFilterCanMatch(static_cast<std::int16_t>(property.intValue))))
                    issue(identity + " BRK " +
                          std::string(PropertyName(property.eopid) ? PropertyName(property.eopid) : "class filter") +
                          " must name a class that can match an SO");
            for (const std::int16_t id : {std::int16_t{217}, std::int16_t{220}, std::int16_t{221}, std::int16_t{232},
                                          std::int16_t{237}, std::int16_t{238}})
                if (const auto *value = classProperty(id); value && value->floatValue[0] < 0.0f)
                    issue(identity + " BRK " + std::string(PropertyName(id) ? PropertyName(id) : "numeric property") +
                          " must be non-negative");
            if (const auto *coins = classProperty(233); coins && coins->intValue < -1)
                issue(identity + " BRK coin count may be -1 for the class default, or zero and above");
            if (const auto *volume = classProperty(239);
                volume && (volume->floatValue[0] < 0.0f || volume->floatValue[0] > 1.0f))
                issue(identity + " BRK sound volume must be between zero and one");
            const bool hasCustomSound = classProperty(236) != nullptr || classProperty(237) != nullptr ||
                                        classProperty(238) != nullptr || classProperty(239) != nullptr;
            const auto *audibleDistance = classProperty(237);
            const auto *fullVolumeDistance = classProperty(238);
            const float effectiveAudibleDistance = audibleDistance ? audibleDistance->floatValue[0] : 3000.0f;
            const float effectiveFullVolumeDistance = fullVolumeDistance ? fullVolumeDistance->floatValue[0] : 300.0f;
            if (hasCustomSound && effectiveFullVolumeDistance > effectiveAudibleDistance)
                issue(identity + " BRK full-volume distance exceeds its maximum audible distance");
            if ((classProperty(237) != nullptr || classProperty(238) != nullptr || classProperty(239) != nullptr) &&
                classProperty(236) == nullptr)
                issue(
                    identity +
                    " customizes BRK sound distances or volume without a sound ID; PsfxEnsureBrk creates an SFXID_Nil record instead of using stock sound 42");
            const auto runtimeParentOf = [&](const BrxObjectRecord &candidate)
            {
                return candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                              : candidate.parentIndex;
            };
            const auto isRuntimeDescendant = [&](std::int32_t candidateIndex)
            {
                std::size_t guard = 0;
                for (std::int32_t current = candidateIndex;
                     current >= 0 && static_cast<std::size_t>(current) < document.Objects().size() &&
                     guard++ <= document.Objects().size();)
                {
                    const std::int32_t parent = runtimeParentOf(document.Objects()[static_cast<std::size_t>(current)]);
                    if (parent == static_cast<std::int32_t>(index))
                        return true;
                    if (parent == current)
                        break;
                    current = parent;
                }
                return false;
            };
            const auto *explicitExplosion = classProperty(240);
            std::size_t inferredExplosionCount = 0;
            bool explicitExplosionFound = false;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (!CidDerivesFrom(candidate.cid, 119) ||
                    !isRuntimeDescendant(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                ++inferredExplosionCount;
                if (explicitExplosion != nullptr && explicitExplosion->intValue != -1 &&
                    candidate.oid == explicitExplosion->intValue)
                    explicitExplosionFound = true;
            }
            if (explicitExplosion != nullptr && explicitExplosion->intValue != -1 && !explicitExplosionFound)
                issue(identity +
                      " BRK explosion OID does not identify a descendant EXPL; runtime search mode 1 cannot bind it");
            if ((explicitExplosion == nullptr || explicitExplosion->intValue == -1) && inferredExplosionCount > 8)
                issue(
                    identity +
                    " has more than eight inferred EXPL descendants; InferExpl removes every match but retains only the first eight");
        }
        if (CidDerivesFrom(object.cid, 34))
        {
            if (classProperty(298) != nullptr)
                issue(identity + " serializes runtime-only ALBRK owning-alarm link 298");
            std::size_t owningAlarmCount = 0;
            for (std::size_t alarmIndex = 0; alarmIndex < document.Objects().size(); ++alarmIndex)
            {
                const auto &candidate = document.Objects()[alarmIndex];
                if (!CidDerivesFrom(candidate.cid, 47))
                    continue;
                for (const auto &property : candidate.properties)
                {
                    if (property.eopid != 289 || property.intValue != object.oid)
                        continue;
                    const auto resolved =
                        findHierarchyNearestObjects(object.oid, static_cast<std::int32_t>(alarmIndex));
                    if (resolved.size() > 1)
                        issue(std::string(BrxDocument::ClassName(candidate.cid)) + " OID " +
                              std::to_string(candidate.oid) + " ALBRK OID " + std::to_string(object.oid) +
                              " has multiple equally-near mode-4 matches");
                    if (std::find(resolved.begin(), resolved.end(), static_cast<std::int32_t>(index)) != resolved.end())
                        ++owningAlarmCount;
                }
            }
            if (owningAlarmCount == 0)
                issue(identity + " is not referenced by a contextual ALARM option 289; BreakAlbrk "
                                 "will call DisableAlarmAlbrk through a null owning-alarm pointer");
            else if (owningAlarmCount > 1)
                issue(identity + " is referenced " + std::to_string(owningAlarmCount) +
                      " times by contextual ALARM option 289; ALBRK stores only one owning alarm and "
                      "the last callback overwrites earlier ownership");
        }
        if (CidDerivesFrom(object.cid, 35))
        {
            for (const std::int16_t id : {std::int16_t{821}, std::int16_t{822}})
                if (classProperty(id) != nullptr)
                    issue(identity + " serializes runtime-only DARTGUN option " + std::to_string(id) +
                          " instead of SetUser configuration");
            const auto *dartTemplateProperty = classProperty(816);
            if (dartTemplateProperty == nullptr || dartTemplateProperty->intValue == -1)
                issue(identity +
                      " has no DART template OID; PostDartgunLoad dereferences the child lookup unconditionally");
            else
            {
                const auto dartTemplates = findContextDescendants(
                    static_cast<std::int16_t>(dartTemplateProperty->intValue), static_cast<std::int32_t>(index));
                if (dartTemplates.empty())
                    issue(identity + " DART template OID " + std::to_string(dartTemplateProperty->intValue) +
                          " is not in its runtime hierarchy; PloFindSwChild will return null");
                else if (dartTemplates.size() > 1)
                    issue(identity + " has multiple hierarchy descendants matching DART template OID " +
                          std::to_string(dartTemplateProperty->intValue) +
                          "; PloFindSwChild will bind the first hash-list match");
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(dartTemplates.front())].cid, 70))
                    issue(identity + " DART template OID " + std::to_string(dartTemplateProperty->intValue) + " is " +
                          BrxDocument::ClassName(
                              document.Objects()[static_cast<std::size_t>(dartTemplates.front())].cid) +
                          ", expected DART");
            }
            if (const auto *count = classProperty(817); count && count->intValue < 1)
                issue(identity + " DARTGUN dart count must be at least one");
            if (const auto *interval = classProperty(818);
                interval && (!std::isfinite(interval->floatValue[0]) || interval->floatValue[0] < 0.0f))
                issue(identity + " DARTGUN fire interval must be non-negative");
            if (const auto *offset = classProperty(819); offset && !std::isfinite(offset->floatValue[0]))
                issue(identity + " DARTGUN target-height offset is not finite");
            const auto ratTargetCount =
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 820; });
            if (ratTargetCount > 8)
                issue(identity + " has more than eight DARTGUN rat targets");
            std::unordered_set<std::int32_t> ratTargetOids;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 820)
                    continue;
                if (!ratTargetOids.insert(property.intValue).second)
                    issue(identity + " repeats DARTGUN rat target OID " + std::to_string(property.intValue));
                const auto target = findContextualObject(static_cast<std::int16_t>(property.intValue));
                if (target == document.Objects().end())
                    issue(identity + " references missing DARTGUN rat target OID " + std::to_string(property.intValue));
                else if (!CidDerivesFrom(target->cid, 65))
                    issue(identity + " DARTGUN rat target OID " + std::to_string(property.intValue) + " is " +
                          BrxDocument::ClassName(target->cid) + ", expected RAT");
            }
            struct DartgunBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                const char *role;
            };
            constexpr DartgunBinding bindings[] = {
                {692, 2, "look-at controller"}, {693, 2, "lid"}, {694, 136, "state machine"}};
            std::int32_t machineIndex = -1;
            for (const auto &binding : bindings)
            {
                const auto found = findContextDescendants(binding.oid, static_cast<std::int32_t>(index));
                if (found.empty())
                    issue(identity + " is missing required DARTGUN " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (found.size() > 1)
                    issue(identity + " has multiple hierarchy descendants matching DARTGUN " + binding.role + " OID " +
                          std::to_string(binding.oid) + "; SnipAloObjects will bind the first hash-list match");
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(found.front())].cid, binding.cid))
                    issue(identity + " DARTGUN " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[static_cast<std::size_t>(found.front())].cid) +
                          ", expected " + BrxDocument::ClassName(binding.cid));
                else if (binding.oid == 694)
                    machineIndex = found.front();
            }
            if (machineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " DARTGUN state machine OID 694 has no decoded payload");
                else
                    for (std::int16_t stateOid = 695; stateOid <= 699; ++stateOid)
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " DARTGUN state machine OID 694 is missing required state OID " +
                                  std::to_string(stateOid));
            }
        }
        if (CidDerivesFrom(object.cid, 36))
        {
            if (classProperty(1113) != nullptr)
                issue(identity + " serializes runtime-only SWP shape command 1113");
            for (const std::int16_t id :
                 {std::int16_t{1109}, std::int16_t{1110}, std::int16_t{1111}, std::int16_t{1112}})
                if (const auto *property = classProperty(id);
                    property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                    issue(identity + " SWP " + std::string(PropertyName(id) ? PropertyName(id) : "tuning property") +
                          " must be non-negative");
            const auto *fastSpeed = classProperty(1109);
            const auto *slowSpeed = classProperty(1110);
            const float effectiveFastSpeed = fastSpeed ? fastSpeed->floatValue[0] : 1000.0f;
            const float effectiveSlowSpeed = slowSpeed ? slowSpeed->floatValue[0] : 0.0f;
            if (effectiveFastSpeed <= 0.0f)
                issue(
                    identity +
                    " SWP fast-impact speed must be greater than zero because SetSwpShape divides by the generated speed profile");
            if (const auto *maximumDelta = classProperty(1112); maximumDelta && maximumDelta->floatValue[0] <= 0.0f)
                issue(identity +
                      " SWP maximum velocity delta must be greater than zero to generate a usable spline timing curve");
            if (effectiveSlowSpeed > effectiveFastSpeed)
                issue(identity + " SWP slow-impact speed exceeds fast-impact speed");
            std::int32_t stateMachineIndex = -1;
            const auto stateMachines = findContextDescendants(875, static_cast<std::int32_t>(index));
            const auto crashExplosions = findContextDescendants(876, static_cast<std::int32_t>(index));
            if (stateMachines.empty())
                issue(identity + " SWP is missing descendant SM OID 875 required by PostSwpLoad");
            else if (stateMachines.size() > 1)
                issue(
                    identity +
                    " has multiple hierarchy descendants matching SWP SM OID 875; SnipAloObjects will bind the first hash-list match");
            else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(stateMachines.front())].cid, 136))
                issue(identity + " SWP OID 875 is not an SM");
            else
                stateMachineIndex = stateMachines.front();
            if (crashExplosions.empty())
                issue(identity + " SWP is missing descendant EXPLO OID 876 required by its crash path");
            else if (crashExplosions.size() > 1)
                issue(
                    identity +
                    " has multiple hierarchy descendants matching SWP EXPLO OID 876; SnipAloObjects will bind the first hash-list match");
            else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(crashExplosions.front())].cid, 121))
                issue(identity + " SWP OID 876 is not an EXPLO");
            if (stateMachineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == stateMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " SWP state-machine object OID 875 has no decoded state-machine payload");
                else
                {
                    for (const std::int16_t requiredState : {std::int16_t{878}, std::int16_t{879}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                            issue(identity + " SWP state machine OID 875 is missing required state OID " +
                                  std::to_string(requiredState));
                }
            }
        }
        if (CidDerivesFrom(object.cid, 37))
        {
            if (classProperty(245) != nullptr)
                issue(identity + " serializes runtime-only FRAGILE impacting-object link 245");
            if (const auto *mass = classProperty(242);
                mass && (!std::isfinite(mass->floatValue[0]) || mass->floatValue[0] < 0.0f))
                issue(identity + " FRAGILE impact mass must be non-negative");
            else if (mass && mass->floatValue[0] == 0.0f)
                issue(identity + " FRAGILE impact mass multiplier is zero; contact response may be degenerate");
            for (const std::int16_t id : {std::int16_t{243}, std::int16_t{244}})
                if (const auto *constraint = classProperty(id);
                    constraint && (constraint->intValue < -1 || constraint->intValue > 7))
                    issue(identity + " has invalid FRAGILE " +
                          std::string(PropertyName(id) ? PropertyName(id) : "constraint"));
            const bool hasImpactCollision = object.so.present && (!object.so.collisionGeometry.positions.empty() ||
                                                                  !object.so.collisionGeometry.surfaces.empty() ||
                                                                  !object.so.collisionGeometry.bspNodes.empty());
            if (!hasImpactCollision)
                issue(
                    identity +
                    " FRAGILE has no gameplay collision geometry, so AdjustFragileNewXp cannot receive a physical contact");
        }
        const auto countOption = [&](const std::int16_t id)
        {
            return std::count_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
        };
        const auto validateNonNegativeFloat = [&](const std::int16_t id, const char *owner)
        {
            if (const auto *property = classProperty(id);
                property && (!std::isfinite(property->floatValue[0]) || property->floatValue[0] < 0.0f))
                issue(identity + " " + owner + " " +
                      std::string(PropertyName(id) ? PropertyName(id) : "numeric property") + " must be non-negative");
        };
        if (CidDerivesFrom(object.cid, 39))
        {
            validateNonNegativeFloat(247, "BRKP");
            validateNonNegativeFloat(248, "BRKP");
            if (const auto *state = classProperty(249); state && (state->intValue < -1 || state->intValue > 2))
                issue(identity + " has invalid BRKP state " + std::to_string(state->intValue));
            const int initialState = classProperty(249) ? classProperty(249)->intValue : 0;

            const auto runtimeParentOf = [](const BrxObjectRecord &candidate)
            {
                return candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                              : candidate.parentIndex;
            };
            std::int32_t ownerIndex = -1;
            bool ownerIsBomb = false;
            const std::int32_t runtimeParent = runtimeParentOf(object);
            for (std::int32_t ancestor = runtimeParent, depth = 0;
                 ancestor >= 0 && static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                 depth <= static_cast<std::int32_t>(document.Objects().size());
                 ++depth)
            {
                const auto &candidate = document.Objects()[static_cast<std::size_t>(ancestor)];
                if (CidDerivesFrom(candidate.cid, 59) || CidDerivesFrom(candidate.cid, 32))
                {
                    ownerIndex = ancestor;
                    ownerIsBomb = CidDerivesFrom(candidate.cid, 59);
                    break;
                }
                const std::int32_t parent = runtimeParentOf(candidate);
                if (parent == ancestor)
                    break;
                ancestor = parent;
            }
            if (initialState == 0 && ownerIndex >= 0 && runtimeParent != ownerIndex)
                issue(identity + " BRKP is nested below its " + std::string(ownerIsBomb ? "BOMB" : "BRK") +
                      " owner; the runtime debris roster only discovers direct children");
        }
        if (CidDerivesFrom(object.cid, 38))
        {
            if (const auto *damageKind = classProperty(246);
                damageKind && (damageKind->intValue < -1 || damageKind->intValue > 5))
                issue(identity + " has invalid ZAPBREAK damage kind " + std::to_string(damageKind->intValue));
        }
        if (CidDerivesFrom(object.cid, 40))
        {
            for (const auto id : {std::int16_t{251}, std::int16_t{252}, std::int16_t{253}, std::int16_t{254}})
                validateAllObjectReferences(id, "BUTTON");
            validateObjectReference(265, "BUTTON");
            // Both values intentionally have signed runtime semantics. A negative
            // reset delay latches the button, and a negative animation speed plays
            // the configured ASEGs in reverse.
            for (const auto id : {std::int16_t{257}, std::int16_t{259}})
            {
                if (const auto *value = classProperty(id); value != nullptr && !std::isfinite(value->floatValue[0]))
                    issue(identity + " BUTTON " +
                          std::string(PropertyName(id) ? PropertyName(id) : "numeric property") + " must be finite");
            }
            if (const auto *kind = classProperty(258); kind && (kind->intValue < -1 || kind->intValue > 4))
                issue(identity + " has invalid BUTTON type " + std::to_string(kind->intValue));
            if (classProperty(266) != nullptr)
                issue(identity + " serializes runtime-only BUTTON state 266");
            const auto validateTypedButtonReference =
                [&](const BrxPropertyRecord &property, const std::int16_t requiredCid, const char *label)
            {
                if (property.intValue < 0)
                    return;
                const auto referenced = findContextualObject(static_cast<std::int16_t>(property.intValue));
                if (referenced == document.Objects().end())
                    issue(identity + " BUTTON " + label + " references missing OID " +
                          std::to_string(property.intValue));
                else if (!CidDerivesFrom(referenced->cid, requiredCid))
                    issue(identity + " BUTTON " + label + " OID " + std::to_string(property.intValue) + " references " +
                          BrxDocument::ClassName(referenced->cid) + ", expected " +
                          BrxDocument::ClassName(requiredCid));
            };
            for (const auto &property : object.properties)
            {
                if (property.eopid == 251 || property.eopid == 252)
                    validateTypedButtonReference(property, 131, "animation");
                else if (property.eopid == 265)
                    validateTypedButtonReference(property, 101, "dialog");
            }
            std::vector<std::pair<std::int32_t, std::int32_t>> stateMachinePairs;
            std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> stateMachineSides;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 264)
                    continue;
                if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4)
                {
                    issue(identity + " has malformed BUTTON state-machine goal rule");
                    continue;
                }
                if (property.arguments[0].type != 5 || property.arguments[0].intValue < 0 ||
                    property.arguments[0].intValue > 1)
                    issue(identity + " BUTTON state-machine rule has invalid activation flag");
                for (std::size_t argumentIndex = 1; argumentIndex < 4; ++argumentIndex)
                    if (property.arguments[argumentIndex].type != 6)
                        issue(identity + " BUTTON state-machine rule argument " + std::to_string(argumentIndex + 1) +
                              " has the wrong serialized type");
                const auto routeRoots =
                    property.arguments[1].intValue == -1
                        ? std::vector<std::int32_t>{}
                        : findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[1].intValue),
                                                      static_cast<std::int32_t>(index));
                if (property.arguments[1].intValue != -1 && routeRoots.empty())
                    issue(identity + " BUTTON state-machine root references missing OID " +
                          std::to_string(property.arguments[1].intValue));
                else if (!routeRoots.empty() &&
                         !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(routeRoots.front())].cid, 2))
                    issue(identity + " BUTTON state-machine root must resolve to an ALO-derived object");
                if (property.arguments[1].intValue != -1 && routeRoots.size() != 1)
                    issue(identity + " BUTTON state-machine root OID " +
                          std::to_string(property.arguments[1].intValue) + " has " + std::to_string(routeRoots.size()) +
                          " equally-near matches; retail TriggerRsmg requires exactly one");
                if (property.arguments[1].intValue != -1 && property.arguments[2].intValue != -1)
                {
                    const std::int16_t machineOid = static_cast<std::int16_t>(property.arguments[2].intValue);
                    std::vector<std::int32_t> machineIndices;
                    for (const auto &machine : document.StateMachines())
                    {
                        if (machine.ownerObjectIndex < 0 ||
                            static_cast<std::size_t>(machine.ownerObjectIndex) >= document.Objects().size())
                            continue;
                        if (document.Objects()[static_cast<std::size_t>(machine.ownerObjectIndex)].oid == machineOid)
                            machineIndices.push_back(machine.ownerObjectIndex);
                    }
                    if (machineIndices.empty())
                        issue(identity + " BUTTON route state-machine OID " + std::to_string(machineOid) +
                              " does not identify an authored SM");
                    const std::int32_t goal = property.arguments[3].intValue;
                    if (goal != -1 && !machineIndices.empty() &&
                        std::none_of(document.StateMachines().begin(), document.StateMachines().end(),
                                     [&](const BrxSmRecord &machine)
                                     {
                                         return std::find(machineIndices.begin(), machineIndices.end(),
                                                          machine.ownerObjectIndex) != machineIndices.end() &&
                                                std::any_of(machine.states.begin(), machine.states.end(),
                                                            [&](const BrxSmStateRecord &state)
                                                            { return state.oid == goal; });
                                     }))
                        issue(identity + " BUTTON route references missing state-machine goal OID " +
                              std::to_string(goal));
                }
                const auto pair = std::make_pair(property.arguments[1].intValue, property.arguments[2].intValue);
                if (std::find(stateMachinePairs.begin(), stateMachinePairs.end(), pair) == stateMachinePairs.end())
                    stateMachinePairs.push_back(pair);
                const auto side = std::make_tuple(property.arguments[1].intValue, property.arguments[2].intValue,
                                                  property.arguments[0].intValue);
                if (std::find(stateMachineSides.begin(), stateMachineSides.end(), side) != stateMachineSides.end())
                    issue(identity + " repeats a BUTTON route for the same root, SM, and activation side");
                else
                    stateMachineSides.push_back(side);
                for (const auto &earlier : stateMachinePairs)
                    if (earlier != pair && earlier.first == pair.first && earlier.second == -1)
                    {
                        issue(identity +
                              " BUTTON route is overwritten by an earlier wildcard SM route for the same root");
                        break;
                    }
            }
            if (stateMachinePairs.size() > 7)
                issue(identity + " has more than seven unique BUTTON state-machine routes");
            int pointStops = 0;
            int transformStops = 0;
            int hangHooks = 0;
            int pointRestStops = 0;
            int transformRestStops = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidateIndex == index)
                    continue;
                bool inButtonHierarchy = false;
                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        inButtonHierarchy = true;
                        break;
                    }
                    const auto &parent = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t next =
                        parent.proxyRuntimeParentIndex >= 0 ? parent.proxyRuntimeParentIndex : parent.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
                if (!inButtonHierarchy)
                    continue;
                if (CidDerivesFrom(candidate.cid, 126))
                {
                    ++pointStops;
                    if (candidate.oid == 512)
                        ++pointRestStops;
                }
                if (CidDerivesFrom(candidate.cid, 115) && !CidDerivesFrom(candidate.cid, 117))
                {
                    ++transformStops;
                    if (candidate.oid == 512)
                        ++transformRestStops;
                }
                if (CidDerivesFrom(candidate.cid, 118))
                    ++hangHooks;
            }
            if (pointStops > 2)
                issue(identity + " has more than two PNT button stops in its hierarchy; the runtime array holds two");
            if (transformStops > 2)
                issue(identity + " has more than two XFM button stops in its hierarchy; the runtime array holds two");
            const auto *authoredButtonType = classProperty(258);
            const std::int32_t buttonType = authoredButtonType ? authoredButtonType->intValue : 0;
            if (buttonType == 0 && pointStops != 2)
                issue(identity + " is a push BUTTON and requires exactly two PNT stops in its hierarchy");
            if (buttonType == 0 && pointRestStops != 1)
                issue(identity + " push BUTTON should have exactly one PNT rest stop with OID 512");
            if (buttonType == 0 && classProperty(131) == nullptr)
                issue(identity + " is a push BUTTON without an authored SO force constraint");
            if (buttonType == 1 && transformStops != 2)
                issue(identity + " is a lever BUTTON and requires exactly two non-TARGET XFM stops in its hierarchy");
            if (buttonType == 1 && transformRestStops != 1)
                issue(identity + " lever BUTTON should have exactly one XFM rest stop with OID 512");
            if (buttonType == 1 && classProperty(132) == nullptr)
                issue(identity + " is a lever BUTTON without an authored SO torque constraint");
            if (buttonType == 4 && hangHooks == 0)
                issue(identity + " is a hang BUTTON with no HND switch hook in its hierarchy");
            if (countOption(251) > 16 || countOption(252) > 16)
                issue(identity + " has more than 16 BUTTON animation references in one state");
            for (const auto id : {std::int16_t{253}, std::int16_t{254}, std::int16_t{255}, std::int16_t{256}})
                if (countOption(id) > 8)
                    issue(identity + " has more than eight BUTTON " +
                          std::string(PropertyName(id) ? PropertyName(id) : "filter") + " entries");
            for (const auto &property : object.properties)
                if ((property.eopid == 255 || property.eopid == 256) &&
                    (property.intValue < 0 || static_cast<std::size_t>(property.intValue) >= CidOptionIds().size() ||
                     !SoClassFilterCanMatch(static_cast<std::int16_t>(property.intValue))))
                    issue(identity + " BUTTON " +
                          std::string(PropertyName(property.eopid) ? PropertyName(property.eopid) : "class filter") +
                          " must name a class that can match an SO");
        }
        if (CidDerivesFrom(object.cid, 41))
        {
            for (const auto &property : object.properties)
            {
                if (property.eopid != 267 && property.eopid != 268 && property.eopid != 275 && property.eopid != 279)
                    continue;
                if (property.intValue < 0)
                    continue;
                const auto referenced = findContextualObject(static_cast<std::int16_t>(property.intValue));
                if (referenced == document.Objects().end())
                {
                    issue(identity + " VOLBTN " +
                          std::string(PropertyName(property.eopid) ? PropertyName(property.eopid) : "reference") +
                          " references missing OID " + std::to_string(property.intValue));
                    continue;
                }
                if ((property.eopid == 267 || property.eopid == 268) && !CidDerivesFrom(referenced->cid, 131))
                    issue(identity + " VOLBTN animation OID " + std::to_string(property.intValue) + " is not an ASEG");
                else if (property.eopid == 279 && !CidDerivesFrom(referenced->cid, 101))
                    issue(identity + " VOLBTN dialog OID " + std::to_string(property.intValue) + " is not a DIALOG");
                else if (property.eopid == 275 && !CidDerivesFrom(referenced->cid, 109) &&
                         !CidDerivesFrom(referenced->cid, 126) && !CidDerivesFrom(referenced->cid, 115) &&
                         !CidDerivesFrom(referenced->cid, 2))
                    issue(identity + " VOLBTN accepted object OID " + std::to_string(property.intValue) +
                          " has no runtime-supported CM, PNT, XFM, or ALO position");
            }
            // The runtime deliberately gives both values signed semantics:
            // a negative delay disables automatic reset and a negative speed
            // plays the referenced ASEGs in reverse.
            for (const auto id : {std::int16_t{269}, std::int16_t{270}})
            {
                if (const auto *value = classProperty(id); value != nullptr && !std::isfinite(value->floatValue[0]))
                    issue(identity + " VOLBTN " + std::string(PropertyName(id) ? PropertyName(id) : "float") +
                          " is not finite");
            }
            if (classProperty(278) != nullptr)
                issue(identity + " serializes runtime-only VOLBTN occupant query 278");
            if (classProperty(280) != nullptr)
                issue(identity + " serializes runtime-only VOLBTN state 280");
            if (countOption(267) > 16 || countOption(268) > 16)
                issue(identity + " has more than 16 VOLBTN animation references in one state");
            if (countOption(275) > 8)
                issue(identity + " has more than eight VOLBTN push-object filters");
            const auto *triggerSphere = classProperty(133);
            if (triggerSphere != nullptr && !std::isfinite(triggerSphere->floatValue[0]))
                issue(identity + " VOLBTN collision sphere radius is not finite");
            std::vector<std::pair<std::int32_t, std::int32_t>> stateMachinePairs;
            std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> stateMachineSides;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 276)
                    continue;
                if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4)
                {
                    issue(identity + " has malformed VOLBTN state-machine goal rule");
                    continue;
                }
                if (property.arguments[0].type != 5 || property.arguments[0].intValue < 0 ||
                    property.arguments[0].intValue > 1)
                    issue(identity + " VOLBTN state-machine rule has invalid entry/exit flag");
                for (std::size_t argumentIndex = 1; argumentIndex < 4; ++argumentIndex)
                    if (property.arguments[argumentIndex].type != 6)
                        issue(identity + " VOLBTN state-machine rule argument " + std::to_string(argumentIndex + 1) +
                              " has the wrong serialized type");
                const auto routeRoots =
                    property.arguments[1].intValue == -1
                        ? std::vector<std::int32_t>{}
                        : findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[1].intValue),
                                                      static_cast<std::int32_t>(index));
                if (property.arguments[1].intValue != -1 && routeRoots.empty())
                    issue(identity + " VOLBTN state-machine root references missing OID " +
                          std::to_string(property.arguments[1].intValue));
                else if (!routeRoots.empty() &&
                         !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(routeRoots.front())].cid, 2))
                    issue(identity + " VOLBTN state-machine root must resolve to an ALO-derived object");
                if (property.arguments[1].intValue != -1 && routeRoots.size() != 1)
                    issue(identity + " VOLBTN state-machine root OID " +
                          std::to_string(property.arguments[1].intValue) + " has " + std::to_string(routeRoots.size()) +
                          " equally-near matches; retail TriggerRsmg requires exactly one");
                if (property.arguments[1].intValue != -1 && property.arguments[2].intValue != -1)
                {
                    const std::int16_t machineOid = static_cast<std::int16_t>(property.arguments[2].intValue);
                    std::vector<std::int32_t> machineIndices;
                    for (const auto &machine : document.StateMachines())
                    {
                        if (machine.ownerObjectIndex < 0 ||
                            static_cast<std::size_t>(machine.ownerObjectIndex) >= document.Objects().size())
                            continue;
                        if (document.Objects()[static_cast<std::size_t>(machine.ownerObjectIndex)].oid == machineOid)
                            machineIndices.push_back(machine.ownerObjectIndex);
                    }
                    if (machineIndices.empty())
                        issue(identity + " VOLBTN route state-machine OID " + std::to_string(machineOid) +
                              " does not identify an authored SM");
                    const std::int32_t goal = property.arguments[3].intValue;
                    if (goal != -1 && !machineIndices.empty() &&
                        std::none_of(document.StateMachines().begin(), document.StateMachines().end(),
                                     [&](const BrxSmRecord &machine)
                                     {
                                         return std::find(machineIndices.begin(), machineIndices.end(),
                                                          machine.ownerObjectIndex) != machineIndices.end() &&
                                                std::any_of(machine.states.begin(), machine.states.end(),
                                                            [&](const BrxSmStateRecord &state)
                                                            { return state.oid == goal; });
                                     }))
                        issue(identity + " VOLBTN route references missing state-machine goal OID " +
                              std::to_string(goal));
                }
                const auto pair = std::make_pair(property.arguments[1].intValue, property.arguments[2].intValue);
                if (std::find(stateMachinePairs.begin(), stateMachinePairs.end(), pair) == stateMachinePairs.end())
                    stateMachinePairs.push_back(pair);
                const auto side = std::make_tuple(property.arguments[1].intValue, property.arguments[2].intValue,
                                                  property.arguments[0].intValue);
                if (std::find(stateMachineSides.begin(), stateMachineSides.end(), side) != stateMachineSides.end())
                    issue(identity + " repeats a VOLBTN route for the same root, SM, and entry/exit side");
                else
                    stateMachineSides.push_back(side);
                for (const auto &earlier : stateMachinePairs)
                    if (earlier != pair && earlier.first == pair.first && earlier.second == -1)
                    {
                        issue(identity +
                              " VOLBTN route is overwritten by an earlier wildcard SM route for the same root");
                        break;
                    }
            }
            if (stateMachinePairs.size() > 7)
                issue(identity + " has more than seven unique VOLBTN state-machine routes");
        }
        if (CidDerivesFrom(object.cid, 42))
        {
            std::int32_t jlocIndex = -1;
            for (const auto binding :
                 {std::pair<std::int16_t, std::int16_t>{894, 126}, std::pair<std::int16_t, std::int16_t>{896, 100}})
            {
                const auto *property = classProperty(binding.first);
                if (property == nullptr || property->intValue < 0)
                    continue;
                const auto referenced = findContextualObject(static_cast<std::int16_t>(property->intValue));
                if (referenced == document.Objects().end())
                    issue(identity + " JLOVOL references missing OID " + std::to_string(property->intValue));
                else if (!CidDerivesFrom(referenced->cid, binding.second))
                    issue(identity + " JLOVOL OID " + std::to_string(property->intValue) + " resolves to " +
                          BrxDocument::ClassName(referenced->cid) + ", expected " +
                          BrxDocument::ClassName(binding.second));
                else if (binding.first == 896)
                    jlocIndex = static_cast<std::int32_t>(std::distance(document.Objects().begin(), referenced));
            }
            if (classProperty(895) != nullptr || classProperty(897) != nullptr)
                issue(identity + " serializes bind-time-only JLOVOL pointer state (895/897)");
            const BrxPropertyRecord *landingProperty = classProperty(894);
            if (landingProperty == nullptr || landingProperty->intValue == -1)
            {
                bool hasFallbackLandingPoint = false;
                for (const auto &candidate : document.Objects())
                {
                    if (!CidDerivesFrom(candidate.cid, 126))
                        continue;
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    hasFallbackLandingPoint = parent == static_cast<std::int32_t>(index);
                    if (hasFallbackLandingPoint)
                        break;
                }
                if (!hasFallbackLandingPoint)
                    issue(identity +
                          " has no explicit landing-marker OID and no direct child PNT for BindJlovol's fallback");
            }
            const BrxPropertyRecord *controllerProperty = classProperty(896);
            if (controllerProperty == nullptr || controllerProperty->intValue == -1)
                issue(identity + " has no JLOC reference; active JLO behavior dereferences the bound controller");
            if (jlocIndex >= 0)
            {
                std::size_t targetCount = 0;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    if (!CidDerivesFrom(document.Objects()[candidateIndex].cid, 115))
                        continue;
                    std::int32_t ancestor = document.Objects()[candidateIndex].proxyRuntimeParentIndex >= 0
                                                ? document.Objects()[candidateIndex].proxyRuntimeParentIndex
                                                : document.Objects()[candidateIndex].parentIndex;
                    for (std::size_t guard = 0; ancestor >= 0 &&
                                                static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                                guard++ <= document.Objects().size();)
                    {
                        if (ancestor == jlocIndex)
                        {
                            ++targetCount;
                            break;
                        }
                        const auto &parent = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t next =
                            parent.proxyRuntimeParentIndex >= 0 ? parent.proxyRuntimeParentIndex : parent.parentIndex;
                        if (next == ancestor)
                            break;
                        ancestor = next;
                    }
                }
                if (targetCount == 0)
                    issue(identity +
                          " JLOC has no descendant XFM target; PxfmChooseJloc can return null during firing");
                if (targetCount > 16)
                    issue(identity +
                          " JLOC has more than 16 descendant XFM targets; its runtime target array holds 16");
            }
        }
        if (CidDerivesFrom(object.cid, 44))
        {
            // Retail adds this signed contribution to PZI::sAutoCollect and then
            // compares against the square of that sum.  Negative values are
            // therefore legal (and can reduce, cancel, or invert the effective
            // distance); only non-finite data is unusable.
            if (const auto *collectRange = classProperty(286);
                collectRange && !std::isfinite(collectRange->floatValue[0]))
                issue(identity + " SPRIZE additional attraction range must be finite");
            validateAllObjectReferences(287, "SPRIZE");
            std::vector<std::int32_t> animationOids;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 287 || property.intValue < 0)
                    continue;
                if (std::find(animationOids.begin(), animationOids.end(), property.intValue) != animationOids.end())
                    issue(identity + " repeats pickup ASEG OID " + std::to_string(property.intValue) +
                          "; the game would start it more than once");
                else
                    animationOids.push_back(property.intValue);

                const auto referenced = findContextualObject(static_cast<std::int16_t>(property.intValue));
                if (referenced == document.Objects().end())
                    issue(identity + " pickup animation references missing OID " + std::to_string(property.intValue));
                else if (!CidDerivesFrom(referenced->cid, 131))
                    issue(identity + " pickup animation OID " + std::to_string(property.intValue) + " resolves to " +
                          BrxDocument::ClassName(referenced->cid) + " instead of ASEG");
                const bool repeatsAutomaticChild =
                    std::any_of(document.Objects().begin(), document.Objects().end(),
                                [&](const BrxObjectRecord &candidate)
                                {
                                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                                    ? candidate.proxyRuntimeParentIndex
                                                                    : candidate.parentIndex;
                                    return parent == static_cast<std::int32_t>(index) &&
                                           CidDerivesFrom(candidate.cid, 131) && candidate.oid == property.intValue;
                                });
                if (repeatsAutomaticChild)
                    issue(identity + " explicitly repeats direct child ASEG OID " + std::to_string(property.intValue) +
                          "; runtime would enqueue it twice");
            }
            std::size_t childAnimationCount = 0;
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t parent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                const bool directChild = parent == static_cast<std::int32_t>(index);
                if (directChild && CidDerivesFrom(candidate.cid, 131))
                    ++childAnimationCount;
            }
            const std::size_t authoredAnimationCount = countOption(287);
            if (childAnimationCount + authoredAnimationCount > 4)
                issue(identity + " has " + std::to_string(childAnimationCount) + " child ASEG(s) plus " +
                      std::to_string(authoredAnimationCount) +
                      " authored pickup animation(s); SPRIZE stores only four combined animations");
        }
        if (CidDerivesFrom(object.cid, 46))
        {
            // Retail treats a non-positive half-cycle as "do not animate the
            // helper frames" in RenderClueAll.  It is legal when no bounce
            // sequence is wanted; only non-finite serialized data is invalid.
            if (const auto *frameDuration = classProperty(623);
                frameDuration && !std::isfinite(frameDuration->floatValue[0]))
                issue(identity + " CLUE bounce half-cycle duration must be finite");

            std::vector<const BrxObjectRecord *> levelClues;
            for (const auto &candidate : document.Objects())
                if (CidDerivesFrom(candidate.cid, 46))
                    levelClues.push_back(&candidate);
            const auto cluePosition = std::find(levelClues.begin(), levelClues.end(), &object);
            const std::size_t persistenceSlot =
                cluePosition == levelClues.end()
                    ? levelClues.size()
                    : static_cast<std::size_t>(std::distance(levelClues.begin(), cluePosition));
            if (persistenceSlot >= 64)
                issue(identity + " is assigned implicit clue save slot " + std::to_string(persistenceSlot) +
                      ", but the original level save stores only 64 clue bits");

            const auto belongsToClue = [&](const BrxObjectRecord &candidate)
            {
                std::int32_t parent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                while (parent >= 0 && parent < static_cast<std::int32_t>(document.Objects().size()))
                {
                    if (&document.Objects()[static_cast<std::size_t>(parent)] == &object)
                        return true;
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(parent)];
                    const std::int32_t next =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (next == parent)
                        break;
                    parent = next;
                }
                return false;
            };
            const auto helpersWithOid = [&](const std::int32_t oid)
            {
                return static_cast<std::size_t>(std::count_if(
                    document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                    { return candidate.oid == oid && belongsToClue(candidate); }));
            };

            const std::size_t smackCount = helpersWithOid(825);
            if (smackCount == 0)
                issue(
                    identity +
                    " has no descendant OID 825 HUD-flight helper; normal CollectClue presentation can dereference it");
            else if (smackCount > 1)
                issue(identity +
                      " has multiple descendant OID 825 helpers; LoadClueFromBrx cannot bind this unambiguously");
            else
            {
                const auto smack = std::find_if(document.Objects().begin(), document.Objects().end(),
                                                [&](const BrxObjectRecord &candidate)
                                                { return candidate.oid == 825 && belongsToClue(candidate); });
                if (smack != document.Objects().end() && !CidDerivesFrom(smack->cid, 2))
                    issue(identity + " OID 825 helper resolves to " + std::string(BrxDocument::ClassName(smack->cid)) +
                          " instead of an ALO-derived object");
            }

            bool encounteredGap = false;
            bool frameAfterGap = false;
            std::size_t contiguousFrameCount = 0;
            for (std::int32_t oid = 792; oid <= 823; ++oid)
            {
                const std::size_t frameCount = helpersWithOid(oid);
                if (frameCount > 1)
                    issue(identity + " has multiple bounce helpers with OID " + std::to_string(oid));
                const auto frame = std::find_if(document.Objects().begin(), document.Objects().end(),
                                                [&](const BrxObjectRecord &candidate)
                                                { return candidate.oid == oid && belongsToClue(candidate); });
                if (frame != document.Objects().end() && !CidDerivesFrom(frame->cid, 115))
                    issue(identity + " bounce helper OID " + std::to_string(oid) + " resolves to " +
                          BrxDocument::ClassName(frame->cid) + " instead of XFM");
                if (frameCount == 0)
                {
                    encounteredGap = true;
                    continue;
                }
                if (encounteredGap)
                    frameAfterGap = true;
                else
                    ++contiguousFrameCount;
            }
            if (frameAfterGap)
                issue(identity + " has CLUE bounce frames after a missing OID; the loader stops at the first gap");
            if (contiguousFrameCount > 0)
            {
                const BrxPropertyRecord *duration = classProperty(623);
                if (duration == nullptr || !std::isfinite(duration->floatValue[0]) ||
                    duration->floatValue[0] <= 0.000001f)
                    issue(identity + " has bounce frames but no positive Bounce half-cycle duration");
            }

            if (countOption(622) != 0)
                issue(identity + " serializes runtime BREAK as authored data; expose it only as an editor test action");
            if (countOption(624) != 0)
                issue(identity + " serializes runtime Collected state; clue collection belongs to level save data");
        }
        if (CidDerivesFrom(object.cid, 47))
        {
            if (const auto *reset = classProperty(293); reset && !std::isfinite(reset->floatValue[0]))
                issue(identity + " ALARM reset delay must be finite");
            for (const auto binding :
                 {std::pair<std::int16_t, std::int16_t>{289, 34}, std::pair<std::int16_t, std::int16_t>{290, 48},
                  std::pair<std::int16_t, std::int16_t>{291, 8}})
                for (const auto &property : object.properties)
                {
                    if (property.eopid != binding.first || property.intValue < 0)
                        continue;
                    const auto referenced = findContextualObject(static_cast<std::int16_t>(property.intValue));
                    if (referenced == document.Objects().end())
                        issue(identity + " ALARM relationship references missing OID " +
                              std::to_string(property.intValue));
                    else if (!CidDerivesFrom(referenced->cid, binding.second))
                        issue(identity + " ALARM relationship OID " + std::to_string(property.intValue) +
                              " resolves to " + BrxDocument::ClassName(referenced->cid) + ", expected " +
                              BrxDocument::ClassName(binding.second));
                }
            if (classProperty(294) != nullptr || classProperty(295) != nullptr || classProperty(296) != nullptr)
                issue(identity + " serializes runtime-only ALARM state/query/trigger properties (294-296)");
            if (countOption(289) > 4)
                issue(identity + " has more than four ALARM breakable references");
            if (countOption(290) > 16)
                issue(identity + " has more than 16 ALARM sensor references");
            if (countOption(291) > 6)
                issue(identity + " has more than six ALARM guard references");
            for (const auto id : {std::int16_t{289}, std::int16_t{290}, std::int16_t{291}})
            {
                std::vector<std::int32_t> seenOids;
                for (const auto &property : object.properties)
                {
                    if (property.eopid != id || property.intValue == -1)
                        continue;
                    if (std::find(seenOids.begin(), seenOids.end(), property.intValue) != seenOids.end())
                        issue(identity + " contains duplicate " +
                              std::string(PropertyNameForCid(object.cid, id) ? PropertyNameForCid(object.cid, id)
                                                                             : "ALARM relationship") +
                              " OID " + std::to_string(property.intValue));
                    else
                        seenOids.push_back(property.intValue);
                }
            }
            std::size_t childStateMachineCount = 0;
            std::int32_t childStateMachineIndex = -1;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                const std::int32_t parent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (parent != static_cast<std::int32_t>(index) || !CidDerivesFrom(candidate.cid, 136))
                    continue;
                if (childStateMachineIndex < 0)
                    childStateMachineIndex = static_cast<std::int32_t>(candidateIndex);
                ++childStateMachineCount;
            }
            if (childStateMachineCount > 1)
                issue(identity + " has multiple direct child SM objects; PostAlarmLoad uses only the first one");
            if (childStateMachineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == childStateMachineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " direct alarm SM has no decoded state-machine payload");
                else
                    for (const std::int16_t goal : {std::int16_t{538}, std::int16_t{543}, std::int16_t{544}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == goal; }))
                            issue(identity + " alarm SM is missing runtime goal OID " + std::to_string(goal));
            }

            std::vector<std::pair<std::int32_t, std::int32_t>> stateMachinePairs;
            std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> stateMachineSides;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 297)
                    continue;
                if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4)
                {
                    issue(identity + " has malformed ALARM state-machine goal rule");
                    continue;
                }
                if (property.arguments[0].type != 5 || property.arguments[0].intValue < 0 ||
                    property.arguments[0].intValue > 1)
                    issue(identity + " ALARM state-machine rule has invalid trigger flag");
                for (std::size_t argumentIndex = 1; argumentIndex < 4; ++argumentIndex)
                    if (property.arguments[argumentIndex].type != 6)
                        issue(identity + " ALARM state-machine rule argument " + std::to_string(argumentIndex + 1) +
                              " has the wrong serialized type");
                const std::int32_t rootOid = property.arguments[1].intValue;
                const std::int32_t machineOid = property.arguments[2].intValue;
                const std::int32_t goalOid = property.arguments[3].intValue;
                const auto routeRoots = rootOid < 0 ? std::vector<std::int32_t>{}
                                                    : findHierarchyNearestObjects(static_cast<std::int16_t>(rootOid),
                                                                                  static_cast<std::int32_t>(index));
                if (rootOid >= 0 && routeRoots.empty())
                    issue(identity + " ALARM route references missing root OID " + std::to_string(rootOid));
                else if (!routeRoots.empty() &&
                         !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(routeRoots.front())].cid, 2))
                    issue(identity + " ALARM route root is not ALO-derived");
                if (rootOid >= 0 && routeRoots.size() != 1)
                    issue(identity + " ALARM route root OID " + std::to_string(rootOid) + " has " +
                          std::to_string(routeRoots.size()) +
                          " equally-near matches; retail TriggerRsmg requires exactly one");
                std::vector<std::int32_t> machineIndices;
                if (machineOid >= 0)
                {
                    for (const auto &machine : document.StateMachines())
                    {
                        if (machine.ownerObjectIndex < 0 ||
                            static_cast<std::size_t>(machine.ownerObjectIndex) >= document.Objects().size())
                            continue;
                        if (document.Objects()[static_cast<std::size_t>(machine.ownerObjectIndex)].oid == machineOid)
                            machineIndices.push_back(machine.ownerObjectIndex);
                    }
                    if (machineIndices.empty())
                        issue(identity + " ALARM route SM OID " + std::to_string(machineOid) +
                              " does not identify an authored state machine");
                }
                if (!machineIndices.empty() && goalOid >= 0)
                {
                    if (std::none_of(document.StateMachines().begin(), document.StateMachines().end(),
                                     [&](const BrxSmRecord &machine)
                                     {
                                         return std::find(machineIndices.begin(), machineIndices.end(),
                                                          machine.ownerObjectIndex) != machineIndices.end() &&
                                                std::any_of(machine.states.begin(), machine.states.end(),
                                                            [&](const BrxSmStateRecord &state)
                                                            { return state.oid == goalOid; });
                                     }))
                        issue(identity + " ALARM route references missing goal OID " + std::to_string(goalOid));
                }
                const auto pair = std::make_pair(rootOid, machineOid);
                if (std::find(stateMachinePairs.begin(), stateMachinePairs.end(), pair) == stateMachinePairs.end())
                    stateMachinePairs.push_back(pair);
                const auto side = std::make_tuple(rootOid, machineOid, property.arguments[0].intValue);
                if (std::find(stateMachineSides.begin(), stateMachineSides.end(), side) != stateMachineSides.end())
                    issue(identity + " repeats an ALARM route for the same root, SM, and trigger side");
                else
                    stateMachineSides.push_back(side);
                for (const auto &earlier : stateMachinePairs)
                    if (earlier != pair && earlier.first == rootOid && earlier.second == -1)
                    {
                        issue(identity +
                              " ALARM route is overwritten by an earlier wildcard SM route for the same root");
                        break;
                    }
            }
            if (stateMachinePairs.size() > 7)
                issue(identity + " has more than seven unique ALARM state-machine routes");
        }
        if (CidDerivesFrom(object.cid, 48))
        {
            for (const auto id : {std::int16_t{304}, std::int16_t{305}})
                if (const auto *duration = classProperty(id); duration && !std::isfinite(duration->floatValue[0]))
                    issue(identity + " SENSOR transition duration must be finite");
            if (const auto *state = classProperty(306); state && (state->intValue < -1 || state->intValue > 8))
                issue(identity + " has invalid SENSOR initial state " + std::to_string(state->intValue));
            for (const auto runtimeId : {std::int16_t{307}, std::int16_t{308}, std::int16_t{309}, std::int16_t{310},
                                         std::int16_t{311}, std::int16_t{312}})
                if (classProperty(runtimeId) != nullptr)
                    issue(identity + " serializes runtime-only SENSOR property " + std::to_string(runtimeId));
            for (const auto id : {std::int16_t{299}, std::int16_t{300}, std::int16_t{301}, std::int16_t{302}})
                if (countOption(id) > 4)
                    issue(identity + " has more than four SENSOR " +
                          std::string(PropertyName(id) ? PropertyName(id) : "filter") + " entries");
            const bool detectAll = [&]()
            {
                const auto *property = classProperty(303);
                return property != nullptr && property->boolValue;
            }();
            if (!detectAll && countOption(299) == 0 && countOption(301) == 0)
                issue(
                    identity +
                    " has Detect every object disabled but no allowed OID or CID filter; FCheckSensorObject can never accept a target");
            for (const auto pair :
                 {std::pair<std::int16_t, std::int16_t>{299, 300}, std::pair<std::int16_t, std::int16_t>{301, 302}})
            {
                for (const auto &allowed : object.properties)
                {
                    if (allowed.eopid != pair.first)
                        continue;
                    const bool alsoIgnored = std::any_of(
                        object.properties.begin(), object.properties.end(), [&](const BrxPropertyRecord &ignored)
                        { return ignored.eopid == pair.second && ignored.intValue == allowed.intValue; });
                    if (alsoIgnored && !detectAll)
                        issue(identity + " allows and ignores the same SENSOR filter value " +
                              std::to_string(allowed.intValue) + "; the ignore rule wins");
                }
            }
            for (const auto id : {std::int16_t{301}, std::int16_t{302}})
                for (const auto &property : object.properties)
                    if (property.eopid == id && (property.intValue < 0 ||
                                                 static_cast<std::size_t>(property.intValue) >= CidOptionIds().size() ||
                                                 !SoClassFilterCanMatch(static_cast<std::int16_t>(property.intValue))))
                        issue(identity + " SENSOR class filter " + std::to_string(property.intValue) +
                              " cannot match an SO contact target or its superclass chain");
            std::size_t owningAlarms = 0;
            for (std::size_t alarmIndex = 0; alarmIndex < document.Objects().size(); ++alarmIndex)
            {
                const auto &candidate = document.Objects()[alarmIndex];
                if (!CidDerivesFrom(candidate.cid, 47))
                    continue;
                for (const auto &property : candidate.properties)
                {
                    if (property.eopid != 290 || property.intValue != object.oid)
                        continue;
                    const auto resolved =
                        findHierarchyNearestObjects(object.oid, static_cast<std::int32_t>(alarmIndex));
                    if (resolved.size() > 1)
                        issue(std::string(BrxDocument::ClassName(candidate.cid)) + " OID " +
                              std::to_string(candidate.oid) + " SENSOR OID " + std::to_string(object.oid) +
                              " has multiple equally-near mode-4 matches");
                    if (std::find(resolved.begin(), resolved.end(), static_cast<std::int32_t>(index)) != resolved.end())
                        ++owningAlarms;
                }
            }
            if (owningAlarms > 1)
                issue(identity + " is referenced by multiple ALARMs; post-load overwrites its single alarm backlink");
        }
        if (CidDerivesFrom(object.cid, 49))
        {
            if (const auto *shutdown = classProperty(314); shutdown && !std::isfinite(shutdown->floatValue[0]))
                issue(identity + " LASEN damage shutdown time is not finite");
            if (const auto *kind = classProperty(313); kind && (kind->intValue < -1 || kind->intValue > 1))
                issue(identity + " has invalid LASEN beam kind " + std::to_string(kind->intValue));
            if (classProperty(315) != nullptr || classProperty(316) != nullptr)
                issue(identity + " serializes runtime-only LASEN extend/retract commands (315/316)");
            std::size_t validBeamCount = 0;
            std::size_t invalidBeamShapeCount = 0;
            std::size_t damageEmitterCount = 0;
            std::size_t senseAppearanceCount = 0;
            std::size_t damageAppearanceCount = 0;
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent == static_cast<std::int32_t>(index))
                {
                    if (CidDerivesFrom(candidate.cid, 110))
                    {
                        const auto candidateIndex =
                            static_cast<std::int32_t>(std::distance(document.Objects().data(), &candidate));
                        const auto path = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                                                       [&](const BrxShapePathRecord &shape)
                                                       { return shape.ownerObjectIndex == candidateIndex; });
                        if (path != document.ShapePaths().end() && !path->hermite && !path->points.empty())
                            ++validBeamCount;
                        else
                            ++invalidBeamShapeCount;
                    }
                    if (CidDerivesFrom(candidate.cid, 80))
                        ++damageEmitterCount;
                }
                std::int32_t ancestor = runtimeParent;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        if (candidate.oid == 650)
                            ++senseAppearanceCount;
                        if (candidate.oid == 651)
                            ++damageAppearanceCount;
                        break;
                    }
                    const auto &parent = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t next =
                        parent.proxyRuntimeParentIndex >= 0 ? parent.proxyRuntimeParentIndex : parent.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
            }
            if (validBeamCount == 0)
                issue(identity + " has no direct runtime child SHAPE with a non-empty linear CRV");
            if (validBeamCount > 16)
                issue(identity + " has more than 16 valid child beam SHAPEs; LASEN stores only 16 beams");
            if (invalidBeamShapeCount != 0)
                issue(identity + " has " + std::to_string(invalidBeamShapeCount) +
                      " direct child SHAPE(s) that BindLasen ignores because their CRV is missing, empty, or cubic");
            if (damageEmitterCount > 4)
                issue(identity + " has more than four child damage emitters; LASEN uses only the first four");
            if (senseAppearanceCount > 1 || damageAppearanceCount > 1)
                issue(
                    identity +
                    " has ambiguous LASEN beam appearances in its hierarchy; SnipAloObjects binds only one object for each of OID 650/651");
        }
        if (CidDerivesFrom(object.cid, 50))
        {
            for (const auto id : {std::int16_t{317}, std::int16_t{318}, std::int16_t{319}})
                if (const auto *duration = classProperty(id); duration && !std::isfinite(duration->floatValue[0]))
                    issue(identity + " CAMSEN " + std::string(PropertyName(id) ? PropertyName(id) : "damage duration") +
                          " is not finite");
            if (classProperty(320) != nullptr)
                issue(identity + " serializes runtime-only CAMSEN damage-sequence state (320)");
            std::size_t damageAppearanceCount = 0;
            std::size_t zapAppearanceCount = 0;
            for (const auto &candidate : document.Objects())
            {
                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        if (candidate.oid == 652)
                            ++damageAppearanceCount;
                        if (candidate.oid == 653)
                            ++zapAppearanceCount;
                        break;
                    }
                    const auto &parent = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t next =
                        parent.proxyRuntimeParentIndex >= 0 ? parent.proxyRuntimeParentIndex : parent.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
            }
            if (damageAppearanceCount > 1)
                issue(identity +
                      " has multiple CAMSEN damage appearances (OID 652) in its hierarchy; only one is bound");
            if (zapAppearanceCount > 1)
                issue(identity + " has multiple CAMSEN zap appearances (OID 653) in its hierarchy; only one is bound");
            const auto *sphere = classProperty(133);
            const bool validSphere = sphere && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f;
            if (!validSphere && object.so.collisionGeometry.bspNodes.empty())
                issue(identity + " CAMSEN has no sphere or collision BSP for its player detection volume");
        }
        if (CidDerivesFrom(object.cid, 51))
        {
            for (const auto id : {std::int16_t{323}, std::int16_t{327}, std::int16_t{331}})
                if (const auto *duration = classProperty(id); duration && !std::isfinite(duration->floatValue[0]))
                    issue(identity + " PRSEN " +
                          std::string(PropertyName(id) ? PropertyName(id) : "animation duration") + " is not finite");
            if (const auto *speed = classProperty(334); speed && !std::isfinite(speed->floatValue[0]))
                issue(identity + " PRSEN disabling flash speed is not finite");
            for (const auto id : {std::int16_t{335}, std::int16_t{336}})
                if (const auto *duration = classProperty(id); duration && !std::isfinite(duration->floatValue[0]))
                    issue(identity + " has non-finite PRSEN state duration");
            for (const auto id : {std::int16_t{324}, std::int16_t{328}, std::int16_t{332}})
                if (const auto *style = classProperty(id); style && (style->intValue < -1 || style->intValue > 1))
                    issue(identity + " has invalid PRSEN " +
                          std::string(PropertyName(id) ? PropertyName(id) : "playback style"));
            for (const auto [startId, endId, label] :
                 {std::tuple<std::int16_t, std::int16_t, const char *>{321, 322, "sense"},
                  {325, 326, "damage"},
                  {329, 330, "disabled"}})
            {
                const auto *start = classProperty(startId);
                const auto *end = classProperty(endId);
                const std::int32_t first = start ? start->intValue : -1;
                const std::int32_t last = end ? end->intValue : -1;
                // InitPrsen deliberately initializes every range to -1/-1.
                // UpdatePrsenLoopShader produces a single frame and the shader
                // frame setter clamps that sentinel, so only mixed or reversed
                // ranges are malformed.
                if (!((first == -1 && last == -1) || (first >= 0 && last >= first)))
                    issue(identity + " has an invalid PRSEN " + std::string(label) + " shader-frame range");
            }
            if (const auto *flashFrame = classProperty(333); flashFrame && flashFrame->intValue < -1)
                issue(identity + " has an invalid PRSEN disabling-flash frame");
            const bool hasLoopShader =
                std::any_of(document.RenderBatches().begin(), document.RenderBatches().end(),
                            [&](const BrxRenderBatch &batch)
                            {
                                return batch.ownerObjectIndex == static_cast<std::int32_t>(index) &&
                                       batch.hasShaderAnimation &&
                                       batch.shaderAnimation.kind == BrxShaderAnimationKind::Loop;
                            });
            if (!hasLoopShader)
                issue(
                    identity +
                    " has no LOOP shader animation in its own globset; UpdatePrsenLoopShader dereferences that runtime loop every frame");
            const bool hasContactGeometry = object.so.present && (!object.so.collisionGeometry.positions.empty() ||
                                                                  !object.so.collisionGeometry.surfaces.empty() ||
                                                                  !object.so.collisionGeometry.bspNodes.empty());
            if (!hasContactGeometry)
                issue(
                    identity +
                    " has no gameplay collision geometry; SensePrsen only detects objects through runtime contact pairs");
        }
        if (CidDerivesFrom(object.cid, 52))
        {
            validateAllObjectReferences(344, "BARRIER");
            validateObjectReference(347, "BARRIER");
            if (countOption(344) > 4)
                issue(identity + " has more than four BARRIER object filters");
            if (countOption(345) > 4)
                issue(identity + " has more than four BARRIER class filters");
            for (auto current = object.properties.begin(); current != object.properties.end(); ++current)
            {
                if (current->eopid != 344 && current->eopid != 345)
                    continue;
                if (current->eopid == 345 &&
                    (current->intValue < 0 || static_cast<std::size_t>(current->intValue) >= CidOptionIds().size() ||
                     !SoClassFilterCanMatch(static_cast<std::int16_t>(current->intValue))))
                    issue(identity + " has a BARRIER class filter that cannot match an SO");
                if (std::any_of(object.properties.begin(), current, [&](const BrxPropertyRecord &earlier)
                                { return earlier.eopid == current->eopid && earlier.intValue == current->intValue; }))
                    issue(identity + " repeats BARRIER " +
                          std::string(current->eopid == 344 ? "object OID " : "class CID ") +
                          std::to_string(current->intValue));
            }
            // Empty lists are intentional presets: allow=true is a solid wall,
            // while allow=false disables collision for every eligible object.
            std::size_t directPoints = 0;
            std::size_t directChildren = 0;
            const auto barrierObjectIndex =
                static_cast<std::int32_t>(std::distance(document.Objects().data(), &object));
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent != barrierObjectIndex)
                    continue;
                ++directChildren;
                if (CidDerivesFrom(candidate.cid, 126))
                    ++directPoints;
            }
            const auto *warp = classProperty(347);
            const bool hasWarp = warp != nullptr && warp->intValue >= 0;
            if (hasWarp)
            {
                const auto target = findContextualObject(static_cast<std::int16_t>(warp->intValue));
                if (target == document.Objects().end())
                    issue(identity + " references missing BARRIER destination WARP OID " +
                          std::to_string(warp->intValue));
                else if (!CidDerivesFrom(target->cid, 116))
                    issue(identity + " BARRIER destination OID " + std::to_string(warp->intValue) + " resolves to " +
                          BrxDocument::ClassName(target->cid) + " instead of WARP");
            }
            if (hasWarp && directPoints > 0)
                issue(identity + " has both a WARP destination and embedded PNT destination; the WARP wins");
            if (directPoints > 1)
                issue(identity + " has multiple direct destination PNT children");
            if (!hasWarp && directPoints == 1 && directChildren != 1)
                issue(
                    identity +
                    " embeds a destination PNT beside other direct children; retail's child consumption is ambiguous");
            if (!object.so.fixedPhysics)
                issue(identity +
                      " is a movable BARRIER; retail initializes BARRIER translation and rotation as locked");
            const auto &geometry = object.so.collisionGeometry;
            const auto *sphere = classProperty(133);
            const bool validSphere = sphere && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f;
            if (geometry.bspNodes.empty() && !validSphere)
                issue(
                    identity +
                    " has no collision BSP or enabled collision sphere; bounds radius alone does not make a BARRIER solid");
        }
        if (CidDerivesFrom(object.cid, 54))
        {
            validateAllObjectReferences(610, "TZP");
            if (const auto *throwHeight = classProperty(614); throwHeight && !std::isfinite(throwHeight->floatValue[0]))
                issue(identity + " TZP legacy throw-height value is not finite");
            if (classProperty(611) != nullptr || classProperty(612) != nullptr || classProperty(613) != nullptr)
                issue(identity + " serializes runtime-only TZP add/remove/reset operations (611-613)");
            if (const auto *kind = classProperty(609); kind && (kind->intValue < -1 || kind->intValue > 5))
                issue(identity + " has invalid TZP damage kind " + std::to_string(kind->intValue));
            std::size_t automaticTargets = 0;
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent != static_cast<std::int32_t>(index))
                    continue;
                // PostZpdLoad automatically adds exact direct-child PNT and SHAPE objects.
                if (candidate.cid == 126 || candidate.cid == 110)
                    ++automaticTargets;
            }
            const std::size_t authoredTargets = countOption(610);
            if (authoredTargets > 32)
                issue(identity + " has more than 32 authored TZP recovery OIDs; AddZpdZapObject ignores later entries");
            std::size_t validAuthoredTargets = 0;
            // An empty authored list is legal. FindBestJtJumpTarget first reuses the
            // player's recent target history and ultimately synthesizes a point 75
            // units below the player, so this must not be reported as broken data.
            std::size_t authoredOrdinal = 0;
            for (auto it = object.properties.begin(); it != object.properties.end(); ++it)
            {
                if (it->eopid != 610)
                    continue;
                const bool oidRetained = authoredOrdinal++ < 32;
                const bool duplicate =
                    std::any_of(object.properties.begin(), it, [&](const BrxPropertyRecord &earlier)
                                { return earlier.eopid == 610 && earlier.intValue == it->intValue; });
                if (duplicate)
                    issue(identity + " repeats TZP recovery target OID " + std::to_string(it->intValue));
                const auto target = findContextualObject(static_cast<std::int16_t>(it->intValue));
                if (target != document.Objects().end())
                {
                    const bool compatible = CidDerivesFrom(target->cid, 126) || CidDerivesFrom(target->cid, 110) ||
                                            CidDerivesFrom(target->cid, 118) || CidDerivesFrom(target->cid, 112) ||
                                            CidDerivesFrom(target->cid, 113) || CidDerivesFrom(target->cid, 129);
                    if (!compatible)
                        issue(identity + " TZP recovery target OID " + std::to_string(it->intValue) +
                              " resolves to incompatible " + BrxDocument::ClassName(target->cid));
                    else if (oidRetained)
                        ++validAuthoredTargets;
                }
            }
            if (automaticTargets + validAuthoredTargets > 32)
                issue(identity + " has " + std::to_string(automaticTargets + validAuthoredTargets) +
                      " valid TZP recovery targets after automatic children; only the first 32 are retained");
            if (CidDerivesFrom(object.cid, 55))
            {
                const auto *kind = classProperty(609);
                const auto *sphere = classProperty(133);
                const bool hasBsp = object.so.present && !object.so.collisionGeometry.bspNodes.empty();
                if (!object.so.present)
                    issue(identity + " is a VOLZP without a decoded SO collision payload");
                if (!sphere && !hasBsp)
                    issue(identity + " VOLZP has neither a sphere override nor collision BSP");
                if (!sphere && hasBsp &&
                    (!std::isfinite(object.so.collisionGeometry.radius) || object.so.collisionGeometry.radius <= 0.0f))
                    issue(identity + " VOLZP collision BSP has an invalid broad-phase radius");
                if (sphere && (!std::isfinite(sphere->floatValue[0]) || sphere->floatValue[0] <= 0.0f))
                    issue(identity + " VOLZP sphere radius must be finite and greater than zero");
                if (sphere)
                {
                    const float scaleX = std::sqrt(object.worldTransform[0] * object.worldTransform[0] +
                                                   object.worldTransform[1] * object.worldTransform[1] +
                                                   object.worldTransform[2] * object.worldTransform[2]);
                    const float scaleY = std::sqrt(object.worldTransform[4] * object.worldTransform[4] +
                                                   object.worldTransform[5] * object.worldTransform[5] +
                                                   object.worldTransform[6] * object.worldTransform[6]);
                    const float scaleZ = std::sqrt(object.worldTransform[8] * object.worldTransform[8] +
                                                   object.worldTransform[9] * object.worldTransform[9] +
                                                   object.worldTransform[10] * object.worldTransform[10]);
                    const float maximumScale = (std::max)(scaleX, (std::max)(scaleY, scaleZ));
                    const float minimumScale = (std::min)(scaleX, (std::min)(scaleY, scaleZ));
                    if (!std::isfinite(maximumScale) || minimumScale <= 0.0f)
                        issue(identity + " VOLZP sphere has a singular or non-finite world scale");
                    else if (maximumScale - minimumScale > maximumScale * 0.001f)
                        issue(identity +
                              " VOLZP sphere has non-uniform scale; use uniform scale for predictable containment");
                }
                if (kind && (kind->intValue < 0 || kind->intValue > 5))
                    issue(identity + " VOLZP damage kind is outside the retail ZPK range");
                if (kind && kind->intValue == 5 && !hasBsp)
                    issue(identity + " is a pit VOLZP without decoded SO collision BSP data");
                if (object.so.present && !object.so.fixedPhysics)
                    issue(identity + " VOLZP is not marked fixed even though InitVolzp locks it in retail");
            }
            else if (object.cid == 54 && object.so.present)
            {
                const auto *sphere = classProperty(133);
                bool hasContactGeometry = !object.so.collisionGeometry.bspNodes.empty();
                const bool validSphere =
                    sphere != nullptr && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size() && !hasContactGeometry;
                     ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (!candidate.so.present || candidate.so.collisionGeometry.bspNodes.empty())
                        continue;
                    std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                                 : candidate.parentIndex;
                    while (parent >= 0 && parent < static_cast<std::int32_t>(document.Objects().size()))
                    {
                        if (parent == static_cast<std::int32_t>(index))
                        {
                            hasContactGeometry = true;
                            break;
                        }
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(parent)];
                        parent = ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex
                                                                       : ancestor.parentIndex;
                    }
                }
                if (!hasContactGeometry && !validSphere)
                    issue(
                        identity +
                        " TZP hierarchy has no collision BSP or valid sphere-collision override to produce physics contacts");
            }
        }
        if (CidDerivesFrom(object.cid, 56))
        {
            validateAllObjectReferences(619, "CHKPNT");
            validateAllObjectReferences(620, "CHKPNT");
            validateObjectReference(621, "CHKPNT");
            std::size_t childVolumes = 0;
            std::size_t childAnimations = 0;
            std::size_t childWarps = 0;
            std::size_t stateMachines = 0;
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent != static_cast<std::int32_t>(index))
                    continue;
                if (CidDerivesFrom(candidate.cid, 123))
                    ++childVolumes;
                if (CidDerivesFrom(candidate.cid, 131))
                    ++childAnimations;
                if (CidDerivesFrom(candidate.cid, 116))
                    ++childWarps;
                if (CidDerivesFrom(candidate.cid, 136))
                    ++stateMachines;
            }
            const std::size_t totalVolumes = countOption(619) + childVolumes;
            const std::size_t totalAnimations = countOption(620) + childAnimations;
            const auto *contactSphere = classProperty(133);
            const bool hasPhysicalCollision =
                object.so.present &&
                (!object.so.collisionGeometry.bspNodes.empty() ||
                 (contactSphere && std::isfinite(contactSphere->floatValue[0]) && contactSphere->floatValue[0] > 0.0f));
            if (totalVolumes == 0 && !hasPhysicalCollision)
                issue(identity + " has no trigger VOL or usable physical collision activation path");
            if (object.so.present && !object.so.fixedPhysics)
                issue(identity + " is not fixed even though retail InitChkpnt locks its SO constraints");
            if (totalVolumes > 4)
                issue(identity + " binds " + std::to_string(totalVolumes) +
                      " CHKPNT volumes including children; the runtime array holds only four");
            if (totalAnimations > 4)
                issue(identity + " binds " + std::to_string(totalAnimations) +
                      " CHKPNT animation segments including children; the runtime array holds only four");
            const auto *explicitWarp = classProperty(621);
            const bool hasExplicitWarp = explicitWarp && explicitWarp->intValue != -1;
            if (!hasExplicitWarp && childWarps == 0)
                issue(identity + " has no respawn WARP reference or child WARP");
            if (stateMachines > 1)
                issue(
                    identity +
                    " has multiple direct checkpoint state machines; retail uses OID 1349 when found, otherwise the first direct SM");
            if (stateMachines > 0)
            {
                const auto stateMachineObject =
                    std::find_if(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate)
                                 {
                                     const std::int32_t runtimeParent = candidate.proxyRuntimeParentIndex >= 0
                                                                            ? candidate.proxyRuntimeParentIndex
                                                                            : candidate.parentIndex;
                                     return runtimeParent == static_cast<std::int32_t>(index) &&
                                            CidDerivesFrom(candidate.cid, 136) && candidate.oid == 1349;
                                 });
                const auto fallbackStateMachine =
                    stateMachineObject != document.Objects().end()
                        ? stateMachineObject
                        : std::find_if(document.Objects().begin(), document.Objects().end(),
                                       [&](const BrxObjectRecord &candidate)
                                       {
                                           const std::int32_t runtimeParent = candidate.proxyRuntimeParentIndex >= 0
                                                                                  ? candidate.proxyRuntimeParentIndex
                                                                                  : candidate.parentIndex;
                                           return runtimeParent == static_cast<std::int32_t>(index) &&
                                                  CidDerivesFrom(candidate.cid, 136);
                                       });
                if (fallbackStateMachine != document.Objects().end())
                {
                    const std::int32_t machineIndex =
                        static_cast<std::int32_t>(std::distance(document.Objects().begin(), fallbackStateMachine));
                    const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                      [&](const BrxSmRecord &candidate)
                                                      { return candidate.ownerObjectIndex == machineIndex; });
                    if (machine == document.StateMachines().end())
                        issue(identity + " checkpoint state machine has no decoded payload");
                    else
                        for (const std::int16_t goal :
                             {std::int16_t{1350}, std::int16_t{1351}, std::int16_t{1352}, std::int16_t{1353}})
                            if (std::none_of(machine->states.begin(), machine->states.end(),
                                             [&](const BrxSmStateRecord &state) { return state.oid == goal; }))
                                issue(identity + " checkpoint state machine is missing goal OID " +
                                      std::to_string(goal));
                }
            }
            for (const auto eopid : {std::int16_t{619}, std::int16_t{620}})
            {
                for (auto it = object.properties.begin(); it != object.properties.end(); ++it)
                {
                    if (it->eopid != eopid)
                        continue;
                    const bool duplicate =
                        std::any_of(object.properties.begin(), it, [&](const BrxPropertyRecord &earlier)
                                    { return earlier.eopid == eopid && earlier.intValue == it->intValue; });
                    if (duplicate)
                        issue(identity + " repeats CHKPNT " + std::string(eopid == 619 ? "volume" : "animation") +
                              " OID " + std::to_string(it->intValue));
                    const bool repeatsAutomaticChild = std::any_of(
                        document.Objects().begin(), document.Objects().end(),
                        [&](const BrxObjectRecord &candidate)
                        {
                            const std::int32_t runtimeParent = candidate.proxyRuntimeParentIndex >= 0
                                                                   ? candidate.proxyRuntimeParentIndex
                                                                   : candidate.parentIndex;
                            if (runtimeParent != static_cast<std::int32_t>(index) || candidate.oid != it->intValue)
                                return false;
                            return eopid == 619 ? CidDerivesFrom(candidate.cid, 123)
                                                : CidDerivesFrom(candidate.cid, 131);
                        });
                    if (repeatsAutomaticChild)
                        issue(identity + " serializes CHKPNT " + std::string(eopid == 619 ? "volume" : "animation") +
                              " OID " + std::to_string(it->intValue) +
                              " even though that direct child is added automatically");
                }
            }
        }
        if (CidDerivesFrom(object.cid, 57))
        {
            if (const auto *speed = classProperty(625); speed && !std::isfinite(speed->floatValue[0]))
                issue(identity + " CNVO belt speed is not finite");
            if (!object.hasCnvoPayload)
                issue(identity + " CNVO is missing its mandatory two-float UV-average payload");
            else if (!std::isfinite(object.cnvoAverageUVelocity) || !std::isfinite(object.cnvoAverageVVelocity))
                issue(identity + " CNVO UV-average payload contains a non-finite value");
            if (!object.so.present)
                issue(identity + " CNVO is missing its inherited SO payload");
            else
            {
                const auto *sphere = classProperty(133);
                bool hasContactGeometry =
                    !object.so.collisionGeometry.bspNodes.empty() ||
                    (sphere && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f);
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size() && !hasContactGeometry;
                     ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (!candidate.so.present || candidate.so.collisionGeometry.bspNodes.empty())
                        continue;
                    std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                                 : candidate.parentIndex;
                    while (parent >= 0 && parent < static_cast<std::int32_t>(document.Objects().size()))
                    {
                        if (parent == static_cast<std::int32_t>(index))
                        {
                            hasContactGeometry = true;
                            break;
                        }
                        const auto &ancestor = document.Objects()[static_cast<std::size_t>(parent)];
                        const std::int32_t next = ancestor.proxyRuntimeParentIndex >= 0
                                                      ? ancestor.proxyRuntimeParentIndex
                                                      : ancestor.parentIndex;
                        if (next == parent)
                            break;
                        parent = next;
                    }
                }
                if (!hasContactGeometry)
                    issue(identity + " CNVO hierarchy has no collision surface to generate conveyor contacts");
            }
        }
        if (CidDerivesFrom(object.cid, 58))
        {
            validateNonNegativeFloat(640, "HBSK");
            if (const auto *jumpHeight = classProperty(642); jumpHeight && !std::isfinite(jumpHeight->floatValue[0]))
                issue(identity + " HBSK jump-height cutoff is not finite");
            if (!object.so.present)
                issue(identity + " HBSK is missing its inherited SO payload");
            else
            {
                const auto *sphere = classProperty(133);
                const bool validSphere = sphere && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f;
                if (object.so.collisionGeometry.bspNodes.empty() && !validSphere)
                    issue(identity +
                          " HBSK has no collision BSP or valid collision sphere, so darts cannot hit the basket");
            }
        }
        const auto validateOrderedRange = [&](const std::int16_t id, const char *owner)
        {
            if (const auto *range = classProperty(id);
                range && (!std::isfinite(range->floatValue[0]) || !std::isfinite(range->floatValue[1]) ||
                          range->floatValue[0] > range->floatValue[1]))
                issue(identity + " " + owner + " " + std::string(PropertyName(id) ? PropertyName(id) : "range") +
                      " has an invalid minimum/maximum");
        };
        if (CidDerivesFrom(object.cid, 59))
        {
            for (const auto id : {std::int16_t{717}, std::int16_t{727}, std::int16_t{728}, std::int16_t{729}})
                validateNonNegativeFloat(id, "BOMB");
            for (const auto id : {std::int16_t{712}, std::int16_t{713}, std::int16_t{718}, std::int16_t{720}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " BOMB " + std::string(PropertyName(id) ? PropertyName(id) : "value") +
                          " is not finite");
            validateOrderedRange(719, "BOMB");
            if (const auto *maximumDelta = classProperty(717);
                maximumDelta && (!std::isfinite(maximumDelta->floatValue[0]) || maximumDelta->floatValue[0] <= 0.0f))
                issue(identity + " BOMB maximum blast velocity change must be finite and greater than zero");
            if (const auto *mass = classProperty(719);
                mass && (!std::isfinite(mass->floatValue[0]) || mass->floatValue[0] <= 0.0f))
                issue(
                    identity +
                    " BOMB impact-mass minimum must be finite and greater than zero because detonation divides by it");
            if (const auto *trigger = classProperty(714); trigger && (trigger->intValue < -1 || trigger->intValue > 4))
                issue(identity + " has invalid BOMB detonation trigger " + std::to_string(trigger->intValue));
            if (const auto *mode = classProperty(715); mode && (mode->intValue < 0 || mode->intValue > 2))
                issue(identity + " has invalid BOMB direction mode " + std::to_string(mode->intValue));
            if (const auto *kind = classProperty(725); kind && (kind->intValue < -1 || kind->intValue > 5))
                issue(identity + " has invalid BOMB damage kind " + std::to_string(kind->intValue));
            if (const auto *speedThreshold = classProperty(730);
                speedThreshold && !std::isfinite(speedThreshold->floatValue[0]))
                issue(identity + " BOMB detonation-speed threshold is not finite");
            const bool hasCustomSound = classProperty(726) != nullptr || classProperty(727) != nullptr ||
                                        classProperty(728) != nullptr || classProperty(729) != nullptr;
            const auto *audible = classProperty(727);
            const auto *full = classProperty(728);
            const float effectiveAudible = audible ? audible->floatValue[0] : 3000.0f;
            const float effectiveFull = full ? full->floatValue[0] : 300.0f;
            if (hasCustomSound && effectiveAudible < effectiveFull)
                issue(identity + " BOMB maximum audible distance is below its effective full-volume distance");
            if (const auto *volume = classProperty(729);
                volume && (volume->floatValue[0] < 0.0f || volume->floatValue[0] > 1.0f))
                issue(identity + " BOMB playback volume should be between zero and one");
            if ((classProperty(727) != nullptr || classProperty(728) != nullptr || classProperty(729) != nullptr) &&
                classProperty(726) == nullptr)
                issue(
                    identity +
                    " customizes BOMB sound distances or volume without a sound-effect ID; PsfxEnsureBomb creates an SFXID_Nil record instead of using stock sound 53");
            if (classProperty(721) != nullptr || classProperty(722) != nullptr)
                issue(identity + " serializes runtime-only BOMB prime/detonate commands (EOPID 721/722)");
            const auto *objectRadius = classProperty(712);
            const auto *touchDamage = classProperty(716);
            const bool effectiveTouchDamage = touchDamage && touchDamage->boolValue;
            const float effectiveObjectRadius = objectRadius ? objectRadius->floatValue[0] : 200.0f;
            if (!effectiveTouchDamage && effectiveObjectRadius <= 0.0f)
                issue(
                    identity +
                    " BOMB world-object radius must be greater than zero for radial damage because impulse falloff divides by it");
            int directDebris = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (!CidDerivesFrom(candidate.cid, 39))
                    continue;
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent == static_cast<std::int32_t>(index))
                    ++directDebris;
            }
            if (directDebris > 16)
                issue(identity + " BOMB has more than the runtime limit of 16 direct BRKP debris children");
            int explosionDescendants = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!CidDerivesFrom(document.Objects()[candidateIndex].cid, 119))
                    continue;
                const auto &explosion = document.Objects()[candidateIndex];
                std::int32_t ancestor =
                    explosion.proxyRuntimeParentIndex >= 0 ? explosion.proxyRuntimeParentIndex : explosion.parentIndex;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        ++explosionDescendants;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const auto next = ancestorObject.proxyRuntimeParentIndex >= 0
                                          ? ancestorObject.proxyRuntimeParentIndex
                                          : ancestorObject.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
            }
            if (explosionDescendants > 8)
                issue(
                    identity +
                    " has more than eight inferred EXPL descendants; InferExpl removes every match but its runtime EXPLG retains only the first eight");
        }
        if (object.cid == 63)
        {
            if (!object.so.present)
                issue(identity + " MINE is missing its inherited SO payload");
            else
            {
                const auto *sphere = classProperty(133);
                const bool validSphere = sphere && std::isfinite(sphere->floatValue[0]) && sphere->floatValue[0] > 0.0f;
                if (object.so.collisionGeometry.bspNodes.empty() && !validSphere)
                    issue(
                        identity +
                        " MINE has no collision BSP or valid collision sphere; surface contact cannot stop its search propulsion or drive contact-triggered BOMB behavior");
            }
            if (object.oid != 0x192)
                issue(identity + " MINE is not OID 0x192; JT's fixed mine slot will not infer it");
            std::int32_t jtOwnerIndex = -1;
            for (std::int32_t ancestor = object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex
                                                                             : object.parentIndex,
                              guard = 0;
                 ancestor >= 0 && static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                 guard++ <= static_cast<std::int32_t>(document.Objects().size());)
            {
                const auto &candidate = document.Objects()[static_cast<std::size_t>(ancestor)];
                if (CidDerivesFrom(candidate.cid, 7))
                {
                    jtOwnerIndex = ancestor;
                    break;
                }
                const std::int32_t parent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (parent == ancestor)
                    break;
                ancestor = parent;
            }
            if (jtOwnerIndex < 0)
                issue(identity + " MINE is outside a JT hierarchy and cannot be deployed by Sly");
            else
            {
                const auto belongsToOwner = [&](const std::int32_t candidateIndex)
                {
                    std::int32_t ancestor = candidateIndex;
                    for (std::size_t guard = 0; guard <= document.Objects().size() && ancestor >= 0 &&
                                                static_cast<std::size_t>(ancestor) < document.Objects().size();
                         ++guard)
                    {
                        if (ancestor == jtOwnerIndex)
                            return true;
                        const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t parent = ancestorObject.proxyRuntimeParentIndex >= 0
                                                        ? ancestorObject.proxyRuntimeParentIndex
                                                        : ancestorObject.parentIndex;
                        if (parent == ancestor)
                            break;
                        ancestor = parent;
                    }
                    return false;
                };
                int originCount = 0;
                int mineCount = 0;
                std::array<int, 4> hatCounts{};
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    if (!belongsToOwner(static_cast<std::int32_t>(candidateIndex)))
                        continue;
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid == 0x050 && CidDerivesFrom(candidate.cid, 2))
                        ++originCount;
                    if (candidate.cid == 63)
                        ++mineCount;
                    if (candidate.oid >= 0x18e && candidate.oid <= 0x191)
                    {
                        const std::size_t slot = static_cast<std::size_t>(candidate.oid - 0x18e);
                        ++hatCounts[slot];
                        if (!CidDerivesFrom(candidate.cid, 2))
                            issue(identity + " JT mine-hat slot OID " + std::to_string(candidate.oid) +
                                  " is not ALO-compatible");
                    }
                }
                if (originCount == 0)
                    issue(identity + " owning JT has no ALO-compatible OID 0x050 mine-origin fallback");
                else if (originCount > 1)
                    issue(identity + " owning JT has multiple OID 0x050 mine origins");
                if (mineCount > 1)
                    issue(identity + " owning JT hierarchy contains multiple MINE objects");
                for (std::size_t slot = 0; slot < hatCounts.size(); ++slot)
                    if (hatCounts[slot] > 1)
                        issue(identity + " owning JT repeats mine-hat slot OID " +
                              std::to_string(0x18e + static_cast<int>(slot)));
            }
        }
        if (CidDerivesFrom(object.cid, 60))
        {
            if (const auto *launchSpeed = classProperty(823); launchSpeed && !std::isfinite(launchSpeed->floatValue[0]))
                issue(identity + " MISSILE launch speed is not finite");
            validateAllObjectReferences(827, "MISSILE");
            if (classProperty(824) != nullptr || classProperty(825) != nullptr)
                issue(
                    identity +
                    " serializes runtime-only MISSILE target state (EOPID 824/825); FireMissile supplies both values");
            if (classProperty(52) != nullptr || classProperty(53) != nullptr)
                issue(identity +
                      " authors freeze controls that LoadMissileFromBrx overrides by forcing the missile to no-freeze");
            if (countOption(827) > 4)
                issue(identity + " has more than four MISSILE ignored objects");
            if (countOption(828) > 4)
                issue(identity + " has more than four MISSILE ignored classes");
            for (const auto &property : object.properties)
            {
                if (property.eopid == 827)
                {
                    bool ignoresSelf = property.intValue == object.oid;
                    if (!ignoresSelf && object.proxyRecordIndex >= 0 &&
                        static_cast<std::size_t>(object.proxyRecordIndex) < document.Proxies().size())
                    {
                        const auto ownerIndex =
                            document.Proxies()[static_cast<std::size_t>(object.proxyRecordIndex)].ownerObjectIndex;
                        ignoresSelf = ownerIndex >= 0 &&
                                      static_cast<std::size_t>(ownerIndex) < document.Objects().size() &&
                                      document.Objects()[static_cast<std::size_t>(ownerIndex)].oid == property.intValue;
                    }
                    if (ignoresSelf)
                        issue(identity + " MISSILE ignores its own object/proxy-root name");
                }
                if (property.eopid == 828 &&
                    (property.intValue < 0 || static_cast<std::size_t>(property.intValue) >= CidOptionIds().size()))
                    issue(identity + " MISSILE ignored class CID is outside the retail class table");
            }
            for (const auto eopid : {std::int16_t{827}, std::int16_t{828}})
                for (auto it = object.properties.begin(); it != object.properties.end(); ++it)
                {
                    if (it->eopid != eopid)
                        continue;
                    const bool duplicate =
                        std::any_of(object.properties.begin(), it, [&](const BrxPropertyRecord &earlier)
                                    { return earlier.eopid == eopid && earlier.intValue == it->intValue; });
                    if (duplicate)
                        issue(identity + " repeats MISSILE ignored " +
                              std::string(eopid == 827 ? "object OID " : "class CID ") + std::to_string(it->intValue));
                }
            int fireAnimationCount = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (!CidDerivesFrom(candidate.cid, 131) || candidate.oid != 791)
                    continue;
                std::int32_t current = static_cast<std::int32_t>(candidateIndex);
                std::size_t guard = 0;
                while (current >= 0 && static_cast<std::size_t>(current) < document.Objects().size() &&
                       guard++ <= document.Objects().size())
                {
                    const auto &currentObject = document.Objects()[static_cast<std::size_t>(current)];
                    const std::int32_t parent = currentObject.proxyRuntimeParentIndex >= 0
                                                    ? currentObject.proxyRuntimeParentIndex
                                                    : currentObject.parentIndex;
                    if (parent == static_cast<std::int32_t>(index))
                    {
                        ++fireAnimationCount;
                        break;
                    }
                    if (parent == current)
                        break;
                    current = parent;
                }
            }
            if (fireAnimationCount > 1)
                issue(
                    identity +
                    " MISSILE has multiple descendant launch ASEGs with OID 791; runtime hierarchy lookup selects only one");
        }
        if (object.cid == 61)
        {
            validateNonNegativeFloat(829, "ACCMISS");
            validateNonNegativeFloat(830, "ACCMISS");
            if (classProperty(831) != nullptr)
                issue(
                    identity +
                    " serializes runtime-only ACCMISS desired velocity EOPID 831; FireAccmiss calculates it from launch velocity");
            const auto *launchSpeed = classProperty(823);
            const auto *cruiseSpeed = classProperty(829);
            const auto *acceleration = classProperty(830);
            const float effectiveLaunchSpeed = launchSpeed ? launchSpeed->floatValue[0] : 800.0f;
            const float effectiveCruiseSpeed = cruiseSpeed ? cruiseSpeed->floatValue[0] : 2000.0f;
            const float effectiveAcceleration = acceleration ? acceleration->floatValue[0] : 5000.0f;
            if (std::abs(effectiveLaunchSpeed - effectiveCruiseSpeed) > 0.001f && effectiveAcceleration <= 0.0f)
                issue(
                    identity +
                    " ACCMISS has different launch and cruise speeds but cannot transition because acceleration is zero");
        }
        if (CidDerivesFrom(object.cid, 62))
        {
            for (const auto &property : object.properties)
            {
                if (property.eopid == 829 || property.eopid == 830)
                    issue(identity + " TARMISS serializes inherited ACCMISS EOPID " + std::to_string(property.eopid) +
                          ", but TARMISS guidance does not consume the ACCMISS cruise/acceleration profile");
                else if (property.eopid == 831)
                    issue(identity + " TARMISS serializes runtime-only ACCMISS desired velocity EOPID 831");
            }
            if (const auto *lifetime = classProperty(832); lifetime && !std::isfinite(lifetime->floatValue[0]))
                issue(identity + " TARMISS lifetime override is not finite");
            const float effectiveLifetime = classProperty(832) ? classProperty(832)->floatValue[0] : 0.0f;
            if (const auto *speed = classProperty(833); speed)
            {
                if (!std::isfinite(speed->floatValue[0]))
                    issue(identity + " TARMISS automatic intercept speed limit is not finite");
                else if (effectiveLifetime <= 0.0f && speed->floatValue[0] <= 0.0f)
                    issue(
                        identity +
                        " TARMISS automatic intercept speed limit must be greater than zero when no positive lifetime override is active");
            }
            if (const auto *bank = classProperty(834); bank && !std::isfinite(bank->floatValue[0]))
                issue(identity + " TARMISS trajectory bank ratio is not finite");
            if (const auto *velocity = classProperty(835);
                velocity && (!std::isfinite(velocity->floatValue[0]) || !std::isfinite(velocity->floatValue[1]) ||
                             !std::isfinite(velocity->floatValue[2])))
                issue(identity + " TARMISS arrival velocity contains a non-finite component");
        }
        if (CidDerivesFrom(object.cid, 64))
        {
            validateObjectReference(955, "FLY");
            for (const auto id :
                 {std::int16_t{951}, std::int16_t{952}, std::int16_t{953}, std::int16_t{954}, std::int16_t{957},
                  std::int16_t{959}, std::int16_t{960}, std::int16_t{961}, std::int16_t{962}})
                validateOrderedRange(id, "FLY");
            for (const auto id : {std::int16_t{958}, std::int16_t{963}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " FLY " + std::string(PropertyName(id) ? PropertyName(id) : "value") +
                          " is non-finite");
            if (const auto *blend = classProperty(956);
                blend &&
                (!std::isfinite(blend->floatValue[0]) || blend->floatValue[0] < 0.0f || blend->floatValue[0] > 1.0f))
                issue(identity + " FLY soar blend must be between zero and one");
            if (const auto *kind = classProperty(948); kind && (kind->intValue < -1 || kind->intValue > 1))
                issue(identity + " has invalid FLY kind " + std::to_string(kind->intValue));
            if (const auto *state = classProperty(949); state && (state->intValue < -1 || state->intValue > 6))
                issue(identity + " has invalid initial FLY state " + std::to_string(state->intValue));
            const auto *kind = classProperty(948);
            const bool pigeon = kind && kind->intValue == 1;
            const int initialState = classProperty(949) ? classProperty(949)->intValue : 0;
            if (!pigeon && initialState == 6)
                issue(
                    identity +
                    " insect FLY starts in Ground state, but LoadFlyFromBrx only loads ground render frames for pigeons");
            if (!pigeon && classProperty(955) != nullptr)
                issue(identity + " insect FLY serializes a dormant pigeon-only soaring appearance OID");
            if (classProperty(956) != nullptr && (!pigeon || classProperty(955) == nullptr))
                issue(identity + " FLY serializes a dormant soar blend without an active pigeon soaring appearance");
            std::size_t airborneFrames = 0;
            std::size_t groundFrames = 0;
            std::array<int, 16> airborneOidCounts{};
            std::array<int, 8> groundOidCounts{};
            int deadAppearances = 0;
            int targets = 0;
            int droppingsEffects = 0;
            int droppingsPoints = 0;
            bool soarOidIsFrame = !classProperty(955);
            const int soarOid = classProperty(955) ? classProperty(955)->intValue : -1;
            const auto belongsToFly = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == static_cast<std::int32_t>(index))
                        return true;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!belongsToFly(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid >= 572 && candidate.oid <= 587)
                {
                    ++airborneOidCounts[static_cast<std::size_t>(candidate.oid - 572)];
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " FLY airborne frame OID " + std::to_string(candidate.oid) +
                              " is not ALO-compatible");
                    else
                    {
                        ++airborneFrames;
                        if (candidate.oid == soarOid)
                            soarOidIsFrame = true;
                    }
                }
                if (candidate.oid >= 589 && candidate.oid <= 596)
                {
                    ++groundOidCounts[static_cast<std::size_t>(candidate.oid - 589)];
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " FLY ground frame OID " + std::to_string(candidate.oid) +
                              " is not ALO-compatible");
                    else
                        ++groundFrames;
                }
                if (candidate.oid == 598)
                {
                    ++deadAppearances;
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " FLY death appearance OID 598 is not ALO-compatible");
                }
                if (candidate.oid == 600)
                {
                    ++targets;
                    if (!CidDerivesFrom(candidate.cid, 117))
                        issue(identity + " FLY OID 600 must be a TARGET");
                }
                if (candidate.oid == 601)
                {
                    ++droppingsEffects;
                    if (!CidDerivesFrom(candidate.cid, 122))
                        issue(identity + " FLY droppings effect OID 601 must be EXPLS-compatible");
                }
                if (candidate.oid == 602)
                {
                    ++droppingsPoints;
                    if (!CidDerivesFrom(candidate.cid, 126))
                        issue(identity + " FLY droppings point OID 602 must be PNT-compatible");
                }
            }
            if (airborneFrames == 0)
                issue(identity + " has no FLY airborne render frames in child OID range 572-587");
            if (deadAppearances == 0)
                issue(identity + " has no ALO-compatible FLY death appearance OID 598");
            if (targets == 0)
                issue(identity + " has no FLY TARGET child OID 600");
            if (pigeon && groundFrames == 0)
                issue(identity + " is a pigeon FLY with no ground render frames in child OID range 589-596");
            if (pigeon && droppingsPoints > 0 && droppingsEffects == 0)
                issue(identity + " has a pigeon droppings PNT OID 602 but no EXPLS OID 601; the point is dormant");
            if (pigeon && soarOid >= 0 && !soarOidIsFrame)
                issue(identity + " FLY soaring appearance OID is not one of its airborne render children");
            for (std::size_t slot = 0; slot < airborneOidCounts.size(); ++slot)
                if (airborneOidCounts[slot] > 1)
                    issue(identity + " has duplicate FLY airborne frame OID " +
                          std::to_string(572 + static_cast<int>(slot)) + "; runtime selects one");
            for (std::size_t slot = 0; slot < groundOidCounts.size(); ++slot)
                if (groundOidCounts[slot] > 1)
                    issue(identity + " has duplicate FLY ground frame OID " +
                          std::to_string(589 + static_cast<int>(slot)) + "; runtime selects one");
            for (const auto [count, role] :
                 {std::pair{deadAppearances, "death appearance OID 598"}, std::pair{targets, "TARGET OID 600"},
                  std::pair{droppingsEffects, "EXPLS OID 601"}, std::pair{droppingsPoints, "PNT OID 602"}})
                if (count > 1)
                    issue(identity + " has multiple FLY " + std::string(role) +
                          " matches; runtime hierarchy lookup selects one");
        }
        if (CidDerivesFrom(object.cid, 65))
        {
            for (const auto id : {std::int16_t{752}, std::int16_t{753}, std::int16_t{754}, std::int16_t{755},
                                  std::int16_t{756}, std::int16_t{757}, std::int16_t{758}, std::int16_t{759}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " RAT " + std::string(PropertyName(id) ? PropertyName(id) : "value") +
                          " is non-finite");
            const auto belongsToRat = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == static_cast<std::int32_t>(index))
                        return true;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            std::array<int, 16> scurryOidCounts{};
            std::array<int, 15> fleeOidCounts{};
            int stoppedFrames = 0;
            int targets = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!belongsToRat(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid >= 617 && candidate.oid <= 632)
                {
                    ++scurryOidCounts[static_cast<std::size_t>(candidate.oid - 617)];
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " RAT scurry frame OID " + std::to_string(candidate.oid) + " is " +
                              BrxDocument::ClassName(candidate.cid) + ", expected an ALO-derived object");
                }
                if (candidate.oid >= 633 && candidate.oid <= 647)
                {
                    ++fleeOidCounts[static_cast<std::size_t>(candidate.oid - 633)];
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " RAT flee frame OID " + std::to_string(candidate.oid) + " is " +
                              BrxDocument::ClassName(candidate.cid) + ", expected an ALO-derived object");
                }
                if (candidate.oid == 616)
                {
                    ++stoppedFrames;
                    if (!CidDerivesFrom(candidate.cid, 2))
                        issue(identity + " RAT stopped appearance OID 616 is not ALO-derived");
                }
                if (candidate.oid == 615)
                {
                    ++targets;
                    if (!CidDerivesFrom(candidate.cid, 117))
                        issue(identity + " RAT damage target OID 615 is not TARGET-derived");
                }
            }
            const std::size_t scurryFrames = static_cast<std::size_t>(
                std::count_if(scurryOidCounts.begin(), scurryOidCounts.end(), [](int count) { return count > 0; }));
            const std::size_t fleeFrames = static_cast<std::size_t>(
                std::count_if(fleeOidCounts.begin(), fleeOidCounts.end(), [](int count) { return count > 0; }));
            if (scurryFrames == 0)
                issue(identity + " has no RAT scurry render frames (child OIDs 617-632)");
            if (fleeFrames == 0)
                issue(identity + " has no RAT flee render frames (child OIDs 633-647)");
            if (stoppedFrames == 0)
                issue(identity + " has no RAT stopped appearance child OID 616");
            if (targets == 0)
                issue(identity + " has no RAT TARGET child OID 615");
            for (std::size_t slot = 0; slot < scurryOidCounts.size(); ++slot)
                if (scurryOidCounts[slot] > 1)
                    issue(identity + " has duplicate RAT scurry frame OID " +
                          std::to_string(617 + static_cast<int>(slot)) + "; runtime selects one");
            for (std::size_t slot = 0; slot < fleeOidCounts.size(); ++slot)
                if (fleeOidCounts[slot] > 1)
                    issue(identity + " has duplicate RAT flee frame OID " +
                          std::to_string(633 + static_cast<int>(slot)) + "; runtime selects one");
            if (stoppedFrames > 1)
                issue(identity +
                      " has multiple RAT stopped appearances at OID 616; runtime hierarchy lookup selects one");
            if (targets > 1)
                issue(identity + " has multiple RAT TARGETs at OID 615; runtime hierarchy lookup selects one");

            const auto *groupRadiusProperty = classProperty(757);
            const float groupRadius = groupRadiusProperty ? groupRadiusProperty->floatValue[0] : 3000.0f;
            const glm::vec3 ratPosition(object.worldTransform[12], object.worldTransform[13],
                                        object.worldTransform[14]);
            std::size_t holes = 0;
            std::size_t eligibleHoles = 0;
            for (const auto &candidate : document.Objects())
            {
                if (!CidDerivesFrom(candidate.cid, 124))
                    continue;
                ++holes;
                const glm::vec3 holePosition(candidate.worldTransform[12], candidate.worldTransform[13],
                                             candidate.worldTransform[14]);
                const glm::vec3 delta = holePosition - ratPosition;
                if (glm::dot(delta, delta) <= groupRadius * groupRadius)
                    ++eligibleHoles;
            }
            if (holes == 0)
                issue(identity + " has no RATHOLE in the world; player-triggered Flee cannot choose a destination");
            else if (eligibleHoles == 0)
                issue(
                    identity +
                    " has no RATHOLE inside its group radius; ProjectCane can enter Flee with a null hole and dereference it");
        }
        if (CidDerivesFrom(object.cid, 66))
        {
            validateOrderedRange(805, "ROH");
            const auto belongsToRoh = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == static_cast<std::int32_t>(index))
                        return true;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            int fixedBindings[6]{};
            const std::int32_t fixedOids[]{668, 669, 670, 671, 673, 674};
            const std::int16_t fixedCids[]{131, 131, 131, 131, 136, 136};
            const char *fixedRoles[]{"move animation",  "carry animation",     "grab animation",
                                     "death animation", "happy state machine", "sad state machine"};
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!belongsToRoh(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                for (int slot = 0; slot < 6; ++slot)
                    if (document.Objects()[candidateIndex].oid == fixedOids[slot])
                    {
                        ++fixedBindings[slot];
                        if (!CidDerivesFrom(document.Objects()[candidateIndex].cid, fixedCids[slot]))
                            issue(identity + " ROH " + fixedRoles[slot] + " OID " + std::to_string(fixedOids[slot]) +
                                  " is " + BrxDocument::ClassName(document.Objects()[candidateIndex].cid) +
                                  ", expected " + BrxDocument::ClassName(fixedCids[slot]));
                    }
            }
            for (int slot = 0; slot < 6; ++slot)
            {
                if (fixedBindings[slot] == 0 && slot < 4)
                    issue(identity + " is missing ROH " + fixedRoles[slot] + " OID " + std::to_string(fixedOids[slot]));
                else if (fixedBindings[slot] > 1)
                    issue(identity + " has multiple hierarchy objects for ROH " + fixedRoles[slot] + " OID " +
                          std::to_string(fixedOids[slot]));
            }
            const bool referencedByRob = std::any_of(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    return candidate.cid == 95 &&
                           std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                       [&](const BrxPropertyRecord &property)
                                       { return property.eopid == 772 && property.intValue == object.oid; });
                });
            if (!referencedByRob)
                issue(
                    identity +
                    " ROH template is not referenced by any ROB harvester slot; PostRohLoad removes standalone ROHs from the world");
        }
        if (CidDerivesFrom(object.cid, 67))
        {
            if (const auto *hits = classProperty(806); hits && (hits->intValue < 1 || hits->intValue > 9))
                issue(identity + " ROC hit limit must be between 1 and 9 damage states");
            validateNonNegativeFloat(807, "ROC");
            validateNonNegativeFloat(808, "ROC");
            if (const auto *curve = classProperty(809);
                curve && (!std::isfinite(curve->floatValue[0]) || !std::isfinite(curve->floatValue[1]) ||
                          !std::isfinite(curve->floatValue[2])))
                issue(identity + " ROC bubble-response curve contains a non-finite coefficient");
            const auto belongsToRoc = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == static_cast<std::int32_t>(index))
                        return true;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            int stateMachines = 0, bubbleEmitters = 0;
            int stateLabels[10]{};
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!belongsToRoc(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                const auto &child = document.Objects()[candidateIndex];
                if (child.oid == 675)
                {
                    ++stateMachines;
                    if (!CidDerivesFrom(child.cid, 136))
                        issue(identity + " ROC state machine OID 675 is not SM-derived");
                }
                if (child.oid >= 676 && child.oid <= 685)
                    ++stateLabels[child.oid - 676];
                if (child.oid == 686)
                {
                    ++bubbleEmitters;
                    if (!CidDerivesFrom(child.cid, 80))
                        issue(identity + " ROC bubble emitter OID 686 is not EMITTER-derived");
                }
            }
            if (stateMachines > 1)
                issue(identity +
                      " has multiple ROC damage state machines at OID 675; runtime hierarchy lookup selects one");
            if (stateMachines > 0)
                for (int state = 0; state < 10; ++state)
                {
                    if (stateLabels[state] == 0)
                        issue(identity + " has a damage state machine but is missing state label OID " +
                              std::to_string(676 + state));
                    else if (stateLabels[state] > 1)
                        issue(identity + " has duplicate ROC damage-state label OID " + std::to_string(676 + state));
                }
            if (bubbleEmitters > 1)
                issue(identity + " has multiple ROC bubble emitters at OID 686; runtime hierarchy lookup selects one");
            if (classProperty(809) && bubbleEmitters == 0)
                issue(identity + " authors a bubble-response curve but has no bubble emitter OID 686");
            const bool referencedByRob = std::any_of(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    return candidate.cid == 95 &&
                           std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                       [&](const BrxPropertyRecord &property)
                                       { return property.eopid == 771 && property.intValue == object.oid; });
                });
            if (!referencedByRob)
                issue(
                    identity +
                    " ROC template is not referenced by any ROB collectible slot; PostRocLoad removes standalone ROCs from the world");
        }
        if (object.cid == 68)
        {
            int returnPoints = 0;
            int stateMachines = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                const std::int32_t runtimeParent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (runtimeParent == static_cast<std::int32_t>(index) && CidDerivesFrom(candidate.cid, 126))
                    ++returnPoints;

                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                bool belongsToRost = false;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        belongsToRost = true;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent = ancestorObject.proxyRuntimeParentIndex >= 0
                                                    ? ancestorObject.proxyRuntimeParentIndex
                                                    : ancestorObject.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
                if (belongsToRost && candidate.oid == 687)
                {
                    ++stateMachines;
                    if (!CidDerivesFrom(candidate.cid, 136))
                        issue(identity + " ROST state machine OID 687 is not SM-derived");
                }
            }
            if (returnPoints == 0)
                issue(identity + " has no direct ROST return-point PNT child; ROH movement dereferences it");
            else if (returnPoints > 1)
                issue(identity +
                      " has multiple direct ROST PNT children; runtime uses the first child in hierarchy order");
            if (stateMachines > 1)
                issue(identity + " has multiple ROST state machines at OID 687; runtime hierarchy lookup selects one");
            const bool referencedByRob = std::any_of(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    return candidate.cid == 95 &&
                           std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                       [&](const BrxPropertyRecord &property)
                                       { return property.eopid == 775 && property.intValue == object.oid; });
                });
            if (!referencedByRob)
                issue(
                    identity +
                    " ROST tunnel is not registered by any ROB spawn-tunnel option; the encounter will never place it in the available tunnel list");
        }
        if (object.cid == 69)
        {
            int keyChildren = 0;
            int revealAnimations = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                bool descendant = false;
                std::size_t hierarchySteps = 0;
                while (ancestor >= 0 && ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                       hierarchySteps++ < document.Objects().size())
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        descendant = true;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t next = ancestorObject.proxyRuntimeParentIndex >= 0
                                                  ? ancestorObject.proxyRuntimeParentIndex
                                                  : ancestorObject.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
                if (!descendant)
                    continue;
                if (candidate.oid == 257 && CidDerivesFrom(candidate.cid, 89))
                    ++keyChildren;
                if (candidate.oid == 691)
                {
                    ++revealAnimations;
                    if (!CidDerivesFrom(candidate.cid, 131))
                        issue(identity + " ROP reveal animation OID 691 is not ASEG-derived");
                }
            }
            if (keyChildren == 0)
                issue(identity + " has no ROP prize KEY OID 257; Collect dereferences it");
            else if (keyChildren > 1)
                issue(identity + " has multiple ROP prize KEY objects at OID 257; runtime lookup selects one");
            if (revealAnimations > 1)
                issue(identity +
                      " has multiple ROP reveal animations at OID 691; runtime hierarchy lookup selects one");
            const bool referencedByRob = std::any_of(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    return candidate.cid == 95 &&
                           std::any_of(candidate.properties.begin(), candidate.properties.end(),
                                       [&](const BrxPropertyRecord &property)
                                       { return property.eopid == 774 && property.intValue == object.oid; });
                });
            if (!referencedByRob)
                issue(identity +
                      " ROP prize is not referenced by any ROB prize slot; PostRopLoad removes an unowned ROP");
        }
        if (CidDerivesFrom(object.cid, 70))
        {
            for (const auto id :
                 {std::int16_t{811}, std::int16_t{812}, std::int16_t{813}, std::int16_t{814}, std::int16_t{815}})
                validateNonNegativeFloat(id, "DART");
            if (const auto *speed = classProperty(811); speed && speed->floatValue[0] <= 0.0f)
                issue(identity + " DART homing flight speed must be greater than zero");
            if (const auto *state = classProperty(810); state && (state->intValue < -1 || state->intValue > 5))
                issue(identity + " has invalid DART state " + std::to_string(state->intValue));
            int stickingAnimations = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid != 700)
                    continue;
                std::int32_t ancestor =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                bool belongsToDart = false;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        belongsToDart = true;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent = ancestorObject.proxyRuntimeParentIndex >= 0
                                                    ? ancestorObject.proxyRuntimeParentIndex
                                                    : ancestorObject.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
                if (!belongsToDart)
                    continue;
                ++stickingAnimations;
                if (!CidDerivesFrom(candidate.cid, 131))
                    issue(identity + " DART sticking animation OID 700 is not ASEG-derived");
            }
            if (stickingAnimations > 1)
                issue(identity +
                      " has multiple DART sticking animations at OID 700; runtime hierarchy lookup selects one");
        }
        if (CidDerivesFrom(object.cid, 71))
        {
            validateObjectReference(887, "JLO");
            validateNonNegativeFloat(891, "JLO");
            validateNonNegativeFloat(892, "JLO");
            const auto *start = classProperty(891);
            const auto *full = classProperty(892);
            if (start && full && start->floatValue[0] < full->floatValue[0])
                issue(identity + " JLO ambient start distance is below its full-volume distance");
            const auto descendantCounts = [&](const std::int16_t oid, const std::int16_t baseCid)
            {
                std::pair<int, int> counts{};
                for (const auto &candidate : document.Objects())
                {
                    if (candidate.oid != oid)
                        continue;
                    std::int32_t ancestor = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                                   : candidate.parentIndex;
                    for (std::size_t steps = 0; ancestor >= 0 &&
                                                ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                                                steps++ < document.Objects().size();)
                    {
                        if (ancestor == static_cast<std::int32_t>(index))
                        {
                            ++counts.first;
                            if (CidDerivesFrom(candidate.cid, baseCid))
                                ++counts.second;
                            break;
                        }
                        const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t next = ancestorObject.proxyRuntimeParentIndex >= 0
                                                      ? ancestorObject.proxyRuntimeParentIndex
                                                      : ancestorObject.parentIndex;
                        if (next == ancestor)
                            break;
                        ancestor = next;
                    }
                }
                return counts;
            };
            struct JloRole
            {
                std::int16_t oid;
                std::int16_t cid;
                const char *name;
                bool required;
            };
            const JloRole roles[]{{750, 136, "state machine", true},
                                  {760, 2, "reticle appearance", false},
                                  {761, 2, "tracer appearance", false},
                                  {763, 131, "reticle-pop animation", false},
                                  {764, 131, "reticle-converge animation", false},
                                  {762, 126, "tracer-start point", false},
                                  {759, 126, "feet point", true}};
            for (const auto &role : roles)
            {
                const auto [matches, typed] = descendantCounts(role.oid, role.cid);
                if (role.required && typed == 0)
                    issue(identity + " JLO is missing required " + role.name + " OID " + std::to_string(role.oid));
                if (matches > typed)
                    issue(identity + " JLO " + role.name + " OID " + std::to_string(role.oid) + " has the wrong class");
                if (matches > 1)
                    issue(identity + " has duplicate JLO " + role.name + " OID " + std::to_string(role.oid));
            }
            const auto stateMachineObject = std::find_if(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    if (candidate.oid != 750 || !CidDerivesFrom(candidate.cid, 136))
                        return false;
                    const auto candidateIndex = static_cast<std::int32_t>(&candidate - document.Objects().data());
                    std::int32_t ancestor = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                                   : candidate.parentIndex;
                    for (std::size_t steps = 0; ancestor >= 0 &&
                                                ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                                                steps++ < document.Objects().size();)
                    {
                        if (ancestor == static_cast<std::int32_t>(index))
                            return true;
                        const auto &parent = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t next =
                            parent.proxyRuntimeParentIndex >= 0 ? parent.proxyRuntimeParentIndex : parent.parentIndex;
                        if (next == ancestor || ancestor == candidateIndex)
                            break;
                        ancestor = next;
                    }
                    return false;
                });
            if (stateMachineObject != document.Objects().end())
            {
                const auto machineIndex =
                    static_cast<std::int32_t>(std::distance(document.Objects().begin(), stateMachineObject));
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " JLO state machine OID 750 has no decoded payload");
                else
                    for (std::int16_t stateOid = 751; stateOid <= 758; ++stateOid)
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " JLO state machine OID 750 is missing runtime state OID " +
                                  std::to_string(stateOid));
            }
            if (descendantCounts(1, 140).second == 0)
                issue(identity +
                      " JLO has no RWM OID 1; firing remains unavailable and active JLOC setup can dereference it");
        }
        if (CidDerivesFrom(object.cid, 72))
        {
            const auto descendantCounts = [&](const std::int16_t oid, const std::int16_t baseCid)
            {
                std::pair<std::size_t, std::size_t> counts{};
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid)
                        continue;
                    std::int32_t ancestor = candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex
                                                                                   : candidate.parentIndex;
                    for (std::size_t steps = 0; ancestor >= 0 &&
                                                ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                                                steps++ < document.Objects().size();)
                    {
                        if (ancestor == static_cast<std::int32_t>(index))
                        {
                            ++counts.first;
                            if (CidDerivesFrom(candidate.cid, baseCid))
                                ++counts.second;
                            break;
                        }
                        const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                        const std::int32_t next = ancestorObject.proxyRuntimeParentIndex >= 0
                                                      ? ancestorObject.proxyRuntimeParentIndex
                                                      : ancestorObject.parentIndex;
                        if (next == ancestor)
                            break;
                        ancestor = next;
                    }
                }
                return counts;
            };
            for (const auto [oid, cid, role, required] :
                 {std::tuple<std::int16_t, std::int16_t, const char *, bool>{996, 80, "activation effect", true},
                  {997, 126, "aim point", true},
                  {998, 121, "projectile destination", false}})
            {
                const auto [allCount, compatibleCount] = descendantCounts(oid, cid);
                if (required && compatibleCount == 0)
                    issue(identity + " PUFFT is missing required " + role + " descendant OID " + std::to_string(oid));
                else if (!required && allCount != 0 && compatibleCount == 0)
                    issue(identity + " PUFFT optional " + role + " OID " + std::to_string(oid) +
                          " has an incompatible class");
                if (compatibleCount > 1)
                    issue(identity + " PUFFT has multiple compatible " + role + " descendants with fixed OID " +
                          std::to_string(oid) + "; hierarchy binding is ambiguous");
            }
            const auto targetCount =
                std::count_if(document.Objects().begin(), document.Objects().end(),
                              [](const BrxObjectRecord &candidate) { return CidDerivesFrom(candidate.cid, 72); });
            const bool firstTarget = std::none_of(
                document.Objects().begin(), document.Objects().begin() + static_cast<std::ptrdiff_t>(index),
                [](const BrxObjectRecord &candidate) { return CidDerivesFrom(candidate.cid, 72); });
            if (firstTarget && targetCount > 32)
                issue("PUFFT encounter has " + std::to_string(targetCount) +
                      " targets; only 32 are searchable while every target increments the completion total");
        }
        if (CidDerivesFrom(object.cid, 73))
        {
            if (const auto *depth = classProperty(1055); depth && !std::isfinite(depth->floatValue[0]))
                issue(identity + " MRKV slab depth is not finite");
            validateOrderedRange(1056, "MRKV");
            if (const auto *normal = classProperty(1054);
                normal &&
                (!std::isfinite(normal->floatValue[0]) || !std::isfinite(normal->floatValue[1]) ||
                 !std::isfinite(normal->floatValue[2]) ||
                 (normal->floatValue[0] * normal->floatValue[0] + normal->floatValue[1] * normal->floatValue[1] +
                  normal->floatValue[2] * normal->floatValue[2]) <= 0.000001f))
                issue(identity + " MRKV surface normal is invalid or zero");
        }
        if (object.cid == 74)
        {
            std::array<int, 13> animationCounts{};
            std::array<int, 13> compatibleAnimationCounts{};
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid < 1128 || candidate.oid > 1140)
                    continue;
                std::int32_t ancestor = static_cast<std::int32_t>(candidateIndex);
                bool belongsToClockwerk = false;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        belongsToClockwerk = true;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent = ancestorObject.proxyRuntimeParentIndex >= 0
                                                    ? ancestorObject.proxyRuntimeParentIndex
                                                    : ancestorObject.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
                if (!belongsToClockwerk)
                    continue;
                ++animationCounts[static_cast<std::size_t>(candidate.oid - 1128)];
                if (CidDerivesFrom(candidate.cid, 131))
                    ++compatibleAnimationCounts[static_cast<std::size_t>(candidate.oid - 1128)];
                else
                    issue(identity + " CLKWORK blend OID " + std::to_string(candidate.oid) + " is not ASEG-derived");
            }
            for (std::size_t slot = 0; slot < animationCounts.size(); ++slot)
            {
                const int oid = 1128 + static_cast<int>(slot);
                if (animationCounts[slot] > 1)
                    issue(identity + " has duplicate CLKWORK blend OID " + std::to_string(oid) +
                          "; runtime hierarchy lookup selects one");
            }
            const auto compatibleInRange = [&](const std::size_t first, const std::size_t count)
            {
                return std::any_of(compatibleAnimationCounts.begin() + static_cast<std::ptrdiff_t>(first),
                                   compatibleAnimationCounts.begin() + static_cast<std::ptrdiff_t>(first + count),
                                   [](const int value) { return value > 0; });
            };
            if (!compatibleInRange(0, 4))
                issue(
                    identity +
                    " has no phase-zero ASEG in OID range 1128-1131; retail leaves the dynamic blend null and immediately dereferences it");
            if (!compatibleInRange(4, 4))
                issue(
                    identity +
                    " has no phase-one ASEG in OID range 1132-1135; retail leaves the dynamic blend null and immediately dereferences it");
            if (!compatibleInRange(8, 5))
                issue(
                    identity +
                    " has no tracking ASEG in OID range 1136-1140; retail leaves the dynamic blend null and immediately dereferences it");
        }
        if (CidDerivesFrom(object.cid, 75))
        {
            if (const auto *state = classProperty(1149); state && (state->intValue < 0 || state->intValue > 2))
                issue(identity + " has invalid CLKWRKDMG state " + std::to_string(state->intValue));
            std::array<int, 3> damageAnimations{};
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid < 1144 || candidate.oid > 1146)
                    continue;
                std::int32_t ancestor = static_cast<std::int32_t>(candidateIndex);
                bool belongsToDamageTarget = false;
                for (std::size_t guard = 0; ancestor >= 0 &&
                                            static_cast<std::size_t>(ancestor) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestor == static_cast<std::int32_t>(index))
                    {
                        belongsToDamageTarget = true;
                        break;
                    }
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t parent = ancestorObject.proxyRuntimeParentIndex >= 0
                                                    ? ancestorObject.proxyRuntimeParentIndex
                                                    : ancestorObject.parentIndex;
                    if (parent == ancestor)
                        break;
                    ancestor = parent;
                }
                if (!belongsToDamageTarget)
                    continue;
                ++damageAnimations[static_cast<std::size_t>(candidate.oid - 1144)];
                if (!CidDerivesFrom(candidate.cid, 131))
                    issue(identity + " CLKWRKDMG animation OID " + std::to_string(candidate.oid) +
                          " is not ASEG-derived");
            }
            for (std::size_t slot = 0; slot < damageAnimations.size(); ++slot)
                if (damageAnimations[slot] > 1)
                    issue(identity + " has duplicate CLKWRKDMG animation OID " +
                          std::to_string(1144 + static_cast<int>(slot)) + "; runtime hierarchy lookup selects one");
            const bool firstDamageEncounter = std::none_of(
                document.Objects().begin(), document.Objects().begin() + static_cast<std::ptrdiff_t>(index),
                [](const BrxObjectRecord &candidate) { return CidDerivesFrom(candidate.cid, 75); });
            if (firstDamageEncounter)
            {
                const auto encounterCount =
                    std::count_if(document.Objects().begin(), document.Objects().end(),
                                  [](const BrxObjectRecord &candidate) { return CidDerivesFrom(candidate.cid, 75); });
                if (encounterCount > 1)
                    issue(
                        "World contains " + std::to_string(encounterCount) +
                        " CLKWRKDMG objects; retail shares one pending-state value and one ending-started latch across every instance");
            }
        }
        if (CidDerivesFrom(object.cid, 76))
        {
            int destructionExplosionCount = 0;
            int ownerIndex = -1;
            for (std::int32_t ancestor = object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex
                                                                             : object.parentIndex,
                              steps = 0;
                 ancestor >= 0 && ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                 steps++ <= static_cast<std::int32_t>(document.Objects().size());)
            {
                if (CidDerivesFrom(document.Objects()[static_cast<std::size_t>(ancestor)].cid, 7))
                {
                    ownerIndex = ancestor;
                    break;
                }
                const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                const std::int32_t next = ancestorObject.proxyRuntimeParentIndex >= 0
                                              ? ancestorObject.proxyRuntimeParentIndex
                                              : ancestorObject.parentIndex;
                if (next == ancestor)
                    break;
                ancestor = next;
            }
            const auto belongsTo = [&](const std::int32_t candidateIndex, const std::int32_t owner)
            {
                for (std::int32_t ancestor = candidateIndex, steps = 0;
                     ancestor >= 0 && ancestor < static_cast<std::int32_t>(document.Objects().size()) &&
                     steps++ <= static_cast<std::int32_t>(document.Objects().size());)
                {
                    if (ancestor == owner)
                        return true;
                    const auto &ancestorObject = document.Objects()[static_cast<std::size_t>(ancestor)];
                    const std::int32_t next = ancestorObject.proxyRuntimeParentIndex >= 0
                                                  ? ancestorObject.proxyRuntimeParentIndex
                                                  : ancestorObject.parentIndex;
                    if (next == ancestor)
                        break;
                    ancestor = next;
                }
                return false;
            };
            int deploymentAnimationCount = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid == 338 && CidDerivesFrom(candidate.cid, 119) &&
                    belongsTo(static_cast<std::int32_t>(candidateIndex), static_cast<std::int32_t>(index)))
                    ++destructionExplosionCount;
                if (ownerIndex >= 0 && candidate.oid == 339 && CidDerivesFrom(candidate.cid, 131) &&
                    belongsTo(static_cast<std::int32_t>(candidateIndex), ownerIndex))
                    ++deploymentAnimationCount;
            }
            if (object.oid != 337)
                issue(identity + " DECOY is not using JT's fixed binding OID 337");
            if (ownerIndex < 0)
                issue(identity + " DECOY is not inside a JT hierarchy and cannot be discovered by Sly");
            if (destructionExplosionCount == 0)
                issue(identity + " DECOY is missing required destruction EXPL descendant OID 338");
            else if (destructionExplosionCount > 1)
                issue(identity + " DECOY has multiple destruction EXPL descendants with OID 338");
            if (ownerIndex >= 0 && deploymentAnimationCount == 0)
                issue(identity + " owning JT is missing DECOY deployment ASEG OID 339");
            else if (deploymentAnimationCount > 1)
                issue(identity + " owning JT has multiple DECOY deployment ASEGs with OID 339");
            for (const auto &property : object.properties)
                if (property.eopid == 1176 || property.eopid == 1177 || property.eopid == 1178)
                    issue(identity + " illegally serializes runtime-only DECOY EOPID " +
                          std::to_string(property.eopid));
        }
        if (object.cid == 77)
        {
            issue(
                identity +
                " is an authored BLIPG, but ProjectCane creates BLIPG only as a runtime particle-group pool; author an EMITTER instead");
        }
        if (CidDerivesFrom(object.cid, 78))
        {
            validateObjectReference(602, "CAMERA");
            bool hasResolvedTarget = false;
            if (const auto *target = classProperty(602); target && target->intValue != -1)
            {
                const auto matchingTargets = findHierarchyNearestObjects(static_cast<std::int16_t>(target->intValue));
                if (matchingTargets.empty())
                    issue(identity + " CAMERA target references missing OID " + std::to_string(target->intValue));
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(matchingTargets.front())].cid, 2))
                    issue(identity + " CAMERA target OID " + std::to_string(target->intValue) +
                          " is not ALO-compatible");
                else
                    hasResolvedTarget = true;
                if (matchingTargets.size() > 1)
                    issue(identity + " CAMERA target OID " + std::to_string(target->intValue) +
                          " has multiple equally-near retail mode-4 matches");
            }
            for (const auto &property : object.properties)
                if (property.eopid == 606 || property.eopid == 607)
                    issue(identity + " illegally serializes runtime CAMERA command EOPID " +
                          std::to_string(property.eopid));
            for (const auto id : {std::int16_t{603}, std::int16_t{604}, std::int16_t{605}})
            {
                const auto *vector = classProperty(id);
                if (vector && (!std::isfinite(vector->floatValue[0]) || !std::isfinite(vector->floatValue[1]) ||
                               !std::isfinite(vector->floatValue[2])))
                    issue(identity + " CAMERA " + std::string(PropertyName(id) ? PropertyName(id) : "vector") +
                          " contains a non-finite component");
            }
            const auto *view = classProperty(604);
            const auto *up = classProperty(605);
            if (!hasResolvedTarget && view && up)
            {
                const float viewLengthSquared = view->floatValue[0] * view->floatValue[0] +
                                                view->floatValue[1] * view->floatValue[1] +
                                                view->floatValue[2] * view->floatValue[2];
                const float upLengthSquared = up->floatValue[0] * up->floatValue[0] +
                                              up->floatValue[1] * up->floatValue[1] +
                                              up->floatValue[2] * up->floatValue[2];
                if (viewLengthSquared <= 0.000001f)
                    issue(identity + " CAMERA view direction is zero");
                if (upLengthSquared <= 0.000001f)
                    issue(identity + " CAMERA up direction is zero");
                if (viewLengthSquared > 0.000001f && upLengthSquared > 0.000001f)
                {
                    const float dot = view->floatValue[0] * up->floatValue[0] +
                                      view->floatValue[1] * up->floatValue[1] + view->floatValue[2] * up->floatValue[2];
                    const float alignment = std::abs(dot) / std::sqrt(viewLengthSquared * upLengthSquared);
                    if (alignment >= 0.999f)
                        issue(identity + " CAMERA view and up directions are parallel or nearly parallel");
                }
            }
        }
        if (object.cid == 27)
        {
            for (const std::int16_t id : {std::int16_t{1145}, std::int16_t{1146}})
                if (const auto *value = classProperty(id);
                    value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] < -1.0f ||
                              value->floatValue[0] > 1.0f))
                    issue(identity + " JETPACK " + std::string(PropertyName(id)) +
                          " must be a finite signed normalized value from -1 to 1");
            struct JetpackBinding
            {
                std::int16_t oid;
                std::int16_t cid;
                bool global;
                const char *role;
            };
            constexpr JetpackBinding bindings[]{
                {1107, 131, false, "blend pose 0"},
                {1108, 131, false, "blend pose 1"},
                {1109, 131, false, "blend pose 2"},
                {1110, 131, false, "blend pose 3"},
                {1111, 131, false, "blend pose 4"},
                {1112, 131, false, "secondary animation"},
                {1113, 131, false, "charm animation"},
                {1114, 131, false, "death animation"},
                {1102, 136, false, "state machine"},
                {80, 2, false, "body"},
                {69, 2, false, "effect basis"},
                {1100, 2, false, "mounted player"},
                {1101, 2, false, "effect"},
                {354, 2, false, "charm target 2"},
                {355, 2, false, "charm target 3"},
                {352, 2, false, "charm target 0"},
                {397, 2, false, "charm effect"},
                {1115, 2, false, "left thruster"},
                {1116, 2, false, "right thruster"},
                {1125, 2, false, "left body"},
                {1126, 2, false, "right body"},
                {1117, 140, false, "weapon RWM"},
                {1121, 2, true, "target origin"},
                {1119, 2, true, "aim basis"},
                {1118, 2, true, "aim limit"},
                {1120, 2, true, "movement basis"},
            };
            const auto underJetpack = [&](std::int32_t candidate)
            {
                for (std::size_t guard = 0;
                     candidate >= 0 && static_cast<std::size_t>(candidate) < document.Objects().size() &&
                     guard++ <= document.Objects().size();
                     candidate =
                         [&]()
                     {
                         const auto &current = document.Objects()[static_cast<std::size_t>(candidate)];
                         return current.proxyRuntimeParentIndex >= 0 ? current.proxyRuntimeParentIndex
                                                                     : current.parentIndex;
                     }())
                    if (candidate == static_cast<std::int32_t>(index))
                        return true;
                return false;
            };
            std::int32_t machineIndex = -1;
            for (const JetpackBinding &binding : bindings)
            {
                std::vector<std::size_t> matching;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == binding.oid &&
                        (binding.global || underJetpack(static_cast<std::int32_t>(candidateIndex))))
                        matching.push_back(candidateIndex);
                if (matching.empty())
                    issue(identity + " is missing required JETPACK " + binding.role + " OID " +
                          std::to_string(binding.oid));
                else if (matching.size() > 1)
                    issue(identity + " has multiple " + std::string(binding.global ? "global" : "hierarchy") +
                          " objects for required JETPACK " + binding.role + " OID " + std::to_string(binding.oid));
                else if (!CidDerivesFrom(document.Objects()[matching.front()].cid, binding.cid))
                    issue(identity + " JETPACK " + binding.role + " OID " + std::to_string(binding.oid) + " is " +
                          BrxDocument::ClassName(document.Objects()[matching.front()].cid) + ", expected " +
                          BrxDocument::ClassName(binding.cid));
                else if (binding.oid == 1102)
                    machineIndex = static_cast<std::int32_t>(matching.front());
            }
            if (machineIndex >= 0)
            {
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " JETPACK state machine OID 1102 has no decoded payload");
                else
                    for (std::int16_t stateOid = 1103; stateOid <= 1105; ++stateOid)
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == stateOid; }))
                            issue(identity + " JETPACK state machine OID 1102 is missing required state OID " +
                                  std::to_string(stateOid));
            }
        }
        if (CidDerivesFrom(object.cid, 80))
        {
            for (auto property = object.properties.begin(); property != object.properties.end(); ++property)
            {
                if (property->eopid == 412)
                    continue; // Each record is one legal skeleton segment.
                if (std::any_of(object.properties.begin(), property,
                                [&](const BrxPropertyRecord &earlier) { return earlier.eopid == property->eopid; }))
                    issue(identity + " repeats EMITTER EOPID " + std::to_string(property->eopid) +
                          "; ProjectCane applies these sequentially, making the authored result order-dependent");
            }
            for (const auto runtimeOnly :
                 {std::int16_t{355}, std::int16_t{362}, std::int16_t{363}, std::int16_t{364}, std::int16_t{365}})
                if (classProperty(runtimeOnly))
                    issue(identity + " serializes runtime-only EMITTER command/query EOPID " +
                          std::to_string(runtimeOnly) + "; it has no SetUser configuration path");
            for (const auto id :
                 {std::int16_t{351}, std::int16_t{354}, std::int16_t{375}, std::int16_t{377}, std::int16_t{384},
                  std::int16_t{388}, std::int16_t{389}, std::int16_t{394}, std::int16_t{410}})
                validateOrderedRange(id, "EMITTER");
            for (const auto id :
                 {std::int16_t{352}, std::int16_t{416}, std::int16_t{417}, std::int16_t{418}, std::int16_t{419}})
                validateNonNegativeFloat(id, "EMITTER");
            if (const auto *count = classProperty(348); count && count->intValue < -1)
                issue(identity + " EMITTER particle count must be -1 or non-negative");
            if (const auto *probability = classProperty(353);
                probability && (!std::isfinite(probability->floatValue[0]) || probability->floatValue[0] < 0.0f ||
                                probability->floatValue[0] > 1.0f))
                issue(identity + " EMITTER pause probability must be between 0 and 1");
            // EOPID 421 is the EXPLO-specific origin selector.  This block also
            // validates EMITTER-derived classes, so it is only foreign data on
            // a base EMITTER; EXPLO legitimately authors it through its own
            // SetUser descriptor.
            if (object.cid == 80 && classProperty(421))
                issue(
                    identity +
                    " serializes EXPLO origin EOPID 421 on an EMITTER; ProjectCane uses EOPID 366 for EMITTER origin type");
            if (const auto *randomRadius = classProperty(386);
                randomRadius && !std::isfinite(randomRadius->floatValue[0]))
                issue(identity + " EMITTER angular-randomness scale must be finite");
            for (const auto id : {std::int16_t{403}, std::int16_t{408}})
                if (const auto *signedScalar = classProperty(id);
                    signedScalar && !std::isfinite(signedScalar->floatValue[0]))
                    issue(identity + " EMITTER " + std::string(PropertyName(id) ? PropertyName(id) : "BLIP scalar") +
                          " must be finite");
            // rSvz (378) is a signed multiplier: negative values intentionally
            // reverse the velocity component along the emission normal.
            if (const auto *forwardRatio = classProperty(378);
                forwardRatio && !std::isfinite(forwardRatio->floatValue[0]))
                issue(identity + " EMITTER forward-speed ratio must be finite");
            // RIP expansion (397) is also signed. Negative values contract the
            // conservative particle bounds and must not be rejected as invalid.
            if (const auto *expansion = classProperty(397); expansion && !std::isfinite(expansion->floatValue[0]))
                issue(identity + " EMITTER RIP expansion must be finite");
            for (const auto id : {std::int16_t{380}, std::int16_t{381}})
                if (const auto *scalar = classProperty(id); scalar && !std::isfinite(scalar->floatValue[0]))
                    issue(identity + " EMITTER " + std::string(PropertyName(id) ? PropertyName(id) : "motion scalar") +
                          " must be finite");
            if (const auto *ringCount = classProperty(385); ringCount && ringCount->intValue < 0)
                issue(identity + " EMITTER particles per ring must be zero (whole batch) or positive");
            if (const auto *lifetime = classProperty(387);
                lifetime && (!std::isfinite(lifetime->floatValue[0]) || lifetime->floatValue[0] <= 0.0f))
                issue(identity + " EMITTER particle lifetime must be finite and greater than zero");
            for (const auto id : {std::int16_t{367}, std::int16_t{368}, std::int16_t{369}, std::int16_t{372},
                                  std::int16_t{373}, std::int16_t{374}, std::int16_t{379}, std::int16_t{382}})
            {
                const auto *value = classProperty(id);
                if (value && (!std::isfinite(value->floatValue[0]) || !std::isfinite(value->floatValue[1]) ||
                              !std::isfinite(value->floatValue[2])))
                    issue(identity + " EMITTER " + std::string(PropertyName(id) ? PropertyName(id) : "vector") +
                          " contains a non-finite component");
            }
            for (const auto id :
                 {std::int16_t{390}, std::int16_t{395}, std::int16_t{399}, std::int16_t{400}, std::int16_t{401}})
            {
                const auto *value = classProperty(id);
                if (value && (!std::isfinite(value->floatValue[0]) || !std::isfinite(value->floatValue[1]) ||
                              !std::isfinite(value->floatValue[2])))
                    issue(identity + " EMITTER " + std::string(PropertyName(id) ? PropertyName(id) : "curve") +
                          " contains a non-finite coefficient");
            }
            int emissionMode = classProperty(350) ? classProperty(350)->intValue : -1;
            if (emissionMode == -1)
            {
                const float pauseProbability = classProperty(353) ? classProperty(353)->floatValue[0] : 0.0f;
                const bool autoPause = classProperty(349) && classProperty(349)->boolValue;
                const float constantCount = classProperty(352) ? classProperty(352)->floatValue[0] : 0.0f;
                emissionMode = pauseProbability == 0.0f && !autoPause ? (constantCount == 0.0f ? 0 : 1) : 3;
            }
            const auto emitterEffect =
                std::find_if(document.Effects().begin(), document.Effects().end(), [&](const BrxEffectRecord &candidate)
                             { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            const int originKind = classProperty(366)
                                       ? classProperty(366)->intValue
                                       : (emitterEffect != document.Effects().end() ? emitterEffect->originKind : 0);
            const int directionKind = classProperty(371) ? classProperty(371)->intValue : 0;
            const int ripType = classProperty(392) ? classProperty(392)->intValue : -1;
            const int representation = ripType != -1 ? 1 : (classProperty(391) ? classProperty(391)->intValue : 0);
            const int motionType = classProperty(405) ? classProperty(405)->intValue : 0;
            const int movementType = classProperty(407) ? classProperty(407)->intValue : 0;
            const int velocityFrame = classProperty(376) ? classProperty(376)->intValue : 0;
            const int curlFrame = classProperty(383) ? classProperty(383)->intValue : 0;
            const int ripGroupType = classProperty(396) ? classProperty(396)->intValue : 0;
            const int rollType = classProperty(409) ? classProperty(409)->intValue : -1;
            if (emissionMode < -1 || emissionMode > 3)
                issue(identity + " has invalid EMITTER emission mode " + std::to_string(emissionMode));
            if (originKind < -1 || originKind > 4)
                issue(identity + " has invalid EMITTER origin type " + std::to_string(originKind));
            if (directionKind < -1 || directionKind > 5)
                issue(identity + " has invalid EMITTER direction type " + std::to_string(directionKind));
            if (representation < -1 || representation > 1)
                issue(identity + " has invalid EMITTER representation " + std::to_string(representation));
            if (ripType < -1 || ripType > 27)
                issue(identity + " has invalid EMITTER RIP type " + std::to_string(ripType));
            const int trailType = classProperty(393) ? classProperty(393)->intValue : -1;
            if (trailType != -1 && trailType != 10 && trailType != 13)
                issue(identity + " has unsafe EMITTER trail RIP type " + std::to_string(trailType) +
                      "; ProjectCane treats the child as TRAIL, so use Trail (10) or Smoke trail (13)");
            if (representation == 1 && ripType == 1)
                issue(identity +
                      " selects Shadow RIP, but ProjectCane's current RIP pool has no Shadow allocator case");
            if (representation == 1 && ripType == 24)
                if (const auto *radius = classProperty(417); radius && radius->floatValue[0] == 0.0f)
                    issue(identity + " uses zero-radius shrapnel; collision and model offset will be degenerate");
            if (motionType < -1 || motionType > 2)
                issue(identity + " has invalid EMITTER BLIP motion type " + std::to_string(motionType));
            if (movementType < -1 || movementType > 2)
                issue(identity + " has invalid EMITTER movement type " + std::to_string(movementType));
            if (velocityFrame < -1 || velocityFrame > 3)
                issue(identity + " has invalid EMITTER velocity frame " + std::to_string(velocityFrame));
            if (curlFrame < -1 || curlFrame > 1)
                issue(identity + " has invalid EMITTER curl frame " + std::to_string(curlFrame));
            if (ripGroupType < -1 || ripGroupType > 2)
                issue(identity + " has invalid EMITTER RIP group type " + std::to_string(ripGroupType));
            if (rollType < -1 || rollType > 1)
                issue(identity + " has invalid EMITTER rolling mirror mode " + std::to_string(rollType));
            if ((directionKind == 2 || directionKind == 3) && originKind != 2 && originKind != 3)
                issue(identity + " uses a curve/skeleton direction mode with an incompatible origin type");
            if (directionKind == 5 && originKind != 4)
                issue(identity + " uses mesh-normal direction without a mesh origin");
            if (originKind == 1)
            {
                const auto *boxMin = classProperty(368);
                const auto *boxMax = classProperty(369);
                if (boxMin && boxMax)
                    for (int axis = 0; axis < 3; ++axis)
                        if (boxMin->floatValue[axis] > boxMax->floatValue[axis])
                        {
                            issue(identity + " EMITTER box minimum exceeds its maximum on axis " +
                                  std::to_string(axis));
                            break;
                        }
            }
            if (directionKind == 0)
                if (const auto *direction = classProperty(372);
                    direction && direction->floatValue[0] * direction->floatValue[0] +
                                         direction->floatValue[1] * direction->floatValue[1] +
                                         direction->floatValue[2] * direction->floatValue[2] <=
                                     0.000001f)
                    issue(identity + " EMITTER normal direction vector must not be zero");
            if (directionKind == 3)
            {
                const auto *direction = classProperty(374);
                if (direction && direction->floatValue[0] * direction->floatValue[0] +
                                         direction->floatValue[1] * direction->floatValue[1] +
                                         direction->floatValue[2] * direction->floatValue[2] <=
                                     0.000001f)
                    issue(identity + " EMITTER curve-normal bias vector must not be zero");
            }
            if (const auto *shader = classProperty(398);
                shader && shader->intValue != -1 &&
                std::none_of(document.Shaders().begin(), document.Shaders().end(),
                             [&](const BrxShaderRecord &candidate) { return candidate.oid == shader->intValue; }))
                issue(identity + " EMITTER particle shader references missing shader OID " +
                      std::to_string(shader->intValue));
            if (representation != 1 && !classProperty(398) &&
                std::none_of(document.Shaders().begin(), document.Shaders().end(),
                             [](const BrxShaderRecord &candidate) { return candidate.oid == 0; }))
                issue(identity + " relies on default particle shader OID 0, but that shader is missing");
            if (representation != 1)
            {
                const std::int16_t shaderOid =
                    static_cast<std::int16_t>(classProperty(398) ? classProperty(398)->intValue : 0);
                auto particleShader =
                    std::find_if(document.Shaders().begin(), document.Shaders().end(),
                                 [&](const BrxShaderRecord &candidate) { return candidate.oid == shaderOid; });
                if (particleShader == document.Shaders().end())
                    particleShader = std::find_if(document.Shaders().begin(), document.Shaders().end(),
                                                  [](const BrxShaderRecord &candidate) { return candidate.oid == 0; });
                if (particleShader != document.Shaders().end())
                {
                    const auto usable = [&](const std::vector<std::int32_t> &bitmaps)
                    {
                        return std::any_of(
                            bitmaps.begin(), bitmaps.end(), [&](const std::int32_t bitmap)
                            { return bitmap >= 0 && static_cast<std::size_t>(bitmap) < document.Bitmaps().size(); });
                    };
                    // DrawBlipg binds CTK_Ambient for ThreeWay shaders and
                    // CTK_Diffuse for every other shader kind. It does not
                    // substitute another material role when that texture is
                    // unavailable.
                    const bool hasTexture = particleShader->kind == 0 ? usable(particleShader->ambientBitmapIndices)
                                                                      : usable(particleShader->diffuseBitmapIndices);
                    if (!hasTexture)
                        issue(identity +
                              " particle shader has no usable bitmap; ProjectCane will not draw its BLIP group");
                }
            }
            if (representation != 1 && motionType == 2 && (!classProperty(406) || classProperty(406)->intValue < 0))
                issue(identity + " selects spline motion but has no target EXPLO OID");
            if (classProperty(413) && classProperty(420))
                issue(identity + " authors both aliases of the EMITTER damage flag (413 and 420); keep only one");
            const auto contextualReference = [&](const std::int16_t propertyId)
            {
                const auto *property = classProperty(propertyId);
                std::pair<const BrxObjectRecord *, std::size_t> result{};
                if (!property || property->intValue < 0)
                    return result;
                const auto matches = findHierarchyNearestObjects(static_cast<std::int16_t>(property->intValue));
                if (!matches.empty())
                    result = {&document.Objects()[static_cast<std::size_t>(matches.front())], matches.size()};
                return result;
            };
            const auto referencedObject = [&](const std::int16_t propertyId) -> const BrxObjectRecord *
            { return contextualReference(propertyId).first; };
            const auto requireReference =
                [&](const std::int16_t propertyId, const std::int16_t requiredCid, const char *role)
            {
                const auto *property = classProperty(propertyId);
                if (!property || property->intValue < 0)
                    return;
                const auto [target, matchCount] = contextualReference(propertyId);
                if (!target)
                    issue(identity + " references missing " + role + " OID " + std::to_string(property->intValue));
                else if (!CidDerivesFrom(target->cid, requiredCid))
                    issue(identity + " " + role + " OID " + std::to_string(property->intValue) +
                          " has incompatible class " + BrxDocument::ClassName(target->cid));
                if (matchCount > 1)
                    issue(identity + " " + role + " OID " + std::to_string(property->intValue) +
                          " is ambiguous in the EMITTER's hierarchy/proxy/splice context");
            };
            requireReference(357, 2, "reference-space object");
            requireReference(358, 2, "render object");
            requireReference(359, 3, "touch object");
            requireReference(360, 2, "next render object");
            requireReference(370, 110, "origin SHAPE");
            requireReference(406, 121, "spline-target EXPLO");
            requireReference(415, 122, "nested EXPLS");
            if (const auto *group = classProperty(361); group && group->intValue >= 0)
            {
                if (group->intValue == object.oid)
                    issue(identity + " uses its own OID as its EMITTER group master OID");
                const auto collision = std::find_if(
                    document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                    { return &candidate != &object && candidate.oid == group->intValue; });
                if (collision != document.Objects().end())
                    issue(identity + " EMITTER group OID " + std::to_string(group->intValue) +
                          " collides with authored " + BrxDocument::ClassName(collision->cid) +
                          "; ProjectCane reserves that OID for the synthetic group master");
            }
            if (representation == 1 && (!classProperty(358) || classProperty(358)->intValue < 0))
            {
                int stockOid = -1;
                switch (ripType)
                {
                case 2:
                case 15:
                    stockOid = 13;
                    break;
                case 4:
                    stockOid = 14;
                    break;
                case 5:
                    stockOid = 15;
                    break;
                case 8:
                    stockOid = 18;
                    break;
                case 10:
                    stockOid = 17;
                    break;
                case 12:
                    stockOid = 20;
                    break;
                case 13:
                    stockOid = 21;
                    break;
                default:
                    break;
                }
                if (stockOid >= 0 &&
                    std::none_of(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate)
                                 { return candidate.oid == stockOid && CidDerivesFrom(candidate.cid, 2); }))
                    issue(identity + " relies on missing stock RIP render object OID " + std::to_string(stockOid));
            }
            if (originKind == 2 && classProperty(370))
            {
                const auto *target = referencedObject(370);
                if (target)
                {
                    const auto targetIndex = static_cast<std::int32_t>(target - document.Objects().data());
                    const auto path = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                                                   [&](const BrxShapePathRecord &candidate)
                                                   { return candidate.ownerObjectIndex == targetIndex; });
                    if (path == document.ShapePaths().end() || path->points.size() < 2)
                        issue(identity + " origin SHAPE has no usable curve with at least two points");
                }
            }
            if (emitterEffect != document.Effects().end())
            {
                if (emitterEffect->curveKind < -1 || emitterEffect->curveKind > 1)
                    issue(identity + " has an invalid inline emitter curve kind " +
                          std::to_string(emitterEffect->curveKind));
                if (originKind != 2 && (!emitterEffect->curvePoints.empty() || emitterEffect->curveKind != -1))
                    issue(identity + " carries an inline curve payload that is inactive for the selected origin type");
                if (originKind != 4 && (!emitterEffect->meshPositions.empty() || !emitterEffect->meshTriangles.empty()))
                    issue(identity + " carries an inline mesh payload that is inactive for the selected origin type");
                if (representation == 1 && !emitterEffect->colors.empty())
                    issue(identity + " carries a BLIP color table while using RIP model particles");
                if (originKind == 2 && !classProperty(370))
                {
                    if (emitterEffect->curvePoints.size() < 2)
                        issue(identity + " inline emitter curve needs at least two control points");
                    if (emitterEffect->curvePoints.size() > 255)
                        issue(identity + " inline emitter curve exceeds the BRX 8-bit control-point limit");
                    float previous = -(std::numeric_limits<float>::max)();
                    for (const auto &point : emitterEffect->curvePoints)
                    {
                        const bool finitePoint =
                            std::isfinite(point.position[0]) && std::isfinite(point.position[1]) &&
                            std::isfinite(point.position[2]) && std::isfinite(point.tangentIn[0]) &&
                            std::isfinite(point.tangentIn[1]) && std::isfinite(point.tangentIn[2]) &&
                            std::isfinite(point.tangentOut[0]) && std::isfinite(point.tangentOut[1]) &&
                            std::isfinite(point.tangentOut[2]);
                        if (!std::isfinite(point.u) || point.u <= previous || !finitePoint)
                        {
                            issue(
                                identity +
                                " inline emitter curve points/tangents must be finite and parameters strictly increasing");
                            break;
                        }
                        previous = point.u;
                    }
                }
                if (originKind == 4)
                {
                    if (emitterEffect->meshPositions.size() > 65535)
                        issue(identity + " emitter mesh exceeds the 16-bit vertex-index limit");
                    if (emitterEffect->meshTriangles.size() > 65535)
                        issue(identity + " emitter mesh exceeds the BRX 16-bit triangle-count limit");
                    float totalArea = 0.0f;
                    for (const auto &triangle : emitterEffect->meshTriangles)
                    {
                        if (triangle.indices[0] >= emitterEffect->meshPositions.size() ||
                            triangle.indices[1] >= emitterEffect->meshPositions.size() ||
                            triangle.indices[2] >= emitterEffect->meshPositions.size())
                        {
                            issue(identity + " emitter mesh contains an out-of-range triangle index");
                            break;
                        }
                        if (!std::isfinite(triangle.area) || triangle.area <= 0.0f)
                        {
                            issue(identity + " emitter mesh triangle areas must be finite and positive");
                            break;
                        }
                        if (triangle.indices[0] == triangle.indices[1] || triangle.indices[1] == triangle.indices[2] ||
                            triangle.indices[0] == triangle.indices[2])
                        {
                            issue(identity + " emitter mesh contains a triangle with repeated vertices");
                            break;
                        }
                        totalArea += triangle.area;
                    }
                    for (const auto &position : emitterEffect->meshPositions)
                        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
                        {
                            issue(identity + " emitter mesh contains a non-finite vertex");
                            break;
                        }
                    if (emitterEffect->meshTriangles.empty() || totalArea <= 0.0f)
                        issue(identity + " mesh-origin emitter has no sampleable triangle area");
                    if (!std::isfinite(totalArea))
                        issue(identity + " emitter mesh total sampling area is not finite");
                    if (!std::isfinite(emitterEffect->meshCenter[0]) || !std::isfinite(emitterEffect->meshCenter[1]) ||
                        !std::isfinite(emitterEffect->meshCenter[2]))
                        issue(identity + " emitter mesh center contains a non-finite component");
                }
                if (emitterEffect->colors.size() > 32)
                    issue(identity + " has more than the runtime maximum of 32 particle colors");
                if (emitterEffect->colorsAreRanges && (emitterEffect->colors.size() & 1u) != 0)
                    issue(identity + " particle color ranges require start/end color pairs");
                for (const auto &color : emitterEffect->colors)
                    if (!std::isfinite(color[0]) || !std::isfinite(color[1]) || !std::isfinite(color[2]) ||
                        !std::isfinite(color[3]))
                    {
                        issue(identity + " particle color table contains a non-finite component");
                        break;
                    }
            }
            else if (originKind == 2 && !classProperty(370))
                issue(identity + " selects an inline curve origin but has no decoded effect curve payload");
            else if (originKind == 4)
                issue(identity + " selects a mesh origin but has no decoded effect mesh payload");
            if (originKind == 3)
            {
                std::size_t skeletonCount = 0;
                for (const auto &skeleton : object.properties)
                {
                    if (skeleton.eopid != 412)
                        continue;
                    ++skeletonCount;
                    if (skeleton.arguments.size() != 6)
                    {
                        issue(identity + " skeleton source must contain exactly six arguments");
                        continue;
                    }
                    for (const std::size_t argument : {std::size_t{0}, std::size_t{1}})
                    {
                        const auto oid = skeleton.arguments[argument].intValue;
                        const auto targets = findHierarchyNearestObjects(static_cast<std::int16_t>(oid));
                        if (targets.empty())
                            issue(identity + " skeleton endpoint references missing ALO OID " + std::to_string(oid));
                        else
                        {
                            if (targets.size() != 1)
                                issue(identity + " skeleton endpoint OID " + std::to_string(oid) +
                                      " has multiple equally-near matches");
                            const auto &target = document.Objects()[static_cast<std::size_t>(targets.front())];
                            if (!CidDerivesFrom(target.cid, 2))
                                issue(identity + " skeleton endpoint OID " + std::to_string(oid) +
                                      " does not resolve to an ALO-derived object");
                        }
                    }
                    if (skeleton.arguments[2].floatValue[0] < 0.0f || skeleton.arguments[3].floatValue[0] < 0.0f ||
                        skeleton.arguments[4].floatValue[0] < -1.0f || skeleton.arguments[5].floatValue[0] < -1.0f)
                        issue(identity +
                              " skeleton radii/densities must be non-negative or -1 for inherited end values");
                    for (const std::size_t argument : {std::size_t{2}, std::size_t{3}, std::size_t{4}, std::size_t{5}})
                        if (!std::isfinite(skeleton.arguments[argument].floatValue[0]))
                        {
                            issue(identity + " skeleton source contains a non-finite radius/density");
                            break;
                        }
                }
                if (skeletonCount == 0)
                    issue(identity + " selects a skeleton origin but has no skeleton source segments");
                if (skeletonCount > 32)
                    issue(identity + " has more than the runtime maximum of 32 skeleton segments");
            }
            const auto inactive = [&](const std::int16_t id, const bool active, const char *context)
            {
                if (!active && classProperty(id))
                {
                    const char *propertyName = PropertyNameForCid(80, id);
                    const std::string safeName =
                        propertyName ? std::string(propertyName) : "property " + std::to_string(id);
                    issue(identity + " authors inactive EMITTER " + safeName + " for " + context);
                }
            };
            inactive(352, emissionMode == 1, "the selected emission mode");
            inactive(356, originKind == 1, "the selected origin type");
            inactive(367, originKind == 0, "the selected origin type");
            inactive(368, originKind == 1, "the selected origin type");
            inactive(369, originKind == 1, "the selected origin type");
            inactive(370, originKind == 2, "the selected origin type");
            inactive(372, directionKind == 0, "the selected direction type");
            inactive(373, directionKind == 1, "the selected direction type");
            inactive(374, directionKind == 2 || directionKind == 3, "the selected direction type");
            for (const auto id : {std::int16_t{392}, std::int16_t{393}, std::int16_t{394}, std::int16_t{395},
                                  std::int16_t{396}, std::int16_t{397}})
                inactive(id, representation == 1, "the selected particle representation");
            for (const auto id :
                 {std::int16_t{398}, std::int16_t{399}, std::int16_t{400}, std::int16_t{401}, std::int16_t{402},
                  std::int16_t{403}, std::int16_t{404}, std::int16_t{405}, std::int16_t{406}, std::int16_t{407},
                  std::int16_t{408}, std::int16_t{409}, std::int16_t{410}, std::int16_t{411}})
                inactive(id, representation != 1, "the selected particle representation");
            inactive(406, representation != 1 && motionType == 2, "the selected BLIP motion model");
            inactive(408, representation != 1 && movementType == 1, "the selected BLIP movement behavior");
            inactive(409, representation != 1 && movementType == 2, "the selected BLIP movement behavior");
            inactive(410, representation != 1 && movementType == 2, "the selected BLIP movement behavior");
            inactive(413, representation == 1 && ripType == 23, "the selected RIP behavior");
            inactive(414, representation == 1 && ripType == 23, "the selected RIP behavior");
            inactive(420, representation == 1 && ripType == 23, "the selected RIP behavior");
            inactive(360, representation == 1 && ripType == 19, "the selected RIP behavior");
            inactive(415, representation == 1 && ripType == 27, "the selected RIP behavior");
            inactive(416, representation == 1 && ripType == 27, "the selected RIP behavior");
            inactive(417, representation == 1 && ripType == 24, "the selected RIP behavior");
            inactive(418, representation == 1 && ripType == 24, "the selected RIP behavior");
            inactive(419, representation == 1 && ripType == 24, "the selected RIP behavior");
        }
        if (object.cid == 81)
        {
            const auto *kind = classProperty(491);
            const int lightKind = kind != nullptr ? kind->intValue : 0;
            if (kind && (kind->intValue < -1 || kind->intValue > 3))
                issue(identity + " has invalid LIGHT type " + std::to_string(kind->intValue));
            if (classProperty(499) != nullptr && classProperty(500) != nullptr)
                issue(identity + " authors both LIGHT dynamic aliases (499 and 500); keep only one");
            const auto *highlight = classProperty(492);
            if (highlight && (!std::isfinite(highlight->floatValue[0]) || !std::isfinite(highlight->floatValue[1]) ||
                              !std::isfinite(highlight->floatValue[2]) || highlight->floatValue[0] < 0.0f ||
                              highlight->floatValue[0] > 360.0f || highlight->floatValue[1] < 0.0f ||
                              highlight->floatValue[1] > 255.0f || highlight->floatValue[2] < 0.0f ||
                              highlight->floatValue[2] > 255.0f))
                issue(identity + " LIGHT highlight HSV must use H 0..360 and S/V 0..255");
            for (const auto id : {493, 494})
                if (const auto *value = classProperty(static_cast<std::int16_t>(id));
                    value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] < 0.0f ||
                              value->floatValue[0] > 255.0f))
                    issue(identity + " LIGHT tonal strength EOPID " + std::to_string(id) + " must be 0..255");
            for (const auto id : {495, 496, 497})
                if (const auto *value = classProperty(static_cast<std::int16_t>(id));
                    value && (!std::isfinite(value->floatValue[0]) || value->floatValue[0] < 0.0f ||
                              value->floatValue[0] > 360.0f))
                    issue(identity + " LIGHT response angle EOPID " + std::to_string(id) + " must be 0..360 degrees");
            const auto *direction = classProperty(498);
            if (direction && (!std::isfinite(direction->floatValue[0]) || !std::isfinite(direction->floatValue[1]) ||
                              !std::isfinite(direction->floatValue[2])))
                issue(identity + " LIGHT legacy authored direction contains a non-finite component");
            validateOrderedRange(502, "LIGHT");
            if (const auto *falloff = classProperty(502); falloff && falloff->floatValue[0] < 0.0f)
                issue(identity + " LIGHT falloff distances must be non-negative");
            if (const auto *falloff = classProperty(502);
                lightKind >= 2 && falloff &&
                (!std::isfinite(falloff->floatValue[1]) || falloff->floatValue[1] <= 50.0f))
                issue(identity +
                      " projected LIGHT maximum distance must exceed ProjectCane's fixed 50-unit near plane");
            const auto *cone = classProperty(503);
            const auto *hotSpot = classProperty(504);
            if (cone &&
                (!std::isfinite(cone->floatValue[0]) || cone->floatValue[0] <= 0.0f || cone->floatValue[0] > 175.0f))
                issue(identity +
                      " LIGHT cone angle must be greater than 0 and no more than ProjectCane's 175-degree limit");
            if (hotSpot && (!std::isfinite(hotSpot->floatValue[0]) || hotSpot->floatValue[0] < 0.0f ||
                            hotSpot->floatValue[0] > 175.0f))
                issue(identity + " LIGHT hotspot angle must be between 0 and 175 degrees");
            if (cone && hotSpot && hotSpot->floatValue[0] > cone->floatValue[0])
                issue(identity + " LIGHT hotspot angle exceeds its cone angle");
            const auto *up = classProperty(505);
            if (up && (!std::isfinite(up->floatValue[0]) || !std::isfinite(up->floatValue[1]) ||
                       !std::isfinite(up->floatValue[2]) ||
                       up->floatValue[0] * up->floatValue[0] + up->floatValue[1] * up->floatValue[1] +
                               up->floatValue[2] * up->floatValue[2] <=
                           0.000001f))
                issue(identity + " LIGHT frustum up vector is invalid or zero");
            // ProjectCane preserves EOPID 498 in vecDirectionOrig but does not
            // use it to build the submitted light direction. Directional and
            // projected lights use the object's transformed local +Z axis;
            // projected-light basis construction also supplies a safe
            // alternate axis when vecUpLocal is parallel. Do not diagnose the
            // dormant 498 value as if it formed a direction/up basis.
            if (lightKind < 1 && classProperty(502) != nullptr)
                issue(identity +
                      " serializes LIGHT falloff, but the selected light type does not use distance falloff");
            if (lightKind < 2 && (cone != nullptr || hotSpot != nullptr || up != nullptr))
                issue(identity + " serializes projected-light controls that are inactive for the selected light type");
        }
        if (object.cid == 84)
        {
            if (!object.hasAloPayload)
                issue(
                    identity +
                    " has no ALO payload; SKY rendering and camera-follow translation require the inherited ALO state");
            const std::int32_t skyRuntimeParent =
                object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
            if (skyRuntimeParent >= 0)
                issue(
                    identity +
                    " is parented; ProjectCane writes the camera position through TranslateAloToPos, so SKY should normally be a root");
            std::size_t followCount = 0;
            for (const auto &property : object.properties)
                if (property.eopid == 608)
                {
                    ++followCount;
                    if (property.kind != BrxPropertyKind::Bool)
                        issue(identity + " stores Follow camera Z (EOPID 608) with a non-boolean value type");
                }
            if (followCount > 1)
                issue(identity + " repeats Follow camera Z (EOPID 608); only the final serialized setter is effective");
            const auto belongsToSky = [&](std::int32_t owner)
            {
                std::size_t guard = 0;
                while (owner >= 0 && static_cast<std::size_t>(owner) < document.Objects().size() &&
                       guard++ <= document.Objects().size())
                {
                    if (owner == static_cast<std::int32_t>(index))
                        return true;
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(owner)];
                    const auto next =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (next == owner)
                        break;
                    owner = next;
                }
                return false;
            };
            bool hasGeometry = false;
            bool hasBackgroundGeometry = false;
            for (const auto &batch : document.RenderBatches())
                if (belongsToSky(batch.ownerObjectIndex))
                {
                    hasGeometry = true;
                    if (batch.renderPhase == 1)
                        hasBackgroundGeometry = true;
                }
            if (!hasGeometry)
                issue(identity + " has no renderable geometry in its hierarchy");
            else if (!hasBackgroundGeometry)
                issue(identity + " has geometry, but none of it uses ProjectCane's RP_Background render phase");
            const float sx = std::sqrt(object.worldTransform[0] * object.worldTransform[0] +
                                       object.worldTransform[1] * object.worldTransform[1] +
                                       object.worldTransform[2] * object.worldTransform[2]);
            const float sy = std::sqrt(object.worldTransform[4] * object.worldTransform[4] +
                                       object.worldTransform[5] * object.worldTransform[5] +
                                       object.worldTransform[6] * object.worldTransform[6]);
            const float sz = std::sqrt(object.worldTransform[8] * object.worldTransform[8] +
                                       object.worldTransform[9] * object.worldTransform[9] +
                                       object.worldTransform[10] * object.worldTransform[10]);
            if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || sx <= 0.0001f || sy <= 0.0001f ||
                sz <= 0.0001f)
                issue(identity + " has a non-finite or near-zero transform scale");
        }
        if (CidDerivesFrom(object.cid, 85))
        {
            if (const auto *initialState = classProperty(979); initialState && initialState->intValue < -1)
                issue(identity + " DPRIZE initial animation-state OID must be -1 or a valid non-negative OID");
            if (classProperty(981) != nullptr)
                issue(identity + " serializes runtime-owned DPRIZE state EOPID 981, which has no SetUser loader path");

            const auto belongsToPrize = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == static_cast<std::int32_t>(index))
                        return true;
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            struct PrizeRole
            {
                std::int32_t oid;
                std::int16_t cid;
                const char *name;
                bool required;
            };
            static constexpr PrizeRole prizeRoles[]{{607, 136, "animation state machine", true},
                                                    {603, 126, "front glint point", false},
                                                    {604, 126, "back glint point", false},
                                                    {605, 121, "attraction effect", false},
                                                    {606, 121, "collection effect", false}};
            for (const auto &role : prizeRoles)
            {
                int count = 0;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != role.oid || !belongsToPrize(static_cast<std::int32_t>(candidateIndex)))
                        continue;
                    ++count;
                    if (!CidDerivesFrom(candidate.cid, role.cid))
                        issue(identity + " DPRIZE " + role.name + " OID " + std::to_string(role.oid) + " has type " +
                              BrxDocument::ClassName(candidate.cid) + " instead of the required class");
                }
                if (role.required && count == 0)
                    issue(identity + " is missing required DPRIZE " + role.name + " OID " + std::to_string(role.oid) +
                          "; ProjectCane dereferences it when applying collectible animation states");
                else if (count > 1)
                    issue(identity + " has multiple DPRIZE " + role.name + " objects at OID " +
                          std::to_string(role.oid) + "; runtime contextual lookup selects only one");
            }
        }
        if (object.cid == 90 && classProperty(987) != nullptr)
            issue(identity + " serializes GOLD runtime trigger EOPID 987, which has no SetUser loader path");
        if (object.cid == 92)
        {
            const auto belongsToLock = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == static_cast<std::int32_t>(index))
                        return true;
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            struct LockRole
            {
                std::int32_t oid;
                std::int16_t cid;
                const char *name;
            };
            static constexpr LockRole lockRoles[]{{555, 136, "state machine"}, {557, 2, "key visual"}};
            for (const auto &role : lockRoles)
            {
                int count = 0;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != role.oid || !belongsToLock(static_cast<std::int32_t>(candidateIndex)))
                        continue;
                    ++count;
                    if (!CidDerivesFrom(candidate.cid, role.cid))
                        issue(identity + " LOCK " + role.name + " OID " + std::to_string(role.oid) + " has type " +
                              BrxDocument::ClassName(candidate.cid) + " instead of the required class");
                }
                if (count == 0)
                    issue(identity + " is missing required LOCK " + role.name + " OID " + std::to_string(role.oid));
                else if (count > 1)
                    issue(identity + " has multiple LOCK " + role.name + " objects at OID " + std::to_string(role.oid) +
                          "; runtime contextual lookup selects only one");
            }
            const auto machineObject = std::find_if(
                document.Objects().begin(), document.Objects().end(),
                [&](const BrxObjectRecord &candidate)
                {
                    const auto candidateIndex = static_cast<std::int32_t>(&candidate - document.Objects().data());
                    return candidate.oid == 555 && CidDerivesFrom(candidate.cid, 136) && belongsToLock(candidateIndex);
                });
            if (machineObject != document.Objects().end())
            {
                const auto machineIndex =
                    static_cast<std::int32_t>(std::distance(document.Objects().begin(), machineObject));
                const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                  [&](const BrxSmRecord &candidate)
                                                  { return candidate.ownerObjectIndex == machineIndex; });
                if (machine == document.StateMachines().end())
                    issue(identity + " LOCK state-machine object OID 555 has no decoded state-machine payload");
                else
                {
                    // PostLockgLoad drives every member LOCK to 558 or 559.
                    // IsmsFindSmRequired silently falls back to slot zero when a
                    // requested state is absent, producing the wrong animation.
                    for (const std::int16_t requiredState : {std::int16_t{558}, std::int16_t{559}})
                        if (std::none_of(machine->states.begin(), machine->states.end(),
                                         [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                            issue(identity + " LOCK state machine OID 555 is missing required state OID " +
                                  std::to_string(requiredState));
                }
            }
        }
        if (object.cid == 93)
        {
            const auto belongsToLockGroup = [&](std::int32_t candidateIndex)
            {
                for (std::size_t guard = 0; candidateIndex >= 0 &&
                                            static_cast<std::size_t>(candidateIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (candidateIndex == static_cast<std::int32_t>(index))
                        return true;
                    const auto &candidate = document.Objects()[static_cast<std::size_t>(candidateIndex)];
                    const std::int32_t parent = candidate.proxyRuntimeParentIndex >= 0
                                                    ? candidate.proxyRuntimeParentIndex
                                                    : candidate.parentIndex;
                    if (parent == candidateIndex)
                        break;
                    candidateIndex = parent;
                }
                return false;
            };
            int stateMachineCount = 0;
            std::size_t childLockCount = 0;
            std::vector<std::int32_t> childLockOids;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (!belongsToLockGroup(static_cast<std::int32_t>(candidateIndex)))
                    continue;
                if (candidate.oid == 556)
                {
                    ++stateMachineCount;
                    if (!CidDerivesFrom(candidate.cid, 136))
                        issue(identity + " LOCKG state machine OID 556 is not SM-derived");
                }
                if (candidateIndex != index && CidDerivesFrom(candidate.cid, 92))
                {
                    ++childLockCount;
                    childLockOids.push_back(candidate.oid);
                }
            }
            if (stateMachineCount == 0)
                issue(identity + " is missing required LOCKG state machine OID 556");
            else if (stateMachineCount > 1)
                issue(identity +
                      " has multiple LOCKG state machines at OID 556; runtime contextual lookup selects only one");
            else
            {
                const auto machineObject =
                    std::find_if(document.Objects().begin(), document.Objects().end(),
                                 [&](const BrxObjectRecord &candidate)
                                 {
                                     const auto candidateIndex =
                                         static_cast<std::int32_t>(&candidate - document.Objects().data());
                                     return candidate.oid == 556 && CidDerivesFrom(candidate.cid, 136) &&
                                            belongsToLockGroup(candidateIndex);
                                 });
                if (machineObject != document.Objects().end())
                {
                    const auto machineIndex =
                        static_cast<std::int32_t>(std::distance(document.Objects().begin(), machineObject));
                    const auto machine = std::find_if(document.StateMachines().begin(), document.StateMachines().end(),
                                                      [&](const BrxSmRecord &candidate)
                                                      { return candidate.ownerObjectIndex == machineIndex; });
                    if (machine == document.StateMachines().end())
                        issue(identity + " LOCKG state-machine object OID 556 has no decoded state-machine payload");
                    else
                        for (const std::int16_t requiredState : {std::int16_t{558}, std::int16_t{559}})
                            if (std::none_of(machine->states.begin(), machine->states.end(),
                                             [&](const BrxSmStateRecord &state) { return state.oid == requiredState; }))
                                issue(identity + " LOCKG state machine OID 556 is missing required state OID " +
                                      std::to_string(requiredState));
                }
            }

            std::size_t explicitLockCount = 0;
            std::vector<std::int32_t> explicitLockOids;
            for (const auto &property : object.properties)
                if (property.eopid == 1000)
                {
                    ++explicitLockCount;
                    if (std::find(explicitLockOids.begin(), explicitLockOids.end(), property.intValue) !=
                        explicitLockOids.end())
                        issue(identity + " repeats explicit LOCK OID " + std::to_string(property.intValue) +
                              "; ProjectCane appends and drives the same lock more than once");
                    if (std::find(childLockOids.begin(), childLockOids.end(), property.intValue) != childLockOids.end())
                        issue(identity + " explicitly references child LOCK OID " + std::to_string(property.intValue) +
                              "; automatic discovery already adds it");
                    explicitLockOids.push_back(property.intValue);
                    validateObjectReference(1000, "LOCKG");
                }
            if (childLockCount > 8)
                issue(identity + " LOCKG has more than eight child LOCKs; ProjectCane keeps only the first eight");
            if (childLockCount + explicitLockCount > 8)
                issue(identity +
                      " LOCKG child and explicit lock references exceed the shared eight-lock runtime capacity");
        }
        if (object.cid == 94)
        {
            for (const auto id : {std::int16_t{763}, std::int16_t{764}, std::int16_t{765}, std::int16_t{766}})
                if (const auto *scalar = classProperty(id); scalar && !std::isfinite(scalar->floatValue[0]))
                    issue(identity + " TAIL " + std::string(PropertyName(id)) + " is not finite");
            for (const auto id : {std::int16_t{768}, std::int16_t{770}})
                if (const auto *vector = classProperty(id);
                    vector && (!std::isfinite(vector->floatValue[0]) || !std::isfinite(vector->floatValue[1]) ||
                               !std::isfinite(vector->floatValue[2])))
                    issue(identity + " TAIL " + std::string(PropertyName(id)) + " contains a non-finite component");
            std::size_t directTailChildren = 0;
            for (const auto &candidate : document.Objects())
            {
                const std::int32_t parent =
                    candidate.proxyRuntimeParentIndex >= 0 ? candidate.proxyRuntimeParentIndex : candidate.parentIndex;
                if (parent == static_cast<std::int32_t>(index) && CidDerivesFrom(candidate.cid, 94))
                    ++directTailChildren;
            }
            if (directTailChildren > 1)
                issue(
                    identity +
                    " has multiple direct TAIL children; ProjectCane follows only the first child as the solver chain");
        }
        if (object.cid == 95)
        {
            std::array<std::array<int, 18>, 2> profileOccurrences{};
            std::array<float, 2> hardRocMaximum{5.0f, 5.0f};
            std::array<float, 2> hardRohMaximum{8.0f, 8.0f};
            int selectedProfile = 0;
            for (const auto &property : object.properties)
            {
                if (property.eopid == 802)
                {
                    if (property.intValue < 0 || property.intValue > 1)
                    {
                        issue(identity + " ROB tuning selector must be profile 0 or profile 1");
                        continue;
                    }
                    selectedProfile = property.intValue;
                    continue;
                }
                if (property.eopid < 776 || property.eopid > 793)
                    continue;
                const std::size_t slot = static_cast<std::size_t>(property.eopid - 776);
                if (++profileOccurrences[selectedProfile][slot] > 1)
                    issue(identity + " writes " + std::string(PropertyName(property.eopid)) +
                          " more than once in ROB profile " + std::to_string(selectedProfile) +
                          "; the last serialized setter wins");
                if (property.eopid == 777 && std::isfinite(property.floatValue[0]))
                    hardRocMaximum[selectedProfile] = property.floatValue[0];
                if (property.eopid == 780 && std::isfinite(property.floatValue[0]))
                    hardRohMaximum[selectedProfile] = property.floatValue[0];
                const bool scalar = property.eopid == 776 || property.eopid == 777 || property.eopid == 779 ||
                                    property.eopid == 780 || property.eopid == 788 || property.eopid == 789 ||
                                    property.eopid == 791 || property.eopid == 792;
                const bool range =
                    property.eopid == 782 || property.eopid == 783 || property.eopid == 785 || property.eopid == 786;
                if (!std::isfinite(property.floatValue[0]) || (range && !std::isfinite(property.floatValue[1])) ||
                    (!scalar && !range &&
                     (!std::isfinite(property.floatValue[1]) || !std::isfinite(property.floatValue[2]))))
                    issue(identity + " has non-finite ROB profile " + std::to_string(selectedProfile) + " value for " +
                          PropertyName(property.eopid));
            }
            for (const auto id :
                 {std::int16_t{771}, std::int16_t{772}, std::int16_t{773}, std::int16_t{774}, std::int16_t{775}})
                validateObjectReference(id, "ROB");
            const std::size_t stationCount = static_cast<std::size_t>(
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 775; }));
            if (stationCount > 16)
                issue(identity + " ROB has more than sixteen return-station references");
            const float maximumActiveRocs = (std::max)(hardRocMaximum[0], hardRocMaximum[1]);
            const float maximumActiveRohs = (std::max)(hardRohMaximum[0], hardRohMaximum[1]);
            if (stationCount < static_cast<std::size_t>(std::ceil((std::max)(0.0f, maximumActiveRohs))))
                issue(identity +
                      " ROB has fewer ROST stations than its largest authored active-ROH limit; spawning can stall");
            // Retail stores these values without clamping. Active limits are converted to
            // integers, LM endpoints are passed directly to GRandInRange (which accepts
            // either order), and signed arena values intentionally mirror/offset the arena.
            // Reject only non-finite payloads here; positivity is an authoring choice.
            for (const auto id :
                 {std::int16_t{795}, std::int16_t{796}, std::int16_t{797}, std::int16_t{798}, std::int16_t{799}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " ROB " + std::string(PropertyName(id)) + " is not finite");
            if (const auto *count = classProperty(800); count && count->intValue <= 0)
                issue(identity + " ROB maximum created ROC count must be greater than zero for a playable encounter");
            if (const auto *kind = classProperty(801); !kind || kind->intValue < 0 || kind->intValue > 2)
                issue(
                    identity +
                    " ROB encounter variant must select Primary, Secondary, or Tertiary; Nil ROBs remove themselves after loading");
            if (const auto *profile = classProperty(802); profile && (profile->intValue < 0 || profile->intValue > 1))
                issue(identity + " ROB tuning profile must select profile 0 or 1");
            if (const auto *center = classProperty(794);
                center && (!std::isfinite(center->floatValue[0]) || !std::isfinite(center->floatValue[1]) ||
                           !std::isfinite(center->floatValue[2])))
                issue(identity + " ROB arena center contains a non-finite component");
            for (const auto id : {std::int16_t{778}, std::int16_t{781}, std::int16_t{784}, std::int16_t{787},
                                  std::int16_t{790}, std::int16_t{793}, std::int16_t{803}})
                if (const auto *curve = classProperty(id);
                    curve && (!std::isfinite(curve->floatValue[0]) || !std::isfinite(curve->floatValue[1]) ||
                              !std::isfinite(curve->floatValue[2])))
                    issue(identity + " ROB " + std::string(PropertyName(id)) + " contains a non-finite coefficient");
            for (const auto id : {std::int16_t{776}, std::int16_t{777}, std::int16_t{779}, std::int16_t{780}})
                if (const auto *limit = classProperty(id);
                    limit && std::abs(limit->floatValue[0] - std::round(limit->floatValue[0])) > 0.0001f)
                    issue(identity + " ROB " + std::string(PropertyName(id)) +
                          " is fractional; runtime pool allocation effectively rounds it upward");
            const auto resolvesAs = [&](const std::int16_t eopid, const std::int16_t cid)
            {
                const auto *reference = classProperty(eopid);
                return reference &&
                       std::any_of(
                           document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                           { return candidate.oid == reference->intValue && CidDerivesFrom(candidate.cid, cid); });
            };
            if (!resolvesAs(771, 67))
                issue(identity + " has no usable ROC collectible template");
            if (!resolvesAs(772, 66))
                issue(identity + " has no usable ROH harvester template");
            if (!resolvesAs(773, 21))
                issue(identity + " has no usable ROV encounter vehicle");
            if (stationCount == 0)
                issue(identity + " has no ROST station; ROH spawning and returns cannot run");
            std::vector<std::int32_t> stationOids;
            for (const auto &station : object.properties)
            {
                if (station.eopid != 775)
                    continue;
                if (std::find(stationOids.begin(), stationOids.end(), station.intValue) != stationOids.end())
                    issue(identity + " references ROST OID " + std::to_string(station.intValue) + " more than once");
                stationOids.push_back(station.intValue);
                std::int32_t stationIndex = -1;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == station.intValue &&
                        document.Objects()[candidateIndex].cid == 68)
                    {
                        stationIndex = static_cast<std::int32_t>(candidateIndex);
                        break;
                    }
                if (stationIndex < 0)
                    issue(identity + " references missing/incompatible ROST OID " + std::to_string(station.intValue));
                else
                {
                    const bool hasReturnPoint = std::any_of(
                        document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                        { return candidate.parentIndex == stationIndex && CidDerivesFrom(candidate.cid, 126); });
                    if (!hasReturnPoint)
                        issue(identity + " references ROST OID " + std::to_string(station.intValue) +
                              " without the PNT child required for ROH spawn/return positioning");
                }
            }
            if (const auto *tube = classProperty(795); tube && tube->floatValue[0] > 0.0f)
                if (const auto *rocArena = classProperty(796);
                    rocArena && rocArena->floatValue[0] <= tube->floatValue[0])
                    issue(identity + " ROC spawn radius must exceed the tube exclusion radius");
            if (const auto *total = classProperty(800);
                total && maximumActiveRocs > static_cast<float>(total->intValue))
                issue(
                    identity +
                    " hard active ROC limit across its two profiles exceeds the total number of ROCs the encounter can create");
            if (const auto *kind = classProperty(801); kind && kind->intValue >= 0 && kind->intValue <= 2)
            {
                const std::size_t sameVariantCount = static_cast<std::size_t>(std::count_if(
                    document.Objects().begin(), document.Objects().end(),
                    [&](const BrxObjectRecord &candidate)
                    {
                        if (candidate.cid != 95)
                            return false;
                        const auto candidateKind =
                            std::find_if(candidate.properties.begin(), candidate.properties.end(),
                                         [](const BrxPropertyRecord &property) { return property.eopid == 801; });
                        return candidateKind != candidate.properties.end() && candidateKind->intValue == kind->intValue;
                    }));
                if (sameVariantCount > 1)
                    issue(
                        identity +
                        " shares its ROB encounter variant with another controller; both can survive variant filtering and compete for HUD/gameplay ownership");
            }
        }
        if (object.cid == 98)
        {
            if (const auto *pause = classProperty(838); pause && !std::isfinite(pause->floatValue[0]))
                issue(identity + " WAYPOINT pause duration is not finite");
            std::vector<std::pair<std::int32_t, std::int32_t>> routePairs;
            std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> routeSides;
            std::vector<std::int32_t> wildcardRouteRoots;
            std::size_t routeCount = 0;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 840)
                    continue;
                ++routeCount;
                if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4)
                {
                    issue(identity + " has malformed WAYPOINT state-machine goal route");
                    continue;
                }
                if (property.arguments[0].type != 5 || property.arguments[0].intValue < 0 ||
                    property.arguments[0].intValue > 1)
                    issue(identity + " WAYPOINT route has an invalid activation flag");
                for (std::size_t argumentIndex = 1; argumentIndex < 4; ++argumentIndex)
                    if (property.arguments[argumentIndex].type != 6)
                        issue(identity + " WAYPOINT route argument " + std::to_string(argumentIndex + 1) +
                              " has the wrong serialized type");
                const auto routeRoots =
                    property.arguments[1].intValue == -1
                        ? std::vector<std::int32_t>{}
                        : findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[1].intValue));
                const auto routeRoot =
                    routeRoots.empty() ? document.Objects().end() : document.Objects().begin() + routeRoots.front();
                if (property.arguments[1].intValue != -1)
                {
                    if (routeRoot == document.Objects().end())
                        issue(identity + " WAYPOINT root references missing contextual OID " +
                              std::to_string(property.arguments[1].intValue));
                    else if (!CidDerivesFrom(routeRoot->cid, 1))
                        issue(identity + " WAYPOINT root must reference an LO-derived object");
                    if (routeRoots.size() != 1)
                        issue(identity + " WAYPOINT route root OID " + std::to_string(property.arguments[1].intValue) +
                              " resolves to " + std::to_string(routeRoots.size()) +
                              " equally-near objects; TriggerRsmg requires exactly one");
                }

                if (property.arguments[2].intValue != -1 && routeRoot != document.Objects().end())
                {
                    bool foundStateMachine = false;
                    bool foundGoal = property.arguments[3].intValue == -1;
                    for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    {
                        const auto &candidate = document.Objects()[candidateIndex];
                        if (candidate.oid != property.arguments[2].intValue || !CidDerivesFrom(candidate.cid, 136))
                            continue;
                        foundStateMachine = true;
                        const auto machine = std::find_if(
                            document.StateMachines().begin(), document.StateMachines().end(),
                            [&](const BrxSmRecord &record)
                            { return record.ownerObjectIndex == static_cast<std::int32_t>(candidateIndex); });
                        if (machine != document.StateMachines().end() &&
                            std::any_of(machine->states.begin(), machine->states.end(),
                                        [&](const BrxSmStateRecord &state)
                                        { return state.oid == property.arguments[3].intValue; }))
                            foundGoal = true;
                    }
                    if (!foundStateMachine)
                        issue(identity + " WAYPOINT state machine OID " +
                              std::to_string(property.arguments[2].intValue) + " does not identify an authored SM");
                    else if (!foundGoal)
                        issue(identity + " WAYPOINT route references missing state-machine goal OID " +
                              std::to_string(property.arguments[3].intValue));
                }
                else if (property.arguments[2].intValue != -1)
                    issue(identity + " WAYPOINT cannot resolve its state machine because its root is missing");
                const auto pair = std::make_pair(property.arguments[1].intValue, property.arguments[2].intValue);
                if (std::find(routePairs.begin(), routePairs.end(), pair) == routePairs.end())
                    routePairs.push_back(pair);
                const auto side = std::make_tuple(property.arguments[1].intValue, property.arguments[2].intValue,
                                                  property.arguments[0].intValue);
                if (std::find(routeSides.begin(), routeSides.end(), side) != routeSides.end())
                    issue(identity +
                          " has duplicate WAYPOINT routes for the same root, state machine, and activation side");
                else
                    routeSides.push_back(side);

                if (property.arguments[2].intValue != -1)
                {
                    if (std::find(wildcardRouteRoots.begin(), wildcardRouteRoots.end(),
                                  property.arguments[1].intValue) != wildcardRouteRoots.end())
                        issue(
                            identity +
                            " WAYPOINT route is overwritten by an earlier wildcard state-machine route for the same root");
                }
                else
                    wildcardRouteRoots.push_back(property.arguments[1].intValue);
            }
            if (routeCount > 6)
                issue(identity + " has more than six serialized WAYPOINT goal entries");
            if (routePairs.size() > 3)
                issue(identity + " has more than three unique WAYPOINT state-machine routes");

            int limitAnimationCount = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.oid != 747)
                    continue;
                std::int32_t ancestorIndex = static_cast<std::int32_t>(candidateIndex);
                bool belongs = false;
                for (std::size_t guard = 0; ancestorIndex >= 0 &&
                                            static_cast<std::size_t>(ancestorIndex) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (ancestorIndex == static_cast<std::int32_t>(index))
                    {
                        belongs = true;
                        break;
                    }
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                    const std::int32_t parent =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (parent == ancestorIndex)
                        break;
                    ancestorIndex = parent;
                }
                if (!belongs)
                    continue;
                ++limitAnimationCount;
                if (!CidDerivesFrom(candidate.cid, 131))
                    issue(identity + " WAYPOINT limit animation OID 747 is not ASEG-derived");
            }
            if (limitAnimationCount > 1)
                issue(identity +
                      " has multiple WAYPOINT limit animations at OID 747; runtime contextual lookup selects one");
        }
        if (object.cid == 99)
        {
            if (!object.tn.present)
                issue(identity + " TN class payload was not decoded");
            else
            {
                if (object.tn.surfaces.empty() || object.tn.bspNodes.empty())
                    issue(identity + " TN has an empty trigger BSP and can never activate from volume containment");
                if (object.tn.surfaces.size() > 65535u)
                    issue(identity + " TN has more trigger planes than its 16-bit BRX surface count can encode");
                if (object.tn.bspNodes.size() > 32768u)
                    issue(identity + " TN has more BSP nodes than its signed 16-bit BRX child indices can address");
                for (std::size_t surfaceIndex = 0; surfaceIndex < object.tn.surfaces.size(); ++surfaceIndex)
                {
                    const auto &surface = object.tn.surfaces[surfaceIndex];
                    const float lengthSquared = surface.normal[0] * surface.normal[0] +
                                                surface.normal[1] * surface.normal[1] +
                                                surface.normal[2] * surface.normal[2];
                    if (!std::isfinite(surface.planeDistance) || !std::isfinite(lengthSquared) ||
                        lengthSquared <= 0.000001f)
                        issue(identity + " TN trigger plane " + std::to_string(surfaceIndex) +
                              " has a non-finite or zero normal");
                }
                for (std::size_t nodeIndex = 0; nodeIndex < object.tn.bspNodes.size(); ++nodeIndex)
                {
                    const auto &node = object.tn.bspNodes[nodeIndex];
                    if (node.surfaceIndex >= object.tn.surfaces.size())
                        issue(identity + " TN BSP node " + std::to_string(nodeIndex) +
                              " references an invalid trigger plane");
                    for (const auto child : {node.negativeChild, node.positiveChild})
                        if (child < -1 || child >= static_cast<std::int32_t>(object.tn.bspNodes.size()))
                            issue(identity + " TN BSP node " + std::to_string(nodeIndex) +
                                  " references an invalid child node");
                }
                if (!object.tn.bspNodes.empty())
                {
                    std::vector<std::uint8_t> state(object.tn.bspNodes.size(), 0u);
                    bool cycleReported = false;
                    const auto visit = [&](const auto &self, std::int32_t node) -> void
                    {
                        if (node < 0 || static_cast<std::size_t>(node) >= object.tn.bspNodes.size())
                            return;
                        auto &mark = state[static_cast<std::size_t>(node)];
                        if (mark == 1u)
                        {
                            if (!cycleReported)
                                issue(identity + " TN trigger BSP contains a cycle reachable from root node 0");
                            cycleReported = true;
                            return;
                        }
                        if (mark == 2u)
                            return;
                        mark = 1u;
                        const auto &nodeRecord = object.tn.bspNodes[static_cast<std::size_t>(node)];
                        self(self, nodeRecord.negativeChild);
                        self(self, nodeRecord.positiveChild);
                        mark = 2u;
                    };
                    visit(visit, 0);
                    const auto unreachable =
                        static_cast<std::size_t>(std::count(state.begin(), state.end(), std::uint8_t{0}));
                    if (unreachable != 0u)
                        issue(identity + " TN trigger BSP has " + std::to_string(unreachable) +
                              " node(s) unreachable from runtime root node 0");
                }
                if (object.tn.curveKind < -1 || object.tn.curveKind > 1)
                    issue(identity + " TN has an invalid camera curve kind");
                else if (object.tn.curveKind >= 0)
                {
                    if (object.tn.curvePoints.size() < 2)
                        issue(identity + " TN camera curve requires at least two control points");
                    if (object.tn.curvePoints.size() > 255u)
                        issue(identity + " TN camera curve exceeds its 8-bit BRX control-point count");
                    float previousParameter = -std::numeric_limits<float>::infinity();
                    for (std::size_t pointIndex = 0; pointIndex < object.tn.curvePoints.size(); ++pointIndex)
                    {
                        const auto &point = object.tn.curvePoints[pointIndex];
                        if (!std::isfinite(point.parameter) || point.parameter <= previousParameter)
                            issue(identity + " TN camera-curve parameters must be finite and strictly increasing");
                        previousParameter = point.parameter;
                        for (const float component : point.position)
                            if (!std::isfinite(component))
                            {
                                issue(identity + " TN camera curve has a non-finite control point");
                                break;
                            }
                        if (object.tn.curveKind == 1)
                        {
                            for (const float component : point.incomingTangent)
                                if (!std::isfinite(component))
                                {
                                    issue(identity + " TN cubic curve has a non-finite incoming tangent");
                                    break;
                                }
                            for (const float component : point.outgoingTangent)
                                if (!std::isfinite(component))
                                {
                                    issue(identity + " TN cubic curve has a non-finite outgoing tangent");
                                    break;
                                }
                        }
                    }
                }
                else
                {
                    bool finiteFrame = true;
                    for (const auto component :
                         {object.tn.staticFrame[0], object.tn.staticFrame[1], object.tn.staticFrame[2],
                          object.tn.staticFrame[4], object.tn.staticFrame[5], object.tn.staticFrame[6],
                          object.tn.staticFrame[8], object.tn.staticFrame[9], object.tn.staticFrame[10],
                          object.tn.staticFrame[12], object.tn.staticFrame[13], object.tn.staticFrame[14]})
                        finiteFrame = finiteFrame && std::isfinite(component);
                    if (!finiteFrame)
                        issue(identity + " TN static camera frame contains a non-finite serialized component");
                    const auto axisLengthSquared = [&](const int base)
                    {
                        return object.tn.staticFrame[base] * object.tn.staticFrame[base] +
                               object.tn.staticFrame[base + 1] * object.tn.staticFrame[base + 1] +
                               object.tn.staticFrame[base + 2] * object.tn.staticFrame[base + 2];
                    };
                    if (finiteFrame && (axisLengthSquared(0) <= 0.000001f || axisLengthSquared(4) <= 0.000001f ||
                                        axisLengthSquared(8) <= 0.000001f))
                        issue(identity + " TN static camera frame has a collapsed basis axis");
                }
            }
            // dsCam is a signed offset along the tunnel (the retail default is
            // -450), so it must not be checked as a non-negative magnitude.
            // ProjectCane stores these TN controls verbatim. Non-positive pan
            // durations disable their runtime branches, and the remaining values
            // are signed tuning controls rather than constrained magnitudes.
            for (const auto id : {std::int16_t{858}, std::int16_t{873}, std::int16_t{874}, std::int16_t{875},
                                  std::int16_t{876}, std::int16_t{885}, std::int16_t{886}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " TN " + std::string(PropertyName(id)) + " must be finite");
            for (const auto id : {std::int16_t{851}, std::int16_t{859}, std::int16_t{860}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " TN " + std::string(PropertyName(id)) + " must be finite");
            // Screen preferences and camera blend/strength controls are stored
            // and consumed verbatim. glm::mix and the follow/lock calculations
            // intentionally permit extrapolation or inverted response, so only
            // reject non-finite values rather than imposing an editor-only 0..1.
            for (const auto id : {std::int16_t{861}, std::int16_t{862}, std::int16_t{863}, std::int16_t{864}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " TN " + std::string(PropertyName(id)) + " must be finite");
            if (const auto *fov = classProperty(878);
                fov && fov->floatValue[0] != 0.0f && (fov->floatValue[0] <= 0.0f || fov->floatValue[0] >= 3.141592654f))
                issue(identity + " TN field-of-view override must be zero (disabled) or between 0 and pi radians");
            if (const auto *framing = classProperty(872); framing && (framing->intValue < -1 || framing->intValue > 1))
                issue(identity + " TN camera framing orientation must be default, behind, or side");
            if (const auto *state = classProperty(884); state && (state->intValue < -1 || state->intValue > 1))
                issue(identity + " TN camera-state override must be default, outside, or inside");
            if (classProperty(883) != nullptr)
                issue(identity + " serializes runtime-owned TN camera state EOPID 883");
            if (classProperty(880) != nullptr)
                issue(identity + " serializes runtime-owned TN follow-target pointer EOPID 880");
            if (classProperty(864) != nullptr && classProperty(865) != nullptr)
                issue(
                    identity +
                    " serializes both TN focus ratio and focus vector; runtime property order decides which value wins");
            for (const auto id :
                 {std::int16_t{852}, std::int16_t{854}, std::int16_t{856}, std::int16_t{865}, std::int16_t{881}})
                if (const auto *value = classProperty(id); value)
                    for (const float component : value->floatValue)
                        if (!std::isfinite(component))
                        {
                            issue(identity + " TN " + std::string(PropertyName(id)) + " contains a non-finite value");
                            break;
                        }
            for (const auto id : {std::int16_t{853}, std::int16_t{855}, std::int16_t{857}, std::int16_t{882}})
                if (const auto *value = classProperty(id);
                    value && (!std::isfinite(value->floatValue[0]) || !std::isfinite(value->floatValue[1]) ||
                              value->floatValue[0] > value->floatValue[1]))
                    issue(identity + " TN " + std::string(PropertyName(id)) +
                          " must contain finite minimum/maximum limits in ascending order");
            if (const auto *directions = classProperty(867);
                directions && ((directions->intValue & ~7) != 0 || (directions->intValue & 7) == 0))
                issue(
                    identity +
                    " TN camera-direction flags must enable at least one of forward, reverse, or manual and contain no other bits");
            if (const auto *priority = classProperty(870);
                priority && (priority->intValue < 0 || priority->intValue > 3))
                issue(identity + " TN camera priority must be 0, 1, 2, or 3");
        }
        if (object.cid == 100)
        {
            for (const auto id : {std::int16_t{899}, std::int16_t{900}, std::int16_t{902}, std::int16_t{903},
                                  std::int16_t{905}, std::int16_t{906}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " JLOC " + std::string(PropertyName(id)) + " is not finite");
            if (const auto *aim = classProperty(898); aim && (aim->intValue < -1 || aim->intValue > 1))
                issue(identity + " JLOC target selection must be none, closest to player, or random");
            if (const auto *mode = classProperty(901); mode && (mode->intValue < -1 || mode->intValue > 2))
                issue(identity + " JLOC launcher mode must be none, straight, lobbed, or spline");
            if (const auto *tilt = classProperty(904); tilt && !std::isfinite(tilt->floatValue[0]))
                issue(identity + " JLOC lob elevation blend must be finite");
            const auto *missileTime = classProperty(900);
            if (missileTime && missileTime->floatValue[0] <= 0.0f)
                issue(identity + " JLOC missile travel time must be greater than zero");
            const float speedRatio = classProperty(903) ? classProperty(903)->floatValue[0] : 4.0f;
            const float accelerationRatio = classProperty(902) ? classProperty(902)->floatValue[0] : 0.8f;
            if (speedRatio != 1.0f && accelerationRatio == 0.0f)
                issue(identity + " JLOC acceleration-time ratio cannot be zero when missile-speed ratio is not 1");
            const float acceleratedDenominator = accelerationRatio * (1.0f - speedRatio) + 2.0f * speedRatio;
            if (speedRatio != 1.0f && std::abs(acceleratedDenominator) <= 0.000001f)
                issue(identity + " JLOC accelerated-missile ratios produce a zero launch-speed denominator");
            std::size_t targetCount = 0;
            for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                if (!CidDerivesFrom(document.Objects()[candidateIndex].cid, 115))
                    continue;
                std::int32_t ancestorIndex = document.Objects()[candidateIndex].proxyRuntimeParentIndex >= 0
                                                 ? document.Objects()[candidateIndex].proxyRuntimeParentIndex
                                                 : document.Objects()[candidateIndex].parentIndex;
                for (std::size_t depth = 0; depth <= document.Objects().size() && ancestorIndex >= 0 &&
                                            static_cast<std::size_t>(ancestorIndex) < document.Objects().size();
                     ++depth)
                {
                    if (ancestorIndex == static_cast<std::int32_t>(index))
                    {
                        ++targetCount;
                        break;
                    }
                    const auto &ancestor = document.Objects()[static_cast<std::size_t>(ancestorIndex)];
                    const auto next =
                        ancestor.proxyRuntimeParentIndex >= 0 ? ancestor.proxyRuntimeParentIndex : ancestor.parentIndex;
                    if (next == ancestorIndex)
                        break;
                    ancestorIndex = next;
                }
            }
            if (targetCount == 0)
                issue(identity + " JLOC has no descendant XFM firing targets");
            else if (targetCount > 16)
                issue(identity + " JLOC has more than sixteen descendant XFM targets");
        }
        if (object.cid == 111)
        {
            validateObjectReference(635, "HSHAPE");
            if (const auto *maximumHeight = classProperty(638);
                maximumHeight && !std::isfinite(maximumHeight->floatValue[0]))
                issue(identity + " HSHAPE maximum acquisition height must be finite");
            if (const auto *kind = classProperty(633); kind && (kind->intValue < 0 || kind->intValue > 7))
                issue(identity + " HSHAPE movement kind is outside the engine range 0..7");
            if (const auto *flags = classProperty(636); flags && (flags->intValue & ~3) != 0)
                issue(identity + " HSHAPE endpoint flags contain unknown bits");
            if (const auto *camera = classProperty(635); camera && camera->intValue != -1)
            {
                const auto target = findContextualObject(static_cast<std::int16_t>(camera->intValue));
                if (target != document.Objects().end() && target->cid != 99 && !CidDerivesFrom(target->cid, 99))
                    issue(identity + " HSHAPE camera OID must reference a TN object");
            }
            const auto shape = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                                            [&](const BrxShapePathRecord &candidate)
                                            { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (shape != document.ShapePaths().end())
            {
                if (!shape->closed && shape->parameters.size() == shape->points.size() &&
                    shape->parameters.size() >= 2 && shape->parameters.back() - shape->parameters.front() <= 60.0f)
                    issue(identity +
                          " HSHAPE open path has no usable range after the runtime's 30-unit endpoint clamps");
                const float *parentWorld = nullptr;
                if (object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < document.Objects().size())
                    parentWorld = document.Objects()[static_cast<std::size_t>(object.parentIndex)].worldTransform;
                const auto hasWorldHorizontalDirection = [&](const float x, const float y, const float z)
                {
                    const float worldX = parentWorld ? parentWorld[0] * x + parentWorld[4] * y + parentWorld[8] * z : x;
                    const float worldY = parentWorld ? parentWorld[1] * x + parentWorld[5] * y + parentWorld[9] * z : y;
                    return worldX * worldX + worldY * worldY > 0.000001f;
                };
                bool hasHorizontalDirection = false;
                for (std::size_t point = 1; point < shape->points.size(); ++point)
                {
                    const float dx = shape->points[point][0] - shape->points[point - 1][0];
                    const float dy = shape->points[point][1] - shape->points[point - 1][1];
                    const float dz = shape->points[point][2] - shape->points[point - 1][2];
                    if (hasWorldHorizontalDirection(dx, dy, dz))
                    {
                        hasHorizontalDirection = true;
                        break;
                    }
                }
                if (!hasHorizontalDirection && shape->hermite)
                    for (const auto &tangent : shape->tangentsOut)
                        if (hasWorldHorizontalDirection(tangent[0], tangent[1], tangent[2]))
                        {
                            hasHorizontalDirection = true;
                            break;
                        }
                if (!hasHorizontalDirection && shape->hermite)
                    for (const auto &tangent : shape->tangentsIn)
                        if (hasWorldHorizontalDirection(tangent[0], tangent[1], tangent[2]))
                        {
                            hasHorizontalDirection = true;
                            break;
                        }
                if (!hasHorizontalDirection)
                    issue(identity +
                          " HSHAPE has no horizontal path direction for its perpendicular hide offset and facing");
            }
        }
        if (object.cid == 112)
        {
            validateObjectReference(649, "PIPE");
            if (const auto *maximumHeight = classProperty(651);
                maximumHeight && !std::isfinite(maximumHeight->floatValue[0]))
                issue(identity + " PIPE maximum jump-target height must be finite");
            if (const auto *facing = classProperty(643);
                facing && (!std::isfinite(facing->floatValue[0]) || !std::isfinite(facing->floatValue[1]) ||
                           !std::isfinite(facing->floatValue[2])))
                issue(identity + " PIPE facing direction contains a non-finite value");
            for (const auto runtimeOnly : {std::int16_t{647}, std::int16_t{650}})
                if (classProperty(runtimeOnly) != nullptr)
                    issue(identity + " serializes runtime-only PIPE EOPID " + std::to_string(runtimeOnly));
            const auto pipePath = std::find_if(
                document.ShapePaths().begin(), document.ShapePaths().end(), [&](const BrxShapePathRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (pipePath != document.ShapePaths().end())
            {
                float pathLength = 0.0f;
                if (pipePath->points.size() >= 2)
                {
                    // MeasureCrvl/MeasureCrvc both process ccv - 1 stored
                    // segments. Closed curves carry their closure in that
                    // authored point sequence rather than adding another edge.
                    const std::size_t segmentCount = pipePath->points.size() - 1;
                    for (std::size_t segment = 0; segment < segmentCount; ++segment)
                    {
                        const std::size_t next = (segment + 1) % pipePath->points.size();
                        const glm::vec3 p0(pipePath->points[segment][0], pipePath->points[segment][1],
                                           pipePath->points[segment][2]);
                        const glm::vec3 p3(pipePath->points[next][0], pipePath->points[next][1],
                                           pipePath->points[next][2]);
                        const bool cubic = pipePath->hermite &&
                                           pipePath->tangentsOut.size() == pipePath->points.size() &&
                                           pipePath->tangentsIn.size() == pipePath->points.size() &&
                                           pipePath->parameters.size() == pipePath->points.size();
                        if (!cubic)
                        {
                            pathLength += glm::length(p3 - p0);
                            continue;
                        }
                        float parameterSpan = next > segment
                                                  ? pipePath->parameters[next] - pipePath->parameters[segment]
                                                  : pipePath->parameters.back() - pipePath->parameters.front();
                        if (!(parameterSpan > 0.0f) || !std::isfinite(parameterSpan))
                            continue;
                        const glm::vec3 p1 =
                            p0 + glm::vec3(pipePath->tangentsOut[segment][0], pipePath->tangentsOut[segment][1],
                                           pipePath->tangentsOut[segment][2]) *
                                     (parameterSpan / 3.0f);
                        const glm::vec3 p2 =
                            p3 - glm::vec3(pipePath->tangentsIn[next][0], pipePath->tangentsIn[next][1],
                                           pipePath->tangentsIn[next][2]) *
                                     (parameterSpan / 3.0f);
                        glm::vec3 previous = p0;
                        // ProjectCane's SBezierPosLength tessellates exactly
                        // twenty positions (nineteen line intervals).
                        for (int sample = 1; sample < 20; ++sample)
                        {
                            const float t = static_cast<float>(sample) / 19.0f;
                            const float u = 1.0f - t;
                            const glm::vec3 point =
                                u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
                            pathLength += glm::length(point - previous);
                            previous = point;
                        }
                    }
                }
                if (!pipePath->closed && pipePath->points.size() >= 2 && pathLength < 100.0f)
                    issue(identity +
                          " PIPE open path inverts the runtime acquisition bounds after its 50-unit endpoint margins");
                else if (!pipePath->closed && pipePath->points.size() >= 2 && pathLength < 135.0f)
                    issue(identity + " PIPE open path inverts the runtime's 100-unit foot and 35-unit hand limits");
                // GetJtPipeGoalMat deliberately repairs zero or parallel
                // tangent/facing vectors with +Z/+Y fallbacks, so those
                // authored combinations are valid and need no diagnostic.
            }
        }
        if (object.cid == 113)
        {
            validateObjectReference(1037, "RAIL");
            for (const auto id : {std::int16_t{1032}, std::int16_t{1036}})
                if (const auto *value = classProperty(id); value && value->kind != BrxPropertyKind::Bool)
                    issue(identity + " RAIL EOPID " + std::to_string(id) + " must be serialized as Bool");
            for (const auto id : {std::int16_t{1033}, std::int16_t{1034}, std::int16_t{1035}})
                if (const auto *value = classProperty(id); value && value->kind != BrxPropertyKind::Float)
                    issue(identity + " RAIL EOPID " + std::to_string(id) + " must be serialized as Float");
            if (const auto *control = classProperty(1037);
                control && control->kind != BrxPropertyKind::Int16 && control->kind != BrxPropertyKind::TypedInt16)
                issue(identity + " RAIL control-direction OID must be serialized as a 16-bit OID");
            for (const auto id : {std::int16_t{1032}, std::int16_t{1033}, std::int16_t{1034}, std::int16_t{1035},
                                  std::int16_t{1036}, std::int16_t{1037}, std::int16_t{1038}})
            {
                const auto count =
                    std::count_if(object.properties.begin(), object.properties.end(),
                                  [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                if (count > 1)
                    issue(identity + " repeats RAIL EOPID " + std::to_string(id) +
                          "; runtime behavior depends on setter order");
            }
            validateNonNegativeFloat(1033, "RAIL");
            for (const auto id : {std::int16_t{1034}, std::int16_t{1035}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " RAIL EOPID " + std::to_string(id) + " must be finite");
            if (classProperty(1038) != nullptr)
                issue(identity + " serializes runtime-only RAIL EOPID 1038");
            const auto railPath = std::find_if(
                document.ShapePaths().begin(), document.ShapePaths().end(), [&](const BrxShapePathRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (railPath == document.ShapePaths().end() || railPath->points.size() < 2)
                issue(identity + " RAIL has no usable SHAPE curve");
            else
            {
                float pathLength = 0.0f;
                for (std::size_t point = 1; point < railPath->points.size(); ++point)
                {
                    const glm::vec3 from(railPath->points[point - 1][0], railPath->points[point - 1][1],
                                         railPath->points[point - 1][2]);
                    const glm::vec3 to(railPath->points[point][0], railPath->points[point][1],
                                       railPath->points[point][2]);
                    const bool cubic = railPath->hermite && railPath->tangentsOut.size() == railPath->points.size() &&
                                       railPath->tangentsIn.size() == railPath->points.size() &&
                                       railPath->parameters.size() == railPath->points.size();
                    if (!cubic)
                    {
                        pathLength += glm::distance(from, to);
                        continue;
                    }
                    const float parameterSpan = railPath->parameters[point] - railPath->parameters[point - 1];
                    if (!(parameterSpan > 0.0f) || !std::isfinite(parameterSpan))
                        continue;
                    const glm::vec3 controlFrom =
                        from + glm::vec3(railPath->tangentsOut[point - 1][0], railPath->tangentsOut[point - 1][1],
                                         railPath->tangentsOut[point - 1][2]) *
                                   (parameterSpan / 3.0f);
                    const glm::vec3 controlTo =
                        to - glm::vec3(railPath->tangentsIn[point][0], railPath->tangentsIn[point][1],
                                       railPath->tangentsIn[point][2]) *
                                 (parameterSpan / 3.0f);
                    glm::vec3 previous = from;
                    for (int sample = 1; sample < 20; ++sample)
                    {
                        const float t = static_cast<float>(sample) / 19.0f;
                        const float u = 1.0f - t;
                        const glm::vec3 current = u * u * u * from + 3.0f * u * u * t * controlFrom +
                                                  3.0f * u * t * t * controlTo + t * t * t * to;
                        pathLength += glm::distance(previous, current);
                        previous = current;
                    }
                }
                if (!(pathLength > 0.001f) || !std::isfinite(pathLength))
                    issue(identity + " RAIL path has no finite traversable length");
                if (!railPath->closed && pathLength <= 100.0f)
                    issue(identity +
                          " RAIL open path has no safe acquisition range after the runtime's 50-unit endpoint margins");
                const bool slippery = classProperty(1032) != nullptr && classProperty(1032)->boolValue;
                if (!slippery)
                {
                    glm::mat3 parentRotation(1.0f);
                    if (object.parentIndex >= 0 &&
                        static_cast<std::size_t>(object.parentIndex) < document.Objects().size())
                        parentRotation = glm::mat3(glm::make_mat4(
                            document.Objects()[static_cast<std::size_t>(object.parentIndex)].worldTransform));
                    bool hasHorizontalDirection = false;
                    for (std::size_t point = 1; point < railPath->points.size(); ++point)
                    {
                        const glm::vec3 local(railPath->points[point][0] - railPath->points[point - 1][0],
                                              railPath->points[point][1] - railPath->points[point - 1][1],
                                              railPath->points[point][2] - railPath->points[point - 1][2]);
                        const glm::vec3 world = parentRotation * local;
                        if (world.x * world.x + world.y * world.y > 0.000001f)
                        {
                            hasHorizontalDirection = true;
                            break;
                        }
                    }
                    if (!hasHorizontalDirection)
                        issue(identity +
                              " player-driven RAIL has no horizontal direction; runtime facing falls back to world +X");
                }
            }
            if (const auto *slippery = classProperty(1032); slippery && slippery->boolValue)
                if (const auto *speed = classProperty(1033); speed && speed->floatValue[0] <= 0.0f)
                    issue(identity + " slippery RAIL has a non-positive maximum slide speed");
            if (const auto *control = classProperty(1037); control && control->intValue != -1)
            {
                const auto target = findContextualObject(static_cast<std::int16_t>(control->intValue));
                if (target != document.Objects().end())
                {
                    const auto targetIndex =
                        static_cast<std::int32_t>(std::distance(document.Objects().begin(), target));
                    if (targetIndex == static_cast<std::int32_t>(index))
                        issue(identity + " RAIL control-direction curve references itself");
                    if (!CidDerivesFrom(target->cid, 110))
                        issue(identity + " RAIL control-direction OID does not resolve to a SHAPE-derived curve");
                    const auto targetPath = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                                                         [&](const BrxShapePathRecord &candidate)
                                                         { return candidate.ownerObjectIndex == targetIndex; });
                    if (targetPath == document.ShapePaths().end() || targetPath->points.size() < 2)
                        issue(identity + " RAIL control-direction object has no usable curve");
                    else
                    {
                        glm::mat3 parentRotation(1.0f);
                        if (target->parentIndex >= 0 &&
                            static_cast<std::size_t>(target->parentIndex) < document.Objects().size())
                            parentRotation = glm::mat3(glm::make_mat4(
                                document.Objects()[static_cast<std::size_t>(target->parentIndex)].worldTransform));
                        bool hasHorizontalDirection = false;
                        for (std::size_t point = 1; point < targetPath->points.size(); ++point)
                        {
                            const glm::vec3 local(targetPath->points[point][0] - targetPath->points[point - 1][0],
                                                  targetPath->points[point][1] - targetPath->points[point - 1][1],
                                                  targetPath->points[point][2] - targetPath->points[point - 1][2]);
                            const glm::vec3 world = parentRotation * local;
                            if (world.x * world.x + world.y * world.y > 0.000001f)
                            {
                                hasHorizontalDirection = true;
                                break;
                            }
                        }
                        if (!hasHorizontalDirection)
                            issue(
                                identity +
                                " RAIL control-direction curve has no horizontal tangent; runtime input falls back to world +X");
                    }
                }
            }
        }
        if (CidDerivesFrom(object.cid, 115))
        {
            if (const auto *position = classProperty(149);
                position && (!std::isfinite(position->floatValue[0]) || !std::isfinite(position->floatValue[1]) ||
                             !std::isfinite(position->floatValue[2])))
                issue(identity + " XFM local position contains a non-finite component");
            bool finiteTransform = true;
            for (const float component : object.localTransform)
                if (!std::isfinite(component))
                {
                    issue(identity + " XFM local transform contains a non-finite component");
                    finiteTransform = false;
                    break;
                }
            if (finiteTransform)
            {
                const auto basisLengthSquared = [&](const std::size_t column)
                {
                    const std::size_t offset = column * 4;
                    return object.localTransform[offset] * object.localTransform[offset] +
                           object.localTransform[offset + 1] * object.localTransform[offset + 1] +
                           object.localTransform[offset + 2] * object.localTransform[offset + 2];
                };
                const auto basisDot = [&](const std::size_t lhs, const std::size_t rhs)
                {
                    const std::size_t a = lhs * 4, b = rhs * 4;
                    return object.localTransform[a] * object.localTransform[b] +
                           object.localTransform[a + 1] * object.localTransform[b + 1] +
                           object.localTransform[a + 2] * object.localTransform[b + 2];
                };
                const float lx = basisLengthSquared(0), ly = basisLengthSquared(1), lz = basisLengthSquared(2);
                const float maximumBasisError =
                    (std::max)({std::abs(lx - 1.0f), std::abs(ly - 1.0f), std::abs(lz - 1.0f), std::abs(basisDot(0, 1)),
                                std::abs(basisDot(0, 2)), std::abs(basisDot(1, 2))});
                if (lx <= 0.000001f || ly <= 0.000001f || lz <= 0.000001f)
                    issue(identity + " XFM local basis is singular");
                else if (maximumBasisError > 0.002f)
                    issue(
                        identity +
                        " XFM local basis is scaled or non-orthogonal; runtime inverse operations assume a rigid frame");
            }
            const bool serializedUnderProxyWrapper =
                object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < document.Objects().size() &&
                document.Objects()[static_cast<std::size_t>(object.parentIndex)].cid == 83 &&
                object.proxyRecordIndex >= 0 && object.proxyRootIndex >= 0;
            const std::int32_t runtimeParentIndex =
                serializedUnderProxyWrapper ? object.proxyRuntimeParentIndex : object.parentIndex;
            if (runtimeParentIndex >= 0 && static_cast<std::size_t>(runtimeParentIndex) >= document.Objects().size())
                issue(identity + " XFM runtime parent index is invalid");
            std::unordered_set<std::int32_t> visitedParents;
            for (std::int32_t parent = runtimeParentIndex; parent >= 0;)
            {
                if (parent == static_cast<std::int32_t>(index) || !visitedParents.insert(parent).second)
                {
                    issue(identity + " XFM parent hierarchy contains a cycle");
                    break;
                }
                if (static_cast<std::size_t>(parent) >= document.Objects().size())
                    break;
                const auto &parentObject = document.Objects()[static_cast<std::size_t>(parent)];
                const bool parentUnderProxyWrapper =
                    parentObject.parentIndex >= 0 &&
                    static_cast<std::size_t>(parentObject.parentIndex) < document.Objects().size() &&
                    document.Objects()[static_cast<std::size_t>(parentObject.parentIndex)].cid == 83 &&
                    parentObject.proxyRecordIndex >= 0 && parentObject.proxyRootIndex >= 0;
                parent = parentUnderProxyWrapper ? parentObject.proxyRuntimeParentIndex : parentObject.parentIndex;
            }
        }
        if (object.cid == 116)
        {
            const auto warpRecordCount =
                std::count_if(document.Warps().begin(), document.Warps().end(), [&](const BrxWarpRecord &candidate)
                              { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (warpRecordCount != 1)
                issue(identity + " must own exactly one decoded WARP payload record");
            if (const auto *velocity = classProperty(153);
                velocity && (!std::isfinite(velocity->floatValue[0]) || !std::isfinite(velocity->floatValue[1]) ||
                             !std::isfinite(velocity->floatValue[2])))
                issue(identity + " WARP arrival velocity contains a non-finite component");
            if (classProperty(153) != nullptr)
                issue(identity +
                      " serializes runtime-only composite WARP velocity EOPID 153; author components 154-156 instead");
            if (classProperty(162) != nullptr)
                issue(identity + " serializes runtime-only WARP trigger EOPID 162");
            std::size_t routeCount = 0;
            std::vector<std::pair<std::int32_t, std::int32_t>> routePairs;
            std::vector<std::tuple<std::int32_t, std::int32_t, std::int32_t>> routeSides;
            std::vector<std::int32_t> wildcardRouteRoots;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 161)
                    continue;
                ++routeCount;
                if (property.arguments.size() != 4)
                {
                    issue(identity + " has a malformed WARP state-machine route");
                    continue;
                }
                if (property.arguments[0].intValue < 0 || property.arguments[0].intValue > 1)
                    issue(identity + " WARP state-machine route has an invalid trigger-side flag");
                if (property.arguments[1].intValue == -1)
                    issue(identity + " WARP state-machine route has no root OID");
                if (property.arguments[3].intValue == -1)
                    issue(identity + " WARP state-machine route has no goal OID");

                const auto routeRoots =
                    property.arguments[1].intValue == -1
                        ? std::vector<std::int32_t>{}
                        : findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[1].intValue));
                const auto routeRoot =
                    routeRoots.empty() ? document.Objects().end() : document.Objects().begin() + routeRoots.front();
                if (property.arguments[1].intValue != -1 && routeRoot == document.Objects().end())
                    issue(identity + " WARP route root references missing contextual OID " +
                          std::to_string(property.arguments[1].intValue));
                else if (routeRoot != document.Objects().end() && !CidDerivesFrom(routeRoot->cid, 2))
                    issue(identity + " WARP route root must resolve to an ALO-derived object");
                if (property.arguments[1].intValue != -1 && routeRoots.size() != 1)
                    issue(identity + " WARP route root OID " + std::to_string(property.arguments[1].intValue) +
                          " resolves to " + std::to_string(routeRoots.size()) +
                          " equally-near objects; TriggerRsmg requires exactly one");

                if (property.arguments[2].intValue != -1)
                {
                    bool foundMachine = false;
                    bool foundGoal = property.arguments[3].intValue == -1;
                    for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    {
                        const auto &candidate = document.Objects()[candidateIndex];
                        if (candidate.oid != property.arguments[2].intValue || !CidDerivesFrom(candidate.cid, 136))
                            continue;
                        foundMachine = true;
                        const auto machine = std::find_if(
                            document.StateMachines().begin(), document.StateMachines().end(),
                            [&](const BrxSmRecord &record)
                            { return record.ownerObjectIndex == static_cast<std::int32_t>(candidateIndex); });
                        if (machine != document.StateMachines().end() &&
                            std::any_of(machine->states.begin(), machine->states.end(),
                                        [&](const BrxSmStateRecord &state)
                                        { return state.oid == property.arguments[3].intValue; }))
                            foundGoal = true;
                    }
                    if (!foundMachine)
                        issue(identity + " WARP route state-machine OID does not identify an authored SM");
                    else if (!foundGoal)
                        issue(identity + " WARP route references a goal absent from its state machine");
                }

                const auto pair = std::make_pair(property.arguments[1].intValue, property.arguments[2].intValue);
                if (std::find(routePairs.begin(), routePairs.end(), pair) == routePairs.end())
                    routePairs.push_back(pair);
                const auto side = std::make_tuple(property.arguments[1].intValue, property.arguments[2].intValue,
                                                  property.arguments[0].intValue);
                if (std::find(routeSides.begin(), routeSides.end(), side) != routeSides.end())
                    issue(identity + " repeats a WARP route for the same root, SM, and trigger side");
                else
                    routeSides.push_back(side);
                if (property.arguments[2].intValue != -1)
                {
                    if (std::find(wildcardRouteRoots.begin(), wildcardRouteRoots.end(),
                                  property.arguments[1].intValue) != wildcardRouteRoots.end())
                        issue(identity +
                              " WARP route is overwritten by an earlier wildcard SM route for the same root");
                }
                else
                    wildcardRouteRoots.push_back(property.arguments[1].intValue);
            }
            if (routeCount > 6)
                issue(identity + " has more than six serialized WARP goal entries");
            if (routePairs.size() > 3)
                issue(identity + " has more than three unique WARP state-machine routes; FAddRsmg rejects the rest");
        }
        if (CidDerivesFrom(object.cid, 117))
        {
            validateNonNegativeFloat(617, "TARGET");
            if (const auto *categories = classProperty(616); categories && (categories->intValue & ~0x3f) != 0)
                issue(identity + " TARGET category mask contains unknown bits");
        }
        if (object.cid == 118)
        {
            validateNonNegativeFloat(341, "HND");
            for (const auto id : {std::int16_t{338}, std::int16_t{343}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " HND EOPID " + std::to_string(id) + " must be finite");
            if (const auto *direction = classProperty(337);
                direction && (direction->intValue < -1 || direction->intValue > 2))
                issue(identity + " has invalid HND grab direction " + std::to_string(direction->intValue));
        }
        if (CidDerivesFrom(object.cid, 121))
        {
            validateObjectReference(425, "EXPLO");
            validateObjectReference(426, "EXPLO");
            // ProjectCane's GRandInRange accepts either endpoint order. These
            // ranges therefore only require finite values; reversed endpoints
            // are authored behavior, not corruption.
            for (const auto id : {std::int16_t{431}, std::int16_t{433}, std::int16_t{435}, std::int16_t{438}})
                if (const auto *range = classProperty(id);
                    range && (!std::isfinite(range->floatValue[0]) || !std::isfinite(range->floatValue[1])))
                    issue(identity + " EXPLO " + std::string(PropertyName(id) ? PropertyName(id) : "range") +
                          " must contain finite endpoints");
            // CalculateEmitvx treats every value below one as "use the whole
            // batch", so negative authored values are valid too.
            for (const auto id : {std::int16_t{434}, std::int16_t{437}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " EXPLO " + std::string(PropertyName(id) ? PropertyName(id) : "value") +
                          " must be finite");
            if (const auto *origin = classProperty(421); origin && (origin->intValue < -1 || origin->intValue > 4))
                issue(identity + " has invalid EXPLO spawn-shape kind " + std::to_string(origin->intValue));
            if (const auto *direction = classProperty(427);
                direction && (direction->intValue < -1 || direction->intValue > 5))
                issue(identity + " has invalid EXPLO direction kind " + std::to_string(direction->intValue));
            if (const auto *velocitySpace = classProperty(432);
                velocitySpace && (velocitySpace->intValue < -1 || velocitySpace->intValue > 3))
                issue(identity + " has invalid EXPLO velocity coordinate space " +
                      std::to_string(velocitySpace->intValue));
            if (const auto *origin = classProperty(421); origin)
            {
                // EOPIDs 422 and 423 intentionally alias EMITO::posOrigin.
                // Either remains active as the point origin / box minimum,
                // regardless of which authoring name produced the value.
                if (origin->intValue != 1 && classProperty(424))
                    issue(identity + " stores a box maximum while not using the EXPLO box spawn shape");
                if (origin->intValue != 3 && classProperty(439))
                    issue(identity + " stores skeleton pairs while not using the EXPLO skeleton spawn shape");
            }
            const auto skeletonPairCount = static_cast<int>(
                std::count_if(object.properties.begin(), object.properties.end(),
                              [](const BrxPropertyRecord &property) { return property.eopid == 439; }));
            if (skeletonPairCount > 32)
                issue(identity + " has " + std::to_string(skeletonPairCount) +
                      " EXPLO skeleton pairs; ProjectCane keeps only the first 32");
            for (const auto &property : object.properties)
            {
                if (property.eopid != 439)
                    continue;
                if (property.arguments.size() != 6)
                {
                    issue(identity + " has a malformed EXPLO skeleton pair");
                    continue;
                }
                for (std::size_t endpoint = 0; endpoint < 2; ++endpoint)
                {
                    const auto targets =
                        findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[endpoint].intValue));
                    if (targets.empty())
                        issue(identity + " EXPLO skeleton endpoint references a missing object");
                    else
                    {
                        if (targets.size() != 1)
                            issue(identity + " EXPLO skeleton endpoint has multiple equally-near matches");
                        if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(targets.front())].cid, 2))
                            issue(identity + " EXPLO skeleton endpoint must resolve to an ALO");
                    }
                }
                for (std::size_t argument = 2; argument < 6; ++argument)
                    if (!std::isfinite(property.arguments[argument].floatValue[0]))
                        issue(identity + " EXPLO skeleton radius/density must be finite");
                // Negative endpoint-B values are ProjectCane's sentinel for
                // inheriting the corresponding endpoint-A value.
            }
        }
        if (object.cid == 120)
        {
            int childCount = 0;
            for (std::size_t embeddedIndex = 0; embeddedIndex < document.Objects().size(); ++embeddedIndex)
            {
                const auto &embedded = document.Objects()[embeddedIndex];
                if (embedded.parentIndex != static_cast<std::int32_t>(index))
                    continue;
                if (!CidDerivesFrom(embedded.cid, 2))
                {
                    if (CidDerivesFrom(embedded.cid, 120) || CidDerivesFrom(embedded.cid, 122))
                        ++childCount;
                    else
                        issue(identity + " EXPLG embeds " + BrxDocument::ClassName(embedded.cid) +
                              "; ProjectCane ignores non-EXPLG/EXPLS leaf records");
                    continue;
                }

                int wrapperChildren = 0;
                for (const auto &wrapped : document.Objects())
                {
                    if (wrapped.parentIndex != static_cast<std::int32_t>(embeddedIndex))
                        continue;
                    if (wrapperChildren++ >= 8)
                        break;
                    if (CidDerivesFrom(wrapped.cid, 120) || CidDerivesFrom(wrapped.cid, 122))
                        ++childCount;
                }
            }
            if (childCount == 0)
                issue(identity + " EXPLG has no explosion children");
            if (childCount > 8)
                issue(identity + " EXPLG exceeds the runtime limit of eight explosion children");
        }
        if (object.cid == 122)
        {
            for (const auto id :
                 {std::int16_t{441}, std::int16_t{442}, std::int16_t{443}, std::int16_t{469}, std::int16_t{483}})
                validateObjectReference(id, "EXPLS");
            // GRandInRange accepts reversed endpoints. Particle counts must
            // stay non-negative because FireExplsExplso converts the sampled
            // value to an allocation count, but the other ranges may reverse.
            if (const auto *count = classProperty(440);
                count && (!std::isfinite(count->floatValue[0]) || !std::isfinite(count->floatValue[1]) ||
                          count->floatValue[0] < 0.0f || count->floatValue[1] < 0.0f))
                issue(identity + " EXPLS particle-count endpoints must be finite and non-negative");
            for (const auto id : {std::int16_t{452}, std::int16_t{457}, std::int16_t{473}})
                if (const auto *range = classProperty(id);
                    range && (!std::isfinite(range->floatValue[0]) || !std::isfinite(range->floatValue[1])))
                    issue(identity + " EXPLS " + std::string(PropertyName(id) ? PropertyName(id) : "range") +
                          " must contain finite endpoints");
            for (const auto id :
                 {std::int16_t{444}, std::int16_t{447}, std::int16_t{448}, std::int16_t{451}, std::int16_t{460},
                  std::int16_t{466}, std::int16_t{471}, std::int16_t{478}, std::int16_t{479}, std::int16_t{480},
                  std::int16_t{484}, std::int16_t{485}, std::int16_t{486}, std::int16_t{487}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " EXPLS " + std::string(PropertyName(id) ? PropertyName(id) : "value") +
                          " must be finite");
            if (const auto *representation = classProperty(454);
                representation && (representation->intValue < -1 || representation->intValue > 1))
                issue(identity + " has invalid EXPLS particle representation " +
                      std::to_string(representation->intValue));
            if (const auto *motion = classProperty(468); motion && (motion->intValue < -1 || motion->intValue > 2))
                issue(identity + " has invalid EXPLS motion model " + std::to_string(motion->intValue));
            if (const auto *movement = classProperty(470);
                movement && (movement->intValue < -1 || movement->intValue > 2))
                issue(identity + " has invalid EXPLS movement behavior " + std::to_string(movement->intValue));
            if (const auto *curlSpace = classProperty(450);
                curlSpace && (curlSpace->intValue < -1 || curlSpace->intValue > 1))
                issue(identity + " has invalid EXPLS curl coordinate space " + std::to_string(curlSpace->intValue));
            if (const auto *ripTypeValue = classProperty(455);
                ripTypeValue && (ripTypeValue->intValue < -1 || ripTypeValue->intValue > 27))
                issue(identity + " has invalid EXPLS RIP type " + std::to_string(ripTypeValue->intValue));
            if (const auto *trailType = classProperty(456);
                trailType && (trailType->intValue < -1 || trailType->intValue > 27))
                issue(identity + " has invalid EXPLS trail RIP type " + std::to_string(trailType->intValue));
            if (const auto *groupBehavior = classProperty(459);
                groupBehavior && (groupBehavior->intValue < -1 || groupBehavior->intValue > 2))
                issue(identity + " has invalid EXPLS RIP group behavior " + std::to_string(groupBehavior->intValue));
            if (const auto *mirror = classProperty(472); mirror && (mirror->intValue < -1 || mirror->intValue > 1))
                issue(identity + " has invalid EXPLS rolling mirror behavior " + std::to_string(mirror->intValue));
            if (const auto *shader = classProperty(461); shader && shader->intValue != -1)
            {
                const bool found =
                    std::any_of(document.Shaders().begin(), document.Shaders().end(),
                                [&](const BrxShaderRecord &candidate) { return candidate.oid == shader->intValue; });
                if (!found)
                    issue(identity + " EXPLS particle shader OID does not exist in this level");
            }
            const int representation = classProperty(454) ? classProperty(454)->intValue : 0;
            const int ripType = classProperty(455) ? classProperty(455)->intValue : -1;
            if (representation == 0)
            {
                for (const auto id :
                     {std::int16_t{441}, std::int16_t{442}, std::int16_t{443}, std::int16_t{455}, std::int16_t{456},
                      std::int16_t{457}, std::int16_t{458}, std::int16_t{459}, std::int16_t{460}, std::int16_t{475},
                      std::int16_t{476}, std::int16_t{483}, std::int16_t{484}, std::int16_t{485}, std::int16_t{486},
                      std::int16_t{487}, std::int16_t{488}})
                    if (classProperty(id))
                        issue(identity + " stores RIP-only option " + std::string(PropertyName(id)) +
                              " while rendering BLIP particles");
            }
            else if (representation == 1)
            {
                for (const auto id :
                     {std::int16_t{461}, std::int16_t{462}, std::int16_t{463}, std::int16_t{464}, std::int16_t{465},
                      std::int16_t{466}, std::int16_t{467}, std::int16_t{468}, std::int16_t{469}, std::int16_t{470},
                      std::int16_t{471}, std::int16_t{472}, std::int16_t{473}, std::int16_t{474}})
                    if (classProperty(id))
                        issue(identity + " stores BLIP-only option " + std::string(PropertyName(id)) +
                              " while rendering RIP particles");
                for (const auto id : {std::int16_t{475}, std::int16_t{476}, std::int16_t{488}})
                    if (ripType != 23 && classProperty(id))
                        issue(identity + " stores bullet behavior for a non-bullet RIP type");
                for (const auto id : {std::int16_t{485}, std::int16_t{486}, std::int16_t{487}})
                    if (ripType != 24 && classProperty(id))
                        issue(identity + " stores shrapnel behavior for a non-shrapnel RIP type");
                for (const auto id : {std::int16_t{483}, std::int16_t{484}})
                    if (ripType != 27 && classProperty(id))
                        issue(identity + " stores nested-emitter behavior for a non-emitter RIP type");
            }
        }
        if (object.cid == 96 || object.cid == 123 || object.cid == 124 || object.cid == 125)
        {
            const auto volume =
                std::find_if(document.Volumes().begin(), document.Volumes().end(), [&](const BrxVolumeRecord &candidate)
                             { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (volume == document.Volumes().end())
                issue(identity + " is missing its volume BSP payload");
            else
            {
                if (volume->planes.size() > 0xffff ||
                    volume->nodes.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u)
                    issue(identity + " volume BSP exceeds the retail surface/child index range");
                for (std::size_t plane = 0; plane < volume->planes.size(); ++plane)
                {
                    const auto &value = volume->planes[plane];
                    const float lengthSquared = value.normal[0] * value.normal[0] + value.normal[1] * value.normal[1] +
                                                value.normal[2] * value.normal[2];
                    if (!std::isfinite(value.distance) || !std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
                        issue(identity + " has invalid volume plane " + std::to_string(plane));
                }
                for (std::size_t node = 0; node < volume->nodes.size(); ++node)
                {
                    const auto &value = volume->nodes[node];
                    if (value.surfaceIndex >= volume->planes.size() ||
                        (value.negativeChild != 0xffff &&
                         (value.negativeChild >= volume->nodes.size() ||
                          value.negativeChild >
                              static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))) ||
                        (value.positiveChild != 0xffff &&
                         (value.positiveChild >= volume->nodes.size() ||
                          value.positiveChild >
                              static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))))
                        issue(identity + " has invalid volume BSP node " + std::to_string(node));
                }
                if (!volume->nodes.empty())
                {
                    std::vector<std::uint8_t> reachable(volume->nodes.size(), 0);
                    std::vector<std::uint8_t> active(volume->nodes.size(), 0);
                    bool cycle = false;
                    bool invalidLink = false;
                    const std::function<void(std::uint16_t)> visit = [&](const std::uint16_t nodeIndex)
                    {
                        if (nodeIndex == 0xffff)
                            return;
                        if (nodeIndex >= volume->nodes.size())
                        {
                            invalidLink = true;
                            return;
                        }
                        if (active[nodeIndex])
                        {
                            cycle = true;
                            return;
                        }
                        if (reachable[nodeIndex])
                            return;
                        reachable[nodeIndex] = active[nodeIndex] = 1;
                        const auto &node = volume->nodes[nodeIndex];
                        visit(node.negativeChild);
                        visit(node.positiveChild);
                        active[nodeIndex] = 0;
                    };
                    visit(0);
                    if (cycle)
                        issue(identity + " has a cyclic volume BSP");
                    if (invalidLink)
                        issue(identity + " has a volume BSP branch outside the node array");
                    const auto unreachable =
                        static_cast<std::size_t>(std::count(reachable.begin(), reachable.end(), std::uint8_t{0}));
                    if (unreachable != 0)
                        issue(identity + " has " + std::to_string(unreachable) + " unreachable volume BSP node(s)");
                }
                const float *m = object.worldTransform;
                const float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) +
                                          m[8] * (m[1] * m[6] - m[2] * m[5]);
                if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001f)
                    issue(identity + " has a singular transform, so world-to-local volume tests are undefined");
            }
        }
        if (object.cid == 125)
        {
            // EXIT warp OIDs belong to the destination level and usually cannot
            // resolve in the currently opened world. Do not report them as broken
            // local references.
            // Negative dtUnblock enables immediately after leaving the volume.
            // Non-positive dtTriggerWipe selects ASEG-derived/immediate timing.
            // Both are explicit runtime branches, so only reject non-finite data.
            for (const auto id : {std::int16_t{167}, std::int16_t{173}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " EXIT " + std::string(PropertyName(id) ? PropertyName(id) : "timing value") +
                          " must be finite");
            if (const auto *key = classProperty(164); key && (key->intValue < -1 || key->intValue > 1))
                issue(identity + " has invalid EXIT alternate destination-key mode");
            if (const auto *wipe = classProperty(166); wipe && (wipe->intValue < -1 || wipe->intValue > 3))
                issue(identity + " has invalid EXIT screen-transition type");
            if (classProperty(175))
                issue(identity +
                      " serializes runtime-only EXIT state; remove EOPID 175 and let the engine initialize it");
            const auto exit =
                std::find_if(document.Exits().begin(), document.Exits().end(), [&](const BrxExitRecord &candidate)
                             { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (exit == document.Exits().end())
                issue(identity + " is missing its EXIT payload");
            else
            {
                const bool useDefault = classProperty(171) && classProperty(171)->boolValue;
                const int alternateMode = classProperty(164) ? classProperty(164)->intValue : -1;
                if (useDefault && (classProperty(163) || classProperty(164) || classProperty(165)))
                    issue(identity + " uses default-exit progression, so its explicit destination routing is dormant");
                if (!useDefault && exit->destinationLevel.empty())
                    issue(identity + " has no primary destination level and does not use the default exit");
                if (!useDefault && (!classProperty(163) || classProperty(163)->intValue < 0))
                    issue(identity + " has no primary destination WARP OID");
                if (exit->destinationLevel.size() > 31)
                    issue(identity + " primary destination level exceeds the EXIT 31-character limit");
                if (exit->alternateLevel.size() > 31)
                    issue(identity + " alternate destination level exceeds the EXIT 31-character limit");
                if (alternateMode >= 0 && exit->alternateLevel.empty())
                    issue(identity + " enables alternate key routing but has no alternate destination level");
                if (alternateMode >= 0 && (!classProperty(165) || classProperty(165)->intValue < 0))
                    issue(identity + " enables alternate key routing but has no alternate destination WARP OID");
                if (alternateMode < 0 && (!exit->alternateLevel.empty() || classProperty(165)))
                    issue(identity + " stores an alternate destination while alternate key routing is disabled");
                if (classProperty(168) && classProperty(168)->boolValue)
                {
                    const std::size_t defaults = static_cast<std::size_t>(std::count_if(
                        document.Objects().begin(), document.Objects().end(),
                        [](const BrxObjectRecord &candidate)
                        {
                            if (candidate.cid != 125)
                                return false;
                            const auto property =
                                std::find_if(candidate.properties.begin(), candidate.properties.end(),
                                             [](const BrxPropertyRecord &value) { return value.eopid == 168; });
                            return property != candidate.properties.end() && property->boolValue;
                        }));
                    if (defaults > 1)
                        issue(identity + " is one of multiple default EXITs; retail uses the first match");
                }
                if (classProperty(169) && classProperty(169)->boolValue)
                {
                    std::int32_t root = static_cast<std::int32_t>(index);
                    while (root >= 0 && static_cast<std::size_t>(root) < document.Objects().size() &&
                           document.Objects()[static_cast<std::size_t>(root)].parentIndex >= 0)
                        root = document.Objects()[static_cast<std::size_t>(root)].parentIndex;
                    if (root != static_cast<std::int32_t>(index) && root >= 0)
                        issue(identity + " is keyed, so ProjectCane removes hierarchy root OID " +
                              std::to_string(document.Objects()[static_cast<std::size_t>(root)].oid) +
                              "; verify the root has no unrelated objects");
                }
                std::size_t embeddedAsegs = 0;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.parentIndex != static_cast<std::int32_t>(index) ||
                        !CidDerivesFrom(candidate.cid, 131))
                        continue;
                    ++embeddedAsegs;
                    if (std::none_of(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                                     [&](const BrxAsegRecord &segment)
                                     { return segment.ownerObjectIndex == static_cast<std::int32_t>(candidateIndex); }))
                        issue(identity + " owns an embedded ASEG without a decoded animation payload");
                }
                if (embeddedAsegs !=
                    static_cast<std::size_t>((std::max)(0, static_cast<int>(exit->embeddedAnimationCount))))
                    issue(identity + " embedded ASEG ownership does not match its serialized count");
            }
        }
        if (object.cid == 127)
        {
            validateObjectReference(629, "HPNT");
            if (const auto *height = classProperty(631); height && !std::isfinite(height->floatValue[0]))
                issue(identity + " HPNT maximum jump-target height must be finite");
            if (const auto *radius = classProperty(626);
                radius && (!std::isfinite(radius->floatValue[0]) || radius->floatValue[0] <= 0.0f))
                issue(identity +
                      " has a zero, negative, or non-finite HPNT radius; GetHpntClosestHidePos divides by this value");
            if (const auto *posture = classProperty(627); posture && (posture->intValue < 0 || posture->intValue > 7))
                issue(identity + " has an invalid HPNT cover posture; selectable hide targets require 0..7");
            if (const auto *tunnel = classProperty(628); tunnel && !tunnel->boolValue)
                if (const auto *camera = classProperty(629); camera && camera->intValue != -1)
                    issue(identity + " assigns a hide-camera TN while tunnel behavior is disabled");
        }
        if (object.cid == 128)
        {
            if (const auto *radius = classProperty(836); radius && !std::isfinite(radius->floatValue[0]))
                issue(identity + " has a non-finite JMT capture radius");
            validateObjectReference(837, "JMT");
            if (const auto *ignored = classProperty(837); ignored && ignored->intValue != -1)
            {
                const auto matches = findHierarchyNearestObjects(static_cast<std::int16_t>(ignored->intValue));
                if (matches.empty())
                    issue(identity + " ignored-object OID " + std::to_string(ignored->intValue) +
                          " does not resolve from the JMT hierarchy context");
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(matches.front())].cid, 3))
                    issue(identity + " ignored-object OID " + std::to_string(ignored->intValue) +
                          " does not resolve to an SO-derived object");
                if (matches.size() > 1)
                    issue(
                        identity + " ignored-object OID " + std::to_string(ignored->intValue) +
                        " has multiple equally-near matches; runtime list order determines which object is changed to ZOK_Ignore");
            }
        }
        if (object.cid == 129)
        {
            // The setter imposes no non-negative constraint. Negative values
            // are unusual but have defined behavior: they reject Sly unless he
            // is sufficiently below the marker. Do not destroy that editability.
            if (const auto *height = classProperty(1030); height && !std::isfinite(height->floatValue[0]))
                issue(identity +
                      " has a non-finite SPIRE maximum-above height; retail uses FLT_MAX for unlimited reach");
            if (const auto *height = classProperty(1030); height && height->kind != BrxPropertyKind::Float)
                issue(identity + " serializes SPIRE EOPID 1030 with the wrong type; ProjectCane defines it as Float");
            if (const auto *history = classProperty(1031); history && history->kind != BrxPropertyKind::Bool)
                issue(identity + " serializes SPIRE EOPID 1031 with the wrong type; ProjectCane defines it as Bool");
            const float x = object.worldTransform[12];
            const float y = object.worldTransform[13];
            const float z = object.worldTransform[14];
            for (std::size_t candidateIndex = index + 1; candidateIndex < document.Objects().size(); ++candidateIndex)
            {
                const auto &candidate = document.Objects()[candidateIndex];
                if (candidate.cid != 129)
                    continue;
                const float dx = candidate.worldTransform[12] - x;
                const float dy = candidate.worldTransform[13] - y;
                const float dz = candidate.worldTransform[14] - z;
                if (dx * dx + dy * dy + dz * dz <= 0.01f)
                    issue(identity + " overlaps SPIRE OID " + std::to_string(candidate.oid) +
                          "; coincident targets compete by runtime list order");
            }
        }
        if (object.cid == 130)
        {
            if (std::none_of(document.Objects().begin(),
                             document.Objects().begin() + static_cast<std::ptrdiff_t>(index),
                             [](const BrxObjectRecord &candidate) { return candidate.cid == 130; }))
            {
                const auto scanObjectCount =
                    std::count_if(document.Objects().begin(), document.Objects().end(),
                                  [](const BrxObjectRecord &candidate) { return candidate.cid == 130; });
                if (scanObjectCount > 32)
                    issue(identity + " world contains " + std::to_string(scanObjectCount) +
                          " SCAN objects, but DrawBinocScan searches only the first 32 runtime list entries");
            }
            const auto payloadCount = scanOwners[index];
            const auto scan =
                std::find_if(document.Scans().begin(), document.Scans().end(), [&](const BrxScanRecord &candidate)
                             { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (scan == document.Scans().end())
                issue(identity + " is missing its SCAN text payload");
            else
            {
                if (payloadCount != 1)
                    issue(identity + " owns " + std::to_string(payloadCount) +
                          " SCAN text payloads; expected exactly one");
                // Empty strings, embedded nulls, control bytes, and strings
                // longer than the 255-byte display buffer are all accepted by
                // ReadStringSw. DrawBinocScan simply stops at the first null or
                // after 255 bytes, so none of those is malformed SCAN data.
            }
        }
        if (object.cid == 131 || object.cid == 132)
        {
            if (const auto *repeat = classProperty(178); repeat && (repeat->intValue < -1 || repeat->intValue > 3))
                issue(identity + " ASEG repeat mode is outside the engine range -1..3");
            if (const auto *speed = classProperty(182); speed && !std::isfinite(speed->floatValue[0]))
                issue(identity + " ASEG master playback speed is not finite");
            for (const auto runtimeId : {std::int16_t{185}, std::int16_t{186}, std::int16_t{187}, std::int16_t{188}})
                if (classProperty(runtimeId) != nullptr)
                    issue(identity + " serializes runtime-only ASEG EOPID " + std::to_string(runtimeId));

            const auto payloadCount =
                std::count_if(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                              [&](const BrxAsegRecord &candidate)
                              { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            const std::size_t expectedPayloadCount = object.cid == 131 ? 1u : 0u;
            if (payloadCount != expectedPayloadCount)
                issue(identity + " has " + std::to_string(payloadCount) + " decoded animation payloads (expected " +
                      std::to_string(expectedPayloadCount) + ")");
            const auto payload =
                std::find_if(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                             [&](const BrxAsegRecord &candidate)
                             { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (payload != document.AnimationSegments().end())
            {
                if (payload->maximumFrame < 0)
                    issue(identity + " has a negative maximum animation frame");
                if (payload->searchRootOids.size() > 65535)
                    issue(identity + " exceeds the U16 limit of 65535 animation search roots");
                if (payload->channels.size() > 127)
                    issue(identity + " exceeds the signed S8 limit of 127 channels");
                std::int32_t appliedRootIndex = static_cast<std::int32_t>(index);
                if (payload->rootOid != -1)
                {
                    const auto roots = findHierarchyNearestObjects(payload->rootOid, static_cast<std::int32_t>(index));
                    if (roots.empty())
                        issue(identity + " animation root references missing hierarchy-nearest OID " +
                              std::to_string(payload->rootOid));
                    else
                    {
                        appliedRootIndex = roots.front();
                        if (roots.size() > 1)
                            issue(identity + " animation root OID " + std::to_string(payload->rootOid) +
                                  " has multiple equally-near matches; runtime OID-list order selects one");
                        if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(appliedRootIndex)].cid, 2))
                            issue(identity + " animation root does not resolve to an ALO-derived object");
                    }
                }
                std::vector<std::int32_t> resolvedSearchRoots;
                for (const auto oid : payload->searchRootOids)
                {
                    const auto roots = findHierarchyNearestObjects(oid, appliedRootIndex);
                    if (roots.empty())
                        issue(identity + " search root references missing hierarchy-nearest OID " +
                              std::to_string(oid));
                    else
                    {
                        resolvedSearchRoots.push_back(roots.front());
                        if (roots.size() > 1)
                            issue(identity + " search-root OID " + std::to_string(oid) +
                                  " has multiple equally-near matches");
                        if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(roots.front())].cid, 2))
                            issue(identity + " search root OID " + std::to_string(oid) +
                                  " does not resolve to an ALO-derived object");
                    }
                }
                std::unordered_set<std::int16_t> channelOids;
                std::size_t stripChannels = 0;
                for (const auto &channel : payload->channels)
                {
                    auto channelTargets = findHierarchyNearestObjects(channel.targetOid, appliedRootIndex);
                    if (channelTargets.empty())
                    {
                        for (const std::int32_t searchRootIndex : resolvedSearchRoots)
                        {
                            channelTargets = findContextDescendants(channel.targetOid, searchRootIndex);
                            if (!channelTargets.empty())
                                break;
                        }
                    }
                    if (channelTargets.empty())
                        issue(identity + " channel target OID " + std::to_string(channel.targetOid) +
                              " cannot be resolved by the ASEG root/search-root sequence");
                    else
                    {
                        if (channelTargets.size() > 1)
                            issue(identity + " channel target OID " + std::to_string(channel.targetOid) +
                                  " has multiple matches at the selected lookup tier");
                        if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(channelTargets.front())].cid,
                                            2))
                            issue(identity + " channel target OID " + std::to_string(channel.targetOid) +
                                  " does not resolve to an ALO-derived object");
                    }
                    if (!channelOids.insert(channel.targetOid).second)
                        issue(identity + " repeats channel target OID " + std::to_string(channel.targetOid));
                    if (channel.stripChannel)
                        ++stripChannels;
                    if ((channel.flags & 0xc0u) != 0)
                        issue(identity + " channel OID " + std::to_string(channel.targetOid) +
                              " uses unsupported flag bits");
                    if (channel.poses.size() > 255)
                        issue(identity + " channel OID " + std::to_string(channel.targetOid) +
                              " exceeds the U8 limit of 255 pose-controller slots");
                }
                if (stripChannels > 1)
                    issue(identity + " declares more than one strip channel");
                std::int16_t previousFrame = -1;
                std::unordered_set<std::int16_t> labels;
                std::array<std::size_t, 3> laneCounts{};
                const auto validateAsegNearest = [&](const std::int16_t oid, const std::int16_t requiredCid,
                                                     const std::string &role, const std::int32_t contextIndex,
                                                     const bool optional = true)
                {
                    if (optional && oid == -1)
                        return;
                    const auto matches = findHierarchyNearestObjects(oid, contextIndex);
                    if (matches.empty())
                    {
                        issue(identity + " " + role + " references missing OID " + std::to_string(oid));
                        return;
                    }
                    if (matches.size() > 1)
                        issue(identity + " " + role + " OID " + std::to_string(oid) +
                              " has multiple equally-near matches");
                    const auto &target = document.Objects()[static_cast<std::size_t>(matches.front())];
                    if (requiredCid >= 0 && target.cid != requiredCid && !CidDerivesFrom(target.cid, requiredCid))
                        issue(identity + " " + role + " references incompatible " +
                              std::string(BrxDocument::ClassName(target.cid)) + " OID " + std::to_string(oid));
                };
                const std::int32_t asegParentIndex =
                    object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
                for (std::size_t eventIndex = 0; eventIndex < payload->events.size(); ++eventIndex)
                {
                    const auto &event = payload->events[eventIndex];
                    const auto eventIdentity = identity + " event " + std::to_string(eventIndex + 1);
                    const auto laneIndex = static_cast<std::size_t>(event.lane);
                    if (laneIndex >= laneCounts.size())
                        issue(eventIdentity + " has an invalid event lane");
                    else
                        ++laneCounts[laneIndex];
                    if (event.type < -1 || event.type >= 60)
                        issue(eventIdentity + " has an invalid type");
                    if (event.lane == BrxAsegEventLane::Frame)
                    {
                        if (event.frame < 0 || event.frame > payload->maximumFrame)
                            issue(eventIdentity + " lies outside the segment duration");
                        if (event.frame < previousFrame)
                            issue(eventIdentity + " is out of chronological order");
                        previousFrame = event.frame;
                    }
                    if (event.type == 39 && !labels.insert(event.shorts[0]).second)
                        issue(eventIdentity + " duplicates label OID " + std::to_string(event.shorts[0]));
                    if (event.type == 0 && event.shorts[1] != -1)
                        validateAsegNearest(event.shorts[1], 2, "sound target", appliedRootIndex, true);
                    if (event.type == 6)
                        validateAsegNearest(event.shorts[0], 101, "dialog target", appliedRootIndex, true);
                    if (event.type == 12 || event.type == 13)
                        validateAsegNearest(event.shorts[0], 109, "camera target", static_cast<std::int32_t>(index),
                                            true);
                    if (event.type == 25 || event.type == 26)
                    {
                        validateTypedOid(event.shorts[1], 116, "wipe WARP target");
                        if (event.text.size() > 31)
                            issue(eventIdentity + " destination level exceeds the fixed 32-byte field");
                    }
                    if (event.type == 28)
                    {
                        validateAsegNearest(event.shorts[0], 131, "nested animation", asegParentIndex, true);
                        validateAsegNearest(event.shorts[1], 2, "nested animation root", asegParentIndex, true);
                    }
                    if (event.type == 38 && event.shorts[0] != -1)
                        validateAsegNearest(event.shorts[0], 2, "user-message target", appliedRootIndex, true);
                    if (event.type == 41)
                        validateAsegNearest(event.shorts[0], -1, "waypoint target", appliedRootIndex, true);
                    if (event.type == 45)
                        validateAsegNearest(event.shorts[0], 2, "pose-combo target", appliedRootIndex, true);
                    if (event.type == 44 || event.type == 51)
                        if (event.text.size() > 31)
                            issue(eventIdentity + " resource name exceeds the fixed 32-byte field");
                    if (event.type == 46)
                    {
                        validateAsegNearest(event.shorts[0], 136, "state machine", appliedRootIndex, true);
                        validateAsegNearest(event.shorts[1], 2, "state-machine root", appliedRootIndex, true);
                    }
                    if (event.type == 47)
                    {
                        validateAsegNearest(event.shorts[0], 136, "state machine", appliedRootIndex, true);
                        validateAsegNearest(event.shorts[1], 2, "state-machine root", appliedRootIndex, true);
                    }
                    if (event.type == 48)
                    {
                        validateAsegNearest(event.shorts[0], 121, "explosion effect", appliedRootIndex, true);
                        validateAsegNearest(event.shorts[1], -1, "explosion reference", appliedRootIndex, true);
                        validateAsegNearest(event.shorts[2], 3, "explosion touch object", appliedRootIndex, true);
                        if (!std::isfinite(event.floats[0]))
                            issue(eventIdentity + " explosion scale is not finite");
                    }
                    if (event.type == 54 && event.text.size() > 65535)
                        issue(eventIdentity + " credit text exceeds the U16 string-length limit");
                    if (event.type == 59)
                        validateAsegNearest(event.shorts[0], 143, "script target", appliedRootIndex, true);
                    for (const float value : event.floats)
                        if (!std::isfinite(value))
                        {
                            issue(eventIdentity + " contains a non-finite floating-point argument");
                            break;
                        }
                    if (event.type == 9 || event.type == 10 || event.type == 16)
                        validateAsegNearest(event.shorts[0], event.type == 16 ? 3 : 2, "event object", appliedRootIndex,
                                            true);
                    if ((event.type >= 17 && event.type <= 24) || (event.type >= 29 && event.type <= 32) ||
                        event.type == 35 || event.type == 36)
                        if (!channelOids.contains(event.shorts[0]))
                            issue(eventIdentity + " references channel OID " + std::to_string(event.shorts[0]) +
                                  " that is not bound by this ASEG");
                }
                if (laneCounts[static_cast<std::size_t>(BrxAsegEventLane::Apply)] > 127)
                    issue(identity + " exceeds the signed S8 limit of 127 apply events");
                if (laneCounts[static_cast<std::size_t>(BrxAsegEventLane::Frame)] > 127)
                    issue(identity + " exceeds the signed S8 limit of 127 frame events");
                if (laneCounts[static_cast<std::size_t>(BrxAsegEventLane::Retract)] > 127)
                    issue(identity + " exceeds the signed S8 limit of 127 retract events");
            }
        }
        if (object.cid == 135)
        {
            std::unordered_set<std::int16_t> members;
            std::size_t memberCount = 0;
            const std::int32_t freezeParentIndex =
                object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
            for (const auto &property : object.properties)
                if (property.eopid == 285)
                {
                    ++memberCount;
                    if (property.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                        property.intValue > (std::numeric_limits<std::int16_t>::max)())
                    {
                        issue(identity + " freeze-group member OID " + std::to_string(property.intValue) +
                              " cannot be serialized by the signed 16-bit OID field");
                        continue;
                    }
                    const auto memberOid = static_cast<std::int16_t>(property.intValue);
                    const auto childMatches = findContextDescendants(memberOid, freezeParentIndex);
                    const auto compatible = std::count_if(
                        childMatches.begin(), childMatches.end(),
                        [&](const std::int32_t candidateIndex)
                        {
                            return CidDerivesFrom(document.Objects()[static_cast<std::size_t>(candidateIndex)].cid, 2);
                        });
                    if (compatible == 0)
                        issue(identity + " freeze-group member OID " + std::to_string(memberOid) +
                              " does not resolve to an ALO below the FRZG parent context");
                    else if (compatible > 1)
                        issue(identity + " freeze-group member OID " + std::to_string(memberOid) +
                              " is ambiguous below the FRZG parent context");
                    if (!members.insert(static_cast<std::int16_t>(property.intValue)).second)
                        issue(identity + " repeats freeze-group member OID " + std::to_string(property.intValue));
                }
            if (memberCount > 8)
                issue(identity + " exceeds the FRZG limit of eight members");
        }
        if (object.cid == 136)
        {
            const auto machine = std::find_if(
                document.StateMachines().begin(), document.StateMachines().end(), [&](const BrxSmRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (machine == document.StateMachines().end())
                issue(identity + " has no decoded state-machine payload");
            else
            {
                if (machine->states.size() > 255 || machine->transitions.size() > 255)
                    issue(identity + " exceeds the U8 state/transition count limit");
                std::unordered_set<std::int16_t> stateOids;
                for (const auto &state : machine->states)
                    if (!stateOids.insert(state.oid).second)
                        issue(identity + " repeats state OID " + std::to_string(state.oid));
                for (const auto &state : machine->states)
                    if (state.automaticNextOid != -1 && !stateOids.contains(state.automaticNextOid))
                        issue(identity + " state OID " + std::to_string(state.oid) +
                              " automatically advances to missing state OID " + std::to_string(state.automaticNextOid));
                std::unordered_set<std::int32_t> ownedAnimations;
                for (std::size_t transitionIndex = 0; transitionIndex < machine->transitions.size(); ++transitionIndex)
                {
                    const auto &transition = machine->transitions[transitionIndex];
                    const auto label = identity + " transition " + std::to_string(transitionIndex + 1);
                    if (!stateOids.contains(transition.fromOid) || !stateOids.contains(transition.toOid))
                        issue(label + " references a missing state");
                    if (!std::isfinite(transition.probability))
                        issue(label + " has a non-finite probability");
                    if ((transition.flags & ~3) != 0)
                        issue(label + " uses unknown flag bits");
                    if (transition.ownsAnimation)
                    {
                        if (!ownedAnimations.insert(transition.animationObjectIndex).second)
                            issue(label + " shares its embedded ASEG with another transition");
                        if (transition.animationObjectIndex < 0 ||
                            static_cast<std::size_t>(transition.animationObjectIndex) >= document.Objects().size())
                            issue(label + " has no valid embedded ASEG object");
                        else if (!CidDerivesFrom(
                                     document.Objects()[static_cast<std::size_t>(transition.animationObjectIndex)].cid,
                                     131))
                            issue(label + " embeds an object that is not an ASEG");
                        else if (document.Objects()[static_cast<std::size_t>(transition.animationObjectIndex)]
                                     .parentIndex != static_cast<std::int32_t>(index))
                            issue(label + " ASEG is not owned directly by its SM");
                    }
                    else if (transition.animationObjectIndex != -1)
                        issue(label + " is instant but retains an embedded-animation index");
                }
                std::size_t directChildCount = 0;
                for (std::size_t childIndex = 0; childIndex < document.Objects().size(); ++childIndex)
                {
                    const auto &child = document.Objects()[childIndex];
                    if (child.parentIndex != static_cast<std::int32_t>(index) || child.proxyClone)
                        continue;
                    bool attachedProxyChild = false;
                    for (const auto &proxy : document.Proxies())
                        if (std::find(proxy.attachedChildObjectIndices.begin(), proxy.attachedChildObjectIndices.end(),
                                      static_cast<std::int32_t>(childIndex)) != proxy.attachedChildObjectIndices.end())
                        {
                            attachedProxyChild = true;
                            break;
                        }
                    if (attachedProxyChild)
                        continue;
                    const bool sameProxyRoot = object.proxyRootIndex >= 0
                                                   ? child.proxyRootIndex == object.proxyRootIndex
                                                   : child.proxyRootIndex < 0;
                    const bool nestedProxyWrapper = child.cid == 83 && child.proxyRootIndex < 0;
                    if (!sameProxyRoot && !nestedProxyWrapper)
                        continue;
                    ++directChildCount;
                    if (ownedAnimations.find(static_cast<std::int32_t>(childIndex)) == ownedAnimations.end())
                        issue(identity + " owns an object that is not referenced by an animated transition");
                }
                if (directChildCount != ownedAnimations.size())
                    issue(identity + " embedded ASEG ownership does not match its animated transitions");
            }
            if (classProperty(514) != nullptr)
                issue(identity + " serializes runtime-only Apply State Machine EOPID 514");
        }
        if (object.cid == 137)
        {
            const auto runtimeParent = [&](const std::int32_t objectIndex)
            {
                if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= document.Objects().size())
                    return -1;
                const auto &record = document.Objects()[static_cast<std::size_t>(objectIndex)];
                return record.proxyRuntimeParentIndex >= 0 ? record.proxyRuntimeParentIndex : record.parentIndex;
            };
            const auto contextAncestorDepth = [&](std::int32_t from, std::int32_t to)
            {
                std::unordered_map<std::int32_t, int> ancestors;
                if (from >= 0 && static_cast<std::size_t>(from) < document.Objects().size() &&
                    !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(from)].cid, 2))
                    from = runtimeParent(from);
                int depth = 1;
                for (std::size_t guard = 0; from >= 0 && static_cast<std::size_t>(from) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    ancestors.emplace(from, depth++);
                    const auto next = runtimeParent(from);
                    if (next == from)
                        break;
                    from = next;
                }
                for (std::size_t guard = 0; to >= 0 && static_cast<std::size_t>(to) < document.Objects().size() &&
                                            guard++ <= document.Objects().size();)
                {
                    if (const auto found = ancestors.find(to); found != ancestors.end())
                        return found->second;
                    const auto next = runtimeParent(to);
                    if (next == to)
                        break;
                    to = next;
                }
                return (std::numeric_limits<int>::max)() / 4;
            };
            const auto resolveNearestFor = [&](const std::size_t ownerIndex, const std::int16_t oid,
                                               const std::int16_t requiredCid) -> std::int32_t
            {
                std::int32_t best = -1;
                int bestDistance = (std::numeric_limits<int>::max)();
                const auto consider = [&](const std::size_t candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (!CidDerivesFrom(candidate.cid, requiredCid))
                        return;
                    const int distance = contextAncestorDepth(static_cast<std::int32_t>(ownerIndex),
                                                              static_cast<std::int32_t>(candidateIndex));
                    if (distance < bestDistance)
                    {
                        best = static_cast<std::int32_t>(candidateIndex);
                        bestDistance = distance;
                    }
                };
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                {
                    const auto &candidate = document.Objects()[candidateIndex];
                    if (candidate.oid != oid)
                        continue;
                    if (candidate.cid != 83)
                    {
                        consider(candidateIndex);
                        continue;
                    }
                    for (const auto &proxy : document.Proxies())
                    {
                        if (proxy.ownerObjectIndex != static_cast<std::int32_t>(candidateIndex))
                            continue;
                        for (const auto &root : proxy.roots)
                            if (root.instantiatedRootObjectIndex >= 0 &&
                                static_cast<std::size_t>(root.instantiatedRootObjectIndex) < document.Objects().size())
                                consider(static_cast<std::size_t>(root.instantiatedRootObjectIndex));
                    }
                }
                return best;
            };
            const auto resolveNearest = [&](const std::int16_t oid, const std::int16_t requiredCid) -> std::int32_t
            { return resolveNearestFor(index, oid, requiredCid); };
            std::unordered_set<std::int32_t> resolvedGuards;
            for (const auto [eopid, requiredCid, limit, role] :
                 {std::tuple<std::int16_t, std::int16_t, std::size_t, const char *>{694, 8, 16, "guard"},
                  {695, 115, 16, "search marker"}})
            {
                std::size_t count = 0;
                for (const auto &property : object.properties)
                    if (property.eopid == eopid)
                    {
                        ++count;
                        if (property.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                            property.intValue > (std::numeric_limits<std::int16_t>::max)())
                        {
                            issue(identity + " " + role + " OID " + std::to_string(property.intValue) +
                                  " cannot be serialized by the signed 16-bit OTYP_Oid field");
                            continue;
                        }
                        const auto resolved = resolveNearest(static_cast<std::int16_t>(property.intValue), requiredCid);
                        if (resolved < 0)
                            issue(identity + " " + role + " OID " + std::to_string(property.intValue) +
                                  " does not resolve to a compatible object in the SGG hierarchy/proxy context");
                        else if (eopid == 694)
                        {
                            resolvedGuards.insert(resolved);
                        }
                        else
                        {
                            const auto &marker = document.Objects()[static_cast<std::size_t>(resolved)];
                            bool finite = true;
                            for (const float component : marker.worldTransform)
                                finite = finite && std::isfinite(component);
                            if (!finite)
                                issue(identity + " search marker OID " + std::to_string(property.intValue) +
                                      " has a non-finite transform");
                        }
                    }
                if (count > limit)
                    issue(identity + " exceeds the 16-entry " + role + " limit");
            }
            if (resolvedGuards.empty())
                issue(identity + " has no resolvable guards; it will never leave its empty/dead runtime state");

            std::unordered_set<std::int32_t> connectedAlarms;
            for (std::size_t alarmIndex = 0; alarmIndex < document.Objects().size(); ++alarmIndex)
            {
                const auto &alarm = document.Objects()[alarmIndex];
                if (!CidDerivesFrom(alarm.cid, 47))
                    continue;
                for (const auto &property : alarm.properties)
                    if (property.eopid == 291 && resolvedGuards.contains(resolveNearestFor(
                                                     alarmIndex, static_cast<std::int16_t>(property.intValue), 8)))
                    {
                        connectedAlarms.insert(static_cast<std::int32_t>(alarmIndex));
                        break;
                    }
            }
            if (connectedAlarms.size() > 4)
                issue(identity + " resolves to " + std::to_string(connectedAlarms.size()) +
                      " alarms, but the runtime SGG stores only four unique alarm pointers");

            for (const auto guardIndex : resolvedGuards)
            {
                std::size_t ownerCount = 0;
                for (std::size_t squadIndex = 0; squadIndex < document.Objects().size(); ++squadIndex)
                {
                    const auto &otherSquad = document.Objects()[squadIndex];
                    if (otherSquad.cid != 137)
                        continue;
                    for (const auto &property : otherSquad.properties)
                    {
                        if (property.eopid != 694)
                            continue;
                        if (resolveNearestFor(squadIndex, static_cast<std::int16_t>(property.intValue), 8) ==
                            guardIndex)
                        {
                            ++ownerCount;
                            break;
                        }
                    }
                }
                if (ownerCount > 1)
                    issue(
                        identity + " guard OID " +
                        std::to_string(document.Objects()[static_cast<std::size_t>(guardIndex)].oid) +
                        " is authored in multiple SGG rosters; the last successful bind overwrites its group pointer");
            }
        }
        if (object.cid == 138)
        {
            if (classProperty(1108) != nullptr)
                issue(identity + " serializes runtime-only Random Point EOPID 1108");
            const auto payloadCount = std::count_if(
                document.PathZones().begin(), document.PathZones().end(), [&](const BrxPathZoneRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (payloadCount != 1)
                issue(identity + " owns " + std::to_string(payloadCount) +
                      " decoded path-zone payloads instead of one");
            const auto zone = std::find_if(document.PathZones().begin(), document.PathZones().end(),
                                           [&](const BrxPathZoneRecord &candidate)
                                           { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (zone == document.PathZones().end())
                issue(identity + " has no decoded path-zone payload");
            else
            {
                for (const auto &pathIssue : AuditPathZone(*zone))
                    issue(identity + " " + pathIssue);
            }
        }
        if (object.cid == 139)
        {
            const auto *hostProperty = classProperty(747);
            const auto *touchProperty = classProperty(748);
            std::vector<std::int32_t> hostMatches;
            if (hostProperty != nullptr && hostProperty->intValue != -1)
            {
                hostMatches = findHierarchyNearestObjects(static_cast<std::int16_t>(hostProperty->intValue),
                                                          static_cast<std::int32_t>(index));
                if (hostMatches.empty())
                    issue(identity + " host OID " + std::to_string(hostProperty->intValue) +
                          " does not resolve through retail hierarchy-nearest lookup");
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(hostMatches.front())].cid, 2))
                    issue(identity + " host OID does not resolve to an ALO-derived object");
                if (hostMatches.size() > 1)
                    issue(identity + " host OID " + std::to_string(hostProperty->intValue) +
                          " has multiple equally-near matches");
            }
            if (!hostMatches.empty() && touchProperty != nullptr && touchProperty->intValue != -1)
            {
                const auto touchMatches =
                    findContextDescendants(static_cast<std::int16_t>(touchProperty->intValue), hostMatches.front());
                if (touchMatches.empty())
                    issue(identity + " touch joint OID " + std::to_string(touchProperty->intValue) +
                          " is not an ALO child/descendant of the resolved reach host");
                else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(touchMatches.front())].cid, 2))
                    issue(identity + " touch joint does not resolve to an ALO-derived object");
                if (touchMatches.size() > 1)
                    issue(identity + " touch joint OID " + std::to_string(touchProperty->intValue) +
                          " is ambiguous below the resolved reach host");
            }
            if (const auto *mode = classProperty(746); mode && (mode->intValue < -1 || mode->intValue > 1))
                issue(identity + " RCHM reach mode is outside the engine range -1..1");
            if (const auto *attacks = classProperty(749); attacks && (attacks->intValue & ~(2 | 4 | 8 | 16 | 32)) != 0)
                issue(identity + " RCHM allowed-attack mask contains unknown bits");
            for (const auto eopid : {std::int16_t{750}, std::int16_t{751}})
                if (const auto *coefficients = classProperty(eopid))
                {
                    if (coefficients->kind != BrxPropertyKind::Dynamic || coefficients->arguments.size() != 3)
                        issue(identity + " RCHM coefficient record " + std::to_string(eopid) +
                              " must contain three float arguments");
                    else if (!std::isfinite(coefficients->arguments[0].floatValue[0]) ||
                             !std::isfinite(coefficients->arguments[1].floatValue[0]) ||
                             !std::isfinite(coefficients->arguments[2].floatValue[0]))
                        issue(identity + " RCHM coefficient record " + std::to_string(eopid) +
                              " contains a non-finite value");
                    else if (coefficients->arguments[0].floatValue[0] < -1.0f ||
                             coefficients->arguments[0].floatValue[0] > 1.0f ||
                             coefficients->arguments[1].floatValue[0] < 0.0f ||
                             coefficients->arguments[1].floatValue[0] > 1.0f ||
                             coefficients->arguments[2].floatValue[0] < 0.0f ||
                             coefficients->arguments[2].floatValue[0] > 3.0f)
                        issue(identity + " RCHM coefficient record " + std::to_string(eopid) +
                              " is outside the authored reach-grid range");
                }
            const auto reach = std::find_if(document.ReachMaps().begin(), document.ReachMaps().end(),
                                            [&](const BrxRchmRecord &candidate)
                                            { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (reach == document.ReachMaps().end())
                issue(identity + " has no decoded reach-map payload");
            else
            {
                const BrxObjectRecord *resolvedHost = nullptr;
                if (!hostMatches.empty())
                    resolvedHost = &document.Objects()[static_cast<std::size_t>(hostMatches.front())];
                for (const auto oid : reach->animationOids)
                {
                    if (resolvedHost == nullptr)
                        continue;
                    const auto hostIndex = static_cast<std::int32_t>(resolvedHost - document.Objects().data());
                    const auto childMatches = findContextDescendants(oid, hostIndex);
                    const bool childAseg =
                        std::any_of(childMatches.begin(), childMatches.end(),
                                    [&](const std::int32_t candidateIndex)
                                    {
                                        return CidDerivesFrom(
                                            document.Objects()[static_cast<std::size_t>(candidateIndex)].cid, 131);
                                    });
                    if (!childAseg)
                        issue(identity + " animation sample OID " + std::to_string(oid) +
                              " is not an ASEG child/descendant of the reach host; PostRchmLoad will not bind it");
                }
                if (reach->enabledPositions.size() != reach->positions.size())
                    issue(identity + " reach-grid enabled mask does not match its point count");
                if (reach->gridResolution <= 0)
                    issue(identity + " has a non-positive reach-grid resolution");
                else
                {
                    const std::size_t resolution = static_cast<std::size_t>(reach->gridResolution);
                    const std::size_t expected = (resolution * 2 + 1) * (resolution + 1) * (resolution * 3 + 1);
                    if (reach->positions.size() != expected)
                        issue(identity + " reach-grid point count does not match its resolution");
                }
                if (!std::isfinite(reach->geometryRadius) || reach->geometryRadius < 0.0f)
                    issue(identity + " has an invalid reach-grid geometry radius");
                if (reach->positions.size() >
                        static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
                    reach->surfaces.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u)
                    issue(identity + " reach geometry exceeds the retail signed index range");
                for (const auto &position : reach->positions)
                    if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
                    {
                        issue(identity + " has a non-finite reach-grid position");
                        break;
                    }
                std::size_t actualEdgeCount = 0;
                for (const auto &surface : reach->surfaces)
                {
                    if (!std::isfinite(surface.radius) || surface.radius < 0.0f || !std::isfinite(surface.center[0]) ||
                        !std::isfinite(surface.center[1]) || !std::isfinite(surface.center[2]))
                        issue(identity + " has invalid reach-grid surface bounds");
                    for (const auto position : surface.planePositions)
                        if (position >= reach->positions.size())
                            issue(identity + " has a surface plane with an invalid grid-point index");
                    actualEdgeCount += surface.edges.size();
                    if (surface.edges.size() > static_cast<std::size_t>((std::numeric_limits<std::int8_t>::max)()))
                        issue(identity + " has a surface exceeding the retail signed edge-count range");
                    for (const auto &edge : surface.edges)
                    {
                        if (edge.firstPosition >= reach->positions.size() ||
                            edge.secondPosition >= reach->positions.size())
                            issue(identity + " has an edge with an invalid grid-point index");
                        if (edge.adjacentSurface >= reach->surfaces.size())
                            issue(identity + " has an edge with an invalid adjacent-surface index");
                    }
                }
                if (actualEdgeCount != reach->totalEdgeCount)
                    issue(identity + " reach-grid edge total does not match its surface records");
                for (const auto &tetrahedron : reach->tetrahedra)
                {
                    bool valid = true;
                    std::unordered_set<std::int16_t> uniqueVertices;
                    for (const auto vertex : tetrahedron)
                    {
                        if (vertex < 0 || static_cast<std::size_t>(vertex) >= reach->positions.size())
                        {
                            valid = false;
                            issue(identity + " has a tetrahedron with an invalid grid-point index");
                        }
                        uniqueVertices.insert(vertex);
                    }
                    if (uniqueVertices.size() != 4)
                    {
                        valid = false;
                        issue(identity + " has a tetrahedron with repeated vertices");
                    }
                    if (valid)
                    {
                        const auto &a = reach->positions[static_cast<std::size_t>(tetrahedron[0])];
                        const auto &b = reach->positions[static_cast<std::size_t>(tetrahedron[1])];
                        const auto &c = reach->positions[static_cast<std::size_t>(tetrahedron[2])];
                        const auto &d = reach->positions[static_cast<std::size_t>(tetrahedron[3])];
                        const float abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
                        const float acx = c[0] - a[0], acy = c[1] - a[1], acz = c[2] - a[2];
                        const float adx = d[0] - a[0], ady = d[1] - a[1], adz = d[2] - a[2];
                        const float volume6 = abx * (acy * adz - acz * ady) - aby * (acx * adz - acz * adx) +
                                              abz * (acx * ady - acy * adx);
                        if (!std::isfinite(volume6) || std::abs(volume6) <= 1.0e-6f)
                            issue(
                                identity +
                                " has a degenerate tetrahedron; runtime barycentric blending divides by its zero volume");
                    }
                }
                for (const auto &node : reach->bspNodes)
                {
                    if (node.surfaceIndex < 0 || static_cast<std::size_t>(node.surfaceIndex) >= reach->surfaces.size())
                        issue(identity + " has a category BSP node with an invalid surface index");
                    for (const auto child : {node.negativeChild, node.positiveChild})
                        if ((child > 0 && static_cast<std::size_t>(child - 1) >= reach->bspNodes.size()) ||
                            (child < 0 && static_cast<std::size_t>(~child) >= reach->tetrahedra.size()))
                            issue(identity + " has an invalid category BSP child");
                }
                if (!reach->bspNodes.empty())
                {
                    std::vector<std::uint8_t> state(reach->bspNodes.size(), 0);
                    const auto visit = [&](const auto &self, const std::size_t nodeIndex) -> bool
                    {
                        if (nodeIndex >= state.size())
                            return false;
                        if (state[nodeIndex] == 1)
                            return true;
                        if (state[nodeIndex] == 2)
                            return false;
                        state[nodeIndex] = 1;
                        const auto &node = reach->bspNodes[nodeIndex];
                        for (const auto child : {node.negativeChild, node.positiveChild})
                            if (child > 0 && self(self, static_cast<std::size_t>(child - 1)))
                                return true;
                        state[nodeIndex] = 2;
                        return false;
                    };
                    if (visit(visit, 0))
                        issue(identity + " category BSP contains a reachable cycle");
                }
                if (!std::isfinite(reach->radiusSquared) || reach->radiusSquared < 0.0f)
                    issue(identity + " has an invalid squared reach radius");
            }
        }
        if (object.cid == 140)
        {
            for (const auto id : {std::int16_t{918}, std::int16_t{929}, std::int16_t{941}, std::int16_t{946}})
            {
                const auto *reference = classProperty(id);
                if (reference == nullptr || reference->intValue == -1)
                    continue;
                const auto matches = findHierarchyNearestObjects(static_cast<std::int16_t>(reference->intValue),
                                                                 static_cast<std::int32_t>(index));
                if (matches.empty())
                {
                    issue(identity + " RWM " + std::string(PropertyName(id) ? PropertyName(id) : "reference") +
                          " references missing hierarchy-nearest OID " + std::to_string(reference->intValue));
                    continue;
                }
                if (matches.size() > 1)
                    issue(identity + " RWM reference OID " + std::to_string(reference->intValue) +
                          " has multiple equally-near matches");
                const auto cid = document.Objects()[static_cast<std::size_t>(matches.front())].cid;
                bool compatible = false;
                if (id == 918)
                    compatible = CidDerivesFrom(cid, 126) || CidDerivesFrom(cid, 115) || CidDerivesFrom(cid, 2);
                else if (id == 929)
                    compatible = CidDerivesFrom(cid, 2) || CidDerivesFrom(cid, 117);
                else if (id == 941)
                    compatible = CidDerivesFrom(cid, 115) || CidDerivesFrom(cid, 2);
                else
                    compatible = CidDerivesFrom(cid, 2);
                if (!compatible)
                    issue(identity + " RWM reference OID " + std::to_string(reference->intValue) +
                          " resolves to incompatible " + BrxDocument::ClassName(cid));
            }
            if (const auto *cacheMode = classProperty(909);
                cacheMode && (cacheMode->intValue < -1 || cacheMode->intValue > 1))
                issue(identity + " RWM cache-selection mode is outside the engine range -1..1");
            if (const auto *targetMode = classProperty(933);
                targetMode && (targetMode->intValue < -1 || targetMode->intValue > 2))
                issue(identity + " RWM targeting mode is outside the engine range -1..2");
            else if (targetMode != nullptr && targetMode->intValue == -1)
                issue(identity + " uses RWM targeting mode Nil; GetRwtiPos leaves the firing target unresolved");
            if (const auto *aimFlags = classProperty(940);
                aimFlags && (aimFlags->intValue < 0 || (aimFlags->intValue & ~7) != 0))
                issue(identity + " RWM aim-control flags contain unsupported bits");
            for (const std::int16_t runtimeOnly :
                 {std::int16_t{910}, std::int16_t{911}, std::int16_t{912}, std::int16_t{913}, std::int16_t{914},
                  std::int16_t{915}, std::int16_t{916}, std::int16_t{917}, std::int16_t{919}, std::int16_t{920},
                  std::int16_t{921}, std::int16_t{928}, std::int16_t{930}, std::int16_t{931}, std::int16_t{939},
                  std::int16_t{942}, std::int16_t{947}})
                if (classProperty(runtimeOnly) != nullptr)
                    issue(identity + " serializes runtime-only RWM option " + std::to_string(runtimeOnly) +
                          " instead of SetUser configuration");
            const auto resolveCompatible = [&](const std::int16_t id, const auto &accepts) -> const BrxObjectRecord *
            {
                const auto *reference = classProperty(id);
                if (reference == nullptr || reference->intValue == -1)
                    return nullptr;
                const auto matches = findHierarchyNearestObjects(static_cast<std::int16_t>(reference->intValue),
                                                                 static_cast<std::int32_t>(index));
                if (matches.empty())
                    return nullptr;
                const auto &found = document.Objects()[static_cast<std::size_t>(matches.front())];
                return accepts(found) ? &found : nullptr;
            };
            const auto *fireSource = resolveCompatible(918,
                                                       [](const BrxObjectRecord &candidate)
                                                       {
                                                           return CidDerivesFrom(candidate.cid, 126) ||
                                                                  CidDerivesFrom(candidate.cid, 115) ||
                                                                  CidDerivesFrom(candidate.cid, 2);
                                                       });
            if (fireSource != nullptr && CidDerivesFrom(fireSource->cid, 2))
            {
                if (classProperty(924) != nullptr || classProperty(925) != nullptr || classProperty(940) != nullptr ||
                    classProperty(941) != nullptr || classProperty(943) != nullptr || classProperty(944) != nullptr ||
                    classProperty(945) != nullptr)
                    issue(
                        identity +
                        " uses an ALO firing source; FFireRwm inherits that ALO's transform and movement, so authored launch velocity, angular velocity, and aim controls are bypassed");
            }
            const auto *aimReference =
                resolveCompatible(941, [](const BrxObjectRecord &candidate)
                                  { return CidDerivesFrom(candidate.cid, 115) || CidDerivesFrom(candidate.cid, 2); });
            if (aimReference != nullptr && CidDerivesFrom(aimReference->cid, 2) &&
                !CidDerivesFrom(aimReference->cid, 115))
                issue(
                    identity +
                    " uses an ALO aim reference; retail derives pan from it but still reads fixed tilt from EOPID 943");
            for (const std::int16_t id : {std::int16_t{922}, std::int16_t{923}, std::int16_t{924}, std::int16_t{925},
                                          std::int16_t{932}, std::int16_t{934}, std::int16_t{935}, std::int16_t{937}})
                if (const auto *value = classProperty(id);
                    value && (!std::isfinite(value->floatValue[0]) || !std::isfinite(value->floatValue[1]) ||
                              !std::isfinite(value->floatValue[2])))
                    issue(identity + " RWM " + std::string(PropertyName(id) ? PropertyName(id) : "numeric value") +
                          " contains a non-finite component");
            for (const std::int16_t id : {std::int16_t{927}, std::int16_t{943}, std::int16_t{944}, std::int16_t{945}})
                if (const auto *value = classProperty(id); value && !std::isfinite(value->floatValue[0]))
                    issue(identity + " RWM " + std::string(PropertyName(id) ? PropertyName(id) : "aim value") +
                          " is not finite");
            for (const std::int16_t id : {std::int16_t{926}, std::int16_t{936}, std::int16_t{938}})
                if (const auto *limits = classProperty(id);
                    limits && (!std::isfinite(limits->floatValue[0]) || !std::isfinite(limits->floatValue[1]) ||
                               limits->floatValue[0] > limits->floatValue[1]))
                    issue(identity + " RWM " + std::string(PropertyName(id) ? PropertyName(id) : "limits") +
                          " has inverted or non-finite limits");
            if (const auto *angularRange = classProperty(926);
                angularRange != nullptr && angularRange->floatValue[1] > 0.0f && classProperty(925) != nullptr)
                issue(
                    identity +
                    " has a positive random angular-speed maximum; retail ignores EOPID 925 angular velocity in that configuration");
            const int aimFlagValue = classProperty(940) ? classProperty(940)->intValue : 0;
            if (classProperty(945) != nullptr && (aimFlagValue & 4) == 0)
                issue(identity + " authors explicit launch speed EOPID 945 without enabling aim-control bit 4");
            if (const auto *firstAlias = classProperty(934); firstAlias != nullptr)
                if (const auto *secondAlias = classProperty(935);
                    secondAlias != nullptr && (firstAlias->floatValue[0] != secondAlias->floatValue[0] ||
                                               firstAlias->floatValue[1] != secondAlias->floatValue[1] ||
                                               firstAlias->floatValue[2] != secondAlias->floatValue[2]))
                    issue(
                        identity +
                        " authors conflicting RWM EOPIDs 934 and 935; both write the same prediction curve and serialized order decides the winner");
            std::unordered_set<int> cacheOids;
            std::size_t cacheCount = 0;
            bool anyEnabledCache = false;
            bool firstCacheEnabled = false;
            std::vector<std::int16_t> resolvedAmmoCids;
            for (const auto &property : object.properties)
                if (property.eopid == 907)
                {
                    ++cacheCount;
                    if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4 ||
                        property.argumentCount != 4)
                        issue(identity + " has a malformed ammunition-cache declaration");
                    else
                    {
                        if (property.arguments[0].type != 6 || property.arguments[1].type != 5 ||
                            property.arguments[2].type != 5 || property.arguments[3].type != 5)
                            issue(
                                identity +
                                " ammunition-cache declaration has incorrect dynamic argument types; expected OID, int, bool, bool");
                        if (property.arguments[0].intValue < (std::numeric_limits<std::int16_t>::min)() ||
                            property.arguments[0].intValue > (std::numeric_limits<std::int16_t>::max)())
                        {
                            issue(identity + " ammunition-cache OID is outside the runtime 16-bit OID range");
                            continue;
                        }
                        if (!cacheOids.insert(property.arguments[0].intValue).second)
                            issue(identity + " repeats ammunition cache OID " +
                                  std::to_string(property.arguments[0].intValue));
                        if (property.arguments[1].intValue < 0)
                            issue(identity + " has a negative ammunition cache capacity");
                        else if (property.arguments[1].intValue == 0)
                            issue(
                                identity +
                                " declares zero ammunition capacity; BindRwmAmmo still retains the template, so author capacity 1 to express the actual pool");
                        const bool enabled = property.arguments[3].intValue != 0;
                        if (cacheCount == 1)
                            firstCacheEnabled = enabled;
                        anyEnabledCache |= enabled;
                        const auto matchingAmmo =
                            findHierarchyNearestObjects(static_cast<std::int16_t>(property.arguments[0].intValue),
                                                        static_cast<std::int32_t>(index));
                        if (matchingAmmo.empty())
                            issue(identity + " ammunition cache references missing template OID " +
                                  std::to_string(property.arguments[0].intValue));
                        else if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(matchingAmmo.front())].cid,
                                                 3))
                            issue(identity + " ammunition template OID " +
                                  std::to_string(property.arguments[0].intValue) +
                                  " resolves to a non-SO object in the RWM hierarchy context");
                        else
                            resolvedAmmoCids.push_back(
                                document.Objects()[static_cast<std::size_t>(matchingAmmo.front())].cid);
                        if (matchingAmmo.size() > 1)
                            issue(identity + " ammunition template OID " +
                                  std::to_string(property.arguments[0].intValue) +
                                  " has multiple equally-near matches");
                    }
                }
            if (cacheCount > 4)
                issue(identity + " exceeds the runtime limit of four ammunition caches");
            if (cacheCount == 0)
                issue(identity + " has no ammunition cache and FFireRwm can never load an object");
            else if (!anyEnabledCache)
                issue(identity + " has no enabled ammunition cache");
            const int effectiveCacheMode = classProperty(909) ? classProperty(909)->intValue : 0;
            if (effectiveCacheMode != 0 && !firstCacheEnabled)
                issue(
                    identity +
                    " does not use automatic cache cycling and cache zero is disabled; the initial reload will fail until a script selects another cache");
            if (const auto *homing = classProperty(933); homing && homing->intValue == 2)
            {
                const bool hasMissileAmmo = std::any_of(resolvedAmmoCids.begin(), resolvedAmmoCids.end(),
                                                        [](const std::int16_t cid) { return CidDerivesFrom(cid, 60); });
                if (!hasMissileAmmo)
                    issue(identity + " uses Homing targeting but none of its ammunition templates are MISSILE-derived");
            }
            if (const auto *lifetime = classProperty(927); lifetime != nullptr && lifetime->floatValue[0] > 0.0f)
            {
                const bool hasBombAmmo = std::any_of(resolvedAmmoCids.begin(), resolvedAmmoCids.end(),
                                                     [](const std::int16_t cid) { return CidDerivesFrom(cid, 59); });
                if (!hasBombAmmo && lifetime->floatValue[0] != 5.0f)
                    issue(
                        identity +
                        " custom BOMB detonation delay EOPID 927 has no effect because none of its ammunition is BOMB-derived");
            }
        }
        if (object.cid == 141)
        {
            // Wavelength and frequency are deliberately signed in ProjectCane:
            // their sign reverses spatial or temporal phase.  Only non-finite
            // values are malformed.
            int operators = 0, circleOperators = 0, bendOperators = 0;
            bool haveCurrent = false, currentIsBend = false;
            int bendNoise = 0, swivelNoise = 0;
            for (const auto &property : object.properties)
            {
                if (property.eopid == 1003)
                {
                    ++operators;
                    ++circleOperators;
                    haveCurrent = true;
                    currentIsBend = false;
                    continue;
                }
                if (property.eopid == 1004)
                {
                    ++operators;
                    ++bendOperators;
                    haveCurrent = true;
                    currentIsBend = true;
                    bendNoise = swivelNoise = 0;
                    continue;
                }
                if (property.eopid < 1005 || property.eopid > 1020)
                    continue;
                const bool circleField = property.eopid >= 1005 && property.eopid <= 1012;
                const bool bendField = property.eopid >= 1013 && property.eopid <= 1020;
                if (!haveCurrent)
                    issue(identity + " configures a warp field before adding its first operator");
                else if (circleField && currentIsBend)
                    issue(identity + " applies a circular-wave field to the current bend operator");
                else if (bendField && !currentIsBend)
                    issue(identity + " applies a bend/swivel field to the current circular operator");
                if (property.eopid == 1016 || property.eopid == 1020)
                {
                    if (!currentIsBend)
                        issue(identity + (property.eopid == 1016 ? " adds bend noise" : " adds swivel noise") +
                              " without a current bend operator");
                    int &noiseCount = property.eopid == 1016 ? bendNoise : swivelNoise;
                    if (++noiseCount > 4)
                        issue(identity + (property.eopid == 1016 ? " exceeds four bend-noise terms"
                                                                 : " exceeds four swivel-noise terms"));
                    if (property.kind != BrxPropertyKind::Dynamic || property.arguments.size() != 4 ||
                        property.argumentCount != 4)
                        issue(identity + " has a malformed WR noise term; runtime expects four floats");
                    else
                        for (const auto &argument : property.arguments)
                            if (argument.type != 1 || !std::isfinite(argument.floatValue[0]))
                            {
                                issue(identity + " has a WR noise term with a non-float or non-finite argument");
                                break;
                            }
                }
                else
                {
                    const int components = property.kind == BrxPropertyKind::Vector3  ? 3
                                           : property.kind == BrxPropertyKind::Float2 ? 2
                                                                                      : 1;
                    for (int component = 0; component < components; ++component)
                        if (!std::isfinite(property.floatValue[component]))
                        {
                            issue(identity + " has a non-finite WR operator value");
                            break;
                        }
                }
            }
            if (operators > 8)
                issue(identity + " exceeds the runtime limit of eight warp operators");
            const int matrixCount = bendOperators + (circleOperators + 1) / 2;
            if (matrixCount > 4)
                issue(identity + " requires " + std::to_string(matrixCount) +
                      " deformation matrices, exceeding ProjectCane's fixed four-matrix storage");
        }
        if (object.cid == 142)
        {
            const auto keyhole = std::find_if(
                document.Keyholes().begin(), document.Keyholes().end(), [&](const BrxKeyholeRecord &candidate)
                { return candidate.ownerObjectIndex == static_cast<std::int32_t>(index); });
            if (keyhole == document.Keyholes().end())
                issue(identity + " has no decoded keyhole mesh");
            else
            {
                if (!std::isfinite(keyhole->minimum[0]) || !std::isfinite(keyhole->minimum[1]) ||
                    !std::isfinite(keyhole->maximum[0]) || !std::isfinite(keyhole->maximum[1]))
                    issue(identity + " keyhole bounds contain a non-finite value");
                else if (!(keyhole->maximum[0] > keyhole->minimum[0]))
                    issue(identity + " keyhole bounds have no positive width; DrawKeyhole will not render it");
                if (keyhole->positions.size() > (std::numeric_limits<std::uint16_t>::max)())
                    issue(identity + " keyhole position count exceeds U16 storage");
                for (const auto &position : keyhole->positions)
                    if (!std::isfinite(position[0]) || !std::isfinite(position[1]))
                    {
                        issue(identity + " keyhole mesh contains a non-finite position");
                        break;
                    }
                for (std::size_t layerIndex = 0; layerIndex < keyhole->layers.size(); ++layerIndex)
                {
                    const auto &layer = keyhole->layers[layerIndex];
                    if (layer.size() > (std::numeric_limits<std::uint16_t>::max)())
                        issue(identity + " keyhole layer " + std::to_string(layerIndex) +
                              " exceeds U16 triangle storage");
                    for (const auto &triangle : layer)
                    {
                        if (triangle[0] >= keyhole->positions.size() || triangle[1] >= keyhole->positions.size() ||
                            triangle[2] >= keyhole->positions.size())
                            issue(identity + " keyhole layer contains an invalid vertex index");
                        else if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2])
                            issue(identity + " keyhole layer contains a triangle with repeated vertices");
                    }
                }
            }
        }
        if (object.cid == 143)
        {
            std::size_t commandCount = 0;
            std::int32_t jsgContextIndex = -1;
            const auto resolveJsgTarget = [&](const std::int16_t oid)
            {
                if (jsgContextIndex >= 0)
                    return findHierarchyNearestObjects(oid, jsgContextIndex);
                std::vector<std::int32_t> matches;
                for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
                    if (document.Objects()[candidateIndex].oid == oid)
                        matches.push_back(static_cast<std::int32_t>(candidateIndex));
                return matches;
            };
            for (const auto &property : object.properties)
                if ((property.eopid >= 1118 && property.eopid <= 1129) || property.eopid == 1131)
                {
                    ++commandCount;
                    if (property.kind != BrxPropertyKind::Dynamic ||
                        property.arguments.size() != property.argumentCount)
                    {
                        issue(identity + " has a malformed gameplay-script command " + std::to_string(property.eopid));
                        continue;
                    }
                    const std::size_t expectedArguments = property.eopid <= 1122                               ? 1
                                                          : property.eopid == 1123                             ? 2
                                                          : property.eopid == 1124                             ? 4
                                                          : (property.eopid >= 1125 && property.eopid <= 1127) ? 3
                                                          : property.eopid == 1128                             ? 2
                                                          : property.eopid == 1129                             ? 1
                                                                                                               : 0;
                    if (property.arguments.size() != expectedArguments)
                        issue(identity + " gameplay-script command " + std::to_string(property.eopid) +
                              " has the wrong argument count");
                    for (std::size_t argument = 0; argument < property.arguments.size(); ++argument)
                    {
                        const bool floatArgument =
                            (property.eopid == 1123 && argument == 1) ||
                            ((property.eopid == 1125 || property.eopid == 1126) && argument == 1) ||
                            (property.eopid == 1129 && argument == 0);
                        if (floatArgument && !std::isfinite(property.arguments[argument].floatValue[0]))
                            issue(identity + " gameplay-script command contains a non-finite numeric argument");
                    }
                    if (property.eopid == 1118 && !property.arguments.empty())
                    {
                        const auto oid = static_cast<std::int16_t>(property.arguments[0].intValue);
                        const auto matches = oid == -1 ? std::vector<std::int32_t>{} : resolveJsgTarget(oid);
                        if (matches.size() > 1)
                            issue(identity + " CONTEXT command OID " + std::to_string(oid) +
                                  " is ambiguous in the current gameplay-script context");
                        jsgContextIndex = matches.empty() ? -1 : matches.front();
                    }
                    if ((property.eopid == 1122 || property.eopid == 1128) && !property.arguments.empty())
                    {
                        const auto oid = static_cast<std::int16_t>(property.arguments[0].intValue);
                        const auto matches = resolveJsgTarget(oid);
                        const std::int16_t requiredCid = property.eopid == 1122 ? 99 : 131;
                        const char *role = property.eopid == 1122 ? "TUNNEL" : "ASEG";
                        if (matches.empty())
                            issue(identity + " " + role + " command references missing OID " + std::to_string(oid) +
                                  " in the current gameplay-script context");
                        else
                        {
                            if (matches.size() > 1)
                                issue(identity + " " + role + " command OID " + std::to_string(oid) +
                                      " is ambiguous in the current context");
                            if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(matches.front())].cid,
                                                requiredCid))
                                issue(identity + " " + role + " command resolves to incompatible " +
                                      BrxDocument::ClassName(
                                          document.Objects()[static_cast<std::size_t>(matches.front())].cid));
                        }
                    }
                    if (property.eopid == 1124 && property.arguments.size() >= 3)
                    {
                        const int jumpKind = property.arguments[1].intValue;
                        if (jumpKind < 0 || jumpKind > 2)
                            issue(identity + " JUMP command has an invalid jump kind");
                        const int landingKind = property.arguments[2].intValue;
                        if (landingKind < 0 || landingKind >= 3)
                            issue(identity + " JUMP command has an invalid landing kind");
                    }
                    if (property.eopid == 1127 && property.arguments.size() >= 2)
                    {
                        const int attack = property.arguments[1].intValue;
                        if (attack != 2 && attack != 4 && attack != 8)
                            issue(identity + " ATTACK command uses a value that retail dispatches as no action");
                    }
                }
            if (commandCount > 128)
                issue(identity + " exceeds PjsgeAllocJsg's 128-command limit; later commands are discarded");
        }
        if (object.cid == 144)
        {
            int previousMeasure = std::numeric_limits<int>::min();
            float previousBeat = -std::numeric_limits<float>::infinity();
            std::size_t commandCount = 0;
            if (std::none_of(document.Objects().begin(), document.Objects().end(),
                             [](const BrxObjectRecord &candidate) { return CidDerivesFrom(candidate.cid, 19); }))
                issue(identity + " has no RUBY object in the world; PostRythmSequenceLoad cannot bind its clock owner");
            for (const auto &property : object.properties)
                if (property.eopid >= 1161 && property.eopid <= 1166)
                {
                    ++commandCount;
                    const std::size_t expectedArguments = property.eopid <= 1162 ? 3 : 2;
                    if (property.kind != BrxPropertyKind::Dynamic ||
                        property.arguments.size() != property.argumentCount ||
                        property.arguments.size() != expectedArguments)
                    {
                        issue(identity + " has a malformed rhythm command");
                        continue;
                    }
                    const int measure = property.arguments[0].intValue;
                    const float beat = property.arguments[1].floatValue[0];
                    if (!std::isfinite(beat))
                        issue(identity + " has a non-finite rhythm beat");
                    if (measure < previousMeasure || (measure == previousMeasure && beat < previousBeat))
                        issue(identity + " rhythm commands are not sorted by measure and beat");
                    previousMeasure = measure;
                    previousBeat = beat;
                }
            if (commandCount > 192)
                issue(identity + " exceeds the rhythm command limit of 192");
        }
        if (object.cid == 145)
        {
            for (const auto id : {std::int16_t{1133}, std::int16_t{1134}, std::int16_t{1135}})
                if (const auto *color = classProperty(id); color)
                    for (int component = 0; component < 4; ++component)
                        if (!std::isfinite(color->floatValue[component]))
                            issue(identity + " has a non-finite screen-flash color component in EOPID " +
                                  std::to_string(id));
            for (const auto id : {std::int16_t{1136}, std::int16_t{1137}, std::int16_t{1138}})
                if (const auto *phase = classProperty(id); phase && !std::isfinite(phase->floatValue[0]))
                    issue(identity + " has an invalid thunder-flash phase duration in EOPID " + std::to_string(id));
                else if (phase && phase->floatValue[0] < 0.0f)
                    issue(identity + " has a negative screen-flash phase duration in EOPID " + std::to_string(id));
            if (std::any_of(object.properties.begin(), object.properties.end(),
                            [](const BrxPropertyRecord &property) { return property.eopid == 1132; }))
                issue(identity + " serializes runtime-only THNDFLASH Start action 1132");
        }
        if (object.cid == 146)
        {
            issue(
                identity +
                " is a runtime-only ASEGA instance; BRX authors ASEG definitions and ApplyAseg creates ASEGA objects at runtime");
            if (!object.properties.empty())
                issue(
                    identity +
                    " serializes ASEGA runtime state even though CID 146 has no BRX option table or world-object loader");
            for (const auto id : {std::int16_t{193}, std::int16_t{194}})
                if (const auto *speed = classProperty(id); speed && !std::isfinite(speed->floatValue[0]))
                    issue(identity + " ASEGA playback speed is not finite");
        }
        if (object.cid == 147)
        {
            issue(identity + " is a runtime-only SMA instance; PsmaApplySm creates it from an authored SM definition");
            if (!object.properties.empty())
                issue(identity + " serializes SMA runtime state even though CID 147 has no BRX world-object loader");
            if (const auto *speed = classProperty(519); speed && !std::isfinite(speed->floatValue[0]))
                issue(identity + " SMA playback speed is not finite");
        }
    }
    std::size_t defaultWarpCount = 0;
    std::size_t specialDefaultWarpCount = 0;
    std::unordered_set<std::int16_t> warpAliases;
    for (const auto &warp : document.Warps())
    {
        if (warp.isDefault)
            ++defaultWarpCount;
        if (warp.isSpecialDefault)
            ++specialDefaultWarpCount;
        if (warp.aliasOid != -1 && !warpAliases.insert(warp.aliasOid).second)
            issue("Duplicate WARP alias OID " + std::to_string(warp.aliasOid));
        if (!std::isfinite(warp.velocity[0]) || !std::isfinite(warp.velocity[1]) || !std::isfinite(warp.velocity[2]))
            issue("WARP record contains a non-finite arrival velocity");
        if (warp.ownerObjectIndex < 0 || static_cast<std::size_t>(warp.ownerObjectIndex) >= document.Objects().size())
        {
            issue("WARP record has an invalid owner");
            continue;
        }
        const auto &owner = document.Objects()[static_cast<std::size_t>(warp.ownerObjectIndex)];
        const std::string warpIdentity = "WARP OID " + std::to_string(owner.oid);
        if (warp.aliasOid == owner.oid)
            issue(warpIdentity + " aliases its own OID");
        if (warp.aliasOid != -1)
        {
            const auto exactObject =
                std::find_if(document.Objects().begin(), document.Objects().end(),
                             [&](const BrxObjectRecord &candidate) { return candidate.oid == warp.aliasOid; });
            if (exactObject != document.Objects().end() && &*exactObject != &owner)
                issue(warpIdentity + " alias OID " + std::to_string(warp.aliasOid) +
                      " is shadowed by an object's exact OID");
        }
        std::unordered_set<std::int16_t> hiddenOids;
        for (const auto hiddenOid : warp.hiddenObjectOids)
        {
            if (hiddenOid == -1)
            {
                issue(warpIdentity + " has an empty hidden-object entry");
                continue;
            }
            if (!hiddenOids.insert(hiddenOid).second)
                issue(warpIdentity + " hides OID " + std::to_string(hiddenOid) + " more than once");
            if (hiddenOid == owner.oid)
                issue(warpIdentity + " attempts to hide itself");
            if (std::none_of(document.Objects().begin(), document.Objects().end(),
                             [&](const BrxObjectRecord &candidate) { return candidate.oid == hiddenOid; }))
                issue(warpIdentity + " references missing hidden-object OID " + std::to_string(hiddenOid));
        }
        const auto embeddedAsegs = std::count_if(
            document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
            { return candidate.parentIndex == warp.ownerObjectIndex && CidDerivesFrom(candidate.cid, 131); });
        if (embeddedAsegs != warp.embeddedAnimationCount)
            issue(warpIdentity + " decoded " + std::to_string(embeddedAsegs) + " of " +
                  std::to_string(warp.embeddedAnimationCount) + " embedded ASEGs");
    }
    if (defaultWarpCount > 1)
        issue("Multiple WARP objects are marked as the default destination");
    if (specialDefaultWarpCount > 1)
        issue("Multiple WARP objects are marked as the World Map wipe default destination");
    std::vector<std::size_t> shapeCounts(document.Objects().size(), 0);
    for (std::size_t pathIndex = 0; pathIndex < document.ShapePaths().size(); ++pathIndex)
    {
        const auto &path = document.ShapePaths()[pathIndex];
        const std::string identity = "SHAPE path " + std::to_string(pathIndex);
        if (path.ownerObjectIndex < 0 || static_cast<std::size_t>(path.ownerObjectIndex) >= document.Objects().size())
        {
            issue(identity + " has an invalid owner");
            continue;
        }
        ++shapeCounts[static_cast<std::size_t>(path.ownerObjectIndex)];
        const auto &owner = document.Objects()[static_cast<std::size_t>(path.ownerObjectIndex)];
        if (owner.cid < 110 || owner.cid > 114)
            issue(identity + " is owned by a non-SHAPE-family object");
        if (owner.parentIndex >= 0 &&
            (static_cast<std::size_t>(owner.parentIndex) >= document.Objects().size() ||
             !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(owner.parentIndex)].cid, 2)))
            issue(identity + " has a parent that is not ALO-derived");
        if (owner.parentIndex >= 0 && static_cast<std::size_t>(owner.parentIndex) < document.Objects().size())
        {
            const float *m = document.Objects()[static_cast<std::size_t>(owner.parentIndex)].worldTransform;
            const float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) +
                                      m[8] * (m[1] * m[6] - m[2] * m[5]);
            if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
                issue(identity + " has a singular ALO parent; SetShapeParent coordinate conversion is undefined");
        }
        if (owner.proxyClone)
        {
            if (owner.proxySourceObjectIndex < 0)
                issue(identity + " is a linked PROXY clone without a reusable source object");
            else
            {
                const auto source = std::find_if(
                    document.ShapePaths().begin(), document.ShapePaths().end(), [&](const BrxShapePathRecord &candidate)
                    { return candidate.ownerObjectIndex == owner.proxySourceObjectIndex; });
                if (source == document.ShapePaths().end())
                    issue(identity + " is a linked PROXY clone whose source SHAPE has no curve record");
                else if (path.closed != source->closed || path.hermite != source->hermite ||
                         path.parameters != source->parameters || path.points != source->points ||
                         path.tangentsIn != source->tangentsIn || path.tangentsOut != source->tangentsOut)
                    issue(identity + " diverges from its reusable source even though CloneShape shares pcrv");
            }
        }
        if (path.points.size() < 2)
            issue(identity + " has fewer than two control points");
        if (path.closed && path.points.size() < 3)
            issue(identity + " is closed but lacks a distinct segment plus its explicit seam endpoint");
        if (path.points.size() > 255)
            issue(identity + " exceeds the U8 limit of 255 control points");
        if (path.parameters.size() != path.points.size())
            issue(identity + " parameter count does not match its control-point count");
        float chordLength = 0.0f;
        for (std::size_t point = 0; point < path.points.size(); ++point)
        {
            if (!std::isfinite(path.points[point][0]) || !std::isfinite(path.points[point][1]) ||
                !std::isfinite(path.points[point][2]))
                issue(identity + " has a non-finite control point");
            if (point < path.parameters.size() && !std::isfinite(path.parameters[point]))
                issue(identity + " has a non-finite curve parameter");
            if (point > 0 && point < path.parameters.size() && path.parameters[point] <= path.parameters[point - 1])
                issue(identity + " curve parameters are not strictly increasing");
            if (point > 0)
            {
                const float dx = path.points[point][0] - path.points[point - 1][0];
                const float dy = path.points[point][1] - path.points[point - 1][1];
                const float dz = path.points[point][2] - path.points[point - 1][2];
                const float segmentLength = std::sqrt(dx * dx + dy * dy + dz * dz);
                chordLength += segmentLength;
                if (segmentLength <= 0.0001f)
                    issue(identity + " has duplicate consecutive control points at " + std::to_string(point - 1) +
                          " and " + std::to_string(point));
            }
        }
        if (path.points.size() >= 2 && chordLength <= 0.0001f)
            issue(identity + " has zero geometric length");
        if (path.closed && path.points.size() > 1)
        {
            const auto &first = path.points.front();
            const auto &last = path.points.back();
            const float dx = first[0] - last[0], dy = first[1] - last[1], dz = first[2] - last[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > 0.01f)
                issue(identity + " is closed but its authored endpoints differ; runtime wrapping will jump");
        }
        if (path.hermite)
        {
            if (path.tangentsIn.size() != path.points.size() || path.tangentsOut.size() != path.points.size())
                issue(identity + " cubic handle counts do not match its control-point count");
            const auto finiteHandles = [](const std::vector<std::array<float, 3>> &handles)
            {
                return std::all_of(
                    handles.begin(), handles.end(), [](const auto &value)
                    { return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]); });
            };
            if (!finiteHandles(path.tangentsIn) || !finiteHandles(path.tangentsOut))
                issue(identity + " contains a non-finite cubic handle");
            if (path.closed && path.tangentsIn.size() == path.points.size() &&
                path.tangentsOut.size() == path.points.size() && path.points.size() > 1)
            {
                const auto &endDerivative = path.tangentsIn.back();
                const auto &startDerivative = path.tangentsOut.front();
                const float dx = endDerivative[0] - startDerivative[0];
                const float dy = endDerivative[1] - startDerivative[1];
                const float dz = endDerivative[2] - startDerivative[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > 0.01f)
                    issue(identity + " has discontinuous cubic derivatives across its explicit closed seam");
            }
        }
        else if (!path.tangentsIn.empty() || !path.tangentsOut.empty())
            issue(identity + " is linear but retains cubic handle data");
    }
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
        if (document.Objects()[objectIndex].cid >= 110 && document.Objects()[objectIndex].cid <= 114 &&
            shapeCounts[objectIndex] != 1)
            issue(std::string(BrxDocument::ClassName(document.Objects()[objectIndex].cid)) + " OID " +
                  std::to_string(document.Objects()[objectIndex].oid) + " must own exactly one decoded curve");
    const auto &nodes = document.VisibilityNodes();
    const auto &visibilityRows = document.VisibilityOneHopMasks();
    const auto visibilityZoneCount = visibilityRows.size();
    if (document.WorldHeader().visibilityZoneCount != visibilityZoneCount)
        issue("VISMAP header zone count " + std::to_string(document.WorldHeader().visibilityZoneCount) +
              " disagrees with the one-hop row count " + std::to_string(visibilityZoneCount));
    if (visibilityZoneCount > 32u)
        issue("VISMAP has more than 32 one-hop rows");
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto &node = nodes[index];
        if (!std::isfinite(node.normal[0]) || !std::isfinite(node.normal[1]) || !std::isfinite(node.normal[2]) ||
            !std::isfinite(node.planeDistance))
        {
            issue("VISMAP node " + std::to_string(index) + " has a non-finite plane");
            continue;
        }
        const float length = std::sqrt(node.normal[0] * node.normal[0] + node.normal[1] * node.normal[1] +
                                       node.normal[2] * node.normal[2]);
        if (length <= 0.000001f || std::abs(length - 1.0f) > 0.01f)
            out << "Note: VISMAP node " << index << " has plane normal length " << length
                << "; ProjectCane uses its raw dot product without normalization.\n";
        for (const auto child : {node.negativeChild, node.positiveChild})
            if (child < -1 || child >= static_cast<std::int32_t>(nodes.size()) ||
                child == static_cast<std::int32_t>(index))
                issue("VISMAP node " + std::to_string(index) + " has invalid child " + std::to_string(child));
    }
    if (!nodes.empty())
    {
        std::vector<std::uint8_t> state(nodes.size(), 0u);
        std::vector<std::pair<std::int32_t, bool>> work;
        work.emplace_back(0, false);
        while (!work.empty())
        {
            const auto [index, leaving] = work.back();
            work.pop_back();
            if (index < 0 || static_cast<std::size_t>(index) >= nodes.size())
                continue;
            auto &nodeState = state[static_cast<std::size_t>(index)];
            if (leaving)
            {
                nodeState = 2u;
                continue;
            }
            if (nodeState == 1u)
            {
                issue("VISMAP BSP contains a cycle at node " + std::to_string(index));
                continue;
            }
            if (nodeState == 2u)
                continue;
            nodeState = 1u;
            work.emplace_back(index, true);
            work.emplace_back(nodes[static_cast<std::size_t>(index)].positiveChild, false);
            work.emplace_back(nodes[static_cast<std::size_t>(index)].negativeChild, false);
        }
        for (std::size_t index = 0; index < state.size(); ++index)
            if (state[index] == 0u)
                issue("VISMAP node " + std::to_string(index) + " is unreachable from root");
    }
    out << (issues == 0 ? "No structural issues detected.\n" : std::to_string(issues) + " issue(s) detected.\n");

    out << "\n=== OBJECTS ===\n";
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        out << '[' << index << "] " << BrxDocument::ClassName(object.cid) << " CID=" << object.cid
            << " OID=" << object.oid << " parent=" << object.parentIndex << " splice=" << object.spliceIndex
            << " loader=" << object.loaderName << " decoded=" << object.loaderImplemented << " hidden=" << object.hidden
            << " zone=0x" << std::hex << object.zoneMask << std::dec << " globs=" << object.renderGroupCount;
        if (object.proxyRecordIndex >= 0)
            out << " proxy=" << object.proxyRecordIndex << ':' << object.proxyRootIndex
                << " sourceObject=" << object.proxySourceObjectIndex << " clone=" << object.proxyClone;
        out << '\n';
        out << "  offsets header=0x" << std::hex << object.offset << " data=0x" << object.dataOffset << " end=0x"
            << object.endOffset << std::dec << '\n';
        if (object.hasAloPayload)
            out << "  ALO zons=" << static_cast<int>(object.zoneMode)
                << " viss=" << static_cast<int>(object.visibilityMode) << " mrds=" << static_cast<int>(object.mrdMode)
                << " mrd=" << object.maximumRenderDistance << " celMrd=" << object.celBorderMaximumDistance
                << " radiusSelf=" << object.renderRadiusSelf << " radiusAll=" << object.renderRadiusAll << " alox=0x"
                << std::hex << object.alox.flags << std::dec << '\n';
        if (object.so.present)
        {
            const auto &so = object.so;
            out << "  SO fixed=" << so.fixedPhysics << " mass=" << so.mass << " centerOfMass=(" << so.centerOfMass[0]
                << ',' << so.centerOfMass[1] << ',' << so.centerOfMass[2] << ") modified=" << so.modified << '\n'
                << "    gameplay geometry positions/surfaces/edges/bsp=" << so.collisionGeometry.positionCount << '/'
                << so.collisionGeometry.surfaceCount << '/' << so.collisionGeometry.edgeCount << '/'
                << so.collisionGeometry.bspNodes.size() << '\n'
                << "    camera geometry positions/surfaces/edges/bsp=" << so.cameraGeometry.positionCount << '/'
                << so.cameraGeometry.surfaceCount << '/' << so.cameraGeometry.edgeCount << '/'
                << so.cameraGeometry.bspNodes.size() << '\n'
                << "    cameraGroups=" << so.cameraGroups.size() << " bspGroupMap=" << so.bspNodeToCameraGroup.size()
                << " hitSurfaceGroups=" << so.hitSurfaceGroups.size()
                << " surfaceGroupMap=" << so.surfaceToHitSurfaceGroup.size() << '\n';
            const auto optionValue = [&](const std::int16_t id, const char *inherited)
            {
                const auto found =
                    std::find_if(object.properties.begin(), object.properties.end(),
                                 [&](const BrxPropertyRecord &property) { return property.eopid == id; });
                return found == object.properties.end() ? std::string(inherited) : std::to_string(found->intValue);
            };
            const auto sphere = std::find_if(object.properties.begin(), object.properties.end(),
                                             [](const BrxPropertyRecord &property) { return property.eopid == 133; });
            out << "    forceConstraint=" << optionValue(131, "default")
                << " torqueConstraint=" << optionValue(132, "default") << " material=" << optionValue(128, "default")
                << " zoneKind=" << optionValue(143, "default") << " cameraStyle=" << optionValue(145, "default")
                << " collisionShape=" << (sphere == object.properties.end() ? "mesh" : "sphere") << '\n';
        }
        for (const auto &property : object.properties)
            WriteProperty(out, property);
    }

    out << "\n=== PROXY INSTANCES ===\n";
    for (std::size_t proxyIndex = 0; proxyIndex < document.Proxies().size(); ++proxyIndex)
    {
        const auto &proxy = document.Proxies()[proxyIndex];
        out << '[' << proxyIndex << "] owner=" << proxy.ownerObjectIndex << " roots=" << proxy.roots.size()
            << " overrides=" << proxy.overrides.size()
            << " attachedChildren=" << proxy.attachedChildObjectIndices.size() << '\n';
        for (std::size_t rootIndex = 0; rootIndex < proxy.roots.size(); ++rootIndex)
        {
            const auto &root = proxy.roots[rootIndex];
            out << "  root " << rootIndex << ' '
                << (root.kind == BrxProxyRootKind::InlineSource ? "inline" : "reference")
                << " psl=" << root.sourceListIndex << " capacity=" << root.cloneCapacity
                << " object=" << root.instantiatedRootObjectIndex << " members=" << root.objectBegin << '+'
                << root.objectCount << " source=" << root.sourceProxyRecordIndex << ':' << root.sourceProxyRootIndex
                << '\n';
        }
        for (std::size_t overrideIndex = 0; overrideIndex < proxy.overrides.size(); ++overrideIndex)
        {
            const auto &overrideRecord = proxy.overrides[overrideIndex];
            out << "  override " << overrideIndex << " oid=" << overrideRecord.targetOid
                << " object=" << overrideRecord.targetObjectIndex << " splice=" << overrideRecord.replacementSpliceIndex
                << " zones=0x" << std::hex << overrideRecord.zoneMask << std::dec
                << " mrdMode=" << static_cast<int>(overrideRecord.mrdMode)
                << " mrd=" << overrideRecord.maximumRenderDistance << " properties=" << overrideRecord.properties.size()
                << '\n';
            for (const auto &property : overrideRecord.properties)
                WriteProperty(out, property);
        }
    }

    out << "\n=== CEL BORDER BATCHES ===\n";
    out << "Count: " << document.CelBatches().size() << '\n';
    for (std::size_t index = 0; index < document.CelBatches().size(); ++index)
    {
        const auto &cel = document.CelBatches()[index];
        out << '[' << index << "] owner=" << cel.ownerObjectIndex;
        if (cel.ownerObjectIndex >= 0 && static_cast<std::size_t>(cel.ownerObjectIndex) < document.Objects().size())
        {
            const auto &owner = document.Objects()[static_cast<std::size_t>(cel.ownerObjectIndex)];
            out << ' ' << BrxDocument::ClassName(owner.cid) << "/OID=" << owner.oid
                << " mrds=" << static_cast<unsigned>(owner.mrdMode) << " objectMrd=" << owner.maximumRenderDistance
                << " objectCelMrd=" << owner.celBorderMaximumDistance;
        }
        out << " group=" << cel.renderGroup << " phase=" << static_cast<unsigned>(cel.renderPhase)
            << " edges=" << cel.firstEdge << '+' << cel.edgeCount << " center=(" << cel.center[0] << ','
            << cel.center[1] << ',' << cel.center[2] << ')' << " radius=" << cel.radius
            << " globMrd=" << cel.surfaceMaximumDistance << " globCelMrd=" << cel.maximumDistance << '\n';
    }

    out << "\n=== RENDER BATCH MRD ===\n";
    for (std::size_t index = 0; index < document.RenderBatches().size(); ++index)
    {
        const auto &batch = document.RenderBatches()[index];
        out << '[' << index << "] owner=" << batch.ownerObjectIndex;
        if (batch.ownerObjectIndex >= 0 && static_cast<std::size_t>(batch.ownerObjectIndex) < document.Objects().size())
        {
            const auto &owner = document.Objects()[static_cast<std::size_t>(batch.ownerObjectIndex)];
            out << ' ' << BrxDocument::ClassName(owner.cid) << "/OID=" << owner.oid;
        }
        out << " group=" << batch.renderGroup << " phase=" << static_cast<unsigned>(batch.renderPhase) << " center=("
            << batch.sortCenter[0] << ',' << batch.sortCenter[1] << ',' << batch.sortCenter[2]
            << ") radius=" << batch.mrdRadius << " globMrd=" << batch.maximumRenderDistance << '\n';
    }

    out << "\n=== VISMAP ===\n";
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto &node = nodes[index];
        out << '[' << index << "] n=(" << node.normal[0] << ',' << node.normal[1] << ',' << node.normal[2]
            << ") gDot=" << node.planeDistance << " neg=" << node.negativeChild << "/0x" << std::hex
            << node.negativeZoneMask << " pos=" << std::dec << node.positiveChild << "/0x" << std::hex
            << node.positiveZoneMask << std::dec << '\n';
    }
    out << "One-hop masks:\n";
    for (std::size_t zone = 0; zone < document.VisibilityOneHopMasks().size(); ++zone)
        out << "  zone " << zone << " = 0x" << std::hex << document.VisibilityOneHopMasks()[zone] << std::dec << '\n';

    out << "\n=== WRBG DEFORMATION BINDINGS ===\n";
    out << "Count: " << document.GlobWarps().size() << '\n';
    for (std::size_t index = 0; index < document.GlobWarps().size(); ++index)
    {
        const auto &warp = document.GlobWarps()[index];
        out << '[' << index << "] owner=" << warp.ownerObjectIndex;
        if (warp.ownerObjectIndex >= 0 && static_cast<std::size_t>(warp.ownerObjectIndex) < document.Objects().size())
        {
            const auto &owner = document.Objects()[static_cast<std::size_t>(warp.ownerObjectIndex)];
            out << ' ' << BrxDocument::ClassName(owner.cid) << "/OID=" << owner.oid;
        }
        out << " controllerOid=" << warp.controllerOid << " group=" << warp.renderGroup
            << " vertices=" << warp.firstVertex << '+' << warp.vertexCount << " celEdges=" << warp.firstCelEdge << '+'
            << warp.celEdgeCount << " matrices=" << static_cast<unsigned>(warp.matrixCount)
            << " pos=" << warp.deformPosition << " uv=" << warp.deformUv
            << " envelope=" << static_cast<int>(warp.envelopeKind) << " limits=(" << warp.innerDistance << ','
            << warp.innerWeight << ")->(" << warp.outerDistance << ',' << warp.outerWeight << ")\n";
    }
    return out.str();
}

bool WriteLevelDiagnosticDump(const BrxDocument &document, const std::filesystem::path &path, std::string &error)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not open " + path.string();
        return false;
    }
    output << BuildLevelDiagnosticDump(document);
    if (!output)
    {
        error = "Failed while writing " + path.string();
        return false;
    }
    return true;
}
