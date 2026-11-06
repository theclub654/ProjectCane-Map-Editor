#include "EditorMcpRecords.h"
#include "EditorMcpOperations.h"

// Read-only adapters, deliberately separate from the native BRX writer. There
// is no from_json overload: inspection cannot become an unchecked payload edit.
namespace EditorMcp
{
inline Json RecordFields(const std::string &declaration)
{
    Json fields = Json::array();
    std::size_t start = 0;
    while ((start = declaration.find("MCP_FIELD(", start)) != std::string::npos)
    {
        start += 10;
        const auto end = declaration.find(')', start);
        if (end == std::string::npos) break;
        fields.push_back(declaration.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}
}
#define MCP_FIELD(field) {#field, value.field}
#define MCP_RECORD(Type, ...) \
    inline void to_json(nlohmann::json &json, const Type &value) { json = nlohmann::json::object({__VA_ARGS__}); } \
    inline nlohmann::json McpRecordFields(const Type &) { return EditorMcp::RecordFields(#__VA_ARGS__); }
MCP_RECORD(BrxPropertyRecord::DynamicArgument, MCP_FIELD(type), MCP_FIELD(intValue), MCP_FIELD(floatValue))
MCP_RECORD(BrxPropertyRecord, MCP_FIELD(eopid), MCP_FIELD(kind), MCP_FIELD(flags), MCP_FIELD(argumentCount), MCP_FIELD(editable), MCP_FIELD(modified), MCP_FIELD(boolValue), MCP_FIELD(intValue), MCP_FIELD(uintValue), MCP_FIELD(typeValue), MCP_FIELD(floatValue), MCP_FIELD(arguments))
MCP_RECORD(BrxObjectDetail, MCP_FIELD(name), MCP_FIELD(value))
MCP_RECORD(BrxProxyRootRecord, MCP_FIELD(kind), MCP_FIELD(sourceListIndex), MCP_FIELD(sourceProxyRecordIndex), MCP_FIELD(sourceProxyRootIndex), MCP_FIELD(instantiatedRootObjectIndex), MCP_FIELD(objectBegin), MCP_FIELD(objectCount), MCP_FIELD(cloneCapacity))
MCP_RECORD(BrxProxyOverrideRecord, MCP_FIELD(targetOid), MCP_FIELD(targetObjectIndex), MCP_FIELD(replacementSpliceIndex), MCP_FIELD(zoneMask), MCP_FIELD(mrdMode), MCP_FIELD(maximumRenderDistance), MCP_FIELD(inheritedSpliceIndex), MCP_FIELD(inheritedZoneMask), MCP_FIELD(inheritedMrdMode), MCP_FIELD(inheritedMaximumRenderDistance), MCP_FIELD(inheritedPropertiesCaptured), MCP_FIELD(inheritedProperties), MCP_FIELD(properties))
MCP_RECORD(BrxProxyRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(localTransform), MCP_FIELD(roots), MCP_FIELD(overrides), MCP_FIELD(attachedChildObjectIndices))
MCP_RECORD(BrxSoBspNode, MCP_FIELD(surfaceIndex), MCP_FIELD(negativeChild), MCP_FIELD(positiveChild))
MCP_RECORD(BrxSoCameraGroup, MCP_FIELD(cameraStyle), MCP_FIELD(globIndex))
MCP_RECORD(BrxSoHitSurfaceGroup, MCP_FIELD(globIndex), MCP_FIELD(subglobIndex))
MCP_RECORD(BrxSoGeometryInfo::Edge, MCP_FIELD(firstPosition), MCP_FIELD(secondPosition), MCP_FIELD(adjacentSurface))
MCP_RECORD(BrxSoGeometryInfo::Surface, MCP_FIELD(positionIndices), MCP_FIELD(radius), MCP_FIELD(center), MCP_FIELD(edges))
MCP_RECORD(BrxSoGeometryInfo, MCP_FIELD(radius), MCP_FIELD(positionCount), MCP_FIELD(surfaceCount), MCP_FIELD(edgeCount), MCP_FIELD(fullBspNodeCount), MCP_FIELD(boundsMin), MCP_FIELD(boundsMax), MCP_FIELD(positions), MCP_FIELD(surfaces), MCP_FIELD(bspNodes))
MCP_RECORD(BrxSoPayload, MCP_FIELD(present), MCP_FIELD(physicsFlags), MCP_FIELD(fixedPhysics), MCP_FIELD(mass), MCP_FIELD(inertia), MCP_FIELD(centerOfMass), MCP_FIELD(collisionGeometry), MCP_FIELD(cameraGeometry), MCP_FIELD(cameraGroups), MCP_FIELD(bspNodeToCameraGroup), MCP_FIELD(hitSurfaceGroups), MCP_FIELD(surfaceToHitSurfaceGroup), MCP_FIELD(modified))
MCP_RECORD(BrxTnSurface, MCP_FIELD(planeDistance), MCP_FIELD(normal))
MCP_RECORD(BrxTnBspNode, MCP_FIELD(surfaceIndex), MCP_FIELD(negativeChild), MCP_FIELD(positiveChild))
MCP_RECORD(BrxTnCurvePoint, MCP_FIELD(parameter), MCP_FIELD(position), MCP_FIELD(incomingTangent), MCP_FIELD(outgoingTangent))
MCP_RECORD(BrxTnPayload, MCP_FIELD(present), MCP_FIELD(surfaces), MCP_FIELD(bspNodes), MCP_FIELD(curveKind), MCP_FIELD(curveClosed), MCP_FIELD(curvePoints), MCP_FIELD(staticFrame), MCP_FIELD(modified))
MCP_RECORD(BrxDialogEvent, MCP_FIELD(kind), MCP_FIELD(resourceName), MCP_FIELD(primaryOid), MCP_FIELD(replacementOid), MCP_FIELD(zoom), MCP_FIELD(pauseSeconds), MCP_FIELD(waitForInput), MCP_FIELD(promptKind), MCP_FIELD(speakerOid), MCP_FIELD(lipsyncAnimationOid), MCP_FIELD(focusOid), MCP_FIELD(anchorPointOid), MCP_FIELD(subtitle), MCP_FIELD(modified))
MCP_RECORD(BrxDialogPayload, MCP_FIELD(present), MCP_FIELD(events), MCP_FIELD(modified))
MCP_RECORD(BrxAloxRecord, MCP_FIELD(flags), MCP_FIELD(preRotation), MCP_FIELD(postRotation), MCP_FIELD(rotationSourceOid), MCP_FIELD(editorFosterObjectIndex), MCP_FIELD(ikTargetOid), MCP_FIELD(elbowOid), MCP_FIELD(ikPosition), MCP_FIELD(wristPosition), MCP_FIELD(twistRadians), MCP_FIELD(jointParentOid), MCP_FIELD(jointChildOid), MCP_FIELD(segmentScaleCompensation), MCP_FIELD(modified))
MCP_RECORD(BrxPoseCombinationRecord, MCP_FIELD(oid), MCP_FIELD(weights))
MCP_RECORD(BrxObjectRecord, MCP_FIELD(cid), MCP_FIELD(oid), MCP_FIELD(spliceIndex), MCP_FIELD(parentIndex), MCP_FIELD(proxyRuntimeParentIndex), MCP_FIELD(proxyRecordIndex), MCP_FIELD(proxyRootIndex), MCP_FIELD(proxySourceObjectIndex), MCP_FIELD(proxyClone), MCP_FIELD(localTransform), MCP_FIELD(worldTransform), MCP_FIELD(visibilityMode), MCP_FIELD(zoneMode), MCP_FIELD(mrdMode), MCP_FIELD(zoneMask), MCP_FIELD(maximumRenderDistance), MCP_FIELD(celBorderMaximumDistance), MCP_FIELD(maximumRenderDistanceDefault), MCP_FIELD(celBorderMaximumDistanceDefault), MCP_FIELD(renderRadiusSelf), MCP_FIELD(renderRadiusAll), MCP_FIELD(hasAloPayload), MCP_FIELD(so), MCP_FIELD(tn), MCP_FIELD(dialog), MCP_FIELD(alox), MCP_FIELD(hasCnvoPayload), MCP_FIELD(cnvoAverageUVelocity), MCP_FIELD(cnvoAverageVVelocity), MCP_FIELD(globPoseCount), MCP_FIELD(poseCombinations), MCP_FIELD(hidden), MCP_FIELD(properties), MCP_FIELD(details), MCP_FIELD(loaderName), MCP_FIELD(editorName))
MCP_RECORD(BrxShaderAnimationRecord, MCP_FIELD(kind), MCP_FIELD(shaderOid), MCP_FIELD(instanced), MCP_FIELD(values), MCP_FIELD(initialFrame), MCP_FIELD(otherShaderOid), MCP_FIELD(symmetryCount), MCP_FIELD(modified))
MCP_RECORD(BrxShaderRecord::TextureStage, MCP_FIELD(oid), MCP_FIELD(flags), MCP_FIELD(ambientBitmapIndices), MCP_FIELD(diffuseBitmapIndices), MCP_FIELD(saturateBitmapIndices))
MCP_RECORD(BrxShaderRecord, MCP_FIELD(editorResourceId), MCP_FIELD(editorName), MCP_FIELD(kind), MCP_FIELD(flags), MCP_FIELD(oid), MCP_FIELD(zoneFlags), MCP_FIELD(alternateSaturationOid), MCP_FIELD(renderPhase), MCP_FIELD(textureStageCount), MCP_FIELD(animationKind), MCP_FIELD(animation), MCP_FIELD(rgba), MCP_FIELD(volumeRgba), MCP_FIELD(ambientBitmapIndices), MCP_FIELD(diffuseBitmapIndices), MCP_FIELD(saturateBitmapIndices), MCP_FIELD(textureStages))
MCP_RECORD(BrxBitmapRecord, MCP_FIELD(editorResourceId), MCP_FIELD(editorContentRevision), MCP_FIELD(editorName), MCP_FIELD(width), MCP_FIELD(height), MCP_FIELD(zoneFlags), MCP_FIELD(mipCount), MCP_FIELD(pixelBytes), MCP_FIELD(textureFlags), MCP_FIELD(textureFormat))
MCP_RECORD(BrxFontGlyphRecord, MCP_FIELD(character), MCP_FIELD(x), MCP_FIELD(y), MCP_FIELD(width))
MCP_RECORD(BrxFontRecord, MCP_FIELD(bitmapIndex), MCP_FIELD(characterWidth), MCP_FIELD(spaceWidth), MCP_FIELD(height), MCP_FIELD(kind), MCP_FIELD(glyphs), MCP_FIELD(scale))
MCP_RECORD(BrxSourceVertex, MCP_FIELD(position), MCP_FIELD(normal), MCP_FIELD(color), MCP_FIELD(uv), MCP_FIELD(boneIndices), MCP_FIELD(boneWeights))
MCP_RECORD(BrxSourceCelPoint, MCP_FIELD(position), MCP_FIELD(boneIndices), MCP_FIELD(boneWeights))
MCP_RECORD(BrxSourceCelRecord, MCP_FIELD(points), MCP_FIELD(posePositionDeltas))
MCP_RECORD(BrxSubglobRecord, MCP_FIELD(editorName), MCP_FIELD(center), MCP_FIELD(radius), MCP_FIELD(vertexCount), MCP_FIELD(triangleCount), MCP_FIELD(shaderIndex), MCP_FIELD(boneCount), MCP_FIELD(selfIllumination), MCP_FIELD(vertices), MCP_FIELD(triangles), MCP_FIELD(posePositionDeltas), MCP_FIELD(poseNormalDeltas))
MCP_RECORD(BrxGlobsetRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(relight), MCP_FIELD(unknownHeaderByte), MCP_FIELD(boneOids), MCP_FIELD(poseWeights), MCP_FIELD(originalPoseWeights), MCP_FIELD(globIndices))
MCP_RECORD(BrxGlobRecord, MCP_FIELD(editorName), MCP_FIELD(renderGroup), MCP_FIELD(ownerObjectIndex), MCP_FIELD(propertyMask), MCP_FIELD(instanced), MCP_FIELD(sourceGlobIndex), MCP_FIELD(sourceRecordIndex), MCP_FIELD(instanceTransform), MCP_FIELD(sourceToWorld), MCP_FIELD(hasEditorPlacementTransform), MCP_FIELD(editorPlacementTransform), MCP_FIELD(hasZoneMask), MCP_FIELD(zoneMask), MCP_FIELD(hasSubglobRadiusScale), MCP_FIELD(subglobRadiusScale), MCP_FIELD(hasExplicitSortZ), MCP_FIELD(authoredSortZ), MCP_FIELD(effectiveSortZ), MCP_FIELD(hasFogScale), MCP_FIELD(fogScale), MCP_FIELD(hasMaximumRenderDistance), MCP_FIELD(authoredMaximumRenderDistance), MCP_FIELD(maximumRenderDistanceInfinite), MCP_FIELD(maximumRenderDistance), MCP_FIELD(hasCelMaximumRenderDistance), MCP_FIELD(authoredCelMaximumRenderDistance), MCP_FIELD(celMaximumRenderDistanceInfinite), MCP_FIELD(celMaximumRenderDistance), MCP_FIELD(hasShaderAnimation), MCP_FIELD(shaderAnimation), MCP_FIELD(hasGleam), MCP_FIELD(gleamNormal), MCP_FIELD(gleamCurve), MCP_FIELD(hasWarp), MCP_FIELD(warpControllerOid), MCP_FIELD(warpEnvelopeKind), MCP_FIELD(warpMatrixCount), MCP_FIELD(warpPosition), MCP_FIELD(warpUv), MCP_FIELD(center), MCP_FIELD(radius), MCP_FIELD(oid), MCP_FIELD(rotationTracking), MCP_FIELD(renderPhase), MCP_FIELD(flags), MCP_FIELD(sourceSubglobs), MCP_FIELD(sourceCelRecords), MCP_FIELD(modified))
MCP_RECORD(BrxAsegScalarKey, MCP_FIELD(time), MCP_FIELD(value), MCP_FIELD(tangentInKind), MCP_FIELD(tangentOutKind), MCP_FIELD(tangentIn), MCP_FIELD(tangentOut), MCP_FIELD(tangentInTime), MCP_FIELD(tangentOutTime))
MCP_RECORD(BrxAsegVectorKey, MCP_FIELD(time), MCP_FIELD(value), MCP_FIELD(tangentIn), MCP_FIELD(tangentOut))
MCP_RECORD(BrxAsegScalarController, MCP_FIELD(kind), MCP_FIELD(keys))
MCP_RECORD(BrxAsegControllerSummary, MCP_FIELD(kind), MCP_FIELD(keyCount), MCP_FIELD(componentMask), MCP_FIELD(defaultValue), MCP_FIELD(components), MCP_FIELD(scalarKeys), MCP_FIELD(vectorKeys))
MCP_RECORD(BrxAsegChannelRecord, MCP_FIELD(targetOid), MCP_FIELD(flags), MCP_FIELD(position), MCP_FIELD(rotation), MCP_FIELD(scale), MCP_FIELD(twist), MCP_FIELD(poses), MCP_FIELD(stripChannel))
MCP_RECORD(BrxAsegEventRecord, MCP_FIELD(lane), MCP_FIELD(type), MCP_FIELD(frame), MCP_FIELD(shorts), MCP_FIELD(integers), MCP_FIELD(floats), MCP_FIELD(text))
MCP_RECORD(BrxAsegRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(rootOid), MCP_FIELD(maximumFrame), MCP_FIELD(searchRootOids), MCP_FIELD(channels), MCP_FIELD(events), MCP_FIELD(runtimeBlendClass))
MCP_RECORD(BrxShapePathRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(closed), MCP_FIELD(hermite), MCP_FIELD(parameters), MCP_FIELD(points), MCP_FIELD(tangentsIn), MCP_FIELD(tangentsOut))
MCP_RECORD(BrxPathZoneRecord::BspNode, MCP_FIELD(normal), MCP_FIELD(distance), MCP_FIELD(negativeChild), MCP_FIELD(positiveChild))
MCP_RECORD(BrxPathZoneRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(boundaryEdgeCount), MCP_FIELD(vertices), MCP_FIELD(vertexEdges), MCP_FIELD(edges), MCP_FIELD(triangles), MCP_FIELD(bspNodes), MCP_FIELD(modified))
MCP_RECORD(BrxSmStateRecord, MCP_FIELD(oid), MCP_FIELD(automaticNextOid))
MCP_RECORD(BrxSmTransitionRecord, MCP_FIELD(ownsAnimation), MCP_FIELD(fromOid), MCP_FIELD(toOid), MCP_FIELD(flags), MCP_FIELD(probability), MCP_FIELD(animationObjectIndex))
MCP_RECORD(BrxSmRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(states), MCP_FIELD(transitions), MCP_FIELD(serializedTransitionCursor), MCP_FIELD(modified))
MCP_RECORD(BrxRchmBspNode, MCP_FIELD(surfaceIndex), MCP_FIELD(negativeChild), MCP_FIELD(positiveChild))
MCP_RECORD(BrxRchmEdgeRecord, MCP_FIELD(firstPosition), MCP_FIELD(secondPosition), MCP_FIELD(adjacentSurface))
MCP_RECORD(BrxRchmSurfaceRecord, MCP_FIELD(planePositions), MCP_FIELD(radius), MCP_FIELD(center), MCP_FIELD(edges))
MCP_RECORD(BrxRchmRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(animationOids), MCP_FIELD(gridResolution), MCP_FIELD(geometryRadius), MCP_FIELD(positions), MCP_FIELD(totalEdgeCount), MCP_FIELD(surfaces), MCP_FIELD(enabledPositions), MCP_FIELD(tetrahedra), MCP_FIELD(bspNodes), MCP_FIELD(radiusSquared), MCP_FIELD(modified))
MCP_RECORD(BrxKeyholeRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(positions), MCP_FIELD(minimum), MCP_FIELD(maximum), MCP_FIELD(layers), MCP_FIELD(modified))
MCP_RECORD(BrxEffectCurvePoint, MCP_FIELD(u), MCP_FIELD(position), MCP_FIELD(tangentIn), MCP_FIELD(tangentOut))
MCP_RECORD(BrxEffectMeshTriangle, MCP_FIELD(indices), MCP_FIELD(area))
MCP_RECORD(BrxEffectRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(originKind), MCP_FIELD(curveKind), MCP_FIELD(curveClosed), MCP_FIELD(curvePoints), MCP_FIELD(meshPositions), MCP_FIELD(meshTriangles), MCP_FIELD(meshCenter), MCP_FIELD(colorsAreRanges), MCP_FIELD(colors))
MCP_RECORD(BrxVolumePlane, MCP_FIELD(distance), MCP_FIELD(normal))
MCP_RECORD(BrxVolumeNode, MCP_FIELD(surfaceIndex), MCP_FIELD(negativeChild), MCP_FIELD(positiveChild))
MCP_RECORD(BrxVolumeRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(planes), MCP_FIELD(nodes))
MCP_RECORD(BrxCameraRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(targetOid), MCP_FIELD(eye), MCP_FIELD(view), MCP_FIELD(up))
MCP_RECORD(BrxCheckpointRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(warpOid))
MCP_RECORD(BrxWarpRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(velocity), MCP_FIELD(isDefault), MCP_FIELD(isSpecialDefault), MCP_FIELD(aliasOid), MCP_FIELD(cameraInitialAngle), MCP_FIELD(usesCameraInitialAngle), MCP_FIELD(destinationLevel), MCP_FIELD(embeddedAnimationCount), MCP_FIELD(hiddenObjectOids))
MCP_RECORD(BrxExitRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(destinationLevel), MCP_FIELD(alternateLevel), MCP_FIELD(embeddedAnimationCount))
MCP_RECORD(BrxScanRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(text))
MCP_RECORD(BrxLightRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(kind), MCP_FIELD(dynamic), MCP_FIELD(excludeDynamicObjects), MCP_FIELD(position), MCP_FIELD(authoredDirection), MCP_FIELD(direction), MCP_FIELD(highlightHsv), MCP_FIELD(midtoneStrength), MCP_FIELD(shadowStrength), MCP_FIELD(highlightAngle), MCP_FIELD(midtoneAngle), MCP_FIELD(shadowAngle), MCP_FIELD(falloff), MCP_FIELD(coneAngle), MCP_FIELD(hotSpotAngle), MCP_FIELD(up))
MCP_RECORD(BrxGlobWarpRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(renderGroup), MCP_FIELD(firstVertex), MCP_FIELD(vertexCount), MCP_FIELD(firstCelEdge), MCP_FIELD(celEdgeCount), MCP_FIELD(controllerOid), MCP_FIELD(envelopeKind), MCP_FIELD(innerDistance), MCP_FIELD(innerWeight), MCP_FIELD(outerDistance), MCP_FIELD(outerWeight), MCP_FIELD(envelopeMatrix), MCP_FIELD(matrixCount), MCP_FIELD(deformPosition), MCP_FIELD(deformUv), MCP_FIELD(objectToWorld))
MCP_RECORD(BrxLaserBeamRecord, MCP_FIELD(ownerObjectIndex), MCP_FIELD(shapeObjectIndex), MCP_FIELD(points))
MCP_RECORD(BrxWorldHeader, MCP_FIELD(version), MCP_FIELD(soundBankFile), MCP_FIELD(commentaryAudioFile), MCP_FIELD(commentarySpeakers), MCP_FIELD(musicFile), MCP_FIELD(symbolCount), MCP_FIELD(extraOptionCount), MCP_FIELD(expressionNodeCount), MCP_FIELD(rootObjectCount), MCP_FIELD(objectParseDiagnostic), MCP_FIELD(visibilityNodeCount), MCP_FIELD(visibilityZoneCount), MCP_FIELD(worldMidtone), MCP_FIELD(worldShadow), MCP_FIELD(skyColor), MCP_FIELD(cameraFov), MCP_FIELD(cameraNearClip), MCP_FIELD(cameraFarClip), MCP_FIELD(cameraMrdRatio), MCP_FIELD(fogNear), MCP_FIELD(fogFar), MCP_FIELD(fogMaximum), MCP_FIELD(fogColor))
MCP_RECORD(BrxSpliceNode, MCP_FIELD(tag), MCP_FIELD(childNode), MCP_FIELD(nextNode), MCP_FIELD(intValue), MCP_FIELD(floatValue), MCP_FIELD(offset), MCP_FIELD(serializedSize), MCP_FIELD(modified), MCP_FIELD(editorCreated))
MCP_RECORD(BrxSpliceModule, MCP_FIELD(rootNode), MCP_FIELD(offset), MCP_FIELD(serializedSize), MCP_FIELD(modified), MCP_FIELD(editorCreated))
#undef MCP_RECORD
#undef MCP_FIELD

namespace EditorMcp
{
Json InspectRecord(const BrxDocument &doc, const Json &a)
{
    if (!doc.IsOpen()) throw std::runtime_error("Open a map first");
    const auto kind = a.at("kind").get<std::string>();
    const int index = Integer(a, "index");
    const auto pointer = a.value("pointer", std::string());
    const auto inspect = [&](const auto &record) -> Json {
        if (pointer.empty()) return {{"kind", kind}, {"index", index}, {"fields", McpRecordFields(record)},
            {"note", "Use a JSON pointer such as /channels/0/position or /so/collisionGeometry/positions to inspect a field"}};
        return record;
    };
    Json value;
    if (kind == "object") value = inspect(doc.Objects().at(index));
    else if (kind == "glob") value = inspect(doc.Globs().at(index));
    else if (kind == "globset") value = inspect(doc.Globsets().at(index));
    else if (kind == "animation") value = inspect(doc.AnimationSegments().at(index));
    else if (kind == "shape") value = inspect(doc.ShapePaths().at(index));
    else if (kind == "pathzone") value = inspect(doc.PathZones().at(index));
    else if (kind == "state_machine") value = inspect(doc.StateMachines().at(index));
    else if (kind == "reach_map") value = inspect(doc.ReachMaps().at(index));
    else if (kind == "keyhole") value = inspect(doc.Keyholes().at(index));
    else if (kind == "effect") value = inspect(doc.Effects().at(index));
    else if (kind == "volume") value = inspect(doc.Volumes().at(index));
    else if (kind == "camera") value = inspect(doc.Cameras().at(index));
    else if (kind == "light") value = inspect(doc.Lights().at(index));
    else if (kind == "checkpoint") value = inspect(doc.Checkpoints().at(index));
    else if (kind == "warp") value = inspect(doc.Warps().at(index));
    else if (kind == "exit") value = inspect(doc.Exits().at(index));
    else if (kind == "scan") value = inspect(doc.Scans().at(index));
    else if (kind == "bitmap") value = inspect(doc.Bitmaps().at(index));
    else if (kind == "shader") value = inspect(doc.Shaders().at(index));
    else if (kind == "font") value = inspect(doc.Fonts().at(index));
    else if (kind == "proxy") value = inspect(doc.Proxies().at(index));
    else if (kind == "glob_warp") value = inspect(doc.GlobWarps().at(index));
    else if (kind == "laser") value = inspect(doc.LaserBeams().at(index));
    else if (kind == "world_option") value = inspect(doc.WorldOptions().at(index));
    else if (kind == "world_header") { if (index != 0) throw std::runtime_error("World header index is 0"); value = inspect(doc.WorldHeader()); }
    else if (kind == "splice_node") value = inspect(doc.SpliceNodes().at(index));
    else if (kind == "splice_module") value = inspect(doc.SpliceModules().at(index));
    else if (kind == "bitmap_bytes")
    {
        const auto &pixels = doc.Bitmaps().at(index).pixels;
        const int start = a.contains("offset") ? Integer(a, "offset") : 0;
        const int limit = a.contains("limit") ? Integer(a, "limit", 1, 4096) : 256;
        Json data = Json::array();
        for (std::size_t i = static_cast<std::size_t>(start); i < pixels.size() && data.size() < static_cast<std::size_t>(limit); ++i) data.push_back(pixels[i]);
        return {{"total", pixels.size()}, {"offset", start}, {"bytes", data}};
    }
    else throw std::runtime_error("Unknown record kind");
    // A pointer selects one nested field, then arrays are paginated. Empty
    // pointer returns a field inventory so huge meshes cannot flood a response.
    if (pointer.empty()) return value;
    Json field = value.at(Json::json_pointer(pointer));
    if (field.is_array())
    {
        const int start = a.contains("offset") ? Integer(a, "offset") : 0;
        const int limit = a.contains("limit") ? Integer(a, "limit", 1, 300) : 100;
        Json page = Json::array(); const auto total = field.size();
        for (std::size_t i = static_cast<std::size_t>(start); i < total && page.size() < static_cast<std::size_t>(limit); ++i) page.push_back(std::move(field[i]));
        if (page.dump(-1, ' ', false, Json::error_handler_t::replace).size() > 128 * 1024) throw std::runtime_error("Page exceeds 128 KiB; choose a smaller page or deeper JSON pointer");
        return {{"kind", kind}, {"index", index}, {"pointer", pointer}, {"total", total}, {"offset", start}, {"value", page}};
    }
    if (field.dump(-1, ' ', false, Json::error_handler_t::replace).size() > 128 * 1024) throw std::runtime_error("Select a more specific JSON pointer; field exceeds 128 KiB");
    return {{"kind", kind}, {"index", index}, {"pointer", pointer}, {"value", field}};
}
}
