#include "EditorProject.h"

#include "BrxDocument.h"
#include "EditorDocumentOperations.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr std::array<char, 8> kMagic{'C', 'A', 'N', 'E', 'P', 'R', 'J', '\0'};
constexpr std::uint32_t kMaximumRecords = 4'000'000;
constexpr std::uint32_t kMaximumString = 16 * 1024 * 1024;
constexpr std::uint32_t kMaximumBlobBytes = 256 * 1024 * 1024;

class Writer
{
  public:
    explicit Writer(const std::filesystem::path &path) : destination(path)
    {
        // Serialize beside the destination, leaving the previous project
        // untouched until every write and the final close have succeeded.
        static std::atomic<std::uint64_t> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code pathError;
        do
        {
            temporary = destination;
            temporary += ".saving-" + std::to_string(stamp) + "-" +
                         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
        } while (std::filesystem::exists(temporary, pathError) && !pathError);
        if (pathError)
        {
            valid = false;
            return;
        }
        stream.open(temporary, std::ios::binary | std::ios::trunc);
        ownsTemporary = stream.is_open();
    }
    ~Writer()
    {
        if (stream.is_open())
            stream.close();
        if (ownsTemporary)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
    }
    template <class T> void Pod(const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        stream.write(reinterpret_cast<const char *>(&value), sizeof(value));
    }
    void Bytes(const void *data, const std::size_t size)
    {
        stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    }
    void String(const std::string &value)
    {
        if (value.size() > kMaximumString)
        {
            valid = false;
            return;
        }
        const auto size = static_cast<std::uint32_t>(value.size());
        Pod(size);
        Bytes(value.data(), size);
    }
    [[nodiscard]] bool Good() const
    {
        return valid && stream.good();
    }
    bool Commit(std::string &error)
    {
        if (!Good())
        {
            error = "The editor project could not be written completely; the previous project was retained.";
            return false;
        }
        stream.flush();
        if (!stream)
        {
            error = "The editor project could not be flushed; the previous project was retained.";
            return false;
        }
        stream.close();
        if (!stream)
        {
            error = "The editor project could not be closed completely; the previous project was retained.";
            return false;
        }
        std::error_code publishError;
        std::filesystem::rename(temporary, destination, publishError);
        if (publishError)
        {
            error = "The completed editor project could not replace the destination: " + publishError.message() +
                    ". The previous project was retained.";
            return false;
        }
        ownsTemporary = false;
        return true;
    }

  private:
    std::filesystem::path destination;
    std::filesystem::path temporary;
    std::ofstream stream;
    bool valid = true;
    bool ownsTemporary = false;
};

class Reader
{
  public:
    explicit Reader(const std::filesystem::path &path) : stream(path, std::ios::binary) {}
    template <class T> bool Pod(T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return static_cast<bool>(stream.read(reinterpret_cast<char *>(&value), sizeof(value)));
    }
    bool Bytes(void *data, const std::size_t size)
    {
        return static_cast<bool>(stream.read(static_cast<char *>(data), static_cast<std::streamsize>(size)));
    }
    bool String(std::string &value)
    {
        std::uint32_t size = 0;
        if (!Pod(size) || size > kMaximumString)
            return false;
        value.resize(size);
        return size == 0 || Bytes(value.data(), size);
    }
    [[nodiscard]] bool Good() const
    {
        return stream.good();
    }
    [[nodiscard]] bool AtEnd()
    {
        return stream.peek() == std::ifstream::traits_type::eof();
    }

  private:
    std::ifstream stream;
};

void WriteProperty(Writer &writer, const BrxPropertyRecord &property)
{
    writer.Pod(property.eopid);
    writer.Pod(property.kind);
    writer.Pod(property.flags);
    writer.Pod(property.argumentCount);
    writer.Pod(property.offset);
    writer.Pod(property.serializedSize);
    writer.Pod(property.editable);
    writer.Pod(property.modified);
    writer.Pod(property.boolValue);
    writer.Pod(property.intValue);
    writer.Pod(property.uintValue);
    writer.Pod(property.typeValue);
    writer.Bytes(property.floatValue, sizeof(property.floatValue));
    const auto count = static_cast<std::uint32_t>(property.arguments.size());
    writer.Pod(count);
    for (const auto &argument : property.arguments)
    {
        writer.Pod(argument.type);
        writer.Pod(argument.intValue);
        writer.Bytes(argument.floatValue, sizeof(argument.floatValue));
    }
}

bool ReadProperty(Reader &reader, BrxPropertyRecord &property)
{
    if (!reader.Pod(property.eopid) || !reader.Pod(property.kind) || !reader.Pod(property.flags) ||
        !reader.Pod(property.argumentCount) || !reader.Pod(property.offset) || !reader.Pod(property.serializedSize) ||
        !reader.Pod(property.editable) || !reader.Pod(property.modified) || !reader.Pod(property.boolValue) ||
        !reader.Pod(property.intValue) || !reader.Pod(property.uintValue) || !reader.Pod(property.typeValue) ||
        !reader.Bytes(property.floatValue, sizeof(property.floatValue)))
        return false;
    std::uint32_t count = 0;
    if (!reader.Pod(count) || count > kMaximumRecords)
        return false;
    property.arguments.resize(count);
    for (auto &argument : property.arguments)
        if (!reader.Pod(argument.type) || !reader.Pod(argument.intValue) ||
            !reader.Bytes(argument.floatValue, sizeof(argument.floatValue)))
            return false;
    return true;
}

void WriteProperties(Writer &writer, const std::vector<BrxPropertyRecord> &properties)
{
    const auto count = static_cast<std::uint32_t>(properties.size());
    writer.Pod(count);
    for (const auto &property : properties)
        WriteProperty(writer, property);
}

void WriteEditableWorldHeader(Writer &writer, const BrxWorldHeader &header)
{
    writer.String(header.soundBankFile);
    writer.String(header.commentaryAudioFile);
    writer.String(header.commentarySpeakers);
    writer.String(header.musicFile);
    writer.Pod(header.symbolCount);
    writer.Pod(header.extraOptionCount);
    writer.Pod(header.expressionNodeCount);
    writer.Pod(header.worldMidtone);
    writer.Pod(header.worldShadow);
    writer.Bytes(header.skyColor, sizeof(header.skyColor));
    writer.Pod(header.cameraFov);
    writer.Pod(header.cameraNearClip);
    writer.Pod(header.cameraFarClip);
    writer.Pod(header.cameraMrdRatio);
    writer.Pod(header.fogNear);
    writer.Pod(header.fogFar);
    writer.Pod(header.fogMaximum);
    writer.Bytes(header.fogColor, sizeof(header.fogColor));
}

bool ReadEditableWorldHeader(Reader &reader, BrxWorldHeader &header, const std::uint32_t version)
{
    if (version >= 15 && (!reader.String(header.soundBankFile) || !reader.String(header.commentaryAudioFile) ||
                          !reader.String(header.commentarySpeakers) || !reader.String(header.musicFile)))
        return false;
    if (version >= 17 && (!reader.Pod(header.symbolCount) || !reader.Pod(header.extraOptionCount) ||
                          !reader.Pod(header.expressionNodeCount)))
        return false;
    return reader.Pod(header.worldMidtone) && reader.Pod(header.worldShadow) &&
           reader.Bytes(header.skyColor, sizeof(header.skyColor)) && reader.Pod(header.cameraFov) &&
           reader.Pod(header.cameraNearClip) && reader.Pod(header.cameraFarClip) && reader.Pod(header.cameraMrdRatio) &&
           reader.Pod(header.fogNear) && reader.Pod(header.fogFar) && reader.Pod(header.fogMaximum) &&
           reader.Bytes(header.fogColor, sizeof(header.fogColor));
}

bool ReadProperties(Reader &reader, std::vector<BrxPropertyRecord> &properties)
{
    std::uint32_t count = 0;
    if (!reader.Pod(count) || count > kMaximumRecords)
        return false;
    properties.resize(count);
    for (auto &property : properties)
        if (!ReadProperty(reader, property))
            return false;
    return true;
}

template <class T> void WritePodVector(Writer &writer, const std::vector<T> &values)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto count = static_cast<std::uint32_t>(values.size());
    writer.Pod(count);
    if (!values.empty())
        writer.Bytes(values.data(), values.size() * sizeof(T));
}

template <class T> bool ReadPodVector(Reader &reader, std::vector<T> &values)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::uint32_t count = 0;
    if (!reader.Pod(count) || count > kMaximumRecords)
        return false;
    values.resize(count);
    return values.empty() || reader.Bytes(values.data(), values.size() * sizeof(T));
}

void WriteGeometry(Writer &writer, const BrxSoGeometryInfo &geometry)
{
    writer.Pod(geometry.radius);
    writer.Pod(geometry.positionCount);
    writer.Pod(geometry.surfaceCount);
    writer.Pod(geometry.edgeCount);
    writer.Pod(geometry.fullBspNodeCount);
    writer.Bytes(geometry.boundsMin, sizeof(geometry.boundsMin));
    writer.Bytes(geometry.boundsMax, sizeof(geometry.boundsMax));
    WritePodVector(writer, geometry.positions);
    const auto surfaceCount = static_cast<std::uint32_t>(geometry.surfaces.size());
    writer.Pod(surfaceCount);
    for (const auto &surface : geometry.surfaces)
    {
        writer.Pod(surface.positionIndices);
        writer.Pod(surface.radius);
        writer.Pod(surface.center);
        WritePodVector(writer, surface.edges);
    }
    WritePodVector(writer, geometry.bspNodes);
}

bool ReadGeometry(Reader &reader, BrxSoGeometryInfo &geometry, const std::uint32_t version)
{
    if (!reader.Pod(geometry.radius) || !reader.Pod(geometry.positionCount) || !reader.Pod(geometry.surfaceCount) ||
        !reader.Pod(geometry.edgeCount))
        return false;
    if (version >= 6)
    {
        if (!reader.Pod(geometry.fullBspNodeCount))
            return false;
    }
    if (!reader.Bytes(geometry.boundsMin, sizeof(geometry.boundsMin)) ||
        !reader.Bytes(geometry.boundsMax, sizeof(geometry.boundsMax)))
        return false;
    if (version >= 13)
    {
        if (!ReadPodVector(reader, geometry.positions))
            return false;
        std::uint32_t surfaceCount = 0;
        if (!reader.Pod(surfaceCount) || surfaceCount > kMaximumRecords)
            return false;
        geometry.surfaces.resize(surfaceCount);
        for (auto &surface : geometry.surfaces)
            if (!reader.Pod(surface.positionIndices) || !reader.Pod(surface.radius) || !reader.Pod(surface.center) ||
                !ReadPodVector(reader, surface.edges))
                return false;
    }
    if (!ReadPodVector(reader, geometry.bspNodes))
        return false;
    if (version < 6)
        geometry.fullBspNodeCount = static_cast<std::uint16_t>((std::min)(
            geometry.bspNodes.size(), static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())));
    // These counts are serialized redundantly for editor-project compatibility,
    // but the vectors are authoritative and are what the BRX writer emits.
    // Normalize old projects so stale cached counts cannot become false export
    // blockers after geometry editing or migration.
    geometry.positionCount = static_cast<std::uint16_t>(
        (std::min)(geometry.positions.size(), static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())));
    geometry.surfaceCount = static_cast<std::uint16_t>(
        (std::min)(geometry.surfaces.size(), static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())));
    std::size_t edgeCount = 0;
    for (const auto &surface : geometry.surfaces)
        edgeCount += surface.edges.size();
    geometry.edgeCount = static_cast<std::uint16_t>(
        (std::min)(edgeCount, static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())));
    return true;
}

void WriteSo(Writer &writer, const BrxSoPayload &so)
{
    writer.Pod(so.present);
    writer.Pod(so.sourcePrefixOffset);
    writer.Pod(so.sourcePrefixSize);
    writer.Pod(so.massSourceOffset);
    writer.Pod(so.physicsFlags);
    writer.Pod(so.fixedPhysics);
    writer.Pod(so.mass);
    writer.Bytes(so.inertia, sizeof(so.inertia));
    writer.Bytes(so.centerOfMass, sizeof(so.centerOfMass));
    WriteGeometry(writer, so.collisionGeometry);
    WriteGeometry(writer, so.cameraGeometry);
    WritePodVector(writer, so.cameraGroups);
    WritePodVector(writer, so.bspNodeToCameraGroup);
    WritePodVector(writer, so.hitSurfaceGroups);
    WritePodVector(writer, so.surfaceToHitSurfaceGroup);
    writer.Pod(so.modified);
}

bool ReadSo(Reader &reader, BrxSoPayload &so, const std::uint32_t version)
{
    if (!reader.Pod(so.present))
        return false;
    if (version >= 9 && (!reader.Pod(so.sourcePrefixOffset) || !reader.Pod(so.sourcePrefixSize)))
        return false;
    if (version >= 12 && !reader.Pod(so.massSourceOffset))
        return false;
    if (version >= 14 && !reader.Pod(so.physicsFlags))
        return false;
    return reader.Pod(so.fixedPhysics) && reader.Pod(so.mass) && reader.Bytes(so.inertia, sizeof(so.inertia)) &&
           reader.Bytes(so.centerOfMass, sizeof(so.centerOfMass)) &&
           ReadGeometry(reader, so.collisionGeometry, version) && ReadGeometry(reader, so.cameraGeometry, version) &&
           ReadPodVector(reader, so.cameraGroups) && ReadPodVector(reader, so.bspNodeToCameraGroup) &&
           ReadPodVector(reader, so.hitSurfaceGroups) && ReadPodVector(reader, so.surfaceToHitSurfaceGroup) &&
           reader.Pod(so.modified);
}

void WriteTn(Writer &writer, const BrxTnPayload &tn)
{
    writer.Pod(tn.present);
    WritePodVector(writer, tn.surfaces);
    WritePodVector(writer, tn.bspNodes);
    writer.Pod(tn.curveKind);
    writer.Pod(tn.curveClosed);
    WritePodVector(writer, tn.curvePoints);
    writer.Bytes(tn.staticFrame, sizeof(tn.staticFrame));
    writer.Pod(tn.modified);
}

bool ReadTn(Reader &reader, BrxTnPayload &tn)
{
    return reader.Pod(tn.present) && ReadPodVector(reader, tn.surfaces) && ReadPodVector(reader, tn.bspNodes) &&
           reader.Pod(tn.curveKind) && reader.Pod(tn.curveClosed) && ReadPodVector(reader, tn.curvePoints) &&
           reader.Bytes(tn.staticFrame, sizeof(tn.staticFrame)) && reader.Pod(tn.modified);
}

void WriteDialog(Writer &writer, const BrxDialogPayload &dialog)
{
    writer.Pod(dialog.present);
    const auto count = static_cast<std::uint32_t>(dialog.events.size());
    writer.Pod(count);
    for (const auto &event : dialog.events)
    {
        writer.Pod(event.kind);
        writer.String(event.resourceName);
        writer.Pod(event.primaryOid);
        writer.Pod(event.replacementOid);
        writer.Pod(event.zoom);
        writer.Pod(event.pauseSeconds);
        writer.Pod(event.waitForInput);
        writer.Pod(event.promptKind);
        writer.Pod(event.speakerOid);
        writer.Pod(event.lipsyncAnimationOid);
        writer.Pod(event.focusOid);
        writer.Pod(event.anchorPointOid);
        writer.String(event.subtitle);
        writer.Pod(event.modified);
    }
    writer.Pod(dialog.modified);
}

bool ReadDialog(Reader &reader, BrxDialogPayload &dialog)
{
    std::uint32_t count = 0;
    if (!reader.Pod(dialog.present) || !reader.Pod(count) || count > kMaximumRecords)
        return false;
    dialog.events.resize(count);
    for (auto &event : dialog.events)
        if (!reader.Pod(event.kind) || !reader.String(event.resourceName) || !reader.Pod(event.primaryOid) ||
            !reader.Pod(event.replacementOid) || !reader.Pod(event.zoom) || !reader.Pod(event.pauseSeconds) ||
            !reader.Pod(event.waitForInput) || !reader.Pod(event.promptKind) || !reader.Pod(event.speakerOid) ||
            !reader.Pod(event.lipsyncAnimationOid) || !reader.Pod(event.focusOid) ||
            !reader.Pod(event.anchorPointOid) || !reader.String(event.subtitle) || !reader.Pod(event.modified))
            return false;
    return reader.Pod(dialog.modified);
}

void WriteBitmap(Writer &writer, const BrxBitmapRecord &bitmap)
{
    writer.Pod(bitmap.editorResourceId);
    writer.String(bitmap.editorName);
    writer.Pod(bitmap.width);
    writer.Pod(bitmap.height);
    writer.Pod(bitmap.zoneFlags);
    writer.Pod(bitmap.mipCount);
    writer.Pod(bitmap.pixelBytes);
    writer.Pod(bitmap.textureDataOffset);
    writer.Pod(bitmap.textureFlags);
    writer.Pod(bitmap.textureFormat);
    WritePodVector(writer, bitmap.pixels);
}

bool ReadBitmap(Reader &reader, BrxBitmapRecord &bitmap)
{
    return reader.Pod(bitmap.editorResourceId) && reader.String(bitmap.editorName) && reader.Pod(bitmap.width) &&
           reader.Pod(bitmap.height) && reader.Pod(bitmap.zoneFlags) && reader.Pod(bitmap.mipCount) &&
           reader.Pod(bitmap.pixelBytes) && reader.Pod(bitmap.textureDataOffset) && reader.Pod(bitmap.textureFlags) &&
           reader.Pod(bitmap.textureFormat) && ReadPodVector(reader, bitmap.pixels);
}

void WriteShader(Writer &writer, const BrxShaderRecord &shader)
{
    writer.Pod(shader.editorResourceId);
    writer.String(shader.editorName);
    writer.Pod(shader.offset);
    writer.Pod(shader.sourcePrefixOffset);
    writer.Pod(shader.sourcePrefixSize);
    writer.Pod(shader.sourceTailOffset);
    writer.Pod(shader.sourceTailSize);
    writer.Pod(shader.kind);
    writer.Pod(shader.flags);
    writer.Pod(shader.oid);
    writer.Pod(shader.zoneFlags);
    writer.Pod(shader.alternateSaturationOid);
    writer.Pod(shader.renderPhase);
    writer.Pod(shader.textureStageCount);
    writer.Pod(shader.animationKind);
    writer.Pod(shader.animation);
    writer.Bytes(shader.rgba, sizeof(shader.rgba));
    writer.Bytes(shader.volumeRgba, sizeof(shader.volumeRgba));
    WritePodVector(writer, shader.ambientBitmapIndices);
    WritePodVector(writer, shader.diffuseBitmapIndices);
    WritePodVector(writer, shader.saturateBitmapIndices);
    const auto stageCount = static_cast<std::uint32_t>(shader.textureStages.size());
    writer.Pod(stageCount);
    for (const auto &stage : shader.textureStages)
    {
        writer.Pod(stage.oid);
        writer.Pod(stage.flags);
        WritePodVector(writer, stage.ambientBitmapIndices);
        WritePodVector(writer, stage.diffuseBitmapIndices);
        WritePodVector(writer, stage.saturateBitmapIndices);
    }
}

bool ReadShader(Reader &reader, BrxShaderRecord &shader, const std::uint32_t version)
{
    if (!reader.Pod(shader.editorResourceId) || !reader.String(shader.editorName) || !reader.Pod(shader.offset))
        return false;
    if (version >= 11 && (!reader.Pod(shader.sourcePrefixOffset) || !reader.Pod(shader.sourcePrefixSize) ||
                          !reader.Pod(shader.sourceTailOffset) || !reader.Pod(shader.sourceTailSize)))
        return false;
    if (!reader.Pod(shader.kind) || !reader.Pod(shader.flags) || !reader.Pod(shader.oid))
        return false;
    if (version >= 15 && (!reader.Pod(shader.zoneFlags) || !reader.Pod(shader.alternateSaturationOid)))
        return false;
    if (!(reader.Pod(shader.renderPhase) && reader.Pod(shader.textureStageCount) && reader.Pod(shader.animationKind) &&
          reader.Pod(shader.animation) && reader.Bytes(shader.rgba, sizeof(shader.rgba)) &&
          reader.Bytes(shader.volumeRgba, sizeof(shader.volumeRgba)) &&
          ReadPodVector(reader, shader.ambientBitmapIndices) && ReadPodVector(reader, shader.diffuseBitmapIndices) &&
          ReadPodVector(reader, shader.saturateBitmapIndices)))
        return false;
    if (version < 15)
        return true;
    std::uint32_t stageCount = 0;
    if (!reader.Pod(stageCount) || stageCount > kMaximumRecords)
        return false;
    shader.textureStages.resize(stageCount);
    for (auto &stage : shader.textureStages)
        if (!reader.Pod(stage.oid) || !reader.Pod(stage.flags) || !ReadPodVector(reader, stage.ambientBitmapIndices) ||
            !ReadPodVector(reader, stage.diffuseBitmapIndices) || !ReadPodVector(reader, stage.saturateBitmapIndices))
            return false;
    return true;
}

void WriteFont(Writer &writer, const BrxFontRecord &font)
{
    writer.Pod(font.bitmapIndex);
    writer.Pod(font.characterWidth);
    writer.Pod(font.spaceWidth);
    writer.Pod(font.height);
    writer.Pod(font.kind);
    writer.Pod(font.scale);
    WritePodVector(writer, font.glyphs);
}

bool ReadFont(Reader &reader, BrxFontRecord &font)
{
    return reader.Pod(font.bitmapIndex) && reader.Pod(font.characterWidth) && reader.Pod(font.spaceWidth) &&
           reader.Pod(font.height) && reader.Pod(font.kind) && reader.Pod(font.scale) &&
           ReadPodVector(reader, font.glyphs);
}

void WriteSubglob(Writer &writer, const BrxSubglobRecord &subglob)
{
    writer.String(subglob.editorName);
    writer.Bytes(subglob.center, sizeof(subglob.center));
    writer.Pod(subglob.radius);
    writer.Pod(subglob.vertexCount);
    writer.Pod(subglob.triangleCount);
    writer.Pod(subglob.shaderIndex);
    writer.Pod(subglob.boneCount);
    writer.Pod(subglob.selfIllumination);
    WritePodVector(writer, subglob.vertices);
    WritePodVector(writer, subglob.triangles);
    WritePodVector(writer, subglob.posePositionDeltas);
    WritePodVector(writer, subglob.poseNormalDeltas);
}

bool ReadSubglob(Reader &reader, BrxSubglobRecord &subglob, const std::uint32_t version)
{
    if (version >= 20 && !reader.String(subglob.editorName))
        return false;
    return reader.Bytes(subglob.center, sizeof(subglob.center)) && reader.Pod(subglob.radius) &&
           reader.Pod(subglob.vertexCount) && reader.Pod(subglob.triangleCount) && reader.Pod(subglob.shaderIndex) &&
           reader.Pod(subglob.boneCount) && reader.Pod(subglob.selfIllumination) &&
           ReadPodVector(reader, subglob.vertices) && ReadPodVector(reader, subglob.triangles) &&
           ReadPodVector(reader, subglob.posePositionDeltas) && ReadPodVector(reader, subglob.poseNormalDeltas);
}

void WriteGlob(Writer &writer, const BrxGlobRecord &glob)
{
    writer.String(glob.editorName);
    writer.Pod(glob.renderGroup);
    writer.Pod(glob.ownerObjectIndex);
    writer.Pod(glob.propertyMask);
    writer.Pod(glob.instanced);
    writer.Pod(glob.sourceGlobIndex);
    const auto sourceRecordIndex = static_cast<std::uint64_t>(glob.sourceRecordIndex);
    writer.Pod(sourceRecordIndex);
    writer.Bytes(glob.instanceTransform, sizeof(glob.instanceTransform));
    writer.Bytes(glob.sourceToWorld, sizeof(glob.sourceToWorld));
    writer.Pod(glob.hasEditorPlacementTransform);
    writer.Bytes(glob.editorPlacementTransform, sizeof(glob.editorPlacementTransform));
    writer.Pod(glob.hasZoneMask);
    writer.Pod(glob.zoneMask);
    writer.Pod(glob.hasSubglobRadiusScale);
    writer.Pod(glob.subglobRadiusScale);
    writer.Pod(glob.hasExplicitSortZ);
    writer.Pod(glob.authoredSortZ);
    writer.Pod(glob.effectiveSortZ);
    writer.Pod(glob.hasFogScale);
    writer.Pod(glob.fogScale);
    writer.Pod(glob.hasMaximumRenderDistance);
    writer.Pod(glob.authoredMaximumRenderDistance);
    writer.Pod(glob.maximumRenderDistanceInfinite);
    writer.Pod(glob.maximumRenderDistance);
    writer.Pod(glob.hasCelMaximumRenderDistance);
    writer.Pod(glob.authoredCelMaximumRenderDistance);
    writer.Pod(glob.celMaximumRenderDistanceInfinite);
    writer.Pod(glob.celMaximumRenderDistance);
    writer.Pod(glob.hasShaderAnimation);
    writer.Pod(glob.shaderAnimation);
    writer.Pod(glob.hasGleam);
    writer.Bytes(glob.gleamNormal, sizeof(glob.gleamNormal));
    writer.Bytes(glob.gleamCurve, sizeof(glob.gleamCurve));
    writer.Pod(glob.hasWarp);
    writer.Pod(glob.warpControllerOid);
    writer.Pod(glob.warpEnvelopeKind);
    writer.Pod(glob.warpMatrixCount);
    writer.Pod(glob.warpPosition);
    writer.Pod(glob.warpUv);
    writer.Bytes(glob.center, sizeof(glob.center));
    writer.Pod(glob.radius);
    writer.Pod(glob.oid);
    writer.Pod(glob.rotationTracking);
    writer.Pod(glob.renderPhase);
    writer.Pod(glob.flags);
    const auto subglobCount = static_cast<std::uint32_t>(glob.sourceSubglobs.size());
    writer.Pod(subglobCount);
    for (const auto &subglob : glob.sourceSubglobs)
        WriteSubglob(writer, subglob);
    const auto celCount = static_cast<std::uint32_t>(glob.sourceCelRecords.size());
    writer.Pod(celCount);
    for (const auto &cel : glob.sourceCelRecords)
    {
        WritePodVector(writer, cel.points);
        WritePodVector(writer, cel.posePositionDeltas);
    }
    writer.Pod(glob.modified);
    writer.Pod(glob.editorCreatedInstance);
}

bool ReadGlob(Reader &reader, BrxGlobRecord &glob, const std::uint32_t version)
{
    std::uint64_t sourceRecordIndex = 0;
    if ((version >= 20 && !reader.String(glob.editorName)) || !reader.Pod(glob.renderGroup) ||
        !reader.Pod(glob.ownerObjectIndex) || !reader.Pod(glob.propertyMask) || !reader.Pod(glob.instanced) ||
        !reader.Pod(glob.sourceGlobIndex) || !reader.Pod(sourceRecordIndex) ||
        !reader.Bytes(glob.instanceTransform, sizeof(glob.instanceTransform)) ||
        !reader.Bytes(glob.sourceToWorld, sizeof(glob.sourceToWorld)) ||
        !reader.Pod(glob.hasEditorPlacementTransform) ||
        !reader.Bytes(glob.editorPlacementTransform, sizeof(glob.editorPlacementTransform)) ||
        !reader.Pod(glob.hasZoneMask) || !reader.Pod(glob.zoneMask) || !reader.Pod(glob.hasSubglobRadiusScale) ||
        !reader.Pod(glob.subglobRadiusScale) || !reader.Pod(glob.hasExplicitSortZ) || !reader.Pod(glob.authoredSortZ) ||
        !reader.Pod(glob.effectiveSortZ) || !reader.Pod(glob.hasFogScale) || !reader.Pod(glob.fogScale) ||
        !reader.Pod(glob.hasMaximumRenderDistance) || !reader.Pod(glob.authoredMaximumRenderDistance) ||
        !reader.Pod(glob.maximumRenderDistanceInfinite) || !reader.Pod(glob.maximumRenderDistance) ||
        !reader.Pod(glob.hasCelMaximumRenderDistance) || !reader.Pod(glob.authoredCelMaximumRenderDistance) ||
        !reader.Pod(glob.celMaximumRenderDistanceInfinite) || !reader.Pod(glob.celMaximumRenderDistance) ||
        !reader.Pod(glob.hasShaderAnimation) || !reader.Pod(glob.shaderAnimation) || !reader.Pod(glob.hasGleam) ||
        !reader.Bytes(glob.gleamNormal, sizeof(glob.gleamNormal)) ||
        !reader.Bytes(glob.gleamCurve, sizeof(glob.gleamCurve)) || !reader.Pod(glob.hasWarp) ||
        !reader.Pod(glob.warpControllerOid) || !reader.Pod(glob.warpEnvelopeKind) ||
        !reader.Pod(glob.warpMatrixCount) || !reader.Pod(glob.warpPosition) || !reader.Pod(glob.warpUv) ||
        !reader.Bytes(glob.center, sizeof(glob.center)) || !reader.Pod(glob.radius) || !reader.Pod(glob.oid) ||
        !reader.Pod(glob.rotationTracking) || !reader.Pod(glob.renderPhase) || !reader.Pod(glob.flags))
        return false;
    glob.sourceRecordIndex = static_cast<std::size_t>(sourceRecordIndex);
    std::uint32_t subglobCount = 0;
    if (!reader.Pod(subglobCount) || subglobCount > kMaximumRecords)
        return false;
    glob.sourceSubglobs.resize(subglobCount);
    for (auto &subglob : glob.sourceSubglobs)
        if (!ReadSubglob(reader, subglob, version))
            return false;
    std::uint32_t celCount = 0;
    if (!reader.Pod(celCount) || celCount > kMaximumRecords)
        return false;
    glob.sourceCelRecords.resize(celCount);
    for (auto &cel : glob.sourceCelRecords)
        if (!ReadPodVector(reader, cel.points) || !ReadPodVector(reader, cel.posePositionDeltas))
            return false;
    return reader.Pod(glob.modified) && reader.Pod(glob.editorCreatedInstance);
}

void WriteGlobset(Writer &writer, const BrxGlobsetRecord &globset)
{
    writer.Pod(globset.ownerObjectIndex);
    writer.Pod(globset.relight);
    writer.Pod(globset.unknownHeaderByte);
    WritePodVector(writer, globset.boneOids);
    WritePodVector(writer, globset.poseWeights);
    WritePodVector(writer, globset.originalPoseWeights);
    const auto count = static_cast<std::uint32_t>(globset.globIndices.size());
    writer.Pod(count);
    for (const auto index : globset.globIndices)
    {
        const auto value = static_cast<std::uint64_t>(index);
        writer.Pod(value);
    }
}

bool ReadGlobset(Reader &reader, BrxGlobsetRecord &globset)
{
    if (!reader.Pod(globset.ownerObjectIndex) || !reader.Pod(globset.relight) ||
        !reader.Pod(globset.unknownHeaderByte) || !ReadPodVector(reader, globset.boneOids) ||
        !ReadPodVector(reader, globset.poseWeights) || !ReadPodVector(reader, globset.originalPoseWeights))
        return false;
    std::uint32_t count = 0;
    if (!reader.Pod(count) || count > kMaximumRecords)
        return false;
    globset.globIndices.resize(count);
    for (auto &index : globset.globIndices)
    {
        std::uint64_t value = 0;
        if (!reader.Pod(value))
            return false;
        index = static_cast<std::size_t>(value);
    }
    return true;
}

void WriteProxy(Writer &writer, const BrxProxyRecord &proxy)
{
    writer.Pod(proxy.ownerObjectIndex);
    writer.Bytes(proxy.localTransform, sizeof(proxy.localTransform));
    WritePodVector(writer, proxy.roots);
    const auto count = static_cast<std::uint32_t>(proxy.overrides.size());
    writer.Pod(count);
    for (const auto &item : proxy.overrides)
    {
        writer.Pod(item.targetOid);
        writer.Pod(item.targetObjectIndex);
        writer.Pod(item.replacementSpliceIndex);
        writer.Pod(item.zoneMask);
        writer.Pod(item.mrdMode);
        writer.Pod(item.maximumRenderDistance);
        writer.Pod(item.inheritedSpliceIndex);
        writer.Pod(item.inheritedZoneMask);
        writer.Pod(item.inheritedMrdMode);
        writer.Pod(item.inheritedMaximumRenderDistance);
        writer.Pod(item.inheritedPropertiesCaptured);
        WriteProperties(writer, item.inheritedProperties);
        WriteProperties(writer, item.properties);
    }
    WritePodVector(writer, proxy.attachedChildObjectIndices);
}

bool ReadProxy(Reader &reader, BrxProxyRecord &proxy, const std::uint32_t version)
{
    if (!reader.Pod(proxy.ownerObjectIndex) || !reader.Bytes(proxy.localTransform, sizeof(proxy.localTransform)) ||
        !ReadPodVector(reader, proxy.roots))
        return false;
    std::uint32_t count = 0;
    if (!reader.Pod(count) || count > kMaximumRecords)
        return false;
    proxy.overrides.resize(count);
    for (auto &item : proxy.overrides)
    {
        if (!reader.Pod(item.targetOid) || !reader.Pod(item.targetObjectIndex) ||
            !reader.Pod(item.replacementSpliceIndex) || !reader.Pod(item.zoneMask) || !reader.Pod(item.mrdMode) ||
            !reader.Pod(item.maximumRenderDistance) || !reader.Pod(item.inheritedSpliceIndex) ||
            !reader.Pod(item.inheritedZoneMask) || !reader.Pod(item.inheritedMrdMode) ||
            !reader.Pod(item.inheritedMaximumRenderDistance))
            return false;
        if (version >= 21)
        {
            if (!reader.Pod(item.inheritedPropertiesCaptured) || !ReadProperties(reader, item.inheritedProperties))
                return false;
        }
        if (!ReadProperties(reader, item.properties))
            return false;
    }
    return ReadPodVector(reader, proxy.attachedChildObjectIndices);
}

void WriteWarp(Writer &writer, const BrxWarpRecord &warp)
{
    writer.Pod(warp.ownerObjectIndex);
    writer.Bytes(warp.velocity, sizeof(warp.velocity));
    writer.Pod(warp.isDefault);
    writer.Pod(warp.isSpecialDefault);
    writer.Pod(warp.aliasOid);
    writer.Pod(warp.cameraInitialAngle);
    writer.Pod(warp.usesCameraInitialAngle);
    writer.String(warp.destinationLevel);
    writer.Pod(warp.embeddedAnimationCount);
    WritePodVector(writer, warp.hiddenObjectOids);
}

bool ReadWarp(Reader &reader, BrxWarpRecord &warp)
{
    return reader.Pod(warp.ownerObjectIndex) && reader.Bytes(warp.velocity, sizeof(warp.velocity)) &&
           reader.Pod(warp.isDefault) && reader.Pod(warp.isSpecialDefault) && reader.Pod(warp.aliasOid) &&
           reader.Pod(warp.cameraInitialAngle) && reader.Pod(warp.usesCameraInitialAngle) &&
           reader.String(warp.destinationLevel) && reader.Pod(warp.embeddedAnimationCount) &&
           ReadPodVector(reader, warp.hiddenObjectOids);
}

void WriteExit(Writer &writer, const BrxExitRecord &exit)
{
    writer.Pod(exit.ownerObjectIndex);
    writer.String(exit.destinationLevel);
    writer.String(exit.alternateLevel);
    writer.Pod(exit.embeddedAnimationCount);
}

bool ReadExit(Reader &reader, BrxExitRecord &exit)
{
    return reader.Pod(exit.ownerObjectIndex) && reader.String(exit.destinationLevel) &&
           reader.String(exit.alternateLevel) && reader.Pod(exit.embeddedAnimationCount);
}

void WriteVolume(Writer &writer, const BrxVolumeRecord &volume)
{
    writer.Pod(volume.ownerObjectIndex);
    WritePodVector(writer, volume.planes);
    WritePodVector(writer, volume.nodes);
}

bool ReadVolume(Reader &reader, BrxVolumeRecord &volume)
{
    return reader.Pod(volume.ownerObjectIndex) && ReadPodVector(reader, volume.planes) &&
           ReadPodVector(reader, volume.nodes);
}

void WriteEffect(Writer &writer, const BrxEffectRecord &effect)
{
    writer.Pod(effect.ownerObjectIndex);
    writer.Pod(effect.originKind);
    writer.Pod(effect.curveKind);
    writer.Pod(effect.curveClosed);
    WritePodVector(writer, effect.curvePoints);
    WritePodVector(writer, effect.meshPositions);
    WritePodVector(writer, effect.meshTriangles);
    writer.Pod(effect.meshCenter);
    writer.Pod(effect.colorsAreRanges);
    WritePodVector(writer, effect.colors);
}

bool ReadEffect(Reader &reader, BrxEffectRecord &effect)
{
    return reader.Pod(effect.ownerObjectIndex) && reader.Pod(effect.originKind) && reader.Pod(effect.curveKind) &&
           reader.Pod(effect.curveClosed) && ReadPodVector(reader, effect.curvePoints) &&
           ReadPodVector(reader, effect.meshPositions) && ReadPodVector(reader, effect.meshTriangles) &&
           reader.Pod(effect.meshCenter) && reader.Pod(effect.colorsAreRanges) && ReadPodVector(reader, effect.colors);
}

void WriteShapePath(Writer &writer, const BrxShapePathRecord &shape)
{
    writer.Pod(shape.ownerObjectIndex);
    writer.Pod(shape.closed);
    writer.Pod(shape.hermite);
    WritePodVector(writer, shape.parameters);
    WritePodVector(writer, shape.points);
    WritePodVector(writer, shape.tangentsIn);
    WritePodVector(writer, shape.tangentsOut);
}

bool ReadShapePath(Reader &reader, BrxShapePathRecord &shape)
{
    return reader.Pod(shape.ownerObjectIndex) && reader.Pod(shape.closed) && reader.Pod(shape.hermite) &&
           ReadPodVector(reader, shape.parameters) && ReadPodVector(reader, shape.points) &&
           ReadPodVector(reader, shape.tangentsIn) && ReadPodVector(reader, shape.tangentsOut);
}

void WritePathZone(Writer &writer, const BrxPathZoneRecord &zone)
{
    writer.Pod(zone.ownerObjectIndex);
    writer.Pod(zone.boundaryEdgeCount);
    WritePodVector(writer, zone.vertices);
    const auto adjacencyCount = static_cast<std::uint32_t>(zone.vertexEdges.size());
    writer.Pod(adjacencyCount);
    for (const auto &adjacency : zone.vertexEdges)
        WritePodVector(writer, adjacency);
    WritePodVector(writer, zone.edges);
    WritePodVector(writer, zone.triangles);
    WritePodVector(writer, zone.bspNodes);
    writer.Pod(zone.modified);
}

bool ReadPathZone(Reader &reader, BrxPathZoneRecord &zone, const std::uint32_t version)
{
    if (!reader.Pod(zone.ownerObjectIndex) || !reader.Pod(zone.boundaryEdgeCount) ||
        !ReadPodVector(reader, zone.vertices))
        return false;
    if (version >= 5)
    {
        std::uint32_t adjacencyCount = 0;
        if (!reader.Pod(adjacencyCount) || adjacencyCount > kMaximumRecords)
            return false;
        zone.vertexEdges.resize(adjacencyCount);
        for (auto &adjacency : zone.vertexEdges)
            if (!ReadPodVector(reader, adjacency))
                return false;
    }
    if (!ReadPodVector(reader, zone.edges) || !ReadPodVector(reader, zone.triangles) ||
        !ReadPodVector(reader, zone.bspNodes) || !reader.Pod(zone.modified))
        return false;
    if (version < 5)
    {
        zone.vertexEdges.resize(zone.vertices.size());
        for (std::size_t edgeIndex = 0; edgeIndex < zone.edges.size(); ++edgeIndex)
            for (const auto vertexIndex : zone.edges[edgeIndex])
                if (vertexIndex < zone.vertexEdges.size() && edgeIndex <= UINT16_MAX)
                    zone.vertexEdges[vertexIndex].push_back(static_cast<std::uint16_t>(edgeIndex));
    }
    return true;
}

void WriteStateMachine(Writer &writer, const BrxSmRecord &machine)
{
    writer.Pod(machine.ownerObjectIndex);
    WritePodVector(writer, machine.states);
    WritePodVector(writer, machine.transitions);
    writer.Pod(machine.serializedTransitionCursor);
    writer.Pod(machine.modified);
}

bool ReadStateMachine(Reader &reader, BrxSmRecord &machine)
{
    return reader.Pod(machine.ownerObjectIndex) && ReadPodVector(reader, machine.states) &&
           ReadPodVector(reader, machine.transitions) && reader.Pod(machine.serializedTransitionCursor) &&
           reader.Pod(machine.modified);
}

void WriteReachMap(Writer &writer, const BrxRchmRecord &reach)
{
    writer.Pod(reach.ownerObjectIndex);
    writer.Pod(reach.animationOids);
    writer.Pod(reach.gridResolution);
    writer.Pod(reach.geometryRadius);
    WritePodVector(writer, reach.positions);
    writer.Pod(reach.totalEdgeCount);
    const auto surfaceCount = static_cast<std::uint32_t>(reach.surfaces.size());
    writer.Pod(surfaceCount);
    for (const auto &surface : reach.surfaces)
    {
        writer.Pod(surface.planePositions);
        writer.Pod(surface.radius);
        writer.Pod(surface.center);
        WritePodVector(writer, surface.edges);
    }
    const auto enabledCount = static_cast<std::uint32_t>(reach.enabledPositions.size());
    writer.Pod(enabledCount);
    for (const bool enabled : reach.enabledPositions)
        writer.Pod(enabled);
    WritePodVector(writer, reach.tetrahedra);
    WritePodVector(writer, reach.bspNodes);
    writer.Pod(reach.radiusSquared);
    writer.Pod(reach.modified);
}

bool ReadReachMap(Reader &reader, BrxRchmRecord &reach, const std::uint32_t version)
{
    if (!reader.Pod(reach.ownerObjectIndex) || !reader.Pod(reach.animationOids) || !reader.Pod(reach.gridResolution) ||
        !reader.Pod(reach.geometryRadius) || !ReadPodVector(reader, reach.positions))
        return false;
    std::uint16_t legacySurfaceCount = 0;
    if (version < 16 && !reader.Pod(legacySurfaceCount))
        return false;
    if (!reader.Pod(reach.totalEdgeCount))
        return false;
    if (version >= 16)
    {
        std::uint32_t surfaceCount = 0;
        if (!reader.Pod(surfaceCount) || surfaceCount > kMaximumRecords)
            return false;
        reach.surfaces.resize(surfaceCount);
        for (auto &surface : reach.surfaces)
            if (!reader.Pod(surface.planePositions) || !reader.Pod(surface.radius) || !reader.Pod(surface.center) ||
                !ReadPodVector(reader, surface.edges))
                return false;
    }
    else if (version >= 10)
    {
        std::uint32_t surfacePayloadCount = 0;
        if (!reader.Pod(surfacePayloadCount) || surfacePayloadCount > kMaximumRecords)
            return false;
        if (surfacePayloadCount != legacySurfaceCount)
            return false;
        reach.surfaces.reserve(surfacePayloadCount);
        for (std::uint32_t i = 0; i < surfacePayloadCount; ++i)
        {
            std::uint32_t size = 0;
            if (!reader.Pod(size) || size > kMaximumBlobBytes)
                return false;
            std::vector<std::uint8_t> payload(size);
            if (size != 0 && !reader.Bytes(payload.data(), payload.size()))
                return false;
            if (size < 23u || size != 23u + static_cast<std::size_t>(payload[22]) * 6u)
                return false;
            BrxRchmSurfaceRecord surface;
            std::size_t cursor = 0;
            const auto readU16 = [&]()
            {
                const auto value = static_cast<std::uint16_t>(payload[cursor] |
                                                              (static_cast<std::uint16_t>(payload[cursor + 1]) << 8u));
                cursor += 2;
                return value;
            };
            const auto readF32 = [&]()
            {
                const auto bits = static_cast<std::uint32_t>(payload[cursor]) |
                                  (static_cast<std::uint32_t>(payload[cursor + 1]) << 8u) |
                                  (static_cast<std::uint32_t>(payload[cursor + 2]) << 16u) |
                                  (static_cast<std::uint32_t>(payload[cursor + 3]) << 24u);
                cursor += 4;
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            };
            for (auto &position : surface.planePositions)
                position = readU16();
            surface.radius = readF32();
            for (auto &component : surface.center)
                component = readF32();
            const auto edgeCount = payload[cursor++];
            surface.edges.reserve(edgeCount);
            for (std::uint32_t edge = 0; edge < edgeCount; ++edge)
                surface.edges.push_back({readU16(), readU16(), readU16()});
            reach.surfaces.push_back(std::move(surface));
        }
    }
    std::uint32_t enabledCount = 0;
    if (!reader.Pod(enabledCount) || enabledCount > kMaximumRecords)
        return false;
    reach.enabledPositions.assign(enabledCount, false);
    for (std::uint32_t i = 0; i < enabledCount; ++i)
    {
        bool enabled = false;
        if (!reader.Pod(enabled))
            return false;
        reach.enabledPositions[i] = enabled;
    }
    return ReadPodVector(reader, reach.tetrahedra) && ReadPodVector(reader, reach.bspNodes) &&
           reader.Pod(reach.radiusSquared) && reader.Pod(reach.modified);
}

void WriteAsegController(Writer &writer, const BrxAsegControllerSummary &controller)
{
    writer.Pod(controller.kind);
    writer.Pod(controller.keyCount);
    writer.Pod(controller.componentMask);
    writer.Pod(controller.defaultValue);
    for (const auto &component : controller.components)
    {
        writer.Pod(component.kind);
        WritePodVector(writer, component.keys);
    }
    WritePodVector(writer, controller.scalarKeys);
    WritePodVector(writer, controller.vectorKeys);
}

bool ReadAsegController(Reader &reader, BrxAsegControllerSummary &controller)
{
    if (!reader.Pod(controller.kind) || !reader.Pod(controller.keyCount) || !reader.Pod(controller.componentMask) ||
        !reader.Pod(controller.defaultValue))
        return false;
    for (auto &component : controller.components)
        if (!reader.Pod(component.kind) || !ReadPodVector(reader, component.keys))
            return false;
    return ReadPodVector(reader, controller.scalarKeys) && ReadPodVector(reader, controller.vectorKeys);
}

void WriteAseg(Writer &writer, const BrxAsegRecord &segment)
{
    writer.Pod(segment.ownerObjectIndex);
    writer.Pod(segment.rootOid);
    writer.Pod(segment.maximumFrame);
    WritePodVector(writer, segment.searchRootOids);
    const auto channelCount = static_cast<std::uint32_t>(segment.channels.size());
    writer.Pod(channelCount);
    for (const auto &channel : segment.channels)
    {
        writer.Pod(channel.targetOid);
        writer.Pod(channel.flags);
        WriteAsegController(writer, channel.position);
        WriteAsegController(writer, channel.rotation);
        WriteAsegController(writer, channel.scale);
        WriteAsegController(writer, channel.twist);
        const auto poseCount = static_cast<std::uint32_t>(channel.poses.size());
        writer.Pod(poseCount);
        for (const auto &pose : channel.poses)
            WriteAsegController(writer, pose);
        writer.Pod(channel.stripChannel);
    }
    const auto eventCount = static_cast<std::uint32_t>(segment.events.size());
    writer.Pod(eventCount);
    for (const auto &event : segment.events)
    {
        writer.Pod(event.lane);
        writer.Pod(event.type);
        writer.Pod(event.frame);
        writer.Pod(event.shorts);
        writer.Pod(event.integers);
        writer.Pod(event.floats);
        writer.String(event.text);
    }
    writer.Pod(segment.runtimeBlendClass);
}

bool ReadAseg(Reader &reader, BrxAsegRecord &segment)
{
    if (!reader.Pod(segment.ownerObjectIndex) || !reader.Pod(segment.rootOid) || !reader.Pod(segment.maximumFrame) ||
        !ReadPodVector(reader, segment.searchRootOids))
        return false;
    std::uint32_t channelCount = 0;
    if (!reader.Pod(channelCount) || channelCount > kMaximumRecords)
        return false;
    segment.channels.resize(channelCount);
    for (auto &channel : segment.channels)
    {
        if (!reader.Pod(channel.targetOid) || !reader.Pod(channel.flags) ||
            !ReadAsegController(reader, channel.position) || !ReadAsegController(reader, channel.rotation) ||
            !ReadAsegController(reader, channel.scale) || !ReadAsegController(reader, channel.twist))
            return false;
        std::uint32_t poseCount = 0;
        if (!reader.Pod(poseCount) || poseCount > kMaximumRecords)
            return false;
        channel.poses.resize(poseCount);
        for (auto &pose : channel.poses)
            if (!ReadAsegController(reader, pose))
                return false;
        if (!reader.Pod(channel.stripChannel))
            return false;
    }
    std::uint32_t eventCount = 0;
    if (!reader.Pod(eventCount) || eventCount > kMaximumRecords)
        return false;
    segment.events.resize(eventCount);
    for (auto &event : segment.events)
        if (!reader.Pod(event.lane) || !reader.Pod(event.type) || !reader.Pod(event.frame) ||
            !reader.Pod(event.shorts) || !reader.Pod(event.integers) || !reader.Pod(event.floats) ||
            !reader.String(event.text))
            return false;
    return reader.Pod(segment.runtimeBlendClass);
}

void WriteLaser(Writer &writer, const BrxLaserBeamRecord &laser)
{
    writer.Pod(laser.ownerObjectIndex);
    writer.Pod(laser.shapeObjectIndex);
    WritePodVector(writer, laser.points);
}

bool ReadLaser(Reader &reader, BrxLaserBeamRecord &laser)
{
    return reader.Pod(laser.ownerObjectIndex) && reader.Pod(laser.shapeObjectIndex) &&
           ReadPodVector(reader, laser.points);
}

void WriteKeyhole(Writer &writer, const BrxKeyholeRecord &keyhole)
{
    writer.Pod(keyhole.ownerObjectIndex);
    WritePodVector(writer, keyhole.positions);
    writer.Pod(keyhole.minimum);
    writer.Pod(keyhole.maximum);
    for (const auto &layer : keyhole.layers)
        WritePodVector(writer, layer);
    writer.Pod(keyhole.modified);
}

bool ReadKeyhole(Reader &reader, BrxKeyholeRecord &keyhole)
{
    if (!reader.Pod(keyhole.ownerObjectIndex) || !ReadPodVector(reader, keyhole.positions) ||
        !reader.Pod(keyhole.minimum) || !reader.Pod(keyhole.maximum))
        return false;
    for (auto &layer : keyhole.layers)
        if (!ReadPodVector(reader, layer))
            return false;
    return reader.Pod(keyhole.modified);
}

void WriteObject(Writer &writer, const BrxObjectRecord &object)
{
    writer.Pod(object.offset);
    writer.Pod(object.cid);
    writer.Pod(object.oid);
    writer.Pod(object.spliceIndex);
    writer.Pod(object.parentIndex);
    writer.Pod(object.proxyRuntimeParentIndex);
    writer.Pod(object.proxyRecordIndex);
    writer.Pod(object.proxyRootIndex);
    writer.Pod(object.proxySourceObjectIndex);
    writer.Pod(object.proxyClone);
    writer.Pod(object.dataOffset);
    writer.Pod(object.endOffset);
    writer.Pod(object.loaderImplemented);
    writer.Bytes(object.localTransform, sizeof(object.localTransform));
    writer.Bytes(object.worldTransform, sizeof(object.worldTransform));
    writer.Pod(object.firstRenderGroup);
    writer.Pod(object.renderGroupCount);
    writer.Pod(object.visibilityMode);
    writer.Pod(object.zoneMode);
    writer.Pod(object.mrdMode);
    writer.Pod(object.zoneMask);
    writer.Pod(object.maximumRenderDistance);
    writer.Pod(object.celBorderMaximumDistance);
    writer.Pod(object.maximumRenderDistanceDefault);
    writer.Pod(object.celBorderMaximumDistanceDefault);
    writer.Pod(object.renderRadiusSelf);
    writer.Pod(object.renderRadiusAll);
    writer.Pod(object.hasAloPayload);
    writer.Pod(object.aloPayloadOffset);
    writer.Pod(object.globsetSourceOffset);
    writer.Pod(object.globsetSourceSize);
    writer.Pod(object.hasCnvoPayload);
    writer.Pod(object.cnvoAverageUVelocity);
    writer.Pod(object.cnvoAverageVVelocity);
    writer.Pod(object.hidden);
    writer.Pod(object.globPoseCount);
    WriteSo(writer, object.so);
    WriteTn(writer, object.tn);
    WriteDialog(writer, object.dialog);
    writer.Pod(object.alox);
    const auto combinationCount = static_cast<std::uint32_t>(object.poseCombinations.size());
    writer.Pod(combinationCount);
    for (const auto &combination : object.poseCombinations)
    {
        writer.Pod(combination.oid);
        WritePodVector(writer, combination.weights);
    }
    WriteProperties(writer, object.properties);
    const auto detailCount = static_cast<std::uint32_t>(object.details.size());
    writer.Pod(detailCount);
    for (const auto &detail : object.details)
    {
        writer.String(detail.name);
        writer.String(detail.value);
    }
    writer.String(object.loaderName);
    writer.String(object.editorName);
}

bool ReadObject(Reader &reader, BrxObjectRecord &object, const std::uint32_t version)
{
    if (!reader.Pod(object.offset) || !reader.Pod(object.cid) || !reader.Pod(object.oid) ||
        !reader.Pod(object.spliceIndex) || !reader.Pod(object.parentIndex) ||
        !reader.Pod(object.proxyRuntimeParentIndex) || !reader.Pod(object.proxyRecordIndex) ||
        !reader.Pod(object.proxyRootIndex) || !reader.Pod(object.proxySourceObjectIndex) ||
        !reader.Pod(object.proxyClone) || !reader.Pod(object.dataOffset) || !reader.Pod(object.endOffset) ||
        !reader.Pod(object.loaderImplemented) || !reader.Bytes(object.localTransform, sizeof(object.localTransform)) ||
        !reader.Bytes(object.worldTransform, sizeof(object.worldTransform)) || !reader.Pod(object.firstRenderGroup) ||
        !reader.Pod(object.renderGroupCount) || !reader.Pod(object.visibilityMode) || !reader.Pod(object.zoneMode) ||
        !reader.Pod(object.mrdMode) || !reader.Pod(object.zoneMask) || !reader.Pod(object.maximumRenderDistance) ||
        !reader.Pod(object.celBorderMaximumDistance))
        return false;
    if (version >= 8 &&
        (!reader.Pod(object.maximumRenderDistanceDefault) || !reader.Pod(object.celBorderMaximumDistanceDefault)))
        return false;
    if (version < 8)
    {
        object.maximumRenderDistanceDefault = object.maximumRenderDistance == 1.0e10f;
        object.celBorderMaximumDistanceDefault =
            object.celBorderMaximumDistance == (std::min)(object.maximumRenderDistance, 2000.0f);
    }
    if (!reader.Pod(object.renderRadiusSelf) || !reader.Pod(object.renderRadiusAll) ||
        !reader.Pod(object.hasAloPayload))
        return false;
    if (version >= 9 && (!reader.Pod(object.aloPayloadOffset) || !reader.Pod(object.globsetSourceOffset) ||
                         !reader.Pod(object.globsetSourceSize)))
        return false;
    if (version >= 7 && (!reader.Pod(object.hasCnvoPayload) || !reader.Pod(object.cnvoAverageUVelocity) ||
                         !reader.Pod(object.cnvoAverageVVelocity)))
        return false;
    if (!reader.Pod(object.hidden) || !reader.Pod(object.globPoseCount) || !ReadSo(reader, object.so, version) ||
        !ReadTn(reader, object.tn) || !ReadDialog(reader, object.dialog) || !reader.Pod(object.alox))
        return false;
    std::uint32_t combinationCount = 0;
    if (!reader.Pod(combinationCount) || combinationCount > kMaximumRecords)
        return false;
    object.poseCombinations.resize(combinationCount);
    for (auto &combination : object.poseCombinations)
        if (!reader.Pod(combination.oid) || !ReadPodVector(reader, combination.weights))
            return false;
    if (!ReadProperties(reader, object.properties))
        return false;
    std::uint32_t detailCount = 0;
    if (!reader.Pod(detailCount) || detailCount > kMaximumRecords)
        return false;
    object.details.resize(detailCount);
    for (auto &detail : object.details)
        if (!reader.String(detail.name) || !reader.String(detail.value))
            return false;
    if (!reader.String(object.loaderName))
        return false;
    return version < 18 || reader.String(object.editorName);
}
}

std::filesystem::path EditorProject::DefaultPathFor(const BrxDocument &document)
{
    auto path = document.Path();
    path.replace_extension(".caneproject");
    return path;
}

bool EditorProject::Save(const std::filesystem::path &path, const BrxDocument &document, std::string &error)
{
    error.clear();
    if (!document.IsOpen())
    {
        error = "No authored editor world is open.";
        return false;
    }
    std::unordered_set<std::string> editorNames;
    for (const auto &object : document.Objects())
    {
        if (object.editorName.empty())
            continue;
        if ((!std::isalpha(static_cast<unsigned char>(object.editorName.front())) &&
             object.editorName.front() != '_') ||
            !std::all_of(object.editorName.begin(), object.editorName.end(),
                         [](const char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; }))
        {
            error = "Editor object name '" + object.editorName + "' is invalid.";
            return false;
        }
        std::string folded = object.editorName;
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
        if (!editorNames.insert(folded).second)
        {
            error = "Editor object name '" + object.editorName + "' is duplicated.";
            return false;
        }
    }
    if (document.VisibilityZoneNames().size() > 32u)
    {
        error = "The editor project has more than 32 VISMAP zone names.";
        return false;
    }
    Writer writer(path);
    if (!writer.Good())
    {
        error = "Could not create the staged editor project; the previous project was retained.";
        return false;
    }
    writer.Bytes(kMagic.data(), kMagic.size());
    writer.Pod(CurrentVersion);
    // Keep the four legacy header fields so older readers fail cleanly, but do
    // not bind a v17 authored snapshot to source-PCBRX identity or bytes.
    writer.String(std::string{});
    writer.Pod(std::uint64_t{});
    writer.Pod(std::uint32_t{17});
    writer.Pod(std::uint64_t{});
    WriteEditableWorldHeader(writer, document.WorldHeader());
    WriteProperties(writer, document.WorldOptions());
    const auto objectCount = static_cast<std::uint32_t>(document.Objects().size());
    writer.Pod(objectCount);
    for (const auto &object : document.Objects())
        WriteObject(writer, object);
    const auto bitmapCount = static_cast<std::uint32_t>(document.Bitmaps().size());
    writer.Pod(bitmapCount);
    for (const auto &bitmap : document.Bitmaps())
        WriteBitmap(writer, bitmap);
    const auto shaderCount = static_cast<std::uint32_t>(document.Shaders().size());
    writer.Pod(shaderCount);
    for (const auto &shader : document.Shaders())
        WriteShader(writer, shader);
    const auto fontCount = static_cast<std::uint32_t>(document.Fonts().size());
    writer.Pod(fontCount);
    for (const auto &font : document.Fonts())
        WriteFont(writer, font);
    const auto globCount = static_cast<std::uint32_t>(document.Globs().size());
    writer.Pod(globCount);
    for (const auto &glob : document.Globs())
        WriteGlob(writer, glob);
    const auto globsetCount = static_cast<std::uint32_t>(document.Globsets().size());
    writer.Pod(globsetCount);
    for (const auto &globset : document.Globsets())
        WriteGlobset(writer, globset);
    WritePodVector(writer, document.CollisionTriangles());
    WritePodVector(writer, document.Lights());
    WritePodVector(writer, document.Cameras());
    const auto proxyCount = static_cast<std::uint32_t>(document.Proxies().size());
    writer.Pod(proxyCount);
    for (const auto &proxy : document.Proxies())
        WriteProxy(writer, proxy);
    const auto warpCount = static_cast<std::uint32_t>(document.Warps().size());
    writer.Pod(warpCount);
    for (const auto &warp : document.Warps())
        WriteWarp(writer, warp);
    const auto scanCount = static_cast<std::uint32_t>(document.Scans().size());
    writer.Pod(scanCount);
    for (const auto &scan : document.Scans())
    {
        writer.Pod(scan.ownerObjectIndex);
        writer.String(scan.text);
    }
    const auto effectCount = static_cast<std::uint32_t>(document.Effects().size());
    writer.Pod(effectCount);
    for (const auto &effect : document.Effects())
        WriteEffect(writer, effect);
    const auto shapeCount = static_cast<std::uint32_t>(document.ShapePaths().size());
    writer.Pod(shapeCount);
    for (const auto &shape : document.ShapePaths())
        WriteShapePath(writer, shape);
    const auto zoneCount = static_cast<std::uint32_t>(document.PathZones().size());
    writer.Pod(zoneCount);
    for (const auto &zone : document.PathZones())
        WritePathZone(writer, zone);
    const auto machineCount = static_cast<std::uint32_t>(document.StateMachines().size());
    writer.Pod(machineCount);
    for (const auto &machine : document.StateMachines())
        WriteStateMachine(writer, machine);
    const auto reachCount = static_cast<std::uint32_t>(document.ReachMaps().size());
    writer.Pod(reachCount);
    for (const auto &reach : document.ReachMaps())
        WriteReachMap(writer, reach);
    const auto exitCount = static_cast<std::uint32_t>(document.Exits().size());
    writer.Pod(exitCount);
    for (const auto &exit : document.Exits())
        WriteExit(writer, exit);
    const auto volumeCount = static_cast<std::uint32_t>(document.Volumes().size());
    writer.Pod(volumeCount);
    for (const auto &volume : document.Volumes())
        WriteVolume(writer, volume);
    WritePodVector(writer, document.GlobWarps());
    WritePodVector(writer, document.SpliceNodes());
    WritePodVector(writer, document.SpliceModules());
    WritePodVector(writer, document.Checkpoints());
    const auto asegCount = static_cast<std::uint32_t>(document.AnimationSegments().size());
    writer.Pod(asegCount);
    for (const auto &segment : document.AnimationSegments())
        WriteAseg(writer, segment);
    const auto laserCount = static_cast<std::uint32_t>(document.LaserBeams().size());
    writer.Pod(laserCount);
    for (const auto &laser : document.LaserBeams())
        WriteLaser(writer, laser);
    const auto keyholeCount = static_cast<std::uint32_t>(document.Keyholes().size());
    writer.Pod(keyholeCount);
    for (const auto &keyhole : document.Keyholes())
        WriteKeyhole(writer, keyhole);
    WritePodVector(writer, document.VisibilityNodes());
    WritePodVector(writer, document.VisibilityOneHopMasks());
    const auto visibilityNameCount = static_cast<std::uint32_t>(document.VisibilityZoneNames().size());
    writer.Pod(visibilityNameCount);
    for (const auto &name : document.VisibilityZoneNames())
        writer.String(name);
    return writer.Commit(error);
}

bool EditorProject::Load(const std::filesystem::path &path, BrxDocument &document, std::string &error)
{
    error.clear();
    Reader reader(path);
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    if (!reader.Bytes(magic.data(), magic.size()) || magic != kMagic || !reader.Pod(version) || version < 2 ||
        version > CurrentVersion)
    {
        error = "This is not a supported ProjectCane editor project.";
        return false;
    }
    std::string sourceName;
    std::uint64_t sourceSize = 0;
    std::uint32_t sourceVersion = 0;
    std::uint64_t sourceFingerprint = 0;
    if (!reader.String(sourceName) || !reader.Pod(sourceSize) || !reader.Pod(sourceVersion) ||
        !reader.Pod(sourceFingerprint))
    {
        error = "The editor project header is truncated.";
        return false;
    }
    if (version < 17)
    {
        if (!document.IsOpen())
        {
            error = "This legacy editor project still requires its source PCBRX to be open.";
            return false;
        }
        std::uint64_t hash = 1469598103934665603ull;
        for (const auto byte : document.Payload())
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        hash ^= document.FileSize();
        hash *= 1099511628211ull;
        hash ^= document.Version();
        hash *= 1099511628211ull;
        if (sourceName != document.Path().filename().string() || sourceSize != document.FileSize() ||
            sourceVersion != document.Version() || sourceFingerprint != hash)
        {
            error = "The legacy editor project belongs to a different PCBRX source revision.";
            return false;
        }
    }
    BrxWorldHeader worldHeader = version < 17 ? document.WorldHeader() : BrxWorldHeader{};
    std::vector<BrxPropertyRecord> worldOptions;
    if (!ReadEditableWorldHeader(reader, worldHeader, version) || !ReadProperties(reader, worldOptions))
    {
        error = "The editor project world data is damaged.";
        return false;
    }
    if (version >= 17)
    {
        worldHeader.magic = 0x52424350u; // PCBR
        worldHeader.version = 17;
    }
    std::uint32_t objectCount = 0;
    if (!reader.Pod(objectCount) || objectCount > kMaximumRecords)
    {
        error = "The editor project object count is invalid.";
        return false;
    }
    std::vector<BrxObjectRecord> objects(objectCount);
    for (auto &object : objects)
        if (!ReadObject(reader, object, version))
        {
            error = "The editor project object data is truncated or damaged.";
            return false;
        }
    std::uint32_t bitmapCount = 0;
    if (!reader.Pod(bitmapCount) || bitmapCount > kMaximumRecords)
    {
        error = "The editor project bitmap count is invalid.";
        return false;
    }
    std::vector<BrxBitmapRecord> bitmaps(bitmapCount);
    for (auto &bitmap : bitmaps)
        if (!ReadBitmap(reader, bitmap))
        {
            error = "The editor project bitmap data is damaged.";
            return false;
        }
    std::uint32_t shaderCount = 0;
    if (!reader.Pod(shaderCount) || shaderCount > kMaximumRecords)
    {
        error = "The editor project shader count is invalid.";
        return false;
    }
    std::vector<BrxShaderRecord> shaders(shaderCount);
    for (auto &shader : shaders)
        if (!ReadShader(reader, shader, version))
        {
            error = "The editor project shader data is damaged.";
            return false;
        }
    if (version < 11 && shaders.size() == document.Shaders().size())
        for (std::size_t index = 0; index < shaders.size(); ++index)
        {
            shaders[index].sourcePrefixOffset = document.Shaders()[index].sourcePrefixOffset;
            shaders[index].sourcePrefixSize = document.Shaders()[index].sourcePrefixSize;
            shaders[index].sourceTailOffset = document.Shaders()[index].sourceTailOffset;
            shaders[index].sourceTailSize = document.Shaders()[index].sourceTailSize;
        }
    if (version < 15 && shaders.size() == document.Shaders().size())
        for (std::size_t index = 0; index < shaders.size(); ++index)
        {
            shaders[index].zoneFlags = document.Shaders()[index].zoneFlags;
            shaders[index].alternateSaturationOid = document.Shaders()[index].alternateSaturationOid;
            shaders[index].textureStages = document.Shaders()[index].textureStages;
        }
    std::vector<BrxFontRecord> fonts = document.Fonts();
    if (version >= 15)
    {
        std::uint32_t fontCount = 0;
        if (!reader.Pod(fontCount) || fontCount > kMaximumRecords)
        {
            error = "The editor project font count is invalid.";
            return false;
        }
        fonts.resize(fontCount);
        for (auto &font : fonts)
            if (!ReadFont(reader, font))
            {
                error = "The editor project font data is damaged.";
                return false;
            }
    }
    std::uint32_t globCount = 0;
    if (!reader.Pod(globCount) || globCount > kMaximumRecords)
    {
        error = "The editor project GLOB count is invalid.";
        return false;
    }
    std::vector<BrxGlobRecord> globs(globCount);
    for (auto &glob : globs)
        if (!ReadGlob(reader, glob, version))
        {
            error = "The editor project GLOB data is damaged.";
            return false;
        }
    std::uint32_t globsetCount = 0;
    if (!reader.Pod(globsetCount) || globsetCount > kMaximumRecords)
    {
        error = "The editor project GLOBSET count is invalid.";
        return false;
    }
    std::vector<BrxGlobsetRecord> globsets(globsetCount);
    for (auto &globset : globsets)
        if (!ReadGlobset(reader, globset))
        {
            error = "The editor project GLOBSET data is damaged.";
            return false;
        }
    std::vector<BrxCollisionTriangle> collision;
    std::vector<BrxLightRecord> lights;
    std::vector<BrxCameraRecord> cameras;
    if (!ReadPodVector(reader, collision) || !ReadPodVector(reader, lights) || !ReadPodVector(reader, cameras))
    {
        error = "The editor project scene-resource data is damaged.";
        return false;
    }
    std::uint32_t proxyCount = 0;
    if (!reader.Pod(proxyCount) || proxyCount > kMaximumRecords)
    {
        error = "The editor project proxy count is invalid.";
        return false;
    }
    std::vector<BrxProxyRecord> proxies(proxyCount);
    for (auto &proxy : proxies)
        if (!ReadProxy(reader, proxy, version))
        {
            error = "The editor project proxy data is damaged.";
            return false;
        }
    std::uint32_t warpCount = 0;
    if (!reader.Pod(warpCount) || warpCount > kMaximumRecords)
    {
        error = "The editor project warp count is invalid.";
        return false;
    }
    std::vector<BrxWarpRecord> warps(warpCount);
    for (auto &warp : warps)
        if (!ReadWarp(reader, warp))
        {
            error = "The editor project warp data is damaged.";
            return false;
        }
    std::uint32_t scanCount = 0;
    if (!reader.Pod(scanCount) || scanCount > kMaximumRecords)
    {
        error = "The editor project scan count is invalid.";
        return false;
    }
    std::vector<BrxScanRecord> scans(scanCount);
    for (auto &scan : scans)
        if (!reader.Pod(scan.ownerObjectIndex) || !reader.String(scan.text))
        {
            error = "The editor project scan data is damaged.";
            return false;
        }
    std::uint32_t effectCount = 0;
    if (!reader.Pod(effectCount) || effectCount > kMaximumRecords)
    {
        error = "The editor project effect count is invalid.";
        return false;
    }
    std::vector<BrxEffectRecord> effects(effectCount);
    for (auto &effect : effects)
        if (!ReadEffect(reader, effect))
        {
            error = "The editor project effect data is damaged.";
            return false;
        }
    std::uint32_t shapeCount = 0;
    if (!reader.Pod(shapeCount) || shapeCount > kMaximumRecords)
    {
        error = "The editor project shape count is invalid.";
        return false;
    }
    std::vector<BrxShapePathRecord> shapes(shapeCount);
    for (auto &shape : shapes)
        if (!ReadShapePath(reader, shape))
        {
            error = "The editor project shape data is damaged.";
            return false;
        }
    std::uint32_t zoneCount = 0;
    if (!reader.Pod(zoneCount) || zoneCount > kMaximumRecords)
    {
        error = "The editor project path-zone count is invalid.";
        return false;
    }
    std::vector<BrxPathZoneRecord> zones(zoneCount);
    for (auto &zone : zones)
        if (!ReadPathZone(reader, zone, version))
        {
            error = "The editor project path-zone data is damaged.";
            return false;
        }
    std::uint32_t machineCount = 0;
    if (!reader.Pod(machineCount) || machineCount > kMaximumRecords)
    {
        error = "The editor project state-machine count is invalid.";
        return false;
    }
    std::vector<BrxSmRecord> machines(machineCount);
    for (auto &machine : machines)
        if (!ReadStateMachine(reader, machine))
        {
            error = "The editor project state-machine data is damaged.";
            return false;
        }
    std::uint32_t reachCount = 0;
    if (!reader.Pod(reachCount) || reachCount > kMaximumRecords)
    {
        error = "The editor project reach-map count is invalid.";
        return false;
    }
    std::vector<BrxRchmRecord> reaches(reachCount);
    for (auto &reach : reaches)
        if (!ReadReachMap(reader, reach, version))
        {
            error = "The editor project reach-map data is damaged.";
            return false;
        }
    if (version < 10)
        for (auto &reach : reaches)
        {
            const auto source =
                std::find_if(document.ReachMaps().begin(), document.ReachMaps().end(), [&](const auto &candidate)
                             { return candidate.ownerObjectIndex == reach.ownerObjectIndex; });
            if (source != document.ReachMaps().end())
                reach.surfaces = source->surfaces;
        }
    std::vector<BrxExitRecord> exits = document.Exits();
    if (version >= 3)
    {
        std::uint32_t exitCount = 0;
        if (!reader.Pod(exitCount) || exitCount > kMaximumRecords)
        {
            error = "The editor project EXIT count is invalid.";
            return false;
        }
        exits.resize(exitCount);
        for (auto &exit : exits)
            if (!ReadExit(reader, exit))
            {
                error = "The editor project EXIT data is damaged.";
                return false;
            }
    }
    std::vector<BrxVolumeRecord> volumes = document.Volumes();
    if (version >= 4)
    {
        std::uint32_t volumeCount = 0;
        if (!reader.Pod(volumeCount) || volumeCount > kMaximumRecords)
        {
            error = "The editor project volume count is invalid.";
            return false;
        }
        volumes.resize(volumeCount);
        for (auto &volume : volumes)
            if (!ReadVolume(reader, volume))
            {
                error = "The editor project volume data is damaged.";
                return false;
            }
    }
    std::vector<BrxGlobWarpRecord> globWarps;
    if (!ReadPodVector(reader, globWarps))
    {
        error = "The editor project mesh-warp data is damaged.";
        return false;
    }
    std::vector<BrxSpliceNode> spliceNodes;
    std::vector<BrxSpliceModule> spliceModules;
    std::vector<BrxCheckpointRecord> checkpoints;
    std::vector<BrxAsegRecord> animationSegments;
    std::vector<BrxLaserBeamRecord> laserBeams;
    std::vector<BrxKeyholeRecord> keyholes;
    std::vector<BrxVisibilityNode> visibilityNodes;
    std::vector<std::uint32_t> visibilityOneHopMasks;
    std::vector<std::string> visibilityZoneNames;
    if (version >= 17)
    {
        if (!ReadPodVector(reader, spliceNodes) || !ReadPodVector(reader, spliceModules) ||
            !ReadPodVector(reader, checkpoints))
        {
            error = "The editor project authored graph data is damaged.";
            return false;
        }
        std::uint32_t asegCount = 0;
        if (!reader.Pod(asegCount) || asegCount > kMaximumRecords)
        {
            error = "The editor project ASEG count is invalid.";
            return false;
        }
        animationSegments.resize(asegCount);
        for (auto &segment : animationSegments)
            if (!ReadAseg(reader, segment))
            {
                error = "The editor project ASEG data is damaged.";
                return false;
            }
        std::uint32_t laserCount = 0;
        if (!reader.Pod(laserCount) || laserCount > kMaximumRecords)
        {
            error = "The editor project LASEN count is invalid.";
            return false;
        }
        laserBeams.resize(laserCount);
        for (auto &laser : laserBeams)
            if (!ReadLaser(reader, laser))
            {
                error = "The editor project LASEN data is damaged.";
                return false;
            }
        std::uint32_t keyholeCount = 0;
        if (!reader.Pod(keyholeCount) || keyholeCount > kMaximumRecords)
        {
            error = "The editor project keyhole count is invalid.";
            return false;
        }
        keyholes.resize(keyholeCount);
        for (auto &keyhole : keyholes)
            if (!ReadKeyhole(reader, keyhole))
            {
                error = "The editor project keyhole data is damaged.";
                return false;
            }
        if (!ReadPodVector(reader, visibilityNodes) || !ReadPodVector(reader, visibilityOneHopMasks))
        {
            error = "The editor project visibility data is damaged.";
            return false;
        }
        if (version >= 19)
        {
            std::uint32_t nameCount = 0;
            if (!reader.Pod(nameCount) || nameCount > 32u)
            {
                error = "The editor project visibility-zone name count is invalid.";
                return false;
            }
            visibilityZoneNames.resize(nameCount);
            for (auto &name : visibilityZoneNames)
                if (!reader.String(name))
                {
                    error = "The editor project visibility-zone names are damaged.";
                    return false;
                }
        }
    }
    else
    {
        spliceNodes = document.SpliceNodes();
        spliceModules = document.SpliceModules();
        checkpoints = document.Checkpoints();
        animationSegments = document.AnimationSegments();
        laserBeams = document.LaserBeams();
        keyholes = document.Keyholes();
        visibilityNodes = document.VisibilityNodes();
        visibilityOneHopMasks = document.VisibilityOneHopMasks();
    }
    if (version < 9 && objects.size() == document.Objects().size())
    {
        // Source ranges are immutable properties of the bound BRX. Recover
        // them from the freshly decoded document when opening an older editor
        // project instead of permanently disabling the structural writer.
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            const auto &sourceObject = document.Objects()[index];
            if (objects[index].cid != sourceObject.cid || objects[index].oid != sourceObject.oid ||
                objects[index].offset != sourceObject.offset)
                continue;
            objects[index].aloPayloadOffset = sourceObject.aloPayloadOffset;
            objects[index].globsetSourceOffset = sourceObject.globsetSourceOffset;
            objects[index].globsetSourceSize = sourceObject.globsetSourceSize;
            objects[index].so.sourcePrefixOffset = sourceObject.so.sourcePrefixOffset;
            objects[index].so.sourcePrefixSize = sourceObject.so.sourcePrefixSize;
        }
    }
    if (version < 12 && objects.size() == document.Objects().size())
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].cid == document.Objects()[index].cid &&
                objects[index].oid == document.Objects()[index].oid &&
                objects[index].offset == document.Objects()[index].offset)
                objects[index].so.massSourceOffset = document.Objects()[index].so.massSourceOffset;
    if (version < 13 && objects.size() == document.Objects().size())
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].cid == document.Objects()[index].cid &&
                objects[index].oid == document.Objects()[index].oid &&
                objects[index].offset == document.Objects()[index].offset)
            {
                objects[index].so.collisionGeometry.positions =
                    document.Objects()[index].so.collisionGeometry.positions;
                objects[index].so.collisionGeometry.surfaces = document.Objects()[index].so.collisionGeometry.surfaces;
                objects[index].so.cameraGeometry.positions = document.Objects()[index].so.cameraGeometry.positions;
                objects[index].so.cameraGeometry.surfaces = document.Objects()[index].so.cameraGeometry.surfaces;
            }
    if (version < 14 && objects.size() == document.Objects().size())
        for (std::size_t index = 0; index < objects.size(); ++index)
            if (objects[index].cid == document.Objects()[index].cid &&
                objects[index].oid == document.Objects()[index].oid &&
                objects[index].offset == document.Objects()[index].offset)
                objects[index].so.physicsFlags = document.Objects()[index].so.physicsFlags;
    if (version >= 17 && !reader.AtEnd())
    {
        error = "The standalone editor project has trailing or unread data; refusing a partial snapshot load.";
        return false;
    }
    if (version >= 17)
    {
        document.Close();
        document.path_ = std::filesystem::absolute(path);
        document.version_ = 17;
        document.payloadValidated_ = true;
    }
    document.EditableWorldHeader() = worldHeader;
    document.EditableWorldOptions() = std::move(worldOptions);
    document.EditableObjects() = std::move(objects);
    document.EditableBitmaps() = std::move(bitmaps);
    document.EditableShaders() = std::move(shaders);
    document.EditableFonts() = std::move(fonts);
    document.EditableGlobs() = std::move(globs);
    document.EditableGlobsets() = std::move(globsets);
    document.EditableCollisionTriangles() = std::move(collision);
    document.EditableLights() = std::move(lights);
    document.EditableCameras() = std::move(cameras);
    document.EditableProxies() = std::move(proxies);
    document.EditableWarps() = std::move(warps);
    document.EditableScans() = std::move(scans);
    document.EditableEffects() = std::move(effects);
    document.EditableShapePaths() = std::move(shapes);
    document.EditablePathZones() = std::move(zones);
    document.EditableStateMachines() = std::move(machines);
    document.EditableReachMaps() = std::move(reaches);
    document.EditableExits() = std::move(exits);
    document.EditableVolumes() = std::move(volumes);
    document.EditableGlobWarps() = std::move(globWarps);
    document.EditableSpliceNodes() = std::move(spliceNodes);
    document.EditableSpliceModules() = std::move(spliceModules);
    document.EditableCheckpoints() = std::move(checkpoints);
    document.EditableAnimationSegments() = std::move(animationSegments);
    document.EditableLaserBeams() = std::move(laserBeams);
    document.EditableKeyholes() = std::move(keyholes);
    document.EditableVisibilityNodes() = std::move(visibilityNodes);
    document.EditableVisibilityOneHopMasks() = std::move(visibilityOneHopMasks);
    visibilityZoneNames.resize((std::min)(static_cast<std::size_t>(worldHeader.visibilityZoneCount), std::size_t{32}));
    document.EditableVisibilityZoneNames() = std::move(visibilityZoneNames);
    document.nextEditorResourceId_ = 1;
    for (const auto &bitmap : document.Bitmaps())
        document.nextEditorResourceId_ = (std::max)(document.nextEditorResourceId_, bitmap.editorResourceId + 1);
    for (const auto &shader : document.Shaders())
        document.nextEditorResourceId_ = (std::max)(document.nextEditorResourceId_, shader.editorResourceId + 1);
    for (auto &bitmap : document.EditableBitmaps())
        if (bitmap.editorResourceId == 0)
            bitmap.editorResourceId = document.nextEditorResourceId_++;
    for (auto &shader : document.EditableShaders())
        if (shader.editorResourceId == 0)
            shader.editorResourceId = document.nextEditorResourceId_++;
    if (version >= 17)
    {
        auto &header = document.EditableWorldHeader();
        header.spliceMapOffset = 0;
        header.worldOptionsOffset = 0;
        header.commentaryOffset = 0;
        header.musicOffset = 0;
        header.shadersOffset = 0;
        header.fontTableSourceOffset = 0;
        header.fontTableSourceSize = 0;
        header.worldObjectsOffset = 0;
        header.spliceCount = static_cast<std::uint32_t>(document.SpliceModules().size());
        header.expressionNodeCount = static_cast<std::uint32_t>(document.SpliceNodes().size());
        header.declaredShaderAnimationCount = static_cast<std::uint16_t>(
            std::count_if(document.Shaders().begin(), document.Shaders().end(),
                          [](const auto &shader) { return shader.animation.kind != BrxShaderAnimationKind::None; }));
        header.fontCount = static_cast<std::uint16_t>(
            (std::min)(document.Fonts().size(), static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)())));
        header.rootObjectCount = 0;
        header.firstRootCid = -1;
        header.firstRootOid = -1;
        header.firstRootSpliceIndex = -1;
        for (const auto &object : document.Objects())
        {
            if (object.proxyClone || object.parentIndex >= 0)
                continue;
            if (header.rootObjectCount == 0)
            {
                header.firstRootCid = object.cid;
                header.firstRootOid = object.oid;
                header.firstRootSpliceIndex = object.spliceIndex;
            }
            if (header.rootObjectCount != (std::numeric_limits<std::uint16_t>::max)())
                ++header.rootObjectCount;
        }
        header.objectParseDiagnostic = "Self-contained authored editor project";
        header.parsedGlobCount = static_cast<std::uint32_t>(document.Globs().size());
        header.parsedSubglobCount = 0;
        header.parsedVertexCount = 0;
        header.parsedTriangleCount = 0;
        for (const auto &glob : document.Globs())
            for (const auto &subglob : glob.sourceSubglobs)
            {
                ++header.parsedSubglobCount;
                header.parsedVertexCount += subglob.vertices.size();
                header.parsedTriangleCount += subglob.triangles.size();
            }
        header.visibilityNodeCount = static_cast<std::uint32_t>(document.VisibilityNodes().size());
        header.visibilityZoneCount = static_cast<std::uint16_t>(document.VisibilityOneHopMasks().size());
    }
    document.RebuildRenderPacketsFromGlobs();
    return true;
}

bool EditorProject::SaveHierarchy(const std::filesystem::path &path, const BrxDocument &document,
                                  const std::int32_t rootObjectIndex, std::string &error)
{
    if (rootObjectIndex < 0 || static_cast<std::size_t>(rootObjectIndex) >= document.Objects().size())
    {
        error = "No valid hierarchy root is selected.";
        return false;
    }
    BrxDocument package = document;
    constexpr const char *marker = "__CaneHierarchyPackageRoot__";
    for (auto &object : package.EditableObjects())
        if (object.editorName == marker)
            object.editorName.clear();
    auto &root = package.EditableObjects()[static_cast<std::size_t>(rootObjectIndex)];
    root.details.push_back({"Hierarchy package editor name", root.editorName});
    root.editorName = marker;
    return Save(path, package, error);
}

bool EditorProject::LoadHierarchy(const std::filesystem::path &path, BrxDocument &destination,
                                  const std::int32_t parentObjectIndex, std::int32_t &importedRoot, std::string &error)
{
    importedRoot = -1;
    BrxDocument package;
    if (!Load(path, package, error))
        return false;
    constexpr const char *marker = "__CaneHierarchyPackageRoot__";
    const auto root = package.FindObjectByEditorName(marker);
    if (root < 0)
    {
        error = "This editor snapshot is not a hierarchy package.";
        return false;
    }
    auto &rootObject = package.EditableObjects()[static_cast<std::size_t>(root)];
    const auto savedName =
        std::find_if(rootObject.details.begin(), rootObject.details.end(),
                     [](const BrxObjectDetail &detail) { return detail.name == "Hierarchy package editor name"; });
    rootObject.editorName = savedName == rootObject.details.end() ? std::string{} : savedName->value;
    if (savedName != rootObject.details.end())
        rootObject.details.erase(savedName);
    BrxDocument before = destination;
    const auto result = EditorDocumentOperations::ImportHierarchy(destination, package, root, parentObjectIndex);
    if (!result.succeeded)
    {
        destination = std::move(before);
        error = result.message;
        return false;
    }
    importedRoot = result.primaryObjectIndex;
    return true;
}

bool EditorProject::ReplaceHierarchy(const std::filesystem::path &path, BrxDocument &destination,
                                     const std::int32_t destinationRoot, std::int32_t &replacementRoot,
                                     std::string &error)
{
    replacementRoot = -1;
    BrxDocument package;
    if (!Load(path, package, error))
        return false;
    constexpr const char *marker = "__CaneHierarchyPackageRoot__";
    const auto root = package.FindObjectByEditorName(marker);
    if (root < 0)
    {
        error = "This editor snapshot is not a hierarchy package.";
        return false;
    }
    auto &rootObject = package.EditableObjects()[static_cast<std::size_t>(root)];
    const auto savedName =
        std::find_if(rootObject.details.begin(), rootObject.details.end(),
                     [](const BrxObjectDetail &detail) { return detail.name == "Hierarchy package editor name"; });
    rootObject.editorName = savedName == rootObject.details.end() ? std::string{} : savedName->value;
    if (savedName != rootObject.details.end())
        rootObject.details.erase(savedName);
    BrxDocument before = destination;
    const auto result = EditorDocumentOperations::ReplaceHierarchy(destination, package, root, destinationRoot);
    if (!result.succeeded)
    {
        destination = std::move(before);
        error = result.message;
        return false;
    }
    replacementRoot = result.primaryObjectIndex;
    return true;
}

bool EditorProject::SavePortableProxy(const std::filesystem::path &path, const BrxDocument &document,
                                      const std::int32_t selectedObjectIndex, std::string &error)
{
    if (selectedObjectIndex < 0 || static_cast<std::size_t>(selectedObjectIndex) >= document.Objects().size())
    {
        error = "No valid PROXY object or member is selected.";
        return false;
    }
    BrxDocument package = document;
    const auto &selected = package.Objects()[static_cast<std::size_t>(selectedObjectIndex)];
    std::int32_t proxyIndex = selected.proxyRecordIndex;
    if (proxyIndex < 0)
        for (std::size_t i = 0; i < package.Proxies().size(); ++i)
            if (package.Proxies()[i].ownerObjectIndex == selectedObjectIndex)
            {
                proxyIndex = static_cast<std::int32_t>(i);
                break;
            }
    if (proxyIndex < 0 || static_cast<std::size_t>(proxyIndex) >= package.Proxies().size())
    {
        error = "The selection is not a PROXY wrapper or a member of a PROXY hierarchy.";
        return false;
    }

    std::int32_t rootIndex = selected.proxyRootIndex;
    if (rootIndex < 0)
        rootIndex = 0;
    if (static_cast<std::size_t>(rootIndex) >= package.Proxies()[static_cast<std::size_t>(proxyIndex)].roots.size())
    {
        error = "The selected PROXY root identity is invalid.";
        return false;
    }
    auto root = package.Proxies()[static_cast<std::size_t>(proxyIndex)].roots[static_cast<std::size_t>(rootIndex)];
    if (root.kind == BrxProxyRootKind::SourceReference)
    {
        const auto unique = EditorDocumentOperations::MakeProxyRootUnique(package, root.instantiatedRootObjectIndex);
        if (!unique.succeeded)
        {
            error = "Could not materialize the linked guard definition: " + unique.message;
            return false;
        }
    }
    // Portable definitions cannot retain references to source slots in the old
    // level. Materialize every nested component owned by the selected root too.
    root = package.Proxies()[static_cast<std::size_t>(proxyIndex)].roots[static_cast<std::size_t>(rootIndex)];
    const auto belongsToPortableHierarchy = [&](const std::int32_t objectIndex)
    {
        std::int32_t current = objectIndex;
        for (std::size_t guard = 0; current >= 0 && static_cast<std::size_t>(current) < package.Objects().size() &&
                                    guard++ <= package.Objects().size();)
        {
            if (current == root.instantiatedRootObjectIndex)
                return true;
            const auto parent = package.Objects()[static_cast<std::size_t>(current)].parentIndex;
            if (parent == current)
                break;
            current = parent;
        }
        return false;
    };
    for (std::size_t nestedProxyIndex = 0; nestedProxyIndex < package.Proxies().size(); ++nestedProxyIndex)
    {
        if (static_cast<std::int32_t>(nestedProxyIndex) == proxyIndex)
            continue;
        const auto nested = package.Proxies()[nestedProxyIndex];
        if (!belongsToPortableHierarchy(nested.ownerObjectIndex))
            continue;
        for (const auto &nestedRoot : nested.roots)
            if (nestedRoot.kind == BrxProxyRootKind::SourceReference)
            {
                const auto unique =
                    EditorDocumentOperations::MakeProxyRootUnique(package, nestedRoot.instantiatedRootObjectIndex);
                if (!unique.succeeded)
                {
                    error = "Could not materialize a nested PROXY dependency: " + unique.message;
                    return false;
                }
            }
    }

    constexpr const char *marker = "__CanePortableProxyPackageRoot__";
    for (auto &object : package.EditableObjects())
        if (object.editorName == marker)
            object.editorName.clear();
    const auto owner = package.Proxies()[static_cast<std::size_t>(proxyIndex)].ownerObjectIndex;
    if (owner < 0 || static_cast<std::size_t>(owner) >= package.Objects().size())
    {
        error = "The selected PROXY has no valid wrapper object.";
        return false;
    }
    auto &wrapper = package.EditableObjects()[static_cast<std::size_t>(owner)];
    wrapper.details.push_back({"Portable PROXY editor name", wrapper.editorName});
    wrapper.details.push_back({"Portable PROXY root index", std::to_string(rootIndex)});
    wrapper.editorName = marker;
    return Save(path, package, error);
}

bool EditorProject::LoadPortableProxy(const std::filesystem::path &path, BrxDocument &destination,
                                      const std::int32_t parentObjectIndex, std::int32_t &importedProxy,
                                      std::string &error)
{
    importedProxy = -1;
    BrxDocument package;
    if (!Load(path, package, error))
        return false;
    constexpr const char *marker = "__CanePortableProxyPackageRoot__";
    const auto owner = package.FindObjectByEditorName(marker);
    if (owner < 0)
    {
        error = "This editor snapshot is not a portable PROXY package.";
        return false;
    }
    auto &wrapper = package.EditableObjects()[static_cast<std::size_t>(owner)];
    std::int32_t rootIndex = 0;
    std::string savedName;
    for (const auto &detail : wrapper.details)
    {
        if (detail.name == "Portable PROXY editor name")
            savedName = detail.value;
        else if (detail.name == "Portable PROXY root index")
            try
            {
                rootIndex = std::stoi(detail.value);
            }
            catch (...)
            {
                rootIndex = 0;
            }
    }
    wrapper.editorName = savedName;
    wrapper.details.erase(
        std::remove_if(
            wrapper.details.begin(), wrapper.details.end(), [](const BrxObjectDetail &detail)
            { return detail.name == "Portable PROXY editor name" || detail.name == "Portable PROXY root index"; }),
        wrapper.details.end());

    std::int32_t proxyIndex = -1;
    for (std::size_t i = 0; i < package.Proxies().size(); ++i)
        if (package.Proxies()[i].ownerObjectIndex == owner)
        {
            proxyIndex = static_cast<std::int32_t>(i);
            break;
        }
    if (proxyIndex < 0 || rootIndex < 0 ||
        static_cast<std::size_t>(rootIndex) >= package.Proxies()[static_cast<std::size_t>(proxyIndex)].roots.size())
    {
        error = "The portable package has no valid PROXY definition.";
        return false;
    }
    const auto sourceRoot =
        package.Proxies()[static_cast<std::size_t>(proxyIndex)].roots[static_cast<std::size_t>(rootIndex)];
    if (sourceRoot.kind != BrxProxyRootKind::InlineSource || sourceRoot.objectCount == 0 ||
        sourceRoot.objectBegin >= package.Objects().size() ||
        static_cast<std::size_t>(sourceRoot.objectBegin) + sourceRoot.objectCount > package.Objects().size())
    {
        error = "The portable package does not contain a complete inline PROXY source.";
        return false;
    }

    // The effective materialized records are the portable definition.  Remove only
    // their outer source identity; the destination creates a fresh source wrapper.
    for (std::uint32_t offset = 0; offset < sourceRoot.objectCount; ++offset)
    {
        auto &object = package.EditableObjects()[sourceRoot.objectBegin + offset];
        if (object.proxyRecordIndex != proxyIndex || object.proxyRootIndex != rootIndex)
            continue;
        object.proxyRecordIndex = object.proxyRootIndex = object.proxySourceObjectIndex = -1;
        object.proxyRuntimeParentIndex = -1;
        object.proxyClone = false;
    }
    const auto sourceObject = sourceRoot.instantiatedRootObjectIndex;
    // Keep the exact old->new order produced by ImportHierarchy. This lets us
    // recover the runtime-only attached-child boundary after the package gets
    // fresh destination OIDs and object indices.
    std::vector<std::int32_t> sourceHierarchy{sourceObject};
    for (std::size_t cursor = 0; cursor < sourceHierarchy.size(); ++cursor)
        for (std::size_t index = 0; index < package.Objects().size(); ++index)
            if (package.Objects()[index].parentIndex == sourceHierarchy[cursor])
                sourceHierarchy.push_back(static_cast<std::int32_t>(index));
    std::sort(sourceHierarchy.begin(), sourceHierarchy.end());
    const auto attachedSourceObjects =
        package.Proxies()[static_cast<std::size_t>(proxyIndex)].attachedChildObjectIndices;
    BrxDocument before = destination;
    auto imported =
        EditorDocumentOperations::ImportHierarchy(destination, package, sourceObject, parentObjectIndex, true);
    if (!imported.succeeded)
    {
        destination = std::move(before);
        error = imported.message;
        return false;
    }
    if (imported.affectedObjectIndices.size() != sourceHierarchy.size())
    {
        destination = std::move(before);
        error = "Portable PROXY hierarchy remap is incomplete.";
        return false;
    }
    std::unordered_map<std::int32_t, std::int32_t> importedIndexRemap;
    for (std::size_t index = 0; index < sourceHierarchy.size(); ++index)
        importedIndexRemap.emplace(sourceHierarchy[index], imported.affectedObjectIndices[index]);
    std::vector<std::int32_t> importedAttachedObjects;
    for (const auto sourceAttached : attachedSourceObjects)
    {
        const auto mapped = importedIndexRemap.find(sourceAttached);
        if (mapped == importedIndexRemap.end())
        {
            destination = std::move(before);
            error = "Portable PROXY attached-child hierarchy is incomplete.";
            return false;
        }
        importedAttachedObjects.push_back(mapped->second);
        // Exclude this instance-local hierarchy from the reusable source
        // while CreateProxySourceFromHierarchy measures source membership.
        destination.EditableObjects()[static_cast<std::size_t>(mapped->second)].parentIndex = -1;
    }
    // Source-level visibility bits have no meaning in another level's VISMAP.
    // Zero disables both ProjectCane culling tests until the user assigns the
    // imported hierarchy to destination zones with the VISMAP workflow.
    const std::unordered_set<std::int32_t> importedObjects(imported.affectedObjectIndices.begin(),
                                                           imported.affectedObjectIndices.end());
    for (const auto objectIndex : importedObjects)
        if (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < destination.EditableObjects().size())
            destination.EditableObjects()[static_cast<std::size_t>(objectIndex)].zoneMask = 0u;
    for (auto &glob : destination.EditableGlobs())
        if (importedObjects.contains(glob.ownerObjectIndex))
        {
            glob.zoneMask = 0u;
            glob.hasZoneMask = false;
            glob.propertyMask &= ~std::uint16_t{0x002u};
        }
    auto created = EditorDocumentOperations::CreateProxySourceFromHierarchy(destination, imported.primaryObjectIndex);
    if (!created.succeeded)
    {
        destination = std::move(before);
        error = created.message;
        return false;
    }
    if (created.primaryObjectIndex >= 0 &&
        static_cast<std::size_t>(created.primaryObjectIndex) < destination.Objects().size())
    {
        const auto createdProxy =
            destination.Objects()[static_cast<std::size_t>(created.primaryObjectIndex)].proxyRecordIndex;
        if (createdProxy >= 0 && static_cast<std::size_t>(createdProxy) < destination.EditableProxies().size() &&
            !destination.EditableProxies()[static_cast<std::size_t>(createdProxy)].roots.empty())
        {
            auto &proxy = destination.EditableProxies()[static_cast<std::size_t>(createdProxy)];
            proxy.roots[0].cloneCapacity = (std::max<std::uint16_t>)(1, sourceRoot.cloneCapacity);
            const auto instanceRoot = proxy.roots[0].instantiatedRootObjectIndex;
            for (auto attached : importedAttachedObjects)
            {
                // Creating the wrapper inserts one object immediately before
                // the imported source root and remaps every later index.
                if (attached >= imported.primaryObjectIndex)
                    ++attached;
                if (attached < 0 || static_cast<std::size_t>(attached) >= destination.Objects().size())
                    continue;
                destination.EditableObjects()[static_cast<std::size_t>(attached)].parentIndex = instanceRoot;
                proxy.attachedChildObjectIndices.push_back(attached);
            }
            if (instanceRoot >= 0 && static_cast<std::size_t>(instanceRoot) < destination.Objects().size())
            {
                auto &proxyObject = destination.EditableObjects()[static_cast<std::size_t>(created.primaryObjectIndex)];
                auto &rootObject = destination.EditableObjects()[static_cast<std::size_t>(instanceRoot)];
                const glm::mat4 rootWorld = glm::make_mat4(rootObject.worldTransform);
                const glm::mat4 parentWorld =
                    proxyObject.parentIndex >= 0
                        ? glm::make_mat4(
                              destination.Objects()[static_cast<std::size_t>(proxyObject.parentIndex)].worldTransform)
                        : glm::mat4(1.0f);
                const glm::mat4 wrapperLocal = glm::inverse(parentWorld) * rootWorld;
                std::copy(glm::value_ptr(rootWorld), glm::value_ptr(rootWorld) + 16, proxyObject.worldTransform);
                std::copy(glm::value_ptr(wrapperLocal), glm::value_ptr(wrapperLocal) + 16, proxyObject.localTransform);
                const glm::mat4 identity(1.0f);
                std::copy(glm::value_ptr(identity), glm::value_ptr(identity) + 16, rootObject.localTransform);
                std::copy(proxyObject.localTransform, proxyObject.localTransform + 16, proxy.localTransform);
            }
        }
    }
    importedProxy = created.primaryObjectIndex;
    return true;
}
