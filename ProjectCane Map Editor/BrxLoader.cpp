#include "BrxDocument.h"
#include "BrxLoader.h"
#include "EditorOptions.h"
#include "ProxyRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr std::uint32_t kPcbrMagic = 0x52424350u;
constexpr std::uint32_t kUnifiedMagic = 0x494e5550u;

class Reader
{
  public:
    explicit Reader(const std::vector<std::uint8_t> &bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t Remaining() const
    {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] std::size_t Offset() const
    {
        return offset_;
    }

    std::uint32_t U32()
    {
        Require(sizeof(std::uint32_t));
        std::uint32_t value{};
        std::memcpy(&value, bytes_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    std::uint8_t U8()
    {
        Require(sizeof(std::uint8_t));
        return bytes_[offset_++];
    }

    std::uint16_t U16()
    {
        Require(sizeof(std::uint16_t));
        std::uint16_t value{};
        std::memcpy(&value, bytes_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    std::int16_t S16()
    {
        return static_cast<std::int16_t>(U16());
    }
    std::int32_t S32()
    {
        return static_cast<std::int32_t>(U32());
    }

    float F32()
    {
        Require(sizeof(float));
        float value{};
        std::memcpy(&value, bytes_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    std::uint64_t U64()
    {
        Require(sizeof(std::uint64_t));
        std::uint64_t value{};
        std::memcpy(&value, bytes_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    void Skip(std::uint64_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("PCBRX block is too large");
        Require(static_cast<std::size_t>(count));
        offset_ += static_cast<std::size_t>(count);
    }

    void Seek(std::size_t offset)
    {
        if (offset > bytes_.size())
            throw std::runtime_error("PCBRX seek is outside the payload");
        offset_ = offset;
    }

    void Align(std::size_t alignment)
    {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            throw std::runtime_error("Invalid PCBRX alignment");
        Seek((offset_ + alignment - 1) & ~(alignment - 1));
    }

    std::vector<std::uint8_t> Bytes(std::size_t count)
    {
        Require(count);
        std::vector<std::uint8_t> result(count);
        std::memcpy(result.data(), bytes_.data() + offset_, count);
        offset_ += count;
        return result;
    }

    std::string FixedString(std::size_t count)
    {
        Require(count);
        const auto *begin = reinterpret_cast<const char *>(bytes_.data() + offset_);
        std::size_t length = 0;
        while (length < count && begin[length] != '\0')
            ++length;
        std::string result(begin, length);
        offset_ += count;
        return result;
    }

    std::string StringU16()
    {
        const auto count = U16();
        Require(count);
        const auto *begin = reinterpret_cast<const char *>(bytes_.data() + offset_);
        std::string result(begin, count);
        offset_ += count;
        return result;
    }

  private:
    void Require(std::size_t count) const
    {
        if (count > bytes_.size() - offset_)
            throw std::runtime_error("PCBRX file is truncated");
    }

    const std::vector<std::uint8_t> &bytes_;
    std::size_t offset_ = 0;
};

std::uint32_t Crc32(const std::vector<std::uint8_t> &bytes)
{
    std::uint32_t crc = 0xffffffffu;
    for (const auto value : bytes)
    {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
        {
            const auto mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool IsReasonableAssetName(const std::string &value)
{
    return std::all_of(value.begin(), value.end(),
                       [](const unsigned char character) { return character >= 0x20 && character < 0x7f; });
}

enum class OptionKind : std::uint8_t
{
    Bool,
    Float,
    Vector3,
    Vector4,
    Int32,
    Float2,
    UInt32,
    Int16,
    TypedInt16
};

BrxPropertyKind ToPropertyKind(const OptionKind kind)
{
    switch (kind)
    {
    case OptionKind::Bool:
        return BrxPropertyKind::Bool;
    case OptionKind::Float:
        return BrxPropertyKind::Float;
    case OptionKind::Vector3:
        return BrxPropertyKind::Vector3;
    case OptionKind::Vector4:
        return BrxPropertyKind::Vector4;
    case OptionKind::Int32:
        return BrxPropertyKind::Int32;
    case OptionKind::Float2:
        return BrxPropertyKind::Float2;
    case OptionKind::UInt32:
        return BrxPropertyKind::UInt32;
    case OptionKind::Int16:
        return BrxPropertyKind::Int16;
    case OptionKind::TypedInt16:
        return BrxPropertyKind::TypedInt16;
    }
    return BrxPropertyKind::Dynamic;
}

struct OptionSchema
{
    OptionKind kind = OptionKind::Int16;
    std::uint32_t flags = 0;
    std::uint16_t argumentCount = 0;
    bool present = false;
};

const std::array<OptionSchema, 1179> &GetOptionSchema()
{
    static const auto schema = []
    {
        std::array<OptionSchema, 1179> result{};
#define BRX_OPTION(id, kind, flags, argc) result.at(id) = OptionSchema{kind, static_cast<std::uint32_t>(flags), static_cast<std::uint16_t>(argc), true};
#include "OptionSchema.generated.inc"
#undef BRX_OPTION
        // These retail packed-color fields are expanded to float4 values by
        // the PCBRX rebuilder before this editor sees the payload.
        for (const auto id : {95, 96, 528, 535, 988, 1133, 1134, 1135})
            result[id].kind = OptionKind::Vector4;
        return result;
    }();
    return schema;
}

void SkipOptionValue(Reader &reader, OptionKind kind)
{
    switch (kind)
    {
    case OptionKind::Bool:
        reader.Skip(1);
        break;
    case OptionKind::Float:
        reader.Skip(4);
        break;
    case OptionKind::Vector3:
        reader.Skip(12);
        break;
    case OptionKind::Vector4:
        reader.Skip(16);
        break;
    case OptionKind::Int32:
        reader.Skip(4);
        break;
    case OptionKind::Float2:
        reader.Skip(8);
        break;
    case OptionKind::UInt32:
        reader.Skip(4);
        break;
    case OptionKind::Int16:
        reader.Skip(2);
        break;
    case OptionKind::TypedInt16:
        reader.Skip(4);
        break;
    }
}

void SkipDynamicOptionValue(Reader &reader, std::int16_t type)
{
    switch (type)
    {
    case 0:
        reader.Skip(1);
        return;
    case 1:
        reader.Skip(4);
        return;
    case 2:
        reader.Skip(12);
        return;
    case 3:
        reader.Skip(16);
        return;
    case 4:
        reader.Skip(12);
        return;
    case 5:
        reader.Skip(4);
        return;
    case 6:
    case 7:
    case 14:
    case 15:
    case 16:
    case 17:
    case 20:
        reader.Skip(2);
        return;
    case 9:
    case 11:
        reader.Skip(12);
        return;
    case 10:
        reader.Skip(8);
        return;
    case 12:
        reader.Skip(16);
        return;
    case 13:
        reader.Skip(4);
        return;
    default:
        if ((static_cast<std::uint32_t>(type) & 0x7fffffffu) >> 12 == 1)
        {
            reader.Skip(2);
            return;
        }
        throw std::runtime_error("The world options contain an unsupported dynamic type " + std::to_string(type));
    }
}

BrxPropertyRecord::DynamicArgument ReadDynamicOptionValue(Reader &reader, const std::int16_t type)
{
    BrxPropertyRecord::DynamicArgument value;
    value.type = type;
    switch (type)
    {
    case 0:
        value.intValue = reader.U8();
        break;
    case 1:
        value.floatValue[0] = reader.F32();
        break;
    case 2:
    case 4:
    case 9:
    case 11:
        for (int i = 0; i < 3; ++i)
            value.floatValue[i] = reader.F32();
        break;
    case 3:
    case 12:
        for (float &component : value.floatValue)
            component = reader.F32();
        break;
    case 5:
    case 13:
        value.intValue = static_cast<std::int32_t>(reader.U32());
        break;
    case 10:
        value.floatValue[0] = reader.F32();
        value.floatValue[1] = reader.F32();
        break;
    case 6:
    case 7:
    case 14:
    case 15:
    case 16:
    case 17:
    case 20:
        value.intValue = reader.S16();
        break;
    default:
        if ((static_cast<std::uint32_t>(type) & 0x7fffffffu) >> 12 == 1)
            value.intValue = reader.S16();
        else
            throw std::runtime_error("The world options contain an unsupported dynamic type " + std::to_string(type));
        break;
    }
    return value;
}

BrxPropertyRecord ReadProperty(Reader &reader, const std::int16_t id, const OptionSchema &option)
{
    BrxPropertyRecord property;
    property.eopid = id;
    property.kind = ToPropertyKind(option.kind);
    property.flags = option.flags;
    property.argumentCount = option.argumentCount;
    property.offset = reader.Offset();

    if ((option.flags & 0x400u) != 0)
    {
        property.kind = BrxPropertyKind::Dynamic;
        property.editable = false;
        for (std::uint16_t argument = 0; argument < option.argumentCount; ++argument)
            property.arguments.push_back(ReadDynamicOptionValue(reader, reader.S16()));
    }
    else
    {
        switch (option.kind)
        {
        case OptionKind::Bool:
            property.boolValue = reader.U8() != 0;
            break;
        case OptionKind::Float:
            property.floatValue[0] = reader.F32();
            break;
        case OptionKind::Vector3:
            for (int component = 0; component < 3; ++component)
                property.floatValue[component] = reader.F32();
            break;
        case OptionKind::Vector4:
            for (float &component : property.floatValue)
                component = reader.F32();
            break;
        case OptionKind::Int32:
            property.intValue = static_cast<std::int32_t>(reader.U32());
            break;
        case OptionKind::Float2:
            property.floatValue[0] = reader.F32();
            property.floatValue[1] = reader.F32();
            break;
        case OptionKind::UInt32:
            property.uintValue = reader.U32();
            break;
        case OptionKind::Int16:
            property.intValue = reader.S16();
            break;
        case OptionKind::TypedInt16:
            property.typeValue = reader.S16();
            property.intValue = reader.S16();
            break;
        }
    }
    property.serializedSize = static_cast<std::uint32_t>(reader.Offset() - property.offset);
    return property;
}

thread_local std::vector<BrxPropertyRecord> *g_objectProperties = nullptr;
thread_local std::int16_t g_propertyCid = -1;

class PropertyCaptureScope
{
  public:
    PropertyCaptureScope(std::vector<BrxObjectRecord> &objects, const std::size_t objectIndex)
        : objects_(objects), objectIndex_(objectIndex), previous_(g_objectProperties), previousCid_(g_propertyCid)
    {
        g_objectProperties = &captured_;
        g_propertyCid = objects_[objectIndex_].cid;
    }
    ~PropertyCaptureScope()
    {
        auto &destination = objects_[objectIndex_].properties;
        destination.insert(destination.end(), std::make_move_iterator(captured_.begin()),
                           std::make_move_iterator(captured_.end()));
        g_objectProperties = previous_;
        g_propertyCid = previousCid_;
    }

  private:
    std::vector<BrxObjectRecord> &objects_;
    std::size_t objectIndex_ = 0;
    std::vector<BrxPropertyRecord> captured_;
    std::vector<BrxPropertyRecord> *previous_ = nullptr;
    std::int16_t previousCid_ = -1;
};

class PropertyCapturePause
{
  public:
    PropertyCapturePause() : previous_(g_objectProperties)
    {
        g_objectProperties = nullptr;
    }
    ~PropertyCapturePause()
    {
        g_objectProperties = previous_;
    }

  private:
    std::vector<BrxPropertyRecord> *previous_ = nullptr;
};

void SkipOptionList(Reader &reader)
{
    const auto &schema = GetOptionSchema();
    while (true)
    {
        const auto id = reader.S16();
        if (id == -1)
            return;
        if (id < 0 || static_cast<std::size_t>(id) >= schema.size() || !schema[id].present)
            throw std::runtime_error("Unknown object EOPID " + std::to_string(id));
        const OptionSchema option = schema[id];
        auto property = ReadProperty(reader, id, option);
        if (g_objectProperties != nullptr)
            g_objectProperties->push_back(std::move(property));
    }
}

std::int16_t SkipOptionListCaptureInt16(Reader &reader, std::int16_t captureId, std::int16_t defaultValue,
                                        bool *hidden = nullptr)
{
    const auto &schema = GetOptionSchema();
    auto captured = defaultValue;
    while (true)
    {
        const auto id = reader.S16();
        if (id == -1)
            return captured;
        if (id < 0 || static_cast<std::size_t>(id) >= schema.size() || !schema[id].present)
            throw std::runtime_error("Unknown object EOPID " + std::to_string(id));
        const OptionSchema option = schema[id];
        auto property = ReadProperty(reader, id, option);
        if (g_objectProperties != nullptr)
            g_objectProperties->push_back(property);
        if (id == 125 && option.kind == OptionKind::Bool)
        {
            const bool value = property.boolValue;
            if (hidden != nullptr)
                *hidden = value;
        }
        else if (id == captureId && option.kind == OptionKind::Int16)
        {
            captured = static_cast<std::int16_t>(property.intValue);
        }
    }
}

BrxShaderAnimationRecord ReadShaderAnimation(Reader &reader)
{
    BrxShaderAnimationRecord animation;
    const auto rawKind = reader.S16();
    if (rawKind == -1 || rawKind == 0)
        return animation;
    if (rawKind < 1 || rawKind > 8)
        throw std::runtime_error("Invalid object shader-animation kind " + std::to_string(rawKind));
    animation.kind = static_cast<BrxShaderAnimationKind>(rawKind);
    animation.shaderOid = reader.S16();
    animation.instanced = reader.U16() != 0;
    for (float &value : animation.values)
        value = reader.F32();
    // These aliases mirror the actual SAAF union layouts used by ProjectCane.
    std::uint32_t word4{};
    std::memcpy(&word4, &animation.values[4], sizeof(word4));
    const auto rawWord4 = static_cast<std::uint16_t>(word4 & 0xffffu);
    animation.initialFrame = animation.kind == BrxShaderAnimationKind::PingPong
                                 ? static_cast<std::int32_t>(rawWord4)
                                 : static_cast<std::int32_t>(static_cast<std::int16_t>(rawWord4));
    animation.otherShaderOid = static_cast<std::int16_t>(rawWord4);
    if (animation.kind == BrxShaderAnimationKind::Hologram)
        std::memcpy(&animation.symmetryCount, &animation.values[1], sizeof(animation.symmetryCount));
    return animation;
}

enum class LoaderKind : std::uint8_t
{
    Unknown,
    Lo,
    Alo,
    So,
    Custom
};
struct CidLoaderInfo
{
    LoaderKind kind = LoaderKind::Unknown;
    const char *functionName = "Unknown";
};

const std::array<CidLoaderInfo, 148> &GetCidLoaders()
{
    static const auto loaders = []
    {
        std::array<CidLoaderInfo, 148> result{};
#define CID_LOADER(id, kind, functionName) result.at(id) = CidLoaderInfo{kind, functionName};
#include "CidLoaders.generated.inc"
#undef CID_LOADER
        return result;
    }();
    return loaders;
}
}

void BrxLoader::ParseWorld(BrxDocument &document)
{
    auto &payload_ = document.payload_;
    auto &worldHeader_ = document.worldHeader_;
    auto &sections_ = document.sections_;
    auto &worldOptions_ = document.worldOptions_;
    auto &spliceNodes_ = document.spliceNodes_;
    auto &spliceModules_ = document.spliceModules_;
    auto &bitmaps_ = document.bitmaps_;
    auto &shaders_ = document.shaders_;
    auto &fonts_ = document.fonts_;
    auto &objects_ = document.objects_;
    auto &renderVertices_ = document.renderVertices_;
    auto &renderIndices_ = document.renderIndices_;
    auto &renderBatches_ = document.renderBatches_;
    auto &celEdges_ = document.celEdges_;
    auto &celBatches_ = document.celBatches_;
    auto &globs_ = document.globs_;
    auto &globsets_ = document.globsets_;
    auto &collisionTriangles_ = document.collisionTriangles_;
    auto &lights_ = document.lights_;
    auto &cameras_ = document.cameras_;
    auto &checkpoints_ = document.checkpoints_;
    auto &warps_ = document.warps_;
    auto &globWarps_ = document.globWarps_;
    auto &laserBeams_ = document.laserBeams_;
    auto &shapePaths_ = document.shapePaths_;
    auto &pathZones_ = document.pathZones_;
    auto &effects_ = document.effects_;
    auto &volumes_ = document.volumes_;
    auto &exits_ = document.exits_;
    auto &scans_ = document.scans_;
    auto &proxies_ = document.proxies_;
    auto &visibilityNodes_ = document.visibilityNodes_;
    auto &visibilityOneHopMasks_ = document.visibilityOneHopMasks_;
    auto &animationSegments_ = document.animationSegments_;
    auto &stateMachines_ = document.stateMachines_;
    auto &reachMaps_ = document.reachMaps_;
    auto &keyholes_ = document.keyholes_;

    renderVertices_.clear();
    renderIndices_.clear();
    renderBatches_.clear();
    celEdges_.clear();
    celBatches_.clear();
    globs_.clear();
    globsets_.clear();
    collisionTriangles_.clear();
    lights_.clear();
    cameras_.clear();
    checkpoints_.clear();
    warps_.clear();
    globWarps_.clear();
    laserBeams_.clear();
    shapePaths_.clear();
    pathZones_.clear();
    effects_.clear();
    volumes_.clear();
    exits_.clear();
    scans_.clear();
    animationSegments_.clear();
    stateMachines_.clear();
    reachMaps_.clear();
    keyholes_.clear();
    proxies_.clear();
    visibilityNodes_.clear();
    visibilityOneHopMasks_.clear();
    bool haveRenderBounds = false;
    const auto includeRenderPosition = [&](const BrxRenderVertex &vertex)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!haveRenderBounds)
            {
                document.renderBoundsMin_[axis] = vertex.position[axis];
                document.renderBoundsMax_[axis] = vertex.position[axis];
            }
            else
            {
                document.renderBoundsMin_[axis] = std::min(document.renderBoundsMin_[axis], vertex.position[axis]);
                document.renderBoundsMax_[axis] = std::max(document.renderBoundsMax_[axis], vertex.position[axis]);
            }
        }
        haveRenderBounds = true;
    };
    const auto readMatrix4 = [](Reader &stream)
    {
        glm::mat4 matrix(1.0f);
        for (int column = 0; column < 4; ++column)
        {
            matrix[column][0] = stream.F32();
            matrix[column][1] = stream.F32();
            matrix[column][2] = stream.F32();
            matrix[column][3] = column == 3 ? 1.0f : 0.0f;
        }
        return matrix;
    };
    std::uint32_t nextRenderGroup = 0;
    std::int32_t currentObjectIndex = -1;
    const auto appendTransformedGeometry = [&](std::size_t vertexBegin, std::size_t vertexCount, std::size_t indexBegin,
                                               std::size_t indexCount, std::size_t batchBegin, std::size_t batchCount,
                                               const glm::mat4 &transform)
    {
        const auto newBase = static_cast<std::uint32_t>(renderVertices_.size());
        renderVertices_.reserve(renderVertices_.size() + vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            auto vertex = renderVertices_[vertexBegin + i];
            const glm::vec4 position =
                transform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f);
            glm::vec3 normal = glm::mat3(transform) * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            if (glm::dot(normal, normal) > 0.000001f)
                normal = glm::normalize(normal);
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
            includeRenderPosition(vertex);
            renderVertices_.push_back(vertex);
        }
        renderIndices_.reserve(renderIndices_.size() + indexCount);
        for (std::size_t i = 0; i < indexCount; ++i)
        {
            const auto sourceIndex = renderIndices_[indexBegin + i];
            if (sourceIndex < vertexBegin || sourceIndex >= vertexBegin + vertexCount)
                throw std::runtime_error("Instanced geometry references a vertex outside its source range");
            renderIndices_.push_back(newBase + static_cast<std::uint32_t>(sourceIndex - vertexBegin));
        }
        const auto newIndexBegin = renderIndices_.size() - indexCount;
        renderBatches_.reserve(renderBatches_.size() + batchCount);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> groupRemap;
        for (std::size_t i = 0; i < batchCount; ++i)
        {
            auto batch = renderBatches_[batchBegin + i];
            const auto mapping = std::find_if(groupRemap.begin(), groupRemap.end(),
                                              [&](const auto &pair) { return pair.first == batch.renderGroup; });
            if (mapping == groupRemap.end())
            {
                groupRemap.emplace_back(batch.renderGroup, nextRenderGroup++);
                batch.renderGroup = groupRemap.back().second;
            }
            else
            {
                batch.renderGroup = mapping->second;
            }
            batch.firstIndex = static_cast<std::uint32_t>(newIndexBegin + (batch.firstIndex - indexBegin));
            const glm::vec4 transformedCenter =
                transform * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f);
            batch.boundsCenter[0] = transformedCenter.x;
            batch.boundsCenter[1] = transformedCenter.y;
            batch.boundsCenter[2] = transformedCenter.z;
            const glm::vec4 transformedSortCenter =
                transform * glm::vec4(batch.sortCenter[0], batch.sortCenter[1], batch.sortCenter[2], 1.0f);
            batch.sortCenter[0] = transformedSortCenter.x;
            batch.sortCenter[1] = transformedSortCenter.y;
            batch.sortCenter[2] = transformedSortCenter.z;
            glm::mat4 sourceRtckBase(1.0f);
            std::memcpy(glm::value_ptr(sourceRtckBase), batch.rtckBaseMatrix, sizeof(batch.rtckBaseMatrix));
            const glm::mat4 transformedRtckBase = transform * sourceRtckBase;
            std::memcpy(batch.rtckBaseMatrix, glm::value_ptr(transformedRtckBase), sizeof(batch.rtckBaseMatrix));
            const float scaleX = glm::length(glm::vec3(transform[0]));
            const float scaleY = glm::length(glm::vec3(transform[1]));
            const float scaleZ = glm::length(glm::vec3(transform[2]));
            const float radiusScale = std::max(scaleX, std::max(scaleY, scaleZ));
            batch.boundsRadius *= radiusScale;
            // GLOB::sRadius participates in FInsideCmMrd independently of the
            // SUBGLOB/frustum radius.  A transformed PROXY copy must scale both
            // or its surface fade range no longer matches its cel packet.
            batch.mrdRadius *= radiusScale;
            if (batch.hasGleam)
            {
                glm::vec3 normal =
                    glm::mat3(transform) * glm::vec3(batch.gleamNormal[0], batch.gleamNormal[1], batch.gleamNormal[2]);
                if (glm::dot(normal, normal) > 0.000001f)
                    normal = glm::normalize(normal);
                batch.gleamNormal[0] = normal.x;
                batch.gleamNormal[1] = normal.y;
                batch.gleamNormal[2] = normal.z;
            }
            renderBatches_.push_back(batch);
        }
    };

    Reader reader(payload_);
    worldHeader_.magic = reader.U32();
    worldHeader_.version = reader.U32();
    if (worldHeader_.magic != kPcbrMagic || worldHeader_.version != 17)
        throw std::runtime_error("The world payload is not PCBRX v17");

    sections_.push_back({"PCBRX header", 0, 8, BrxSectionState::Parsed});
    const auto soundBankOffset = reader.Offset();
    worldHeader_.soundBankFile = reader.FixedString(32);
    if (!IsReasonableAssetName(worldHeader_.soundBankFile))
        throw std::runtime_error("The world payload has an invalid sound-bank filename");
    sections_.push_back({"Sound bank", soundBankOffset, 32, BrxSectionState::Parsed});

    worldHeader_.spliceMapOffset = reader.Offset();
    worldHeader_.spliceCount = reader.U32();
    worldHeader_.symbolCount = reader.U32();
    worldHeader_.extraOptionCount = reader.U32();
    if (worldHeader_.spliceCount > 1'000'000 || worldHeader_.symbolCount > 1'000'000 ||
        worldHeader_.extraOptionCount > 1'000'000)
        throw std::runtime_error("The world payload has invalid splice-map counts");

    // PpairSerializeIn uses a compact recursive list. The low seven bits are
    // TAGK, bit seven indicates another pair, and 0xFF is a null pointer.
    // Retail PCBRX payloads omit the five debug-info words.
    constexpr std::uint8_t kTagS32 = 20;
    constexpr std::uint8_t kTagF32 = 21;
    constexpr std::uint8_t kTagVector = 22;
    constexpr std::uint8_t kTagMatrix = 23;
    constexpr std::uint8_t kTagClq = 24;
    constexpr std::uint8_t kTagLm = 25;
    constexpr std::uint8_t kTagSmp = 26;
    constexpr std::uint8_t kTagBool = 27;
    constexpr std::uint8_t kTagSymid = 28;
    constexpr std::uint8_t kTagBifk = 29;
    constexpr std::uint8_t kTagPair = 30;
    constexpr std::uint8_t kTagBasic = 32;
    constexpr std::uint8_t kTagMax = 36;
    constexpr std::uint32_t kMaximumExpressionNodes = 10'000'000;
    constexpr std::uint32_t kMaximumExpressionDepth = 4096;

    spliceNodes_.clear();
    spliceModules_.clear();
    spliceModules_.reserve(worldHeader_.spliceCount);
    const auto parsePair = [&](auto &&self, std::uint32_t depth) -> std::int32_t
    {
        if (depth > kMaximumExpressionDepth)
            throw std::runtime_error("The splice expression nesting is invalid");
        const std::uint8_t tagByte = reader.U8();
        if (tagByte == 0xff)
            return -1;

        const std::uint8_t tag = tagByte & 0x7f;
        const bool hasNext = (tagByte & 0x80) != 0;
        if (tag >= kTagMax)
            throw std::runtime_error("The splice map contains an invalid expression tag");
        if (++worldHeader_.expressionNodeCount > kMaximumExpressionNodes)
            throw std::runtime_error("The splice map contains too many expression nodes");

        BrxSpliceNode node;
        node.tag = tag;
        node.offset = reader.Offset() - 1;
        const std::int32_t nodeIndex = static_cast<std::int32_t>(spliceNodes_.size());
        spliceNodes_.push_back(node);

        switch (tag)
        {
        case kTagS32:
            spliceNodes_[nodeIndex].intValue = reader.S32();
            break;
        case kTagF32:
            spliceNodes_[nodeIndex].floatValue[0] = reader.F32();
            break;
        case kTagBool:
        case kTagSymid:
        case kTagBifk:
        case kTagBasic:
            spliceNodes_[nodeIndex].intValue = static_cast<std::int32_t>(reader.U32());
            break;
        case kTagVector:
        case kTagClq:
        case kTagSmp:
            for (std::size_t i = 0; i < 3; ++i)
                spliceNodes_[nodeIndex].floatValue[i] = reader.F32();
            break;
        case kTagMatrix:
            for (float &value : spliceNodes_[nodeIndex].floatValue)
                value = reader.F32();
            break;
        case kTagLm:
            spliceNodes_[nodeIndex].floatValue[0] = reader.F32();
            spliceNodes_[nodeIndex].floatValue[1] = reader.F32();
            break;
        case kTagPair:
            spliceNodes_[nodeIndex].childNode = self(self, depth + 1);
            break;
        default:
            break;
        }
        if (hasNext)
            spliceNodes_[nodeIndex].nextNode = self(self, depth + 1);
        spliceNodes_[nodeIndex].serializedSize =
            static_cast<std::uint32_t>(reader.Offset() - spliceNodes_[nodeIndex].offset);
        return nodeIndex;
    };

    for (std::uint32_t splice = 0; splice < worldHeader_.spliceCount; ++splice)
    {
        BrxSpliceModule module;
        module.offset = reader.Offset();
        module.rootNode = parsePair(parsePair, 0);
        module.serializedSize = static_cast<std::uint32_t>(reader.Offset() - module.offset);
        spliceModules_.push_back(module);
    }

    worldHeader_.worldOptionsOffset = reader.Offset();
    sections_.push_back({"Splice map", worldHeader_.spliceMapOffset,
                         worldHeader_.worldOptionsOffset - worldHeader_.spliceMapOffset, BrxSectionState::Parsed});
    const auto &optionSchema = GetOptionSchema();
    while (true)
    {
        const auto optionOffset = reader.Offset();
        const auto optionId = reader.S16();
        if (optionId == -1)
            break;
        if (optionId < 0 || static_cast<std::size_t>(optionId) >= optionSchema.size() ||
            !optionSchema[optionId].present)
            throw std::runtime_error("Unknown world EOPID " + std::to_string(optionId) + " at payload offset " +
                                     std::to_string(optionOffset));

        const auto &option = optionSchema[optionId];
        BrxPropertyRecord property;
        // The generated legacy schema describes these as packed words, while
        // LoadOptionsFromBrx actually serializes four floats for both colors.
        if (optionId == 528 || optionId == 535)
        {
            property.eopid = optionId;
            property.kind = BrxPropertyKind::Vector4;
            property.flags = option.flags;
            property.argumentCount = option.argumentCount;
            property.offset = reader.Offset();
            for (float &component : property.floatValue)
                component = reader.F32();
            property.serializedSize = static_cast<std::uint32_t>(reader.Offset() - property.offset);
        }
        else
            property = ReadProperty(reader, optionId, option);
        property.offset = optionOffset;
        property.serializedSize = static_cast<std::uint32_t>(reader.Offset() - optionOffset);
        if (optionId == 526)
            worldHeader_.worldMidtone = property.floatValue[0];
        else if (optionId == 527)
            worldHeader_.worldShadow = property.floatValue[0];
        else if (optionId == 528)
            std::copy_n(property.floatValue, 4, worldHeader_.skyColor);
        else if (optionId == 529)
            worldHeader_.cameraFov = property.floatValue[0];
        else if (optionId == 530)
            worldHeader_.cameraNearClip = property.floatValue[0];
        else if (optionId == 531)
            worldHeader_.cameraFarClip = property.floatValue[0];
        else if (optionId == 532)
            worldHeader_.fogNear = property.floatValue[0];
        else if (optionId == 533)
            worldHeader_.fogFar = property.floatValue[0];
        else if (optionId == 534)
            worldHeader_.fogMaximum = property.floatValue[0];
        else if (optionId == 535)
        {
            std::copy_n(property.floatValue, 4, worldHeader_.fogColor);
            // SetSwCameraRgbaFogFloat stores RGB unchanged but doubles alpha;
            // the shader clamps the resulting fog intensity afterward.
            worldHeader_.fogColor[3] *= 2.0f;
        }
        else if (optionId == 536)
            worldHeader_.cameraMrdRatio = property.floatValue[0];
        worldOptions_.push_back(std::move(property));
    }

    worldHeader_.commentaryOffset = reader.Offset();
    sections_.push_back({"World options", worldHeader_.worldOptionsOffset,
                         worldHeader_.commentaryOffset - worldHeader_.worldOptionsOffset, BrxSectionState::Parsed});

    worldHeader_.commentaryAudioFile = reader.FixedString(32);
    if (!IsReasonableAssetName(worldHeader_.commentaryAudioFile))
        throw std::runtime_error("The world payload has an invalid commentary filename");
    worldHeader_.commentarySpeakers = reader.StringU16();
    const auto musicOffset = reader.Offset();
    sections_.push_back({"Commentary metadata", worldHeader_.commentaryOffset,
                         musicOffset - worldHeader_.commentaryOffset, BrxSectionState::Parsed});

    worldHeader_.musicOffset = musicOffset;
    worldHeader_.musicFile = reader.FixedString(32);
    if (!IsReasonableAssetName(worldHeader_.musicFile))
        throw std::runtime_error("The world payload has an invalid music filename");
    worldHeader_.shadersOffset = reader.Offset();
    sections_.push_back({"Music metadata", worldHeader_.musicOffset,
                         worldHeader_.shadersOffset - worldHeader_.musicOffset, BrxSectionState::Parsed});
    const auto bitmapCount = reader.U32();
    if (bitmapCount > 1'000'000)
        throw std::runtime_error("The world payload has an invalid bitmap count");
    bitmaps_.reserve(bitmapCount);
    for (std::uint32_t bitmapIndex = 0; bitmapIndex < bitmapCount; ++bitmapIndex)
    {
        BrxBitmapRecord bitmap;
        bitmap.width = reader.U32();
        bitmap.height = reader.U32();
        bitmap.zoneFlags = reader.U32();
        bitmap.mipCount = reader.U32();
        bitmap.pixelBytes = reader.U32();
        bitmap.textureDataOffset = reader.U32();
        bitmap.textureFlags = reader.U32();
        bitmap.textureFormat = reader.U32();
        if (bitmap.width == 0 || bitmap.height == 0 || bitmap.width > 32768 || bitmap.height > 32768 ||
            bitmap.mipCount == 0 || bitmap.mipCount > 32 || bitmap.textureFormat > 2)
            throw std::runtime_error("The world payload has an invalid bitmap descriptor");
        bitmaps_.push_back(bitmap);
    }

    const auto shaderCount = reader.U16();
    worldHeader_.declaredShaderAnimationCount = reader.U16();
    shaders_.reserve(shaderCount);
    for (std::uint32_t shaderIndex = 0; shaderIndex < shaderCount; ++shaderIndex)
    {
        BrxShaderRecord shader;
        shader.offset = reader.Offset();
        shader.sourcePrefixOffset = reader.Offset();
        shader.kind = reader.U8();
        shader.flags = reader.U8();
        shader.oid = reader.S16();
        for (float &component : shader.rgba)
            component = reader.F32();
        for (float &component : shader.volumeRgba)
            component = reader.F32();
        shader.zoneFlags = reader.U32();
        shader.alternateSaturationOid = reader.S16();
        shader.renderPhase = reader.U8();
        shader.textureStageCount = reader.U8();
        shader.sourcePrefixSize = reader.Offset() - shader.sourcePrefixOffset;

        shader.animation = ReadShaderAnimation(reader);
        shader.animationKind = static_cast<std::int16_t>(shader.animation.kind);
        shader.sourceTailOffset = reader.Offset();

        for (std::uint32_t stage = 0; stage < shader.textureStageCount; ++stage)
        {
            BrxShaderRecord::TextureStage textureStage;
            textureStage.oid = reader.S16();
            textureStage.flags = reader.S16();
            const auto frameCount = reader.U16();
            // LoadTexFromBrx consumes the complete serialized U16 range.
            textureStage.ambientBitmapIndices.reserve(frameCount);
            textureStage.diffuseBitmapIndices.reserve(frameCount);
            textureStage.saturateBitmapIndices.reserve(frameCount);
            for (std::uint32_t frame = 0; frame < frameCount; ++frame)
            {
                const auto ambient = static_cast<std::int32_t>(reader.U32());
                const auto diffuse = static_cast<std::int32_t>(reader.U32());
                const auto saturate = static_cast<std::int32_t>(reader.U32());
                textureStage.ambientBitmapIndices.push_back(ambient);
                textureStage.diffuseBitmapIndices.push_back(diffuse);
                textureStage.saturateBitmapIndices.push_back(saturate);
                if (stage == 0)
                {
                    shader.ambientBitmapIndices.push_back(ambient);
                    shader.diffuseBitmapIndices.push_back(diffuse);
                    shader.saturateBitmapIndices.push_back(saturate);
                }
            }
            shader.textureStages.push_back(std::move(textureStage));
        }
        shader.sourceTailSize = reader.Offset() - shader.sourceTailOffset;
        shaders_.push_back(shader);
    }

    worldHeader_.fontTableSourceOffset = reader.Offset();
    worldHeader_.fontCount = reader.U16();
    fonts_.clear();
    fonts_.reserve(worldHeader_.fontCount);
    for (std::uint32_t fontIndex = 0; fontIndex < worldHeader_.fontCount; ++fontIndex)
    {
        BrxFontRecord font;
        font.bitmapIndex = reader.S32();
        font.characterWidth = reader.U8();
        font.spaceWidth = reader.U8();
        font.height = reader.U8();
        font.kind = reader.U8();
        font.scale = reader.F32();
        const auto glyphCount = reader.U32();
        if (glyphCount > 65536)
            throw std::runtime_error("The world payload has an invalid font glyph count");
        font.glyphs.reserve(glyphCount);
        for (std::uint32_t glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex)
            font.glyphs.push_back({reader.S16(), reader.S16(), reader.S16(), reader.S16()});
        fonts_.push_back(std::move(font));
    }
    worldHeader_.fontTableSourceSize = reader.Offset() - worldHeader_.fontTableSourceOffset;

    worldHeader_.worldObjectsOffset = reader.Offset();
    worldHeader_.rootObjectCount = reader.U16();
    if (worldHeader_.rootObjectCount != 0)
    {
        // Every object begins with this common six-byte header. The class
        // loader immediately following it is CID-specific and has no serialized
        // byte length, so advancing to object N+1 must only happen through the
        // real dispatcher.
        const auto firstObjectOffset = reader.Offset();
        worldHeader_.firstRootCid = reader.S16();
        worldHeader_.firstRootOid = reader.S16();
        worldHeader_.firstRootSpliceIndex = reader.S16();
        if (worldHeader_.firstRootCid < 0 || worldHeader_.firstRootCid > 147)
            throw std::runtime_error("The first world object has an invalid CID");
        objects_.push_back({firstObjectOffset, worldHeader_.firstRootCid, worldHeader_.firstRootOid,
                            worldHeader_.firstRootSpliceIndex, -1, static_cast<std::int32_t>(reader.Offset()), 0,
                            false});
    }

    const auto skipGlobset = [&](Reader &stream, const glm::mat4 &objectToWorld, std::uint8_t visibilityMode,
                                 std::uint32_t objectZoneMask, bool fixedPhysics = false,
                                 bool renderObject = true) -> std::uint8_t
    {
        const bool mergedStatic = currentObjectIndex >= 0 &&
                                  static_cast<std::size_t>(currentObjectIndex) < objects_.size() &&
                                  objects_[static_cast<std::size_t>(currentObjectIndex)].cid == 4;
        const std::size_t objectVertexBegin = renderVertices_.size();
        const std::size_t objectIndexBegin = renderIndices_.size();
        const std::size_t objectBatchBegin = renderBatches_.size();
        const std::size_t objectCelEdgeBegin = celEdges_.size();
        const std::size_t objectCelBatchBegin = celBatches_.size();
        const std::size_t objectGlobWarpBegin = globWarps_.size();
        BrxGlobsetRecord globsetRecord;
        globsetRecord.ownerObjectIndex = currentObjectIndex;
        globsetRecord.relight = stream.U8() != 0;
        globsetRecord.unknownHeaderByte = stream.U8();
        const auto boneCount = stream.U8();
        globsetRecord.boneOids.reserve(boneCount);
        for (std::uint32_t bone = 0; bone < boneCount; ++bone)
            globsetRecord.boneOids.push_back(stream.S16());
        const auto poseCount = stream.U8();
        globsetRecord.poseWeights.reserve(poseCount);
        for (std::uint32_t pose = 0; pose < poseCount; ++pose)
            globsetRecord.poseWeights.push_back(stream.F32());
        globsetRecord.originalPoseWeights = globsetRecord.poseWeights;
        const auto globCount = stream.U16();
        struct GlobGeometryRange
        {
            std::size_t vertexBegin = 0;
            std::size_t vertexCount = 0;
            std::size_t indexBegin = 0;
            std::size_t indexCount = 0;
            std::size_t batchBegin = 0;
            std::size_t batchCount = 0;
            std::size_t celEdgeBegin = 0;
            std::size_t celEdgeCount = 0;
            std::size_t celBatchBegin = 0;
            std::size_t celBatchCount = 0;
        };
        std::vector<GlobGeometryRange> globRanges(globCount);
        worldHeader_.parsedGlobCount += globCount;
        for (std::uint32_t globIndex = 0; globIndex < globCount; ++globIndex)
        {
            const std::uint32_t renderGroup = nextRenderGroup++;
            const auto properties = stream.U16();
            BrxGlobRecord globRecord;
            globRecord.renderGroup = renderGroup;
            globRecord.ownerObjectIndex = currentObjectIndex;
            globRecord.propertyMask = properties;
            const bool instanced = (properties & 1) != 0;
            globRecord.instanced = instanced;
            std::int16_t sourceGlobIndex = -1;
            glm::mat4 instanceTransform(1.0f);
            if (instanced)
            {
                sourceGlobIndex = stream.S16();
                globRecord.sourceGlobIndex = sourceGlobIndex;
                if (sourceGlobIndex < 0 || static_cast<std::uint32_t>(sourceGlobIndex) >= globIndex)
                    throw std::runtime_error("Instanced glob has an invalid source index");
                instanceTransform = readMatrix4(stream);
                std::memcpy(globRecord.instanceTransform, glm::value_ptr(instanceTransform),
                            sizeof(globRecord.instanceTransform));
                if (static_cast<std::size_t>(sourceGlobIndex) < globsetRecord.globIndices.size())
                {
                    globRecord.sourceRecordIndex = globsetRecord.globIndices[static_cast<std::size_t>(sourceGlobIndex)];
                    if (globRecord.sourceRecordIndex < globs_.size())
                    {
                        const auto &sourceRecord = globs_[globRecord.sourceRecordIndex];
                        globRecord.zoneMask = sourceRecord.zoneMask;
                        globRecord.subglobRadiusScale = sourceRecord.subglobRadiusScale;
                        globRecord.authoredSortZ = sourceRecord.authoredSortZ;
                        globRecord.effectiveSortZ = sourceRecord.effectiveSortZ;
                        globRecord.fogScale = sourceRecord.fogScale;
                        globRecord.authoredMaximumRenderDistance = sourceRecord.authoredMaximumRenderDistance;
                        globRecord.maximumRenderDistanceInfinite = sourceRecord.maximumRenderDistanceInfinite;
                        globRecord.maximumRenderDistance = sourceRecord.maximumRenderDistance;
                        globRecord.authoredCelMaximumRenderDistance = sourceRecord.authoredCelMaximumRenderDistance;
                        globRecord.celMaximumRenderDistanceInfinite = sourceRecord.celMaximumRenderDistanceInfinite;
                        globRecord.celMaximumRenderDistance = sourceRecord.celMaximumRenderDistance;
                    }
                }
            }
            const glm::mat4 globSourceToWorld = instanced ? objectToWorld * instanceTransform : objectToWorld;
            std::memcpy(globRecord.sourceToWorld, glm::value_ptr(globSourceToWorld), sizeof(globRecord.sourceToWorld));
            const bool hasGlobZoneMask = (properties & 0x002) != 0;
            const std::uint32_t globZoneMask = hasGlobZoneMask ? stream.U32() : globRecord.zoneMask;
            globRecord.hasZoneMask = hasGlobZoneMask;
            globRecord.zoneMask = globZoneMask;
            // RenderMsGlobset always tests GLOBI::grfzon. Ordinary ALOs use
            // their VISS mode to choose object-, GLOB-, or no zone culling.
            const std::uint32_t effectiveZoneMask =
                mergedStatic ? globZoneMask
                             : (visibilityMode == 2 ? objectZoneMask : (visibilityMode == 1 ? globZoneMask : 0));
            if ((properties & 0x200) != 0)
            {
                globRecord.hasSubglobRadiusScale = true;
                globRecord.subglobRadiusScale = stream.F32();
            }
            float maximumRenderDistance = globRecord.maximumRenderDistance;
            float celBorderMaximumDistance = globRecord.celMaximumRenderDistance;
            if (instanced)
            {
                const auto &source = globRanges[static_cast<std::size_t>(sourceGlobIndex)];
                if (source.celBatchCount != 0)
                {
                    celBorderMaximumDistance = celBatches_[source.celBatchBegin].maximumDistance;
                }
            }
            const bool hasExplicitSortZ = (properties & 0x004) != 0;
            bool usesExplicitSortZ = hasExplicitSortZ;
            float explicitSortZ = 0.0f;
            if (hasExplicitSortZ)
            {
                const float authoredZ = stream.F32();
                // This is exactly how LoadGlobFromBrx stores gZOrder.
                if (authoredZ == std::numeric_limits<float>::max())
                    usesExplicitSortZ = false;
                else
                    explicitSortZ = authoredZ * std::abs(authoredZ);
                globRecord.authoredSortZ = authoredZ;
            }
            globRecord.hasExplicitSortZ = hasExplicitSortZ;
            if (hasExplicitSortZ)
                globRecord.effectiveSortZ = usesExplicitSortZ ? explicitSortZ : std::numeric_limits<float>::max();
            float globFogScale = globRecord.fogScale;
            if ((properties & 0x008) != 0)
                globFogScale = stream.F32();
            globRecord.hasFogScale = (properties & 0x008) != 0;
            globRecord.fogScale = globFogScale;
            if ((properties & 0x010) != 0)
            {
                const float authoredMaximumRenderDistance = stream.F32();
                globRecord.authoredMaximumRenderDistance = authoredMaximumRenderDistance;
                globRecord.maximumRenderDistanceInfinite =
                    authoredMaximumRenderDistance == std::numeric_limits<float>::max();
                maximumRenderDistance = authoredMaximumRenderDistance;
                if (globRecord.maximumRenderDistanceInfinite)
                    maximumRenderDistance = 1.0e10f;
            }
            if ((properties & 0x020) != 0)
            {
                const float authoredCelMaximumRenderDistance = stream.F32();
                globRecord.authoredCelMaximumRenderDistance = authoredCelMaximumRenderDistance;
                globRecord.celMaximumRenderDistanceInfinite =
                    authoredCelMaximumRenderDistance == std::numeric_limits<float>::max();
                celBorderMaximumDistance = authoredCelMaximumRenderDistance;
                if (globRecord.celMaximumRenderDistanceInfinite)
                    celBorderMaximumDistance = 2000.0f;
            }
            celBorderMaximumDistance = std::min(celBorderMaximumDistance, maximumRenderDistance);
            globRecord.hasMaximumRenderDistance = (properties & 0x010) != 0;
            globRecord.maximumRenderDistance = maximumRenderDistance;
            globRecord.hasCelMaximumRenderDistance = (properties & 0x020) != 0;
            globRecord.celMaximumRenderDistance = celBorderMaximumDistance;
            globRecord.hasShaderAnimation = (properties & 0x040) != 0;
            if (globRecord.hasShaderAnimation)
                globRecord.shaderAnimation = ReadShaderAnimation(stream);
            const bool hasGleam = (properties & 0x080) != 0;
            glm::vec3 gleamNormal(0.0f);
            float gleamCurve[4]{};
            if (hasGleam)
            {
                gleamNormal.x = stream.F32();
                gleamNormal.y = stream.F32();
                gleamNormal.z = stream.F32();
                for (float &coefficient : gleamCurve)
                    coefficient = stream.F32();
            }
            globRecord.hasGleam = hasGleam;
            globRecord.gleamNormal[0] = gleamNormal.x;
            globRecord.gleamNormal[1] = gleamNormal.y;
            globRecord.gleamNormal[2] = gleamNormal.z;
            std::copy(std::begin(gleamCurve), std::end(gleamCurve), globRecord.gleamCurve);
            BrxGlobWarpRecord globWarp;
            const bool hasWarp = (properties & 0x100) != 0;
            if (hasWarp)
            {
                globWarp.ownerObjectIndex = currentObjectIndex;
                globWarp.renderGroup = renderGroup;
                globWarp.controllerOid = stream.S16();
                globWarp.envelopeKind = static_cast<std::int8_t>(stream.U8());
                if (globWarp.envelopeKind != -1)
                {
                    globWarp.innerDistance = stream.F32();
                    globWarp.innerWeight = stream.F32();
                    globWarp.outerDistance = stream.F32();
                    globWarp.outerWeight = stream.F32();
                    const glm::mat4 envelope = readMatrix4(stream);
                    std::memcpy(globWarp.envelopeMatrix, glm::value_ptr(envelope), sizeof(globWarp.envelopeMatrix));
                }
                globWarp.matrixCount = stream.U8();
                globWarp.deformPosition = stream.U8() != 0;
                globWarp.deformUv = stream.U8() != 0;
                std::memcpy(globWarp.objectToWorld, glm::value_ptr(objectToWorld), sizeof(globWarp.objectToWorld));
                globRecord.hasWarp = true;
                globRecord.warpControllerOid = globWarp.controllerOid;
                globRecord.warpEnvelopeKind = globWarp.envelopeKind;
                globRecord.warpMatrixCount = globWarp.matrixCount;
                globRecord.warpPosition = globWarp.deformPosition;
                globRecord.warpUv = globWarp.deformUv;
            }

            glm::vec3 globCenter;
            globCenter.x = stream.F32();
            globCenter.y = stream.F32();
            globCenter.z = stream.F32();
            const float globRadius = stream.F32();
            const std::int16_t globOid = stream.S16();
            // RenderMsGlobset always consumes GLOB::posCenter directly.
            // RenderAloGlobset does the same only for fixed-physics objects.
            const glm::vec4 worldGlobCenter = (mergedStatic || fixedPhysics)
                                                  ? glm::vec4(globCenter, 1.0f)
                                                  : objectToWorld * glm::vec4(globCenter, 1.0f);
            const float objectScale = (std::max)(
                glm::length(glm::vec3(objectToWorld[0])),
                (std::max)(glm::length(glm::vec3(objectToWorld[1])), glm::length(glm::vec3(objectToWorld[2]))));
            const float worldGlobRadius = (mergedStatic || fixedPhysics) ? globRadius : globRadius * objectScale;
            const std::uint8_t rotationTracking = stream.U8(); // RTCK
            if (rotationTracking >= 6 && rotationTracking != 0xff)
                throw std::runtime_error("GLOB contains an invalid RTCK mode");
            const auto renderPhase = stream.U8();
            const auto globFlags = stream.U8();
            globRecord.center[0] = globCenter.x;
            globRecord.center[1] = globCenter.y;
            globRecord.center[2] = globCenter.z;
            globRecord.radius = globRadius;
            globRecord.oid = globOid;
            globRecord.rotationTracking = rotationTracking;
            globRecord.renderPhase = renderPhase;
            globRecord.flags = globFlags;
            if (!instanced)
            {
                auto &range = globRanges[globIndex];
                range.vertexBegin = renderVertices_.size();
                range.indexBegin = renderIndices_.size();
                range.batchBegin = renderBatches_.size();
                range.celEdgeBegin = celEdges_.size();
                range.celBatchBegin = celBatches_.size();
                const auto subglobCount = stream.U16();
                worldHeader_.parsedSubglobCount += subglobCount;
                for (std::uint32_t subglob = 0; subglob < subglobCount; ++subglob)
                {
                    glm::vec3 subglobCenter;
                    subglobCenter.x = stream.F32();
                    subglobCenter.y = stream.F32();
                    subglobCenter.z = stream.F32();
                    const float subglobRadius = stream.F32();
                    const auto vertexCount = stream.U16();
                    const auto triangleCount = stream.U16();
                    const auto shaderIndex = stream.U16();
                    const auto subglobBoneCount = stream.U16();
                    const auto selfIllumination = stream.F32();
                    BrxSubglobRecord subglobRecord;
                    subglobRecord.center[0] = subglobCenter.x;
                    subglobRecord.center[1] = subglobCenter.y;
                    subglobRecord.center[2] = subglobCenter.z;
                    subglobRecord.radius = subglobRadius;
                    subglobRecord.vertexCount = vertexCount;
                    subglobRecord.triangleCount = triangleCount;
                    subglobRecord.shaderIndex = shaderIndex;
                    subglobRecord.boneCount = subglobBoneCount;
                    subglobRecord.selfIllumination = selfIllumination;
                    subglobRecord.vertices.reserve(vertexCount);
                    subglobRecord.triangles.reserve(triangleCount);
                    if (shaderIndex >= shaders_.size())
                        throw std::runtime_error("Subglob references an invalid shader index");
                    worldHeader_.parsedVertexCount += vertexCount;
                    worldHeader_.parsedTriangleCount += triangleCount;
                    const auto baseVertex = static_cast<std::uint32_t>(renderVertices_.size());
                    renderVertices_.reserve(renderVertices_.size() + vertexCount);
                    for (std::uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
                    {
                        BrxSourceVertex sourceVertex;
                        BrxRenderVertex vertex;
                        for (float &component : sourceVertex.position)
                            component = stream.F32();
                        for (float &component : sourceVertex.normal)
                            component = stream.F32();
                        for (float &component : sourceVertex.color)
                            component = stream.F32();
                        sourceVertex.uv[0] = stream.F32();
                        sourceVertex.uv[1] = stream.F32();
                        for (auto &boneIndex : sourceVertex.boneIndices)
                            boneIndex = stream.U16();
                        for (float &boneWeight : sourceVertex.boneWeights)
                            boneWeight = stream.F32();
                        std::copy(std::begin(sourceVertex.position), std::end(sourceVertex.position), vertex.position);
                        std::copy(std::begin(sourceVertex.normal), std::end(sourceVertex.normal), vertex.normal);
                        std::copy(std::begin(sourceVertex.color), std::end(sourceVertex.color), vertex.color);
                        std::copy(std::begin(sourceVertex.uv), std::end(sourceVertex.uv), vertex.uv);
                        subglobRecord.vertices.push_back(sourceVertex);
                        const glm::vec4 worldPosition =
                            objectToWorld * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f);
                        glm::vec3 worldNormal =
                            glm::mat3(objectToWorld) * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                        if (glm::dot(worldNormal, worldNormal) > 0.000001f)
                            worldNormal = glm::normalize(worldNormal);
                        else
                            worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
                        vertex.position[0] = worldPosition.x;
                        vertex.position[1] = worldPosition.y;
                        vertex.position[2] = worldPosition.z;
                        vertex.normal[0] = worldNormal.x;
                        vertex.normal[1] = worldNormal.y;
                        vertex.normal[2] = worldNormal.z;
                        includeRenderPosition(vertex);
                        renderVertices_.push_back(vertex);
                    }
                    renderIndices_.reserve(renderIndices_.size() + static_cast<std::size_t>(triangleCount) * 3);
                    for (std::uint32_t triangle = 0; triangle < triangleCount; ++triangle)
                    {
                        const auto a = stream.U16();
                        const auto b = stream.U16();
                        const auto c = stream.U16();
                        if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
                            throw std::runtime_error("PCBRX triangle index is out of range");
                        renderIndices_.push_back(baseVertex + a);
                        renderIndices_.push_back(baseVertex + b);
                        renderIndices_.push_back(baseVertex + c);
                        subglobRecord.triangles.push_back({a, b, c});
                    }
                    const glm::vec4 worldCenter = objectToWorld * glm::vec4(subglobCenter, 1.0f);
                    BrxRenderBatch batch{
                        static_cast<std::uint32_t>(renderIndices_.size() - static_cast<std::size_t>(triangleCount) * 3),
                        static_cast<std::uint32_t>(triangleCount * 3), shaderIndex, renderPhase, selfIllumination};
                    batch.boundsCenter[0] = worldCenter.x;
                    batch.boundsCenter[1] = worldCenter.y;
                    batch.boundsCenter[2] = worldCenter.z;
                    batch.boundsRadius = subglobRadius * objectScale;
                    batch.mrdRadius = worldGlobRadius;
                    batch.zoneMask = effectiveZoneMask;
                    batch.renderGroup = renderGroup;
                    // ProjectCane selects projected-shadow kinds from
                    // GLOB::grfglob.  The low property-mask bits mean
                    // "instance" and "zone override" and must never be used
                    // as shadow receiver flags.
                    batch.shadowReceiverMask = globRecord.flags & 0x3u;
                    batch.sortCenter[0] = worldGlobCenter.x;
                    batch.sortCenter[1] = worldGlobCenter.y;
                    batch.sortCenter[2] = worldGlobCenter.z;
                    batch.explicitSortZ = explicitSortZ;
                    batch.hasExplicitSortZ = usesExplicitSortZ;
                    batch.fogScale = globFogScale;
                    batch.maximumRenderDistance = maximumRenderDistance;
                    batch.dynamicLighting = globsetRecord.relight;
                    batch.hasGleam = hasGleam;
                    batch.rotationTracking = rotationTracking;
                    batch.hasShaderAnimation = globRecord.hasShaderAnimation;
                    batch.shaderAnimation = globRecord.shaderAnimation;
                    std::memcpy(batch.rtckBaseMatrix, glm::value_ptr(objectToWorld), sizeof(batch.rtckBaseMatrix));
                    batch.ownerObjectIndex = currentObjectIndex;
                    if (hasGleam)
                    {
                        glm::vec3 worldGleam = glm::mat3(objectToWorld) * gleamNormal;
                        if (glm::dot(worldGleam, worldGleam) > 0.000001f)
                            worldGleam = glm::normalize(worldGleam);
                        batch.gleamNormal[0] = worldGleam.x;
                        batch.gleamNormal[1] = worldGleam.y;
                        batch.gleamNormal[2] = worldGleam.z;
                        std::copy(std::begin(gleamCurve), std::end(gleamCurve), batch.gleamCurve);
                    }
                    renderBatches_.push_back(batch);
                    const std::size_t poseDeltaCount = static_cast<std::size_t>(vertexCount) * poseCount;
                    subglobRecord.posePositionDeltas.reserve(poseDeltaCount);
                    subglobRecord.poseNormalDeltas.reserve(poseDeltaCount);
                    for (std::size_t deltaIndex = 0; deltaIndex < poseDeltaCount; ++deltaIndex)
                    {
                        std::array<float, 4> positionDelta{};
                        std::array<float, 4> normalDelta{};
                        for (float &component : positionDelta)
                            component = stream.F32();
                        for (float &component : normalDelta)
                            component = stream.F32();
                        subglobRecord.posePositionDeltas.push_back(positionDelta);
                        subglobRecord.poseNormalDeltas.push_back(normalDelta);
                    }
                    globRecord.sourceSubglobs.push_back(std::move(subglobRecord));
                }

                // The retail loader reads a second geometry representation
                // after the normal subglobs. These SUBCEL records contain the
                // original VIF source arrays used by projected/cel geometry.
                const auto subcelCount = stream.U16();
                for (std::uint32_t subcel = 0; subcel < subcelCount; ++subcel)
                {
                    const auto edgeCount = stream.U32();
                    const auto pointCount = static_cast<std::uint64_t>(edgeCount) * 4;
                    BrxSourceCelRecord sourceCel;
                    sourceCel.points.reserve(static_cast<std::size_t>(pointCount));
                    const std::uint32_t firstEdge = static_cast<std::uint32_t>(celEdges_.size());
                    celEdges_.reserve(celEdges_.size() + edgeCount);
                    for (std::uint32_t edge = 0; edge < edgeCount; ++edge)
                    {
                        BrxCelEdge celEdge{};
                        for (int point = 0; point < 4; ++point)
                        {
                            BrxSourceCelPoint sourcePoint;
                            for (float &component : sourcePoint.position)
                                component = stream.F32();
                            for (auto &boneIndex : sourcePoint.boneIndices)
                                boneIndex = stream.U16();
                            for (float &boneWeight : sourcePoint.boneWeights)
                                boneWeight = stream.F32();
                            sourceCel.points.push_back(sourcePoint);
                            const glm::vec4 position(sourcePoint.position[0], sourcePoint.position[1],
                                                     sourcePoint.position[2], sourcePoint.position[3]);
                            const glm::vec4 world = objectToWorld * glm::vec4(position.x, position.y, position.z, 1.0f);
                            celEdge.points[point][0] = world.x;
                            celEdge.points[point][1] = world.y;
                            celEdge.points[point][2] = world.z;
                            celEdge.points[point][3] = 1.0f;
                        }
                        celEdges_.push_back(celEdge);
                    }
                    const std::size_t celPoseDeltaCount = static_cast<std::size_t>(pointCount) * poseCount;
                    sourceCel.posePositionDeltas.reserve(celPoseDeltaCount);
                    for (std::size_t deltaIndex = 0; deltaIndex < celPoseDeltaCount; ++deltaIndex)
                    {
                        std::array<float, 4> positionDelta{};
                        for (float &component : positionDelta)
                            component = stream.F32();
                        sourceCel.posePositionDeltas.push_back(positionDelta);
                    }
                    globRecord.sourceCelRecords.push_back(std::move(sourceCel));
                    if (edgeCount != 0)
                    {
                        // RenderAloGlobset tests cel-border MRD and frustum
                        // visibility with GLOB::posCenter/GLOB::sRadius.  The
                        // SUBCEL points are submission geometry, not a second
                        // culling bound; deriving a bound from them can move
                        // the test toward the camera and keep distant outlines
                        // alive (notably SO OID 1895).
                        std::uint8_t celPhase = 5;
                        if (renderPhase == 7 || renderPhase == 8 || renderPhase == 9)
                            celPhase = 9;
                        else if (renderPhase == 13 || renderPhase == 14)
                            celPhase = 14;
                        BrxCelBatch celBatch{firstEdge, edgeCount, renderGroup, celPhase, effectiveZoneMask};
                        celBatch.center[0] = worldGlobCenter.x;
                        celBatch.center[1] = worldGlobCenter.y;
                        celBatch.center[2] = worldGlobCenter.z;
                        celBatch.radius = worldGlobRadius;
                        celBatch.surfaceMaximumDistance = maximumRenderDistance;
                        celBatch.maximumDistance = celBorderMaximumDistance;
                        celBatch.ownerObjectIndex = currentObjectIndex;
                        celBatches_.push_back(celBatch);
                    }
                }
                range.vertexCount = renderVertices_.size() - range.vertexBegin;
                range.indexCount = renderIndices_.size() - range.indexBegin;
                range.batchCount = renderBatches_.size() - range.batchBegin;
                range.celEdgeCount = celEdges_.size() - range.celEdgeBegin;
                range.celBatchCount = celBatches_.size() - range.celBatchBegin;
                bool translucentSort = false;
                std::uint8_t projectedFlags = 0;
                for (std::size_t batchIndex = range.batchBegin; batchIndex < range.batchBegin + range.batchCount;
                     ++batchIndex)
                {
                    const auto &batchShader = shaders_[renderBatches_[batchIndex].shaderIndex];
                    if ((renderPhase == 1 || renderPhase == 4 || renderPhase == 8) && batchShader.flags == 2)
                        translucentSort = true;
                    if (renderPhase == 6)
                        projectedFlags = batchShader.flags; // retail keeps the last SUBGLOB value
                }
                for (std::size_t batchIndex = range.batchBegin; batchIndex < range.batchBegin + range.batchCount;
                     ++batchIndex)
                {
                    renderBatches_[batchIndex].translucentSort = translucentSort;
                    renderBatches_[batchIndex].projectedFlags = projectedFlags;
                }
            }
            else
            {
                const auto &source = globRanges[static_cast<std::size_t>(sourceGlobIndex)];
                const glm::mat4 delta = objectToWorld * instanceTransform * glm::inverse(objectToWorld);
                // Instances share the source VAO, not its pdmat. Resolve raw
                // geometry to the non-instanced ancestor so instance-of-instance
                // matrices do not accumulate (including collapsed source poses).
                auto geometrySourceSlot = static_cast<std::size_t>(sourceGlobIndex);
                while (globs_[globsetRecord.globIndices[geometrySourceSlot]].instanced)
                    geometrySourceSlot =
                        static_cast<std::size_t>(globs_[globsetRecord.globIndices[geometrySourceSlot]].sourceGlobIndex);
                const auto &geometrySource = globRanges[geometrySourceSlot];
                const auto instanceVertexBegin = renderVertices_.size();
                const auto instanceIndexBegin = renderIndices_.size();
                appendTransformedGeometry(geometrySource.vertexBegin, geometrySource.vertexCount,
                                          geometrySource.indexBegin, geometrySource.indexCount,
                                          geometrySource.batchBegin, geometrySource.batchCount, delta);
                const auto firstNewBatch = renderBatches_.size() - source.batchCount;
                for (std::size_t packet = 0; packet < source.batchCount; ++packet)
                {
                    const auto geometryPacket = renderBatches_[firstNewBatch + packet];
                    auto inheritedPacket = renderBatches_[source.batchBegin + packet];
                    inheritedPacket.firstIndex = geometryPacket.firstIndex;
                    inheritedPacket.renderGroup = geometryPacket.renderGroup;
                    std::copy(std::begin(geometryPacket.boundsCenter), std::end(geometryPacket.boundsCenter),
                              inheritedPacket.boundsCenter);
                    inheritedPacket.boundsRadius = geometryPacket.boundsRadius;
                    renderBatches_[firstNewBatch + packet] = std::move(inheritedPacket);
                }
                const std::uint32_t instanceRenderGroup =
                    source.batchCount != 0 ? renderBatches_[firstNewBatch].renderGroup : nextRenderGroup++;
                const std::uint32_t firstCelEdge = static_cast<std::uint32_t>(celEdges_.size());
                for (std::size_t edgeIndex = 0; edgeIndex < source.celEdgeCount; ++edgeIndex)
                {
                    BrxCelEdge edge = celEdges_[geometrySource.celEdgeBegin + edgeIndex];
                    for (auto &point : edge.points)
                    {
                        const glm::vec4 transformed = delta * glm::vec4(point[0], point[1], point[2], 1.0f);
                        point[0] = transformed.x;
                        point[1] = transformed.y;
                        point[2] = transformed.z;
                        point[3] = 1.0f;
                    }
                    celEdges_.push_back(edge);
                }
                for (std::size_t celIndex = 0; celIndex < source.celBatchCount; ++celIndex)
                {
                    BrxCelBatch batch = celBatches_[source.celBatchBegin + celIndex];
                    batch.firstEdge =
                        firstCelEdge + (batch.firstEdge - static_cast<std::uint32_t>(source.celEdgeBegin));
                    batch.renderGroup = instanceRenderGroup;
                    batch.zoneMask = effectiveZoneMask;
                    // An instanced GLOB has its own serialized center/radius;
                    // ProjectCane uses those current-GLOB values for MRD, not
                    // a transformed copy of the source GLOB's cel bound.
                    batch.center[0] = worldGlobCenter.x;
                    batch.center[1] = worldGlobCenter.y;
                    batch.center[2] = worldGlobCenter.z;
                    batch.radius = worldGlobRadius;
                    batch.surfaceMaximumDistance = maximumRenderDistance;
                    batch.maximumDistance = celBorderMaximumDistance;
                    batch.ownerObjectIndex = currentObjectIndex;
                    celBatches_.push_back(batch);
                }
                for (std::size_t batchIndex = firstNewBatch; batchIndex < renderBatches_.size(); ++batchIndex)
                {
                    const glm::mat4 instanceModel = objectToWorld * instanceTransform;
                    renderBatches_[batchIndex].ownerObjectIndex = currentObjectIndex;
                    renderBatches_[batchIndex].rotationTracking = rotationTracking;
                    // An instanced GLOB starts with the source geometry and
                    // render packets, but its serialized SAA belongs to this
                    // instance. Retail CloneGlob preserves/duplicates that
                    // private animation state and redirects matching SUBGLOBs
                    // to it. Without replacing the copied source SAA here,
                    // CNVO instances lose their SCROLLER and appear static.
                    if (globRecord.hasShaderAnimation)
                    {
                        renderBatches_[batchIndex].hasShaderAnimation = true;
                        renderBatches_[batchIndex].shaderAnimation = globRecord.shaderAnimation;
                    }
                    std::memcpy(renderBatches_[batchIndex].rtckBaseMatrix, glm::value_ptr(instanceModel),
                                sizeof(renderBatches_[batchIndex].rtckBaseMatrix));
                    // LoadGlobsetFromBrx copies the source instance's render
                    // state, but deliberately does not copy GLOB::gleam.
                    // A GLEAM exists on an instance only when this instance's
                    // own property mask contains 0x80.
                    if (!hasGleam)
                    {
                        renderBatches_[batchIndex].hasGleam = false;
                        renderBatches_[batchIndex].gleamNormal[0] = 0.0f;
                        renderBatches_[batchIndex].gleamNormal[1] = 0.0f;
                        renderBatches_[batchIndex].gleamNormal[2] = 0.0f;
                        for (float &coefficient : renderBatches_[batchIndex].gleamCurve)
                            coefficient = 0.0f;
                    }
                    if (hasExplicitSortZ)
                    {
                        renderBatches_[batchIndex].hasExplicitSortZ = usesExplicitSortZ;
                        renderBatches_[batchIndex].explicitSortZ = explicitSortZ;
                    }
                    if ((properties & 0x008) != 0)
                        renderBatches_[batchIndex].fogScale = globFogScale;
                    if (hasGleam)
                    {
                        renderBatches_[batchIndex].hasGleam = true;
                        glm::vec3 worldGleam = glm::mat3(objectToWorld) * gleamNormal;
                        if (glm::dot(worldGleam, worldGleam) > 0.000001f)
                            worldGleam = glm::normalize(worldGleam);
                        renderBatches_[batchIndex].gleamNormal[0] = worldGleam.x;
                        renderBatches_[batchIndex].gleamNormal[1] = worldGleam.y;
                        renderBatches_[batchIndex].gleamNormal[2] = worldGleam.z;
                        std::copy(std::begin(gleamCurve), std::end(gleamCurve), renderBatches_[batchIndex].gleamCurve);
                    }
                    // The serialized GLOB center is authoritative for the
                    // instance's RPL distance key.  The geometry/state clone
                    // above starts from the source GLOB, but LoadGlobsetFromBrx
                    // then applies this instance record's center, radius, and
                    // optional MRD overrides.  Keeping the source packet's
                    // mrdRadius/maximumRenderDistance makes an instance fade
                    // according to an unrelated source GLOB (or never fade
                    // when that source is unbounded).
                    renderBatches_[batchIndex].sortCenter[0] = worldGlobCenter.x;
                    renderBatches_[batchIndex].sortCenter[1] = worldGlobCenter.y;
                    renderBatches_[batchIndex].sortCenter[2] = worldGlobCenter.z;
                    renderBatches_[batchIndex].mrdRadius = worldGlobRadius;
                    renderBatches_[batchIndex].maximumRenderDistance = maximumRenderDistance;
                }
                // The instance's resolved GLOBI value is authoritative for MS
                // as well as VISS 1. VISS 0 must not inherit a source packet's
                // culling mask. Cel packets already receive this value above.
                for (std::size_t batchIndex = firstNewBatch; batchIndex < renderBatches_.size(); ++batchIndex)
                    renderBatches_[batchIndex].zoneMask = effectiveZoneMask;
                auto &instanceRange = globRanges[globIndex];
                instanceRange.vertexBegin = instanceVertexBegin;
                instanceRange.vertexCount = renderVertices_.size() - instanceVertexBegin;
                instanceRange.indexBegin = instanceIndexBegin;
                instanceRange.indexCount = renderIndices_.size() - instanceIndexBegin;
                instanceRange.batchBegin = renderBatches_.size() - source.batchCount;
                instanceRange.batchCount = source.batchCount;
                instanceRange.celEdgeBegin = firstCelEdge;
                instanceRange.celEdgeCount = source.celEdgeCount;
                instanceRange.celBatchBegin = celBatches_.size() - source.celBatchCount;
                instanceRange.celBatchCount = source.celBatchCount;
                if (sourceGlobIndex >= 0 &&
                    static_cast<std::size_t>(sourceGlobIndex) < globsetRecord.globIndices.size())
                {
                    const auto sourceRecordIndex = globsetRecord.globIndices[static_cast<std::size_t>(sourceGlobIndex)];
                    if (sourceRecordIndex < globs_.size())
                    {
                        globRecord.sourceSubglobs = globs_[sourceRecordIndex].sourceSubglobs;
                        globRecord.sourceCelRecords = globs_[sourceRecordIndex].sourceCelRecords;
                    }
                }
            }
            const auto &completedRange = globRanges[globIndex];
            if (completedRange.batchCount != 0)
                globRecord.renderGroup = renderBatches_[completedRange.batchBegin].renderGroup;
            else if (completedRange.celBatchCount != 0)
                globRecord.renderGroup = celBatches_[completedRange.celBatchBegin].renderGroup;
            if (hasWarp)
            {
                const auto &range = globRanges[globIndex];
                globWarp.renderGroup = globRecord.renderGroup;
                globWarp.firstVertex = static_cast<std::uint32_t>(range.vertexBegin);
                globWarp.vertexCount = static_cast<std::uint32_t>(range.vertexCount);
                globWarp.firstCelEdge = static_cast<std::uint32_t>(range.celEdgeBegin);
                globWarp.celEdgeCount = static_cast<std::uint32_t>(range.celEdgeCount);
                if (range.batchCount != 0)
                    globWarp.renderGroup = renderBatches_[range.batchBegin].renderGroup;
                globWarps_.push_back(globWarp);
            }
            globsetRecord.globIndices.push_back(globs_.size());
            globs_.push_back(std::move(globRecord));
        }
        globsets_.push_back(std::move(globsetRecord));
        if (!renderObject)
        {
            // RenderAloAll exits before RenderAloGlobset for fHidden objects.
            // They still have to be fully decoded so the BRX stream and child
            // hierarchy remain aligned, but their own GLOB submissions must
            // never enter the editor's render queues.
            renderVertices_.resize(objectVertexBegin);
            renderIndices_.resize(objectIndexBegin);
            renderBatches_.resize(objectBatchBegin);
            celEdges_.resize(objectCelEdgeBegin);
            celBatches_.resize(objectCelBatchBegin);
            globWarps_.resize(objectGlobWarpBegin);
        }
        return poseCount;
    };

    const auto parseAlox = [](Reader &stream, BrxAloxRecord &alox)
    {
        const auto flags = stream.U32();
        alox.flags = flags;
        if ((flags & 0x01) != 0)
            for (float &value : alox.preRotation)
                value = stream.F32();
        if ((flags & 0x02) != 0)
            for (float &value : alox.postRotation)
                value = stream.F32();
        if ((flags & 0x0c) != 0)
            alox.rotationSourceOid = stream.S16();
        if ((flags & 0x10) != 0)
            alox.ikTargetOid = stream.S16();
        if ((flags & 0x20) != 0)
        {
            alox.elbowOid = stream.S16();
            for (float &value : alox.ikPosition)
                value = stream.F32();
            for (float &value : alox.wristPosition)
                value = stream.F32();
            alox.twistRadians = stream.F32();
        }
        if ((flags & 0x40) != 0)
        {
            alox.jointParentOid = stream.S16();
            alox.jointChildOid = stream.S16();
        }
        if ((flags & 0x80) != 0)
            alox.segmentScaleCompensation = stream.U8() != 0;
    };

    const auto parseGeometry = [&](Reader &stream, std::int32_t ownerObjectIndex, bool retainDebugEdges,
                                   bool cameraGeometry, BrxSoGeometryInfo *info) -> std::uint16_t
    {
        const float radius = stream.F32();
        const auto positionCount = stream.U16();
        worldHeader_.parsedCollisionVertexCount += positionCount;
        std::vector<glm::vec3> positions(positionCount);
        for (auto &position : positions)
        {
            // Preserve BRX stream order explicitly. Function-argument
            // evaluation order is not guaranteed, and MSVC may otherwise
            // consume these values as Z, Y, X.
            position.x = stream.F32();
            position.y = stream.F32();
            position.z = stream.F32();
        }
        if (info != nullptr)
        {
            info->radius = radius;
            info->positionCount = positionCount;
            info->positions.reserve(positionCount);
            for (const auto &position : positions)
                info->positions.push_back({position.x, position.y, position.z});
            if (!positions.empty())
            {
                for (int component = 0; component < 3; ++component)
                    info->boundsMin[component] = info->boundsMax[component] = positions[0][component];
                for (const auto &position : positions)
                    for (int component = 0; component < 3; ++component)
                    {
                        info->boundsMin[component] = (std::min)(info->boundsMin[component], position[component]);
                        info->boundsMax[component] = (std::max)(info->boundsMax[component], position[component]);
                    }
            }
        }
        const auto surfaceCount = stream.U16();
        worldHeader_.parsedCollisionSurfaceCount += surfaceCount;
        const auto edgeCount = stream.U16();
        if (info != nullptr)
        {
            info->surfaceCount = surfaceCount;
            info->edgeCount = edgeCount;
        }
        std::uint64_t decodedEdges = 0;
        for (std::uint32_t surface = 0; surface < surfaceCount; ++surface)
        {
            const std::uint16_t indices[3]{stream.U16(), stream.U16(), stream.U16()};
            if (indices[0] >= positionCount || indices[1] >= positionCount || indices[2] >= positionCount)
                throw std::runtime_error("Collision surface contains an invalid position index");
            const float surfaceRadius = stream.F32();
            const std::array<float, 3> surfaceCenter{stream.F32(), stream.F32(), stream.F32()};
            BrxSoGeometryInfo::Surface retainedSurface;
            retainedSurface.positionIndices = {indices[0], indices[1], indices[2]};
            retainedSurface.radius = surfaceRadius;
            retainedSurface.center = surfaceCenter;
            if (retainDebugEdges)
            {
                // Keep the actual surface triangle.  The old decoder emitted one
                // degenerate triangle per serialized edge (p0, p1, p1).  That was
                // sufficient for drawing wire lines, but made collision raycasts,
                // walkability tests, and PATHZONE generation mathematically
                // impossible because every decoded triangle had zero area.
                BrxCollisionTriangle triangle;
                triangle.ownerObjectIndex = ownerObjectIndex;
                triangle.surfaceIndex = surface;
                triangle.cameraGeometry = cameraGeometry;
                for (int vertex = 0; vertex < 3; ++vertex)
                    for (int component = 0; component < 3; ++component)
                        triangle.positions[vertex][component] = positions[indices[vertex]][component];
                collisionTriangles_.push_back(triangle);
            }
            const auto surfaceEdgeCount = stream.U8();
            decodedEdges += surfaceEdgeCount;
            for (std::uint32_t edge = 0; edge < surfaceEdgeCount; ++edge)
            {
                const auto firstPosition = stream.U16();
                const auto secondPosition = stream.U16();
                const auto adjacentSurface = stream.U16();
                if (firstPosition >= positionCount || secondPosition >= positionCount ||
                    adjacentSurface >= surfaceCount)
                    throw std::runtime_error("Collision edge contains an invalid position or adjacent-surface index");
                retainedSurface.edges.push_back({firstPosition, secondPosition, adjacentSurface});
            }
            if (info != nullptr)
                info->surfaces.push_back(std::move(retainedSurface));
        }
        if (decodedEdges != edgeCount)
            throw std::runtime_error("Collision edge count does not match its surface records");
        return surfaceCount;
    };

    const auto parseBsp = [&](Reader &stream, std::uint16_t surfaceCount, BrxSoGeometryInfo *info)
    {
        const auto nodeCount = stream.U16();
        const auto fullNodeCount = stream.U16();
        // ClsgClipCylinderToBsp allocates cbspFull temporary BSP records and
        // PruneBsp may emit up to the active tree size. A smaller scratch count
        // is not merely cosmetic; it can overrun the runtime work array.
        if (fullNodeCount < nodeCount)
            throw std::runtime_error("Collision BSP full-node capacity is smaller than its active node count");
        if (info != nullptr)
            info->fullBspNodeCount = fullNodeCount;
        worldHeader_.parsedBspNodeCount += nodeCount;
        for (std::uint32_t node = 0; node < nodeCount; ++node)
        {
            const auto surfaceIndex = stream.U16();
            const auto negativeIndex = stream.U16();
            const auto positiveIndex = stream.U16();
            if (surfaceIndex >= surfaceCount || (negativeIndex != 0xffff && negativeIndex >= nodeCount) ||
                (positiveIndex != 0xffff && positiveIndex >= nodeCount))
                throw std::runtime_error("Collision BSP contains an invalid index");
            if (info != nullptr)
                info->bspNodes.push_back({surfaceIndex, negativeIndex, positiveIndex});
        }
    };

    const auto skipCurve = [](Reader &stream)
    {
        const auto kind = static_cast<std::int8_t>(stream.U8());
        if (kind != 0 && kind != 1)
            throw std::runtime_error("Curve has an invalid CRVK");
        stream.Skip(1); // closed
        const auto controlPointCount = stream.U8();
        const std::uint64_t bytesPerPoint = kind == 0 ? 16u : 40u;
        stream.Skip(static_cast<std::uint64_t>(controlPointCount) * bytesPerPoint);
    };

    objects_.clear();
    std::vector<glm::mat4> objectWorldTransforms;
    struct ProxyGeometrySource
    {
        std::size_t vertexBegin = 0;
        std::size_t vertexCount = 0;
        std::size_t indexBegin = 0;
        std::size_t indexCount = 0;
        std::size_t batchBegin = 0;
        std::size_t batchCount = 0;
        std::size_t celEdgeBegin = 0;
        std::size_t celEdgeCount = 0;
        std::size_t celBatchBegin = 0;
        std::size_t celBatchCount = 0;
        std::size_t objectBegin = 0;
        std::size_t objectCount = 0;
        std::size_t laserBeamBegin = 0;
        std::size_t laserBeamCount = 0;
        std::size_t globWarpBegin = 0;
        std::size_t globWarpCount = 0;
        std::size_t collisionBegin = 0;
        std::size_t collisionCount = 0;
        std::size_t lightBegin = 0;
        std::size_t lightCount = 0;
        std::size_t cameraBegin = 0;
        std::size_t cameraCount = 0;
        std::size_t checkpointBegin = 0;
        std::size_t checkpointCount = 0;
        std::size_t warpBegin = 0;
        std::size_t warpCount = 0;
        std::size_t exitBegin = 0;
        std::size_t exitCount = 0;
        std::size_t scanBegin = 0;
        std::size_t scanCount = 0;
        std::size_t shapePathBegin = 0;
        std::size_t shapePathCount = 0;
        std::size_t pathZoneBegin = 0;
        std::size_t pathZoneCount = 0;
        std::size_t effectBegin = 0;
        std::size_t effectCount = 0;
        std::size_t volumeBegin = 0;
        std::size_t volumeCount = 0;
        std::size_t animationSegmentBegin = 0;
        std::size_t animationSegmentCount = 0;
        std::size_t stateMachineBegin = 0;
        std::size_t stateMachineCount = 0;
        std::size_t reachMapBegin = 0;
        std::size_t reachMapCount = 0;
        std::size_t keyholeBegin = 0;
        std::size_t keyholeCount = 0;
        glm::mat4 proxyToWorld{1.0f};
        std::int32_t proxyRecordIndex = -1;
        std::int32_t proxyRootIndex = -1;
        // AddSwProxySource clones the hierarchy immediately, before the
        // defining PROXY's per-instance overrides are applied. Preserve that
        // pristine state instead of later cloning the mutated source instance.
        std::vector<BrxObjectRecord> objectSnapshots;
        std::vector<std::uint32_t> batchZoneMasks;
        std::vector<float> batchMaximumRenderDistances;
        std::vector<std::uint32_t> celZoneMasks;
        std::vector<float> celMaximumRenderDistances;
    };
    std::vector<ProxyGeometrySource> proxyGeometrySources;
    reader.Seek(static_cast<std::size_t>(worldHeader_.worldObjectsOffset + 2));
    bool stopped = false;
    const auto parseObjectList = [&](auto &&self, std::uint16_t count, std::int32_t parentIndex) -> void
    {
        for (std::uint32_t objectNumber = 0; objectNumber < count && !stopped; ++objectNumber)
        {
            BrxObjectRecord object;
            object.offset = reader.Offset();
            object.cid = reader.S16();
            object.oid = reader.S16();
            object.spliceIndex = reader.S16();
            object.parentIndex = parentIndex;
            object.dataOffset = reader.Offset();
            const auto objectIndex = static_cast<std::int32_t>(objects_.size());
            objects_.push_back(object);
            PropertyCaptureScope propertyCapture(objects_, static_cast<std::size_t>(objectIndex));
            if (object.cid == 56) // CHKPNT
                checkpoints_.push_back({static_cast<std::int32_t>(objectIndex), -1});
            currentObjectIndex = objectIndex;
            objectWorldTransforms.push_back(
                parentIndex >= 0 ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)] : glm::mat4(1.0f));
            const auto &loader = object.cid >= 0 && object.cid < static_cast<std::int16_t>(GetCidLoaders().size())
                                     ? GetCidLoaders()[object.cid]
                                     : CidLoaderInfo{};
            objects_[objectIndex].loaderName = loader.functionName;

            try
            {
                const auto usesLoader = [&](const char *name) { return std::strcmp(loader.functionName, name) == 0; };
                const bool delegatesToSo =
                    usesLoader("LoadBombFromBrx") || usesLoader("LoadBrkFromBrx") || usesLoader("LoadButtonFromBrx") ||
                    usesLoader("LoadChkpntFromBrx") || usesLoader("LoadClueFromBrx") || usesLoader("LoadDartFromBrx") ||
                    usesLoader("LoadFlyFromBrx") || usesLoader("LoadHbskFromBrx") || usesLoader("LoadJloFromBrx") ||
                    usesLoader("LoadJtFromBrx") || usesLoader("LoadLasenFromBrx") || usesLoader("LoadMbgFromBrx") ||
                    usesLoader("LoadMgvFromBrx") || usesLoader("LoadMissileFromBrx") ||
                    usesLoader("LoadPufferFromBrx") || usesLoader("LoadRatFromBrx") || usesLoader("LoadRocFromBrx") ||
                    usesLoader("LoadRohFromBrx") || usesLoader("LoadRopFromBrx") || usesLoader("LoadRostFromBrx") ||
                    usesLoader("LoadRovFromBrx") || usesLoader("LoadRubyFromBrx") || usesLoader("LoadRythmFromBrx") ||
                    usesLoader("LoadSprizeFromBrx") || usesLoader("LoadStepGuardFromBrx") ||
                    usesLoader("LoadVolbtnFromBrx");
                const bool delegatesToAlo = usesLoader("LoadDprizeFromBrx") || usesLoader("LoadJlocFromBrx") ||
                                            usesLoader("LoadLockFromBrx") || usesLoader("LoadLockgFromBrx");

                const auto parseAloPayload = [&](LoaderKind baseKind)
                {
                    const std::size_t collisionBegin = collisionTriangles_.size();
                    bool fixedPhysics = false;
                    if (baseKind == LoaderKind::So)
                    {
                        const auto soPrefixOffset = reader.Offset();
                        const auto physicsFlags = reader.U8();
                        fixedPhysics = (physicsFlags & 1u) != 0;
                        auto &so = objects_[objectIndex].so;
                        so.present = true;
                        so.physicsFlags = physicsFlags;
                        so.fixedPhysics = fixedPhysics;
                        objects_[objectIndex].details.push_back({"Fixed physics", fixedPhysics ? "true" : "false"});
                        const auto localSurfaceCount =
                            parseGeometry(reader, objectIndex, true, false, &so.collisionGeometry);
                        parseBsp(reader, localSurfaceCount, &so.collisionGeometry);
                        so.massSourceOffset = reader.Offset();
                        so.mass = reader.F32();
                        for (float &value : so.inertia)
                            value = reader.F32();
                        for (float &value : so.centerOfMass)
                            value = reader.F32();

                        const auto cameraGroupCount = reader.U16();
                        so.cameraGroups.resize(cameraGroupCount);
                        for (auto &group : so.cameraGroups)
                        {
                            group.cameraStyle = reader.S16();
                            group.globIndex = reader.U16();
                        }
                        const auto bspNodeMapCount = reader.U32();
                        so.bspNodeToCameraGroup.resize(bspNodeMapCount);
                        for (auto &groupIndex : so.bspNodeToCameraGroup)
                            groupIndex = reader.S16();
                        const auto hitSurfaceGroupCount = reader.U32();
                        so.hitSurfaceGroups.resize(hitSurfaceGroupCount);
                        for (auto &group : so.hitSurfaceGroups)
                        {
                            group.globIndex = reader.S16();
                            group.subglobIndex = reader.S16();
                        }
                        if (hitSurfaceGroupCount != 0)
                        {
                            so.surfaceToHitSurfaceGroup.resize(static_cast<std::size_t>(localSurfaceCount) + 1);
                            for (auto &groupIndex : so.surfaceToHitSurfaceGroup)
                                groupIndex = reader.S16();
                        }

                        const auto cameraSurfaceCount =
                            parseGeometry(reader, objectIndex, true, true, &so.cameraGeometry);
                        parseBsp(reader, cameraSurfaceCount, &so.cameraGeometry);
                        so.sourcePrefixOffset = soPrefixOffset;
                        so.sourcePrefixSize = reader.Offset() - soPrefixOffset;
                    }

                    objects_[objectIndex].aloPayloadOffset = reader.Offset();
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    const glm::mat4 &worldTransform = objectWorldTransforms[static_cast<std::size_t>(objectIndex)];
                    std::copy(glm::value_ptr(worldTransform), glm::value_ptr(worldTransform) + 16,
                              objects_[objectIndex].worldTransform);
                    // LoadSoFromBrx aliases fixed GEOM directly as geomWorld;
                    // those vertices are already authored in world space. Only
                    // non-fixed gameplay and camera GEOM receive the ALO world
                    // transform through UpdateGeomWorld.
                    if (!fixedPhysics)
                        for (std::size_t collisionIndex = collisionBegin; collisionIndex < collisionTriangles_.size();
                             ++collisionIndex)
                        {
                            for (auto &point : collisionTriangles_[collisionIndex].positions)
                            {
                                const glm::vec4 transformed =
                                    worldTransform * glm::vec4(point[0], point[1], point[2], 1.0f);
                                point[0] = transformed.x;
                                point[1] = transformed.y;
                                point[2] = transformed.z;
                            }
                        }
                    const auto zoneMode = reader.U8();
                    const auto visibilityMode = reader.U8();
                    const auto mrdMode = reader.U8();
                    const auto objectZoneMask = reader.U32();
                    float maximumRenderDistance = reader.F32();
                    float celBorderMaximumDistance = reader.F32();
                    const bool maximumRenderDistanceDefault =
                        maximumRenderDistance == (std::numeric_limits<float>::max)();
                    const bool celBorderMaximumDistanceDefault =
                        celBorderMaximumDistance == (std::numeric_limits<float>::max)();
                    const float renderRadiusSelf = reader.F32();
                    const float renderRadiusAll = reader.F32();
                    if (maximumRenderDistance == (std::numeric_limits<float>::max)())
                        maximumRenderDistance = 1.0e10f;
                    if (celBorderMaximumDistance == (std::numeric_limits<float>::max)())
                        celBorderMaximumDistance = (std::min)(maximumRenderDistance, 2000.0f);
                    objects_[objectIndex].hasAloPayload = true;
                    objects_[objectIndex].zoneMode = zoneMode;
                    objects_[objectIndex].visibilityMode = visibilityMode;
                    objects_[objectIndex].mrdMode = mrdMode;
                    objects_[objectIndex].zoneMask = objectZoneMask;
                    objects_[objectIndex].maximumRenderDistance = maximumRenderDistance;
                    objects_[objectIndex].celBorderMaximumDistance = celBorderMaximumDistance;
                    objects_[objectIndex].maximumRenderDistanceDefault = maximumRenderDistanceDefault;
                    objects_[objectIndex].celBorderMaximumDistanceDefault = celBorderMaximumDistanceDefault;
                    objects_[objectIndex].renderRadiusSelf = renderRadiusSelf;
                    objects_[objectIndex].renderRadiusAll = renderRadiusAll;
                    bool objectHidden = false;
                    if (object.cid == 81) // LIGHT
                    {
                        BrxLightRecord light;
                        light.ownerObjectIndex = objectIndex;
                        const auto &optionSchema = GetOptionSchema();
                        while (true)
                        {
                            const auto id = reader.S16();
                            if (id == -1)
                                break;
                            if (id < 0 || static_cast<std::size_t>(id) >= optionSchema.size() ||
                                !optionSchema[id].present)
                                throw std::runtime_error("Unknown LIGHT EOPID " + std::to_string(id));
                            const auto &option = optionSchema[id];
                            auto property = ReadProperty(reader, id, option);
                            objects_[objectIndex].properties.push_back(property);
                            if (property.kind == BrxPropertyKind::Dynamic)
                                continue;
                            switch (id)
                            {
                            case 125:
                                objectHidden = property.boolValue;
                                break;
                            case 491:
                                light.kind = static_cast<std::int16_t>(property.intValue);
                                break;
                            case 492:
                                std::copy_n(property.floatValue, 3, light.highlightHsv);
                                break;
                            case 493:
                                light.midtoneStrength = property.floatValue[0];
                                break;
                            case 494:
                                light.shadowStrength = property.floatValue[0];
                                break;
                            case 495:
                                light.highlightAngle = property.floatValue[0];
                                break;
                            case 496:
                                light.midtoneAngle = property.floatValue[0];
                                break;
                            case 497:
                                light.shadowAngle = property.floatValue[0];
                                break;
                            case 498:
                                std::copy_n(property.floatValue, 3, light.authoredDirection);
                                break;
                            case 499:
                            case 500:
                                light.dynamic = property.boolValue;
                                break;
                            case 501:
                                light.excludeDynamicObjects = property.boolValue;
                                break;
                            case 502:
                                light.falloff[0] = property.floatValue[0];
                                light.falloff[1] = property.floatValue[1];
                                break;
                            case 503:
                                light.coneAngle = property.floatValue[0];
                                break;
                            case 504:
                                light.hotSpotAngle = property.floatValue[0];
                                break;
                            case 505:
                                std::copy_n(property.floatValue, 3, light.up);
                                break;
                            default:
                                break;
                            }
                        }

                        const glm::mat4 &lightToWorld = objectWorldTransforms[static_cast<std::size_t>(objectIndex)];
                        const glm::vec3 position = glm::vec3(lightToWorld[3]);
                        // Match ProjectCane's BuildLightBlk: the submitted
                        // directional vector is xf.matWorld[2]. EOP 498 is an
                        // object property used while constructing that transform,
                        // not the final renderer-space vector.
                        glm::vec3 direction = glm::vec3(lightToWorld[2]);
                        if (glm::dot(direction, direction) > 0.000001f)
                            direction = glm::normalize(direction);
                        light.position[0] = position.x;
                        light.position[1] = position.y;
                        light.position[2] = position.z;
                        light.direction[0] = direction.x;
                        light.direction[1] = direction.y;
                        light.direction[2] = direction.z;
                        lights_.push_back(light);
                    }
                    else if (object.cid == 78) // CAMERA
                    {
                        BrxCameraRecord camera;
                        camera.ownerObjectIndex = objectIndex;
                        const auto &optionSchema = GetOptionSchema();
                        while (true)
                        {
                            const auto id = reader.S16();
                            if (id == -1)
                                break;
                            if (id < 0 || static_cast<std::size_t>(id) >= optionSchema.size() ||
                                !optionSchema[id].present)
                                throw std::runtime_error("Unknown CAMERA EOPID " + std::to_string(id));
                            const auto &option = optionSchema[id];
                            auto property = ReadProperty(reader, id, option);
                            objects_[objectIndex].properties.push_back(property);
                            if (property.kind == BrxPropertyKind::Dynamic)
                                continue;
                            switch (id)
                            {
                            case 125:
                                objectHidden = property.boolValue;
                                break;
                            case 602:
                                camera.targetOid = static_cast<std::int16_t>(property.intValue);
                                break;
                            case 603:
                                std::copy_n(property.floatValue, 3, camera.eye);
                                break;
                            case 604:
                                std::copy_n(property.floatValue, 3, camera.view);
                                break;
                            case 605:
                                std::copy_n(property.floatValue, 3, camera.up);
                                break;
                            default:
                                break;
                            }
                        }
                        cameras_.push_back(camera);
                    }
                    else
                    {
                        SkipOptionListCaptureInt16(reader, -32768, 0, &objectHidden);
                    }
                    const std::size_t firstOwnedBatch = renderBatches_.size();
                    objects_[objectIndex].globsetSourceOffset = reader.Offset();
                    const auto globsetPoseCount =
                        skipGlobset(reader, objectWorldTransforms[static_cast<std::size_t>(objectIndex)],
                                    visibilityMode, objectZoneMask, fixedPhysics, !objectHidden);
                    objects_[objectIndex].globPoseCount = globsetPoseCount;
                    objects_[objectIndex].globsetSourceSize =
                        reader.Offset() - objects_[objectIndex].globsetSourceOffset;
                    objects_[objectIndex].hidden = objectHidden;
                    if (renderBatches_.size() > firstOwnedBatch)
                    {
                        objects_[objectIndex].firstRenderGroup = renderBatches_[firstOwnedBatch].renderGroup;
                        objects_[objectIndex].renderGroupCount = 1;
                        for (std::size_t batchIndex = firstOwnedBatch + 1; batchIndex < renderBatches_.size();
                             ++batchIndex)
                            if (renderBatches_[batchIndex].renderGroup != renderBatches_[batchIndex - 1].renderGroup)
                                ++objects_[objectIndex].renderGroupCount;
                    }
                    parseAlox(reader, objects_[objectIndex].alox);
                    const auto poseConfigurationCount = reader.U8();
                    objects_[objectIndex].poseCombinations.clear();
                    objects_[objectIndex].poseCombinations.reserve(poseConfigurationCount);
                    for (std::uint32_t pose = 0; pose < poseConfigurationCount; ++pose)
                    {
                        BrxPoseCombinationRecord combination;
                        combination.oid = reader.S16();
                        combination.weights.resize(globsetPoseCount);
                        for (float &weight : combination.weights)
                            weight = reader.F32();
                        objects_[objectIndex].poseCombinations.push_back(std::move(combination));
                    }
                    const auto childCount = reader.U16();
                    self(self, childCount, objectIndex);
                };

                if (object.cid == 83) // PROXY
                {
                    // PROXY does not use the ordinary ALO serialization even
                    // though it derives from ALO. It owns a transform followed
                    // by an inline list of complete BRX objects (or references
                    // to previously registered proxy sources).
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    const glm::mat4 &proxyWorld = objectWorldTransforms[static_cast<std::size_t>(objectIndex)];
                    std::copy(glm::value_ptr(proxyWorld), glm::value_ptr(proxyWorld) + 16,
                              objects_[objectIndex].worldTransform);

                    const std::int32_t proxyRecordIndex = static_cast<std::int32_t>(proxies_.size());
                    BrxProxyRecord proxyRecord;
                    proxyRecord.ownerObjectIndex = objectIndex;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              proxyRecord.localTransform);
                    proxies_.push_back(std::move(proxyRecord));
                    objects_[objectIndex].proxyRecordIndex = proxyRecordIndex;

                    const auto proxyObjectCount = reader.U8();
                    objects_[objectIndex].details.push_back({"Proxy objects", std::to_string(proxyObjectCount)});
                    std::int32_t soleProxyRootIndex = -1;
                    for (std::uint32_t proxyObject = 0; proxyObject < proxyObjectCount; ++proxyObject)
                    {
                        const auto embeddedHeader = reader.Offset();
                        const auto embeddedCid = reader.S16();
                        if (embeddedCid == -1)
                        {
                            const auto sourceIndex = reader.U16();
                            if (sourceIndex >= proxyGeometrySources.size())
                                throw std::runtime_error("PROXY references an invalid source index");
                            const auto &source = proxyGeometrySources[sourceIndex];
                            const std::int32_t proxyRootIndex = static_cast<std::int32_t>(
                                proxies_[static_cast<std::size_t>(proxyRecordIndex)].roots.size());
                            BrxProxyRootRecord root;
                            root.kind = BrxProxyRootKind::SourceReference;
                            root.sourceListIndex = sourceIndex;
                            root.sourceProxyRecordIndex = source.proxyRecordIndex;
                            root.sourceProxyRootIndex = source.proxyRootIndex;
                            root.cloneCapacity = 0;
                            const glm::mat4 delta = objectWorldTransforms[static_cast<std::size_t>(objectIndex)] *
                                                    glm::inverse(source.proxyToWorld);
                            const std::size_t clonedVertexBegin = renderVertices_.size();
                            const std::size_t clonedBatchBegin = renderBatches_.size();
                            appendTransformedGeometry(source.vertexBegin, source.vertexCount, source.indexBegin,
                                                      source.indexCount, source.batchBegin, source.batchCount, delta);
                            const std::size_t clonedCelEdgeBegin = celEdges_.size();
                            for (std::size_t edgeOffset = 0; edgeOffset < source.celEdgeCount; ++edgeOffset)
                            {
                                BrxCelEdge edge = celEdges_[source.celEdgeBegin + edgeOffset];
                                for (auto &point : edge.points)
                                {
                                    const glm::vec4 transformed = delta * glm::vec4(point[0], point[1], point[2], 1.0f);
                                    point[0] = transformed.x;
                                    point[1] = transformed.y;
                                    point[2] = transformed.z;
                                    point[3] = 1.0f;
                                }
                                celEdges_.push_back(edge);
                            }
                            const std::size_t clonedCelBatchBegin = celBatches_.size();
                            const float cloneScale = (std::max)(
                                glm::length(glm::vec3(delta[0])),
                                (std::max)(glm::length(glm::vec3(delta[1])), glm::length(glm::vec3(delta[2]))));
                            for (std::size_t celOffset = 0; celOffset < source.celBatchCount; ++celOffset)
                            {
                                BrxCelBatch cel = celBatches_[source.celBatchBegin + celOffset];
                                cel.firstEdge = static_cast<std::uint32_t>(clonedCelEdgeBegin +
                                                                           (cel.firstEdge - source.celEdgeBegin));
                                const glm::vec4 center =
                                    delta * glm::vec4(cel.center[0], cel.center[1], cel.center[2], 1.0f);
                                cel.center[0] = center.x;
                                cel.center[1] = center.y;
                                cel.center[2] = center.z;
                                cel.radius *= cloneScale;
                                for (std::size_t batchOffset = 0; batchOffset < source.batchCount; ++batchOffset)
                                {
                                    if (renderBatches_[source.batchBegin + batchOffset].renderGroup == cel.renderGroup)
                                    {
                                        cel.renderGroup = renderBatches_[clonedBatchBegin + batchOffset].renderGroup;
                                        break;
                                    }
                                }
                                celBatches_.push_back(cel);
                            }

                            // PROXY references clone complete runtime object
                            // hierarchies in ProjectCane, not geometry alone.
                            // Recreate lightweight object identities so LASEN
                            // curves can bind to their own cloned OID 650/651
                            // templates instead of the source proxy's template.
                            const std::int32_t clonedObjectBase = static_cast<std::int32_t>(objects_.size());
                            root.instantiatedRootObjectIndex = clonedObjectBase;
                            root.objectBegin = static_cast<std::uint32_t>(clonedObjectBase);
                            root.objectCount = static_cast<std::uint32_t>(source.objectCount);
                            proxies_[static_cast<std::size_t>(proxyRecordIndex)].roots.push_back(root);
                            const std::size_t proxyCountBeforeNestedClone = proxies_.size();
                            std::unordered_map<std::int32_t, std::int32_t> nestedProxyRemap;
                            std::vector<std::int32_t> nestedProxyOrder;
                            for (std::size_t candidateProxyIndex = 0; candidateProxyIndex < proxyCountBeforeNestedClone;
                                 ++candidateProxyIndex)
                            {
                                const auto owner = proxies_[candidateProxyIndex].ownerObjectIndex;
                                if (owner < static_cast<std::int32_t>(source.objectBegin) ||
                                    owner >= static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                    continue;
                                const auto oldProxyIndex = static_cast<std::int32_t>(candidateProxyIndex);
                                nestedProxyRemap.emplace(oldProxyIndex, static_cast<std::int32_t>(
                                                                            proxies_.size() + nestedProxyOrder.size()));
                                nestedProxyOrder.push_back(oldProxyIndex);
                            }
                            for (std::size_t sourceOffset = 0; sourceOffset < source.objectCount; ++sourceOffset)
                            {
                                const std::size_t oldIndex = source.objectBegin + sourceOffset;
                                BrxObjectRecord clone = sourceOffset < source.objectSnapshots.size()
                                                            ? source.objectSnapshots[sourceOffset]
                                                            : objects_[oldIndex];
                                const std::int32_t oldParent = clone.parentIndex;
                                if (oldParent >= static_cast<std::int32_t>(source.objectBegin) &&
                                    oldParent < static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                {
                                    clone.parentIndex =
                                        clonedObjectBase + (oldParent - static_cast<std::int32_t>(source.objectBegin));
                                }
                                else
                                {
                                    clone.parentIndex = objectIndex;
                                }
                                if (clone.proxyRuntimeParentIndex >= static_cast<std::int32_t>(source.objectBegin) &&
                                    clone.proxyRuntimeParentIndex <
                                        static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                    clone.proxyRuntimeParentIndex =
                                        clonedObjectBase +
                                        (clone.proxyRuntimeParentIndex - static_cast<std::int32_t>(source.objectBegin));
                                if (clone.alox.editorFosterObjectIndex >=
                                        static_cast<std::int32_t>(source.objectBegin) &&
                                    clone.alox.editorFosterObjectIndex <
                                        static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                    clone.alox.editorFosterObjectIndex =
                                        clonedObjectBase + (clone.alox.editorFosterObjectIndex -
                                                            static_cast<std::int32_t>(source.objectBegin));
                                glm::mat4 oldWorld(1.0f);
                                std::memcpy(glm::value_ptr(oldWorld), clone.worldTransform, sizeof(float) * 16);
                                const glm::mat4 clonedWorld = delta * oldWorld;
                                std::copy(glm::value_ptr(clonedWorld), glm::value_ptr(clonedWorld) + 16,
                                          clone.worldTransform);
                                // RenderAloAll passes these radii directly to
                                // SphereInFrustum/FInsideCmMrd. CloneGlobset
                                // geometry is scaled by the PROXY delta, so the
                                // cloned ALO bounds must follow the same scale.
                                clone.renderRadiusSelf *= cloneScale;
                                clone.renderRadiusAll *= cloneScale;
                                clone.firstRenderGroup = 0xffffffffu;
                                clone.renderGroupCount = 0;
                                const auto nestedIdentity = nestedProxyRemap.find(clone.proxyRecordIndex);
                                if (nestedIdentity != nestedProxyRemap.end())
                                    clone.proxyRecordIndex = nestedIdentity->second;
                                else
                                {
                                    clone.proxyRecordIndex = proxyRecordIndex;
                                    clone.proxyRootIndex = proxyRootIndex;
                                }
                                clone.proxySourceObjectIndex = static_cast<std::int32_t>(oldIndex);
                                clone.proxyClone = true;
                                if (sourceOffset == 0)
                                    clone.proxyRuntimeParentIndex = parentIndex;
                                objects_.push_back(clone);
                                objectWorldTransforms.push_back(clonedWorld);
                            }

                            // CloneProxy clears its root list and ProjectCane rebuilds every nested
                            // PXR after the complete hierarchy exists. Mirror that repair here so
                            // duplicate OIDs and overrides retain their nearest nested-PROXY context.
                            for (const auto oldProxyIndex : nestedProxyOrder)
                            {
                                BrxProxyRecord nested = proxies_[static_cast<std::size_t>(oldProxyIndex)];
                                const auto mapObject = [&](std::int32_t &index)
                                {
                                    if (index >= static_cast<std::int32_t>(source.objectBegin) &&
                                        index < static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                        index =
                                            clonedObjectBase + (index - static_cast<std::int32_t>(source.objectBegin));
                                };
                                mapObject(nested.ownerObjectIndex);
                                for (auto &nestedRoot : nested.roots)
                                {
                                    mapObject(nestedRoot.instantiatedRootObjectIndex);
                                    if (nestedRoot.objectBegin >= source.objectBegin &&
                                        nestedRoot.objectBegin < source.objectBegin + source.objectCount)
                                        nestedRoot.objectBegin = static_cast<std::uint32_t>(
                                            clonedObjectBase + (static_cast<std::int32_t>(nestedRoot.objectBegin) -
                                                                static_cast<std::int32_t>(source.objectBegin)));
                                    if (const auto mapped = nestedProxyRemap.find(nestedRoot.sourceProxyRecordIndex);
                                        mapped != nestedProxyRemap.end())
                                        nestedRoot.sourceProxyRecordIndex = mapped->second;
                                }
                                for (auto &child : nested.attachedChildObjectIndices)
                                    mapObject(child);
                                for (auto &nestedOverride : nested.overrides)
                                    mapObject(nestedOverride.targetObjectIndex);
                                proxies_.push_back(std::move(nested));
                            }

                            for (std::size_t batchOffset = 0; batchOffset < source.batchCount; ++batchOffset)
                            {
                                auto &clonedBatch = renderBatches_[clonedBatchBegin + batchOffset];
                                if (batchOffset < source.batchZoneMasks.size())
                                    clonedBatch.zoneMask = source.batchZoneMasks[batchOffset];
                                if (batchOffset < source.batchMaximumRenderDistances.size())
                                    clonedBatch.maximumRenderDistance = source.batchMaximumRenderDistances[batchOffset];
                                const std::int32_t oldOwner = clonedBatch.ownerObjectIndex;
                                if (oldOwner >= static_cast<std::int32_t>(source.objectBegin) &&
                                    oldOwner < static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                {
                                    clonedBatch.ownerObjectIndex =
                                        clonedObjectBase + (oldOwner - static_cast<std::int32_t>(source.objectBegin));
                                }
                            }
                            for (std::size_t celOffset = 0; celOffset < source.celBatchCount; ++celOffset)
                            {
                                auto &clonedCel = celBatches_[clonedCelBatchBegin + celOffset];
                                if (celOffset < source.celZoneMasks.size())
                                    clonedCel.zoneMask = source.celZoneMasks[celOffset];
                                if (celOffset < source.celMaximumRenderDistances.size())
                                    clonedCel.surfaceMaximumDistance = source.celMaximumRenderDistances[celOffset];
                                const std::int32_t oldOwner = clonedCel.ownerObjectIndex;
                                if (oldOwner >= static_cast<std::int32_t>(source.objectBegin) &&
                                    oldOwner < static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                {
                                    clonedCel.ownerObjectIndex =
                                        clonedObjectBase + (oldOwner - static_cast<std::int32_t>(source.objectBegin));
                                }
                            }

                            // CloneAlo/CloneGlobset gives every referenced
                            // PROXY instance its own GLOBSET, bone bindings,
                            // and authoritative source records. Do not attach
                            // derived packets here: BrxDocument::OpenReadOnly
                            // performs that association once, after loading is
                            // complete. Pre-attaching them here makes that pass
                            // append every clone packet a second time and add
                            // empty synthetic SUBGLOB records, which leaves the
                            // cel silhouette animated while the filled proxy
                            // model remains on its rest geometry.
                            const std::size_t sourceGlobsetCount = globsets_.size();
                            const std::size_t sourceGlobCount = globs_.size();
                            for (std::size_t globsetIndex = 0; globsetIndex < sourceGlobsetCount; ++globsetIndex)
                            {
                                const BrxGlobsetRecord sourceGlobset = globsets_[globsetIndex];
                                if (sourceGlobset.ownerObjectIndex < static_cast<std::int32_t>(source.objectBegin) ||
                                    sourceGlobset.ownerObjectIndex >=
                                        static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                    continue;
                                BrxGlobsetRecord clonedGlobset = sourceGlobset;
                                clonedGlobset.ownerObjectIndex =
                                    clonedObjectBase +
                                    (sourceGlobset.ownerObjectIndex - static_cast<std::int32_t>(source.objectBegin));
                                clonedGlobset.globIndices.clear();
                                for (const std::size_t sourceGlobIndex : sourceGlobset.globIndices)
                                {
                                    if (sourceGlobIndex >= sourceGlobCount)
                                        continue;
                                    BrxGlobRecord clonedGlob = globs_[sourceGlobIndex];
                                    clonedGlob.ownerObjectIndex =
                                        clonedObjectBase +
                                        (clonedGlob.ownerObjectIndex - static_cast<std::int32_t>(source.objectBegin));
                                    glm::mat4 sourceToWorld(1.0f);
                                    std::memcpy(glm::value_ptr(sourceToWorld), clonedGlob.sourceToWorld,
                                                sizeof(clonedGlob.sourceToWorld));
                                    sourceToWorld = delta * sourceToWorld;
                                    std::memcpy(clonedGlob.sourceToWorld, glm::value_ptr(sourceToWorld),
                                                sizeof(clonedGlob.sourceToWorld));
                                    clonedGlob.derivedBatches.clear();
                                    clonedGlob.derivedCelPackets.clear();
                                    if (source.batchCount != 0)
                                    {
                                        for (std::size_t batchOffset = 0; batchOffset < source.batchCount;
                                             ++batchOffset)
                                        {
                                            const auto &sourceBatch = renderBatches_[source.batchBegin + batchOffset];
                                            if (sourceBatch.renderGroup != clonedGlob.renderGroup)
                                                continue;
                                            clonedGlob.renderGroup =
                                                renderBatches_[clonedBatchBegin + batchOffset].renderGroup;
                                            break;
                                        }
                                    }
                                    const std::size_t clonedGlobIndex = globs_.size();
                                    globs_.push_back(std::move(clonedGlob));
                                    clonedGlobset.globIndices.push_back(clonedGlobIndex);
                                }
                                globsets_.push_back(std::move(clonedGlobset));
                            }

                            // CloneGlobset deep-copies every WRBG and repairs
                            // its ALO/GLOB owner pointers.  Mirror that sidecar
                            // state for referenced PROXY hierarchies; copying
                            // only their geometry leaves instanced flags at the
                            // undeformed rest pose while direct water GLOBs work.
                            for (std::size_t warpOffset = 0; warpOffset < source.globWarpCount; ++warpOffset)
                            {
                                BrxGlobWarpRecord clone = globWarps_[source.globWarpBegin + warpOffset];
                                clone.firstVertex = static_cast<std::uint32_t>(
                                    clonedVertexBegin + (clone.firstVertex - source.vertexBegin));
                                if (clone.celEdgeCount != 0)
                                {
                                    clone.firstCelEdge = static_cast<std::uint32_t>(
                                        clonedCelEdgeBegin + (clone.firstCelEdge - source.celEdgeBegin));
                                }
                                if (clone.ownerObjectIndex >= static_cast<std::int32_t>(source.objectBegin) &&
                                    clone.ownerObjectIndex <
                                        static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                {
                                    clone.ownerObjectIndex =
                                        clonedObjectBase +
                                        (clone.ownerObjectIndex - static_cast<std::int32_t>(source.objectBegin));
                                }
                                for (std::size_t batchOffset = 0; batchOffset < source.batchCount; ++batchOffset)
                                {
                                    if (renderBatches_[source.batchBegin + batchOffset].renderGroup ==
                                        clone.renderGroup)
                                    {
                                        clone.renderGroup = renderBatches_[clonedBatchBegin + batchOffset].renderGroup;
                                        break;
                                    }
                                }
                                glm::mat4 oldObjectToWorld(1.0f);
                                std::memcpy(glm::value_ptr(oldObjectToWorld), clone.objectToWorld,
                                            sizeof(clone.objectToWorld));
                                const glm::mat4 clonedObjectToWorld = delta * oldObjectToWorld;
                                std::memcpy(clone.objectToWorld, glm::value_ptr(clonedObjectToWorld),
                                            sizeof(clone.objectToWorld));
                                globWarps_.push_back(clone);
                            }

                            for (std::size_t beamOffset = 0; beamOffset < source.laserBeamCount; ++beamOffset)
                            {
                                BrxLaserBeamRecord clone = laserBeams_[source.laserBeamBegin + beamOffset];
                                clone.ownerObjectIndex =
                                    clonedObjectBase +
                                    (clone.ownerObjectIndex - static_cast<std::int32_t>(source.objectBegin));
                                clone.shapeObjectIndex =
                                    clonedObjectBase +
                                    (clone.shapeObjectIndex - static_cast<std::int32_t>(source.objectBegin));
                                // SHAPE CVs remain in their LASEN parent's local
                                // space when a proxy hierarchy is cloned.  The
                                // game applies the clone's LASEN transform later
                                // in RenderLasenSelf through ConvertAloPos.
                                laserBeams_.push_back(std::move(clone));
                            }
                            const auto remapOwner = [&](const std::int32_t owner)
                            {
                                if (owner >= static_cast<std::int32_t>(source.objectBegin) &&
                                    owner < static_cast<std::int32_t>(source.objectBegin + source.objectCount))
                                    return clonedObjectBase + (owner - static_cast<std::int32_t>(source.objectBegin));
                                return owner;
                            };
                            for (std::size_t offset = 0; offset < source.collisionCount; ++offset)
                            {
                                BrxCollisionTriangle clone = collisionTriangles_[source.collisionBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                for (auto &point : clone.positions)
                                {
                                    const glm::vec4 transformed = delta * glm::vec4(point[0], point[1], point[2], 1.0f);
                                    point[0] = transformed.x;
                                    point[1] = transformed.y;
                                    point[2] = transformed.z;
                                }
                                collisionTriangles_.push_back(clone);
                            }
                            for (std::size_t offset = 0; offset < source.lightCount; ++offset)
                            {
                                BrxLightRecord clone = lights_[source.lightBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                const glm::vec4 position =
                                    delta * glm::vec4(clone.position[0], clone.position[1], clone.position[2], 1.0f);
                                glm::vec3 direction =
                                    glm::mat3(delta) *
                                    glm::vec3(clone.direction[0], clone.direction[1], clone.direction[2]);
                                if (glm::dot(direction, direction) > 0.000001f)
                                    direction = glm::normalize(direction);
                                clone.position[0] = position.x;
                                clone.position[1] = position.y;
                                clone.position[2] = position.z;
                                clone.direction[0] = direction.x;
                                clone.direction[1] = direction.y;
                                clone.direction[2] = direction.z;
                                lights_.push_back(clone);
                            }
                            for (std::size_t offset = 0; offset < source.cameraCount; ++offset)
                            {
                                BrxCameraRecord clone = cameras_[source.cameraBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                cameras_.push_back(clone);
                            }
                            for (std::size_t offset = 0; offset < source.checkpointCount; ++offset)
                            {
                                BrxCheckpointRecord clone = checkpoints_[source.checkpointBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                checkpoints_.push_back(clone);
                            }
                            for (std::size_t offset = 0; offset < source.warpCount; ++offset)
                            {
                                BrxWarpRecord clone = warps_[source.warpBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                warps_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.exitCount; ++offset)
                            {
                                BrxExitRecord clone = exits_[source.exitBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                exits_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.scanCount; ++offset)
                            {
                                BrxScanRecord clone = scans_[source.scanBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                scans_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.animationSegmentCount; ++offset)
                            {
                                BrxAsegRecord clone = animationSegments_[source.animationSegmentBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                animationSegments_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.stateMachineCount; ++offset)
                            {
                                BrxSmRecord clone = stateMachines_[source.stateMachineBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                for (auto &transition : clone.transitions)
                                    transition.animationObjectIndex = remapOwner(transition.animationObjectIndex);
                                stateMachines_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.reachMapCount; ++offset)
                            {
                                BrxRchmRecord clone = reachMaps_[source.reachMapBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                reachMaps_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.keyholeCount; ++offset)
                            {
                                BrxKeyholeRecord clone = keyholes_[source.keyholeBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                keyholes_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.shapePathCount; ++offset)
                            {
                                BrxShapePathRecord clone = shapePaths_[source.shapePathBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                shapePaths_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.pathZoneCount; ++offset)
                            {
                                BrxPathZoneRecord clone = pathZones_[source.pathZoneBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                pathZones_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.effectCount; ++offset)
                            {
                                BrxEffectRecord clone = effects_[source.effectBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                effects_.push_back(std::move(clone));
                            }
                            for (std::size_t offset = 0; offset < source.volumeCount; ++offset)
                            {
                                BrxVolumeRecord clone = volumes_[source.volumeBegin + offset];
                                clone.ownerObjectIndex = remapOwner(clone.ownerObjectIndex);
                                volumes_.push_back(std::move(clone));
                            }
                            // LoadProxyFromBrx subsequently calls
                            // LoadSwObjectsFromBrx and reparents those objects
                            // to proxyObjs[0] when this proxy contains one
                            // source.  For a referenced source, proxyObjs[0] is
                            // this cloned hierarchy root—not the PROXY wrapper.
                            // Elevator LASEN paths are supplied by that
                            // per-instance child block.
                            if (proxyObjectCount == 1 && source.objectCount != 0)
                                soleProxyRootIndex = clonedObjectBase;
                            continue;
                        }

                        // Reuse the normal CID dispatcher for the embedded
                        // object, including any hierarchy it owns.
                        reader.Seek(embeddedHeader);
                        const auto sourceVertexBegin = renderVertices_.size();
                        const auto sourceIndexBegin = renderIndices_.size();
                        const auto sourceBatchBegin = renderBatches_.size();
                        const auto sourceCelEdgeBegin = celEdges_.size();
                        const auto sourceCelBatchBegin = celBatches_.size();
                        const auto sourceObjectBegin = objects_.size();
                        const auto sourceLaserBeamBegin = laserBeams_.size();
                        const auto sourceGlobWarpBegin = globWarps_.size();
                        const auto sourceCollisionBegin = collisionTriangles_.size();
                        const auto sourceLightBegin = lights_.size();
                        const auto sourceCameraBegin = cameras_.size();
                        const auto sourceCheckpointBegin = checkpoints_.size();
                        const auto sourceWarpBegin = warps_.size();
                        const auto sourceExitBegin = exits_.size();
                        const auto sourceScanBegin = scans_.size();
                        const auto sourceShapePathBegin = shapePaths_.size();
                        const auto sourcePathZoneBegin = pathZones_.size();
                        const auto sourceEffectBegin = effects_.size();
                        const auto sourceVolumeBegin = volumes_.size();
                        const auto sourceAnimationSegmentBegin = animationSegments_.size();
                        const auto sourceStateMachineBegin = stateMachines_.size();
                        const auto sourceReachMapBegin = reachMaps_.size();
                        const auto sourceKeyholeBegin = keyholes_.size();
                        const auto sourceObjectIndex = static_cast<std::int32_t>(objects_.size());
                        const std::int32_t proxyRootIndex = static_cast<std::int32_t>(
                            proxies_[static_cast<std::size_t>(proxyRecordIndex)].roots.size());
                        self(self, 1, objectIndex);
                        if (stopped)
                            break;
                        const auto cloneCount = reader.U16();
                        if (cloneCount < 1)
                            throw std::runtime_error("PROXY source has an invalid clone count");
                        BrxProxyRootRecord root;
                        root.kind = BrxProxyRootKind::InlineSource;
                        root.sourceListIndex = static_cast<std::int32_t>(proxyGeometrySources.size());
                        root.sourceProxyRecordIndex = proxyRecordIndex;
                        root.sourceProxyRootIndex = proxyRootIndex;
                        root.instantiatedRootObjectIndex = sourceObjectIndex;
                        root.objectBegin = static_cast<std::uint32_t>(sourceObjectBegin);
                        root.objectCount = static_cast<std::uint32_t>(objects_.size() - sourceObjectBegin);
                        root.cloneCapacity = cloneCount;
                        proxies_[static_cast<std::size_t>(proxyRecordIndex)].roots.push_back(root);
                        const auto belongsToNestedProxy = [&](const BrxObjectRecord &sourceObject)
                        {
                            if (sourceObject.proxyRecordIndex < 0 ||
                                sourceObject.proxyRecordIndex == proxyRecordIndex ||
                                static_cast<std::size_t>(sourceObject.proxyRecordIndex) >= proxies_.size())
                                return false;

                            // A child may arrive with an inherited identity from a previously
                            // registered reusable source.  That is not a nested PROXY.  Preserve
                            // an identity only when its PROXY wrapper is physically contained in
                            // this newly decoded source hierarchy.
                            const auto nestedOwner =
                                proxies_[static_cast<std::size_t>(sourceObject.proxyRecordIndex)].ownerObjectIndex;
                            return nestedOwner >= static_cast<std::int32_t>(sourceObjectBegin) &&
                                   nestedOwner < static_cast<std::int32_t>(objects_.size());
                        };
                        for (std::size_t sourceOffset = sourceObjectBegin; sourceOffset < objects_.size();
                             ++sourceOffset)
                        {
                            // Preserve the nearest PROXY identity for nested
                            // PROXY hierarchies, but replace stale inherited
                            // identities on ordinary descendants. Membership
                            // in this outer instance is represented by the root
                            // object range.
                            if (!belongsToNestedProxy(objects_[sourceOffset]) || sourceOffset == sourceObjectBegin)
                            {
                                objects_[sourceOffset].proxyRecordIndex = proxyRecordIndex;
                                objects_[sourceOffset].proxyRootIndex = proxyRootIndex;
                                objects_[sourceOffset].proxySourceObjectIndex = static_cast<std::int32_t>(sourceOffset);
                                objects_[sourceOffset].proxyClone = false;
                            }
                            if (sourceOffset == sourceObjectBegin)
                                objects_[sourceOffset].proxyRuntimeParentIndex = parentIndex;
                        }
                        proxyGeometrySources.push_back({sourceVertexBegin,
                                                        renderVertices_.size() - sourceVertexBegin,
                                                        sourceIndexBegin,
                                                        renderIndices_.size() - sourceIndexBegin,
                                                        sourceBatchBegin,
                                                        renderBatches_.size() - sourceBatchBegin,
                                                        sourceCelEdgeBegin,
                                                        celEdges_.size() - sourceCelEdgeBegin,
                                                        sourceCelBatchBegin,
                                                        celBatches_.size() - sourceCelBatchBegin,
                                                        sourceObjectBegin,
                                                        objects_.size() - sourceObjectBegin,
                                                        sourceLaserBeamBegin,
                                                        laserBeams_.size() - sourceLaserBeamBegin,
                                                        sourceGlobWarpBegin,
                                                        globWarps_.size() - sourceGlobWarpBegin,
                                                        sourceCollisionBegin,
                                                        collisionTriangles_.size() - sourceCollisionBegin,
                                                        sourceLightBegin,
                                                        lights_.size() - sourceLightBegin,
                                                        sourceCameraBegin,
                                                        cameras_.size() - sourceCameraBegin,
                                                        sourceCheckpointBegin,
                                                        checkpoints_.size() - sourceCheckpointBegin,
                                                        sourceWarpBegin,
                                                        warps_.size() - sourceWarpBegin,
                                                        sourceExitBegin,
                                                        exits_.size() - sourceExitBegin,
                                                        sourceScanBegin,
                                                        scans_.size() - sourceScanBegin,
                                                        sourceShapePathBegin,
                                                        shapePaths_.size() - sourceShapePathBegin,
                                                        sourcePathZoneBegin,
                                                        pathZones_.size() - sourcePathZoneBegin,
                                                        sourceEffectBegin,
                                                        effects_.size() - sourceEffectBegin,
                                                        sourceVolumeBegin,
                                                        volumes_.size() - sourceVolumeBegin,
                                                        sourceAnimationSegmentBegin,
                                                        animationSegments_.size() - sourceAnimationSegmentBegin,
                                                        sourceStateMachineBegin,
                                                        stateMachines_.size() - sourceStateMachineBegin,
                                                        sourceReachMapBegin,
                                                        reachMaps_.size() - sourceReachMapBegin,
                                                        sourceKeyholeBegin,
                                                        keyholes_.size() - sourceKeyholeBegin,
                                                        objectWorldTransforms[static_cast<std::size_t>(objectIndex)],
                                                        proxyRecordIndex,
                                                        proxyRootIndex});
                        auto &registeredSource = proxyGeometrySources.back();
                        registeredSource.objectSnapshots.assign(
                            objects_.begin() + static_cast<std::ptrdiff_t>(sourceObjectBegin),
                            objects_.begin() + static_cast<std::ptrdiff_t>(objects_.size()));
                        for (std::size_t batchIndex = sourceBatchBegin; batchIndex < renderBatches_.size();
                             ++batchIndex)
                        {
                            registeredSource.batchZoneMasks.push_back(renderBatches_[batchIndex].zoneMask);
                            registeredSource.batchMaximumRenderDistances.push_back(
                                renderBatches_[batchIndex].maximumRenderDistance);
                        }
                        for (std::size_t celIndex = sourceCelBatchBegin; celIndex < celBatches_.size(); ++celIndex)
                        {
                            registeredSource.celZoneMasks.push_back(celBatches_[celIndex].zoneMask);
                            registeredSource.celMaximumRenderDistances.push_back(
                                celBatches_[celIndex].surfaceMaximumDistance);
                        }
                        soleProxyRootIndex = sourceObjectIndex;
                    }
                    if (stopped)
                        continue;

                    // Per-object proxy overrides. These are applied to objects
                    // found inside the inline proxy hierarchy at runtime.
                    const auto overrideCount = reader.U8();
                    objects_[objectIndex].details.push_back({"Proxy overrides", std::to_string(overrideCount)});
                    PropertyCapturePause overridePropertyCapture;
                    for (std::uint32_t overrideIndex = 0; overrideIndex < overrideCount; ++overrideIndex)
                    {
                        BrxProxyOverrideRecord overrideRecord;
                        overrideRecord.targetOid = reader.S16();
                        overrideRecord.replacementSpliceIndex = reader.S16();
                        overrideRecord.zoneMask = reader.U32();
                        overrideRecord.mrdMode = static_cast<std::int8_t>(reader.U8());
                        if (overrideRecord.mrdMode == 2)
                            overrideRecord.maximumRenderDistance = reader.F32();
                        overrideRecord.targetObjectIndex =
                            ProxyRuntime::ResolveOverrideTarget(document, proxyRecordIndex, overrideRecord.targetOid);
                        if (overrideRecord.targetObjectIndex >= 0)
                        {
                            const auto &candidate =
                                objects_[static_cast<std::size_t>(overrideRecord.targetObjectIndex)];
                            overrideRecord.inheritedSpliceIndex = candidate.spliceIndex;
                            overrideRecord.inheritedZoneMask = candidate.zoneMask;
                            overrideRecord.inheritedMrdMode = candidate.mrdMode;
                            overrideRecord.inheritedMaximumRenderDistance = candidate.maximumRenderDistance;
                            overrideRecord.inheritedPropertiesCaptured = true;
                            overrideRecord.inheritedProperties = candidate.properties;
                        }
                        const auto &optionSchema = GetOptionSchema();
                        while (true)
                        {
                            const auto id = reader.S16();
                            if (id == -1)
                                break;
                            if (id < 0 || static_cast<std::size_t>(id) >= optionSchema.size() ||
                                !optionSchema[static_cast<std::size_t>(id)].present)
                                throw std::runtime_error("Unknown PROXY override EOPID " + std::to_string(id));
                            overrideRecord.properties.push_back(
                                ReadProperty(reader, id, optionSchema[static_cast<std::size_t>(id)]));
                        }
                        if (overrideRecord.targetObjectIndex >= 0)
                        {
                            auto &target = objects_[static_cast<std::size_t>(overrideRecord.targetObjectIndex)];
                            if (overrideRecord.replacementSpliceIndex != -1)
                                target.spliceIndex = overrideRecord.replacementSpliceIndex;
                            const bool targetIsAlo = CidDerivesFrom(target.cid, 2);
                            if (targetIsAlo)
                                target.zoneMask = overrideRecord.zoneMask;
                            if (overrideRecord.mrdMode == 2)
                            {
                                // PROXY stores an override command, not an ALO MRDS value.
                                // ProjectCane leaves the cloned source MRD untouched for 0/1;
                                // only command 2 installs a per-instance object distance.
                                target.mrdMode = 2;
                                target.maximumRenderDistance =
                                    overrideRecord.maximumRenderDistance == (std::numeric_limits<float>::max)()
                                        ? 1.0e10f
                                        : overrideRecord.maximumRenderDistance;
                            }
                            for (const auto &overrideProperty : overrideRecord.properties)
                            {
                                if (IsAppendSetUserOption(overrideProperty.eopid))
                                {
                                    // ProjectCane executes every Add* option in stream order,
                                    // preserving both inherited and instance-added entries.
                                    target.properties.push_back(overrideProperty);
                                }
                                else
                                {
                                    // A stored SetUser option assigns one field.  If malformed
                                    // source data repeated it, the last runtime assignment wins.
                                    std::erase_if(target.properties, [&](const BrxPropertyRecord &candidate)
                                                  { return candidate.eopid == overrideProperty.eopid; });
                                    target.properties.push_back(overrideProperty);
                                }
                                if (overrideProperty.eopid == 125)
                                    target.hidden = overrideProperty.boolValue;
                            }
                            for (auto &batch : renderBatches_)
                            {
                                if (batch.ownerObjectIndex != overrideRecord.targetObjectIndex)
                                    continue;
                                if (targetIsAlo && target.cid != 4 && target.visibilityMode == 2u)
                                    batch.zoneMask = overrideRecord.zoneMask;
                                if (overrideRecord.mrdMode == 2)
                                    batch.maximumRenderDistance = target.maximumRenderDistance;
                            }
                            for (auto &cel : celBatches_)
                            {
                                if (cel.ownerObjectIndex != overrideRecord.targetObjectIndex)
                                    continue;
                                if (targetIsAlo && target.cid != 4 && target.visibilityMode == 2u)
                                    cel.zoneMask = overrideRecord.zoneMask;
                                if (overrideRecord.mrdMode == 2)
                                    cel.surfaceMaximumDistance = target.maximumRenderDistance;
                            }
                        }
                        proxies_[static_cast<std::size_t>(proxyRecordIndex)].overrides.push_back(
                            std::move(overrideRecord));
                    }

                    // The original loader only accepts an additional child
                    // hierarchy when the proxy contains exactly one source.
                    if (proxyObjectCount == 1)
                    {
                        const auto childCount = reader.U16();
                        const std::size_t childObjectBegin = objects_.size();
                        self(self, childCount, soleProxyRootIndex >= 0 ? soleProxyRootIndex : objectIndex);
                        auto &attached =
                            proxies_[static_cast<std::size_t>(proxyRecordIndex)].attachedChildObjectIndices;
                        const std::int32_t attachedParent = soleProxyRootIndex >= 0 ? soleProxyRootIndex : objectIndex;
                        for (std::size_t childIndex = childObjectBegin; childIndex < objects_.size(); ++childIndex)
                            if (objects_[childIndex].parentIndex == attachedParent)
                                attached.push_back(static_cast<std::int32_t>(childIndex));
                        if (stopped)
                            continue;
                    }
                }
                else if (loader.kind == LoaderKind::Lo)
                {
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadXfmFromBrx"))
                {
                    // XFM serializes a complete local frame: a 3x3 basis followed
                    // by a local-space position.  These markers drive CLUE motion,
                    // WARP destinations, BUTTON stops, targeting and aiming, so
                    // discarding the bytes turns every marker into an identity
                    // transform and silently breaks all of those consumers.
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    objects_[objectIndex].details.push_back(
                        {"Transform marker", std::to_string(localTransform[3][0]) + ", " +
                                                 std::to_string(localTransform[3][1]) + ", " +
                                                 std::to_string(localTransform[3][2])});
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadPntFromBrx"))
                {
                    // PNT, HPNT, JMT and SPIRE all share LoadPntFromBrx:
                    // one local-space position followed by the ordinary LO
                    // option list.  Dispatch by the vtable loader rather than
                    // hard-coding CID_PNT so every derived point is consumed.
                    glm::mat4 localTransform(1.0f);
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    objects_[objectIndex].details.push_back(
                        {"Point position", std::to_string(localTransform[3][0]) + ", " +
                                               std::to_string(localTransform[3][1]) + ", " +
                                               std::to_string(localTransform[3][2])});
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadScanFromBrx"))
                {
                    // SCAN delegates to PNT and then appends its length-prefixed
                    // binocular display string.
                    glm::mat4 localTransform(1.0f);
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    SkipOptionList(reader);
                    const std::string scanText = reader.StringU16();
                    scans_.push_back({static_cast<std::int32_t>(objectIndex), scanText});
                    objects_[objectIndex].details.push_back(
                        {"Point position", std::to_string(localTransform[3][0]) + ", " +
                                               std::to_string(localTransform[3][1]) + ", " +
                                               std::to_string(localTransform[3][2])});
                    objects_[objectIndex].details.push_back({"Scan text", scanText});
                }
                else if (object.cid == 134) // VISMAP
                {
                    const auto visibilityNodeCount = reader.U16();
                    worldHeader_.visibilityNodeCount = visibilityNodeCount;
                    visibilityNodes_.clear();
                    visibilityNodes_.reserve(visibilityNodeCount);
                    for (std::uint32_t node = 0; node < visibilityNodeCount; ++node)
                    {
                        BrxVisibilityNode visibilityNode;
                        for (float &component : visibilityNode.normal)
                            component = reader.F32();
                        visibilityNode.planeDistance = reader.F32();
                        const auto negativeChild = static_cast<std::int32_t>(reader.U32());
                        visibilityNode.negativeChild = negativeChild;
                        visibilityNode.negativeZoneMask = reader.U32();
                        const auto positiveChild = static_cast<std::int32_t>(reader.U32());
                        visibilityNode.positiveChild = positiveChild;
                        visibilityNode.positiveZoneMask = reader.U32();
                        if ((negativeChild != -1 && (negativeChild < 0 || negativeChild >= visibilityNodeCount)) ||
                            (positiveChild != -1 && (positiveChild < 0 || positiveChild >= visibilityNodeCount)))
                            throw std::runtime_error("VISMAP contains an invalid child node index");
                        visibilityNodes_.push_back(visibilityNode);
                    }
                    worldHeader_.visibilityZoneCount = reader.U16();
                    if (worldHeader_.visibilityZoneCount > 32)
                        throw std::runtime_error("VISMAP one-hop zone count exceeds 32-bit masks");
                    visibilityOneHopMasks_.resize(worldHeader_.visibilityZoneCount);
                    for (auto &mask : visibilityOneHopMasks_)
                        mask = reader.U32();
                    SkipOptionList(reader);
                }
                else if (object.cid >= 110 && object.cid <= 114) // SHAPE family
                {
                    const auto curveKind = static_cast<std::int8_t>(reader.U8());
                    if (curveKind != 0 && curveKind != 1)
                        throw std::runtime_error("Curve has an invalid CRVK");
                    const bool closed = reader.U8() != 0;
                    const auto controlPointCount = reader.U8();
                    objects_[objectIndex].details.push_back({"Curve kind", curveKind == 0 ? "Linear" : "Hermite"});
                    objects_[objectIndex].details.push_back({"Closed", closed ? "Yes" : "No"});
                    objects_[objectIndex].details.push_back({"Control points", std::to_string(controlPointCount)});
                    std::vector<float> curveParameters;
                    std::vector<glm::vec3> curvePoints;
                    std::vector<glm::vec3> curveTangentsIn;
                    std::vector<glm::vec3> curveTangentsOut;
                    curveParameters.reserve(controlPointCount);
                    curvePoints.reserve(controlPointCount);
                    curveTangentsIn.reserve(controlPointCount);
                    curveTangentsOut.reserve(controlPointCount);
                    for (std::uint32_t point = 0; point < controlPointCount; ++point)
                    {
                        curveParameters.push_back(reader.F32());
                        // Keep stream reads sequenced. Function-argument evaluation
                        // order is not guaranteed, and MSVC may otherwise consume
                        // serialized Z/Y/X while constructing glm::vec3.
                        const float x = reader.F32();
                        const float y = reader.F32();
                        const float z = reader.F32();
                        curvePoints.emplace_back(x, y, z);
                        if (curveKind == 1)
                        {
                            const float inX = reader.F32();
                            const float inY = reader.F32();
                            const float inZ = reader.F32();
                            const float outX = reader.F32();
                            const float outY = reader.F32();
                            const float outZ = reader.F32();
                            curveTangentsIn.emplace_back(inX, inY, inZ);
                            curveTangentsOut.emplace_back(outX, outY, outZ);
                        }
                    }
                    BrxShapePathRecord shapePath;
                    shapePath.ownerObjectIndex = objectIndex;
                    shapePath.closed = closed;
                    shapePath.hermite = curveKind == 1;
                    shapePath.parameters = std::move(curveParameters);
                    for (std::size_t pointIndex = 0; pointIndex < curvePoints.size(); ++pointIndex)
                    {
                        const auto &point = curvePoints[pointIndex];
                        shapePath.points.push_back({point.x, point.y, point.z});
                        if (curveKind == 1)
                        {
                            const auto &tangentIn = curveTangentsIn[pointIndex];
                            const auto &tangentOut = curveTangentsOut[pointIndex];
                            shapePath.tangentsIn.push_back({tangentIn.x, tangentIn.y, tangentIn.z});
                            shapePath.tangentsOut.push_back({tangentOut.x, tangentOut.y, tangentOut.z});
                        }
                    }
                    shapePaths_.push_back(std::move(shapePath));
                    std::int32_t lasenIndex = parentIndex;
                    while (lasenIndex >= 0 && objects_[static_cast<std::size_t>(lasenIndex)].cid != 49)
                        lasenIndex = objects_[static_cast<std::size_t>(lasenIndex)].parentIndex;
                    const auto beamCount =
                        lasenIndex < 0 ? std::size_t{}
                                       : static_cast<std::size_t>(std::count_if(
                                             laserBeams_.begin(), laserBeams_.end(), [&](const BrxLaserBeamRecord &beam)
                                             { return beam.ownerObjectIndex == lasenIndex; }));
                    // BindLasen uses FIsBasicDerivedFrom(CID_SHAPE), so all five
                    // shape vtables are valid beam paths: SHAPE, HSHAPE, PIPE,
                    // RAIL and LANDING (CID 110..114).  The elevator uses a
                    // derived shape rather than an exact CID_SHAPE object.
                    // It still requires a direct child with a linear CRVL and
                    // the retail fixed LBEAM array remains capped at sixteen.
                    if (object.cid >= 110 && object.cid <= 114 && curveKind == 0 && lasenIndex >= 0 &&
                        parentIndex == lasenIndex && !curvePoints.empty() && beamCount < 16)
                    {
                        BrxLaserBeamRecord beam;
                        beam.ownerObjectIndex = lasenIndex;
                        beam.shapeObjectIndex = objectIndex;
                        for (const glm::vec3 &localPoint : curvePoints)
                            beam.points.push_back({localPoint.x, localPoint.y, localPoint.z});
                        laserBeams_.push_back(std::move(beam));
                    }
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadVolFromBrx"))
                {
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);

                    BrxVolumeRecord volume;
                    volume.ownerObjectIndex = objectIndex;
                    const auto surfaceCount = reader.U16();
                    volume.planes.reserve(surfaceCount);
                    for (std::uint32_t surface = 0; surface < surfaceCount; ++surface)
                    {
                        BrxVolumePlane plane;
                        plane.distance = reader.F32();
                        for (float &component : plane.normal)
                            component = reader.F32();
                        volume.planes.push_back(plane);
                    }
                    const auto nodeCount = reader.U16();
                    volume.nodes.reserve(nodeCount);
                    for (std::uint32_t node = 0; node < nodeCount; ++node)
                        volume.nodes.push_back({reader.U16(), reader.U16(), reader.U16()});
                    objects_[objectIndex].details.push_back({"Volume planes", std::to_string(surfaceCount)});
                    objects_[objectIndex].details.push_back({"Volume BSP nodes", std::to_string(nodeCount)});
                    volumes_.push_back(std::move(volume));
                    SkipOptionList(reader);
                }
                else if (object.cid == 138) // PATHZONE
                {
                    SkipOptionList(reader);
                    const auto vertexCount = reader.U16();
                    const auto boundaryEdgeCount = reader.U16();
                    const auto interiorEdgeCount = reader.U16();
                    const auto edgeCount = static_cast<std::uint32_t>(boundaryEdgeCount) + interiorEdgeCount;
                    const auto triangleCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Vertices", std::to_string(vertexCount)});
                    objects_[objectIndex].details.push_back({"Boundary edges", std::to_string(boundaryEdgeCount)});
                    objects_[objectIndex].details.push_back({"Interior edges", std::to_string(interiorEdgeCount)});
                    objects_[objectIndex].details.push_back({"Triangles", std::to_string(triangleCount)});
                    BrxPathZoneRecord pathZone;
                    pathZone.ownerObjectIndex = objectIndex;
                    pathZone.boundaryEdgeCount = boundaryEdgeCount;
                    pathZone.vertices.reserve(vertexCount);
                    pathZone.vertexEdges.reserve(vertexCount);
                    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex)
                    {
                        const float x = reader.F32();
                        const float y = reader.F32();
                        const float z = reader.F32();
                        pathZone.vertices.push_back({x, y, z});
                        const auto adjacencyCount = reader.U8();
                        // ProjectCane stores this byte into an int8_t before
                        // iterating it. Values 128..255 therefore cannot
                        // describe a valid runtime adjacency list and would
                        // desynchronize the remainder of the PATHZONE payload.
                        if (adjacencyCount > 127)
                            throw std::runtime_error("PATHZONE vertex adjacency exceeds the runtime signed-U8 limit");
                        std::vector<std::uint16_t> adjacency;
                        adjacency.reserve(adjacencyCount);
                        for (std::uint32_t adjacent = 0; adjacent < adjacencyCount; ++adjacent)
                            adjacency.push_back(reader.U16());
                        pathZone.vertexEdges.push_back(std::move(adjacency));
                    }
                    pathZone.edges.reserve(edgeCount);
                    for (std::uint32_t edge = 0; edge < edgeCount; ++edge)
                        pathZone.edges.push_back({reader.U16(), reader.U16()});
                    pathZone.triangles.reserve(triangleCount);
                    for (std::uint32_t triangle = 0; triangle < triangleCount; ++triangle)
                        pathZone.triangles.push_back({reader.U16(), reader.U16(), reader.U16()});
                    const auto bspCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Path BSP nodes", std::to_string(bspCount)});
                    pathZone.bspNodes.reserve(bspCount);
                    for (std::uint32_t bsp = 0; bsp < bspCount; ++bsp)
                    {
                        BrxPathZoneRecord::BspNode node;
                        for (float &component : node.normal)
                            component = reader.F32();
                        node.distance = reader.F32();
                        node.negativeChild = reader.S16();
                        node.positiveChild = reader.S16();
                        pathZone.bspNodes.push_back(node);
                    }
                    pathZones_.push_back(std::move(pathZone));
                }
                else if (object.cid == 142) // KEYHOLE
                {
                    SkipOptionList(reader);
                    const auto positionCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Keyhole positions", std::to_string(positionCount)});
                    BrxKeyholeRecord keyhole;
                    keyhole.ownerObjectIndex = objectIndex;
                    keyhole.positions.reserve(positionCount);
                    for (std::uint32_t position = 0; position < positionCount; ++position)
                        keyhole.positions.push_back({reader.F32(), -reader.F32()});
                    keyhole.minimum = {reader.F32(), reader.F32()};
                    keyhole.maximum = {reader.F32(), reader.F32()};
                    for (int layer = 0; layer < 5; ++layer)
                    {
                        const auto triangleCount = reader.U16();
                        keyhole.layers[static_cast<std::size_t>(layer)].reserve(triangleCount);
                        for (std::uint32_t triangle = 0; triangle < triangleCount; ++triangle)
                            keyhole.layers[static_cast<std::size_t>(layer)].push_back(
                                {reader.U16(), reader.U16(), reader.U16()});
                    }
                    keyholes_.push_back(std::move(keyhole));
                }
                else if (object.cid == 96) // FLASH : ALO + collision TBSP
                {
                    parseAloPayload(LoaderKind::Alo);
                    if (stopped)
                        continue;
                    // LoadFlashFromBrx appends LoadTbspFromBrx, not GEOM plus
                    // the GEOM collision BSP.  TBSP stores plane equations and
                    // six-byte nodes only; interpreting it as vertex/edge data
                    // desynchronizes the stream and reports a false edge-count
                    // mismatch.
                    BrxVolumeRecord volume;
                    volume.ownerObjectIndex = objectIndex;
                    const auto surfaceCount = reader.U16();
                    volume.planes.reserve(surfaceCount);
                    for (std::uint32_t surface = 0; surface < surfaceCount; ++surface)
                    {
                        BrxVolumePlane plane;
                        plane.distance = reader.F32();
                        for (float &component : plane.normal)
                            component = reader.F32();
                        volume.planes.push_back(plane);
                    }
                    const auto nodeCount = reader.U16();
                    volume.nodes.reserve(nodeCount);
                    for (std::uint32_t node = 0; node < nodeCount; ++node)
                        volume.nodes.push_back({reader.U16(), reader.U16(), reader.U16()});
                    objects_[objectIndex].details.push_back({"FLASH TBSP surfaces", std::to_string(surfaceCount)});
                    objects_[objectIndex].details.push_back({"FLASH TBSP nodes", std::to_string(nodeCount)});
                    volumes_.push_back(std::move(volume));
                }
                else if (delegatesToSo)
                {
                    // These class loaders perform runtime initialization only
                    // after delegating their complete serialized payload to SO.
                    parseAloPayload(LoaderKind::So);
                    if (stopped)
                        continue;
                }
                else if (delegatesToAlo)
                {
                    // These loaders consume exactly the ordinary ALO payload;
                    // their remaining work does not read from the BRX stream.
                    parseAloPayload(LoaderKind::Alo);
                    if (stopped)
                        continue;
                }
                else if (usesLoader("LoadCnvoFromBrx"))
                {
                    parseAloPayload(LoaderKind::So);
                    if (stopped)
                        continue;
                    // parseAloPayload recursively appends this ALO's children,
                    // so `object` is only the original temporary record copied
                    // into objects_ before recursion. Store the CNVO tail on
                    // the authoritative document record. Writing the temporary
                    // left both averages at zero and made MatchCnvoScrollerToBeltSpeed
                    // produce a permanently static (0, 0) UV velocity.
                    auto &conveyor = objects_[static_cast<std::size_t>(objectIndex)];
                    conveyor.hasCnvoPayload = true;
                    conveyor.cnvoAverageUVelocity = reader.F32();
                    conveyor.cnvoAverageVVelocity = reader.F32();
                }
                else if (usesLoader("LoadEmitterFromBrx"))
                {
                    // LoadEmitterFromBrx first consumes a complete ALO. EOPID
                    // 366 is EMITTER::emitok and selects the optional origin
                    // mesh serialized after the ALO hierarchy. EOPID 421 is
                    // the corresponding EXPLO property, not an EMITTER alias.
                    objects_[objectIndex].aloPayloadOffset = reader.Offset();
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    const glm::mat4 &emitterWorld = objectWorldTransforms[static_cast<std::size_t>(objectIndex)];
                    std::copy(glm::value_ptr(emitterWorld), glm::value_ptr(emitterWorld) + 16,
                              objects_[objectIndex].worldTransform);
                    const auto zoneMode = reader.U8();
                    const auto visibilityMode = reader.U8();
                    const auto mrdMode = reader.U8();
                    const auto objectZoneMask = reader.U32();
                    float maximumRenderDistance = reader.F32();
                    float celBorderMaximumDistance = reader.F32();
                    const bool maximumRenderDistanceDefault =
                        maximumRenderDistance == (std::numeric_limits<float>::max)();
                    const bool celBorderMaximumDistanceDefault =
                        celBorderMaximumDistance == (std::numeric_limits<float>::max)();
                    const float renderRadiusSelf = reader.F32();
                    const float renderRadiusAll = reader.F32();
                    if (maximumRenderDistance == (std::numeric_limits<float>::max)())
                        maximumRenderDistance = 1.0e10f;
                    if (celBorderMaximumDistance == (std::numeric_limits<float>::max)())
                        celBorderMaximumDistance = (std::min)(maximumRenderDistance, 2000.0f);
                    objects_[objectIndex].hasAloPayload = true;
                    objects_[objectIndex].zoneMode = zoneMode;
                    objects_[objectIndex].visibilityMode = visibilityMode;
                    objects_[objectIndex].mrdMode = mrdMode;
                    objects_[objectIndex].zoneMask = objectZoneMask;
                    objects_[objectIndex].maximumRenderDistance = maximumRenderDistance;
                    objects_[objectIndex].celBorderMaximumDistance = celBorderMaximumDistance;
                    objects_[objectIndex].maximumRenderDistanceDefault = maximumRenderDistanceDefault;
                    objects_[objectIndex].celBorderMaximumDistanceDefault = celBorderMaximumDistanceDefault;
                    objects_[objectIndex].renderRadiusSelf = renderRadiusSelf;
                    objects_[objectIndex].renderRadiusAll = renderRadiusAll;
                    bool objectHidden = false;
                    const auto emitterOriginKind = SkipOptionListCaptureInt16(reader, 366, 0, &objectHidden);
                    const std::size_t firstOwnedBatch = renderBatches_.size();
                    objects_[objectIndex].globsetSourceOffset = reader.Offset();
                    const auto globsetPoseCount =
                        skipGlobset(reader, objectWorldTransforms[static_cast<std::size_t>(objectIndex)],
                                    visibilityMode, objectZoneMask, false, !objectHidden);
                    objects_[objectIndex].globPoseCount = globsetPoseCount;
                    objects_[objectIndex].globsetSourceSize =
                        reader.Offset() - objects_[objectIndex].globsetSourceOffset;
                    objects_[objectIndex].hidden = objectHidden;
                    if (renderBatches_.size() > firstOwnedBatch)
                    {
                        objects_[objectIndex].firstRenderGroup = renderBatches_[firstOwnedBatch].renderGroup;
                        objects_[objectIndex].renderGroupCount = 1;
                    }
                    parseAlox(reader, objects_[objectIndex].alox);
                    const auto poseConfigurationCount = reader.U8();
                    objects_[objectIndex].poseCombinations.clear();
                    objects_[objectIndex].poseCombinations.reserve(poseConfigurationCount);
                    for (std::uint32_t pose = 0; pose < poseConfigurationCount; ++pose)
                    {
                        BrxPoseCombinationRecord combination;
                        combination.oid = reader.S16();
                        combination.weights.resize(globsetPoseCount);
                        for (float &weight : combination.weights)
                            weight = reader.F32();
                        objects_[objectIndex].poseCombinations.push_back(std::move(combination));
                    }
                    const auto childCount = reader.U16();
                    self(self, childCount, objectIndex);
                    if (stopped)
                        continue;

                    const auto curveKind = static_cast<std::int8_t>(reader.U8());
                    BrxEffectRecord effect;
                    effect.ownerObjectIndex = objectIndex;
                    effect.originKind = emitterOriginKind;
                    effect.curveKind = curveKind;
                    if (curveKind != -1)
                    {
                        if (curveKind != 0 && curveKind != 1)
                            throw std::runtime_error("Emitter curve has an invalid CRVK");
                        effect.curveClosed = reader.U8() != 0;
                        const auto controlPointCount = reader.U8();
                        effect.curvePoints.reserve(controlPointCount);
                        for (std::uint32_t pointIndex = 0; pointIndex < controlPointCount; ++pointIndex)
                        {
                            BrxEffectCurvePoint point;
                            point.u = reader.F32();
                            for (float &component : point.position)
                                component = reader.F32();
                            if (curveKind == 1)
                            {
                                for (float &component : point.tangentIn)
                                    component = reader.F32();
                                for (float &component : point.tangentOut)
                                    component = reader.F32();
                            }
                            effect.curvePoints.push_back(point);
                        }
                    }

                    if (emitterOriginKind == 4) // EMITOK_Mesh
                    {
                        const auto positionCount = reader.U16();
                        effect.meshPositions.reserve(positionCount);
                        for (std::uint32_t positionIndex = 0; positionIndex < positionCount; ++positionIndex)
                            effect.meshPositions.push_back({reader.F32(), reader.F32(), reader.F32()});
                        const auto triangleCount = reader.U16();
                        effect.meshTriangles.reserve(triangleCount);
                        for (std::uint32_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
                        {
                            BrxEffectMeshTriangle triangle;
                            triangle.indices = {reader.U16(), reader.U16(), reader.U16()};
                            triangle.area = reader.F32();
                            effect.meshTriangles.push_back(triangle);
                        }
                        for (float &component : effect.meshCenter)
                            component = reader.F32();
                    }

                    const auto colorCount = reader.U16();
                    if (colorCount != 0)
                    {
                        effect.colorsAreRanges = reader.U8() != 0;
                        effect.colors.reserve(colorCount);
                        for (std::uint32_t colorIndex = 0; colorIndex < colorCount; ++colorIndex)
                            effect.colors.push_back({reader.F32(), reader.F32(), reader.F32(), reader.F32()});
                    }
                    objects_[objectIndex].details.push_back({"Effect origin", std::to_string(emitterOriginKind)});
                    objects_[objectIndex].details.push_back(
                        {"Effect curve CVs", std::to_string(effect.curvePoints.size())});
                    objects_[objectIndex].details.push_back(
                        {"Effect mesh", std::to_string(effect.meshPositions.size()) + " vertices, " +
                                            std::to_string(effect.meshTriangles.size()) + " triangles"});
                    objects_[objectIndex].details.push_back({"Effect colors", std::to_string(effect.colors.size())});
                    effects_.push_back(std::move(effect));
                }
                else if (usesLoader("LoadExploFromBrx"))
                {
                    // EXPLO begins with XFM, but option 421 selects the
                    // emitter-origin representation and therefore controls
                    // whether an inline mesh follows.
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    const auto emitterOriginKind = SkipOptionListCaptureInt16(reader, 421, 0);

                    const auto curveKind = static_cast<std::int8_t>(reader.U8());
                    BrxEffectRecord effect;
                    effect.ownerObjectIndex = objectIndex;
                    effect.originKind = emitterOriginKind;
                    effect.curveKind = curveKind;
                    if (curveKind != -1)
                    {
                        if (curveKind != 0 && curveKind != 1)
                            throw std::runtime_error("Explosion curve has an invalid CRVK");
                        effect.curveClosed = reader.U8() != 0;
                        const auto controlPointCount = reader.U8();
                        effect.curvePoints.reserve(controlPointCount);
                        for (std::uint32_t pointIndex = 0; pointIndex < controlPointCount; ++pointIndex)
                        {
                            BrxEffectCurvePoint point;
                            point.u = reader.F32();
                            for (float &component : point.position)
                                component = reader.F32();
                            if (curveKind == 1)
                            {
                                for (float &component : point.tangentIn)
                                    component = reader.F32();
                                for (float &component : point.tangentOut)
                                    component = reader.F32();
                            }
                            effect.curvePoints.push_back(point);
                        }
                    }

                    if (emitterOriginKind == 4) // EMITOK_Mesh
                    {
                        const auto positionCount = reader.U16();
                        effect.meshPositions.reserve(positionCount);
                        for (std::uint32_t positionIndex = 0; positionIndex < positionCount; ++positionIndex)
                            effect.meshPositions.push_back({reader.F32(), reader.F32(), reader.F32()});
                        const auto triangleCount = reader.U16();
                        effect.meshTriangles.reserve(triangleCount);
                        for (std::uint32_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
                        {
                            BrxEffectMeshTriangle triangle;
                            triangle.indices = {reader.U16(), reader.U16(), reader.U16()};
                            triangle.area = reader.F32();
                            effect.meshTriangles.push_back(triangle);
                        }
                        for (float &component : effect.meshCenter)
                            component = reader.F32();
                    }

                    const auto colorCount = reader.U16();
                    if (colorCount != 0)
                    {
                        effect.colorsAreRanges = reader.U8() != 0;
                        effect.colors.reserve(colorCount);
                        for (std::uint32_t colorIndex = 0; colorIndex < colorCount; ++colorIndex)
                            effect.colors.push_back({reader.F32(), reader.F32(), reader.F32(), reader.F32()});
                    }
                    objects_[objectIndex].details.push_back({"Effect origin", std::to_string(emitterOriginKind)});
                    objects_[objectIndex].details.push_back(
                        {"Effect curve CVs", std::to_string(effect.curvePoints.size())});
                    objects_[objectIndex].details.push_back(
                        {"Effect mesh", std::to_string(effect.meshPositions.size()) + " vertices, " +
                                            std::to_string(effect.meshTriangles.size()) + " triangles"});
                    objects_[objectIndex].details.push_back({"Effect colors", std::to_string(effect.colors.size())});
                    effects_.push_back(std::move(effect));
                }
                else if (usesLoader("LoadExplgFromBrx"))
                {
                    // EXPLG starts as an XFM, then embeds complete LO records.
                    // Their six-byte prefix is the normal CID/OID/splice header.
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    SkipOptionList(reader);
                    const auto embeddedCount = reader.S16();
                    if (embeddedCount < 0)
                        throw std::runtime_error("EXPLG has a negative embedded-object count");
                    self(self, static_cast<std::uint16_t>(embeddedCount), objectIndex);
                    if (stopped)
                        continue;
                }
                else if (usesLoader("LoadAsegFromBrx"))
                {
                    // ASEG owns animation channels rather than an ALO/LO
                    // payload. Controller kinds and event records are
                    // serialized without lengths, so mirror the original
                    // factories and LoadAsegFromBrx exactly.
                    const auto readScalarController = [&](std::int8_t kind) -> BrxAsegControllerSummary
                    {
                        BrxAsegControllerSummary summary;
                        summary.kind = kind;
                        switch (kind)
                        {
                        case 0: // ACGB: Bezier scalar keys
                        {
                            summary.keyCount = reader.U16();
                            summary.scalarKeys.reserve(summary.keyCount);
                            for (std::uint32_t keyIndex = 0; keyIndex < summary.keyCount; ++keyIndex)
                            {
                                BrxAsegScalarKey key;
                                key.time = reader.S16() * (1.0f / 60.0f);
                                key.value = reader.F32();
                                key.tangentInKind = static_cast<std::int8_t>(reader.U8());
                                key.tangentOutKind = static_cast<std::int8_t>(reader.U8());
                                key.tangentIn = reader.F32();
                                key.tangentOut = reader.F32();
                                summary.scalarKeys.push_back(key);
                            }
                            break;
                        }
                        case 1: // ACGBW: weighted Bezier scalar keys
                        {
                            summary.keyCount = reader.U16();
                            summary.scalarKeys.reserve(summary.keyCount);
                            for (std::uint32_t keyIndex = 0; keyIndex < summary.keyCount; ++keyIndex)
                            {
                                BrxAsegScalarKey key;
                                key.time = reader.S16() * (1.0f / 60.0f);
                                key.value = reader.F32();
                                key.tangentInKind = static_cast<std::int8_t>(reader.U8());
                                key.tangentOutKind = static_cast<std::int8_t>(reader.U8());
                                key.tangentInTime = reader.F32();
                                key.tangentIn = key.value - reader.F32();
                                key.tangentOutTime = reader.F32();
                                key.tangentOut = key.value + reader.F32();
                                summary.scalarKeys.push_back(key);
                            }
                            break;
                        }
                        case 2: // ACGL: linear scalar keys
                        {
                            summary.keyCount = reader.U16();
                            summary.scalarKeys.reserve(summary.keyCount);
                            for (std::uint32_t keyIndex = 0; keyIndex < summary.keyCount; ++keyIndex)
                            {
                                BrxAsegScalarKey key;
                                key.time = reader.S16() * (1.0f / 60.0f);
                                key.value = reader.F32();
                                summary.scalarKeys.push_back(key);
                            }
                            break;
                        }
                        case 3: // blend-twist controller, runtime generated
                        case 4: // blend-pose controller, runtime generated
                            break;
                        default:
                            throw std::runtime_error("ASEG has an invalid ACGK " + std::to_string(kind));
                        }
                        return summary;
                    };

                    const auto readVectorController = [&](std::int8_t kind)
                    {
                        BrxAsegControllerSummary summary;
                        summary.kind = kind;
                        switch (kind)
                        {
                        case 0: // component: flags + default vec3 + optional ACG per component
                        {
                            auto componentFlags = reader.U8();
                            summary.componentMask = componentFlags;
                            for (float &value : summary.defaultValue)
                                value = reader.F32();
                            for (int component = 0; component < 3; ++component)
                            {
                                if ((componentFlags & 1u) != 0)
                                {
                                    auto scalar = readScalarController(static_cast<std::int8_t>(reader.U8()));
                                    summary.keyCount = static_cast<std::uint16_t>(summary.keyCount + scalar.keyCount);
                                    summary.components[component].kind = scalar.kind;
                                    summary.components[component].keys = std::move(scalar.scalarKeys);
                                }
                                componentFlags >>= 1;
                            }
                            break;
                        }
                        case 1: // Bezier: S16 frame + three vec3 values
                        {
                            summary.keyCount = reader.U16();
                            summary.vectorKeys.reserve(summary.keyCount);
                            for (std::uint32_t keyIndex = 0; keyIndex < summary.keyCount; ++keyIndex)
                            {
                                BrxAsegVectorKey key;
                                key.time = reader.S16() * (1.0f / 60.0f);
                                for (float &value : key.value)
                                    value = reader.F32();
                                for (float &value : key.tangentIn)
                                    value = reader.F32();
                                for (float &value : key.tangentOut)
                                    value = reader.F32();
                                summary.vectorKeys.push_back(key);
                            }
                            break;
                        }
                        case 2: // ACPBL/ACRBL are runtime-generated only
                            break;
                        default:
                            throw std::runtime_error("ASEG has an invalid ACVK " + std::to_string(kind));
                        }
                        return summary;
                    };

                    BrxAsegRecord segment;
                    segment.ownerObjectIndex = objectIndex;
                    segment.runtimeBlendClass = objects_[objectIndex].cid == 132;
                    const auto readEventList = [&](const BrxAsegEventLane lane)
                    {
                        const auto eventCount = static_cast<std::int8_t>(reader.U8());
                        if (eventCount < 0)
                            throw std::runtime_error("ASEG has a negative event count");
                        for (int event = 0; event < eventCount; ++event)
                        {
                            BrxAsegEventRecord record;
                            record.lane = lane;
                            record.type = static_cast<std::int8_t>(reader.U8());
                            if (lane == BrxAsegEventLane::Frame)
                                record.frame = reader.S16();
                            switch (record.type)
                            {
                            case -1:
                                break;
                            case 0:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.integers[0] = reader.S32();
                                record.integers[1] = reader.S32();
                                record.floats[0] = reader.F32();
                                record.floats[1] = reader.F32();
                                record.floats[2] = reader.F32();
                                break;
                            case 4:
                            case 7:
                                record.shorts[0] = reader.S16();
                                record.floats[0] = reader.F32();
                                break;
                            case 5:
                            case 33:
                            case 57:
                            case 58:
                                record.floats[0] = reader.F32();
                                break;
                            case 6:
                            case 12:
                            case 13:
                            case 17:
                            case 18:
                            case 22:
                            case 24:
                            case 29:
                            case 30:
                            case 31:
                            case 32:
                            case 35:
                            case 36:
                            case 39:
                            case 41:
                            case 59:
                                record.shorts[0] = reader.S16();
                                break;
                            case 9:
                            case 10:
                                record.shorts[0] = reader.S16();
                                record.floats[0] = reader.F32();
                                break;
                            case 16:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = static_cast<std::int8_t>(reader.U8());
                                record.shorts[2] = static_cast<std::int8_t>(reader.U8());
                                break;
                            case 19:
                            case 20:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.U8();
                                break;
                            case 21:
                            case 23:
                                record.shorts[0] = reader.S16();
                                record.floats[0] = reader.F32();
                                record.floats[1] = reader.F32();
                                break;
                            case 25:
                            case 26:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.text = reader.FixedString(32);
                                break;
                            case 27:
                                record.shorts[0] = reader.U8();
                                record.shorts[1] = reader.S16();
                                break;
                            case 28:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.floats[0] = reader.F32();
                                record.floats[1] = reader.F32();
                                break;
                            case 37:
                            case 38:
                            case 45:
                            case 47:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                break;
                            case 40:
                                record.shorts[0] = static_cast<std::int16_t>(reader.U16());
                                break;
                            case 44:
                            case 51:
                                record.text = reader.FixedString(32);
                                break;
                            case 46:
                            case 52:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.shorts[2] = reader.S16();
                                break;
                            case 48:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.shorts[2] = reader.S16();
                                record.floats[0] = reader.F32();
                                break;
                            case 54:
                                record.shorts[0] = reader.S16();
                                record.shorts[1] = reader.S16();
                                record.text = reader.StringU16();
                                break;
                            case 1:
                            case 2:
                            case 3:
                            case 8:
                            case 11:
                            case 14:
                            case 15:
                            case 34:
                            case 42:
                            case 43:
                            case 49:
                            case 50:
                            case 53:
                            case 55:
                            case 56:
                                break;
                            default:
                                throw std::runtime_error("ASEG has an invalid event type " +
                                                         std::to_string(record.type));
                            }
                            segment.events.push_back(std::move(record));
                        }
                    };

                    segment.rootOid = reader.S16();
                    segment.maximumFrame = reader.S16();
                    objects_[objectIndex].details.push_back({"Animation root OID", std::to_string(segment.rootOid)});
                    objects_[objectIndex].details.push_back({"Maximum frame", std::to_string(segment.maximumFrame)});
                    const auto searchRootCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Search roots", std::to_string(searchRootCount)});
                    segment.searchRootOids.reserve(searchRootCount);
                    for (std::uint32_t i = 0; i < searchRootCount; ++i)
                        segment.searchRootOids.push_back(reader.S16());
                    const auto channelCount = static_cast<std::int8_t>(reader.U8());
                    if (channelCount < 0)
                        throw std::runtime_error("ASEG has a negative channel count");
                    objects_[objectIndex].details.push_back({"Animation channels", std::to_string(channelCount)});
                    for (int channel = 0; channel < channelCount; ++channel)
                    {
                        BrxAsegChannelRecord channelRecord;
                        channelRecord.targetOid = reader.S16();
                        const auto flags = reader.U8();
                        channelRecord.flags = flags;
                        if ((flags & 0x01u) != 0)
                            channelRecord.position = readVectorController(static_cast<std::int8_t>(reader.U8()));
                        if ((flags & 0x02u) != 0)
                            channelRecord.rotation = readVectorController(static_cast<std::int8_t>(reader.U8()));
                        if ((flags & 0x04u) != 0)
                            channelRecord.scale = readVectorController(static_cast<std::int8_t>(reader.U8()));
                        if ((flags & 0x08u) != 0)
                            channelRecord.twist = readScalarController(static_cast<std::int8_t>(reader.U8()));
                        if ((flags & 0x10u) != 0)
                        {
                            const auto poseControllerCount = reader.U8();
                            for (std::uint32_t pose = 0; pose < poseControllerCount; ++pose)
                            {
                                const auto kind = static_cast<std::int8_t>(reader.U8());
                                if (kind != -1)
                                    channelRecord.poses.push_back(readScalarController(kind));
                                else
                                    channelRecord.poses.push_back({});
                            }
                        }
                        channelRecord.stripChannel = (flags & 0x20u) != 0;
                        // Flag 0x20 only identifies the strip channel.
                        if ((flags & 0xc0u) != 0)
                            throw std::runtime_error("ASEG channel has unsupported flag bits");
                        segment.channels.push_back(std::move(channelRecord));
                    }
                    readEventList(BrxAsegEventLane::Apply);
                    readEventList(BrxAsegEventLane::Frame);
                    readEventList(BrxAsegEventLane::Retract);
                    SkipOptionList(reader);
                    animationSegments_.push_back(std::move(segment));
                }
                else if (usesLoader("LoadHndFromBrx"))
                {
                    // HND derives from XFM and begins with the identical marker
                    // frame before its option list.
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadExitFromBrx"))
                {
                    // EXIT is not serialized as a normal ALO. ProjectCane's
                    // LoadExitFromBrx reads two fixed-size level names, then
                    // an XFM, collision BSP, EOP list, and inline ASEGs.
                    const auto destinationLevel = reader.FixedString(32);
                    const auto alternateLevel = reader.FixedString(32);
                    BrxExitRecord exit;
                    exit.ownerObjectIndex = objectIndex;
                    exit.destinationLevel = destinationLevel;
                    exit.alternateLevel = alternateLevel;
                    objects_[objectIndex].details.push_back(
                        {"Destination level", destinationLevel.empty() ? "(default)" : destinationLevel});
                    objects_[objectIndex].details.push_back(
                        {"Alternate level", alternateLevel.empty() ? "(none)" : alternateLevel});

                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    const glm::mat4 exitWorld = parentTransform * localTransform;
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = exitWorld;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(exitWorld), glm::value_ptr(exitWorld) + 16,
                              objects_[objectIndex].worldTransform);

                    BrxVolumeRecord volume;
                    volume.ownerObjectIndex = objectIndex;
                    const auto surfaceCount = reader.U16();
                    volume.planes.reserve(surfaceCount);
                    for (std::uint32_t surface = 0; surface < surfaceCount; ++surface)
                    {
                        BrxVolumePlane plane;
                        plane.distance = reader.F32();
                        for (float &component : plane.normal)
                            component = reader.F32();
                        volume.planes.push_back(plane);
                    }
                    const auto nodeCount = reader.U16();
                    volume.nodes.reserve(nodeCount);
                    for (std::uint32_t node = 0; node < nodeCount; ++node)
                        volume.nodes.push_back({reader.U16(), reader.U16(), reader.U16()});
                    objects_[objectIndex].details.push_back({"Exit volume planes", std::to_string(surfaceCount)});
                    objects_[objectIndex].details.push_back({"Exit volume BSP nodes", std::to_string(nodeCount)});
                    volumes_.push_back(std::move(volume));
                    SkipOptionList(reader);
                    const auto animationCount = reader.S16();
                    if (animationCount < 0)
                        throw std::runtime_error("EXIT has a negative embedded-ASEG count");
                    objects_[objectIndex].details.push_back({"Embedded animations", std::to_string(animationCount)});
                    exit.embeddedAnimationCount = animationCount;
                    exits_.push_back(std::move(exit));
                    for (int animation = 0; animation < animationCount; ++animation)
                    {
                        self(self, 1, objectIndex);
                        if (stopped)
                            break;
                    }
                    if (stopped)
                        continue;
                }
                else if (usesLoader("LoadWarpFromBrx"))
                {
                    BrxWarpRecord warp;
                    warp.ownerObjectIndex = objectIndex;
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    const glm::mat4 warpWorld = parentTransform * localTransform;
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = warpWorld;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(warpWorld), glm::value_ptr(warpWorld) + 16,
                              objects_[objectIndex].worldTransform);
                    SkipOptionList(reader);

                    if (g_objectProperties != nullptr)
                    {
                        for (const auto &property : *g_objectProperties)
                        {
                            switch (property.eopid)
                            {
                            case 153:
                                std::copy_n(property.floatValue, 3, warp.velocity);
                                break;
                            case 154:
                                warp.velocity[0] = property.floatValue[0];
                                break;
                            case 155:
                                warp.velocity[1] = property.floatValue[0];
                                break;
                            case 156:
                                warp.velocity[2] = property.floatValue[0];
                                break;
                            case 157:
                                warp.isDefault = property.boolValue;
                                break;
                            case 158:
                                warp.isSpecialDefault = property.boolValue;
                                break;
                            case 159:
                                warp.aliasOid = static_cast<std::int16_t>(property.intValue);
                                break;
                            case 160:
                                warp.cameraInitialAngle = property.floatValue[0];
                                warp.usesCameraInitialAngle = true;
                                break;
                            default:
                                break;
                            }
                        }
                    }

                    const auto animationCount = reader.S16();
                    if (animationCount < 0)
                        throw std::runtime_error("WARP has a negative embedded-ASEG count");
                    objects_[objectIndex].details.push_back({"Embedded animations", std::to_string(animationCount)});
                    warp.embeddedAnimationCount = animationCount;
                    for (int animation = 0; animation < animationCount; ++animation)
                    {
                        // Each embedded animation carries an ordinary
                        // CID/OID/splice header followed by LoadAsegFromBrx.
                        self(self, 1, objectIndex);
                        if (stopped)
                            break;
                    }
                    if (stopped)
                        continue;

                    const auto destinationLevel = reader.StringU16();
                    warp.destinationLevel = destinationLevel;
                    objects_[objectIndex].details.push_back({"Destination level", destinationLevel});
                    const auto hiddenObjectCount = reader.S16();
                    if (hiddenObjectCount < 0)
                        throw std::runtime_error("WARP has a negative hidden-object count");
                    objects_[objectIndex].details.push_back(
                        {"Hidden destination objects", std::to_string(hiddenObjectCount)});
                    warp.hiddenObjectOids.reserve(static_cast<std::size_t>(hiddenObjectCount));
                    for (int hiddenIndex = 0; hiddenIndex < hiddenObjectCount; ++hiddenIndex)
                        warp.hiddenObjectOids.push_back(reader.S16());
                    warps_.push_back(std::move(warp));
                }
                else if (usesLoader("LoadDialogFromBrx"))
                {
                    parseAloPayload(LoaderKind::Alo);
                    if (stopped)
                        continue;
                    auto &dialog = objects_[objectIndex].dialog;
                    dialog.present = true;
                    const auto eventCount = static_cast<std::int8_t>(reader.U8());
                    if (eventCount < 0)
                        throw std::runtime_error("Dialog has a negative event count");
                    dialog.events.reserve(static_cast<std::size_t>(eventCount));
                    objects_[objectIndex].details.push_back({"Dialog events", std::to_string(eventCount)});
                    for (int event = 0; event < eventCount; ++event)
                    {
                        const auto kind = static_cast<std::int8_t>(reader.U8());
                        BrxDialogEvent record{};
                        record.kind = static_cast<BrxDialogEventKind>(kind);
                        switch (kind)
                        {
                        case 0: // preload VAG
                        case 7: // movie
                            record.resourceName = reader.FixedString(32);
                            break;
                        case 1: // left speaker
                        case 2: // right speaker
                            record.primaryOid = reader.S16();
                            record.replacementOid = reader.S16();
                            break;
                        case 3: // talk
                            record.zoom = reader.F32();
                            record.pauseSeconds = reader.F32();
                            record.waitForInput = reader.U8() != 0;
                            record.promptKind = reader.U8();
                            record.speakerOid = reader.S16();
                            record.lipsyncAnimationOid = reader.S16();
                            record.focusOid = reader.S16();
                            record.anchorPointOid = reader.S16();
                            record.subtitle = reader.StringU16();
                            break;
                        case 4:
                            record.primaryOid = reader.S16();
                            break;
                        case 5:
                        case 6:
                            break; // letterbox
                        default:
                            throw std::runtime_error("Dialog has an invalid event type");
                        }
                        dialog.events.push_back(std::move(record));
                    }
                }
                else if (usesLoader("LoadTnFromBrx"))
                {
                    auto &tn = objects_[objectIndex].tn;
                    tn.present = true;
                    objects_[objectIndex].hasAloPayload = true;
                    glm::mat4 localTransform(1.0f);
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            localTransform[column][row] = reader.F32();
                    localTransform[3][0] = reader.F32();
                    localTransform[3][1] = reader.F32();
                    localTransform[3][2] = reader.F32();
                    const glm::mat4 parentTransform = parentIndex >= 0
                                                          ? objectWorldTransforms[static_cast<std::size_t>(parentIndex)]
                                                          : glm::mat4(1.0f);
                    objectWorldTransforms[static_cast<std::size_t>(objectIndex)] = parentTransform * localTransform;
                    std::copy(glm::value_ptr(localTransform), glm::value_ptr(localTransform) + 16,
                              objects_[objectIndex].localTransform);
                    std::copy(glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]),
                              glm::value_ptr(objectWorldTransforms[static_cast<std::size_t>(objectIndex)]) + 16,
                              objects_[objectIndex].worldTransform);

                    const auto surfaceCount = reader.U16();
                    tn.surfaces.resize(surfaceCount);
                    for (auto &surface : tn.surfaces)
                    {
                        surface.planeDistance = reader.F32();
                        for (float &component : surface.normal)
                            component = reader.F32();
                    }
                    const auto nodeCount = reader.U16();
                    tn.bspNodes.resize(nodeCount);
                    for (auto &node : tn.bspNodes)
                    {
                        node.surfaceIndex = reader.U16();
                        node.negativeChild = reader.S16();
                        node.positiveChild = reader.S16();
                    }

                    tn.curveKind = static_cast<std::int8_t>(reader.U8());
                    if (tn.curveKind == -1)
                    {
                        glm::mat4 staticFrame(1.0f);
                        for (int column = 0; column < 3; ++column)
                            for (int row = 0; row < 3; ++row)
                                staticFrame[column][row] = reader.F32();
                        staticFrame[3][0] = reader.F32();
                        staticFrame[3][1] = reader.F32();
                        staticFrame[3][2] = reader.F32();
                        std::copy(glm::value_ptr(staticFrame), glm::value_ptr(staticFrame) + 16, tn.staticFrame);
                    }
                    else if (tn.curveKind == 0 || tn.curveKind == 1)
                    {
                        tn.curveClosed = reader.U8() != 0;
                        const auto pointCount = reader.U8();
                        tn.curvePoints.resize(pointCount);
                        for (auto &point : tn.curvePoints)
                        {
                            point.parameter = reader.F32();
                            for (float &component : point.position)
                                component = reader.F32();
                            if (tn.curveKind == 1)
                            {
                                for (float &component : point.incomingTangent)
                                    component = reader.F32();
                                for (float &component : point.outgoingTangent)
                                    component = reader.F32();
                            }
                        }
                    }
                    else
                        throw std::runtime_error("TN has an invalid CRVK");
                    objects_[objectIndex].details.push_back({"Tunnel volume planes", std::to_string(surfaceCount)});
                    objects_[objectIndex].details.push_back({"Tunnel BSP nodes", std::to_string(nodeCount)});
                    objects_[objectIndex].details.push_back(
                        {"Tunnel camera path",
                         tn.curveKind < 0 ? "static frame" : (tn.curveKind == 0 ? "linear" : "cubic")});
                    SkipOptionList(reader);
                }
                else if (usesLoader("LoadSmFromBrx"))
                {
                    BrxSmRecord machine;
                    machine.ownerObjectIndex = objectIndex;
                    const auto stateCount = reader.U8();
                    objects_[objectIndex].details.push_back({"States", std::to_string(stateCount)});
                    machine.states.reserve(stateCount);
                    for (std::uint32_t state = 0; state < stateCount; ++state)
                        machine.states.push_back({reader.S16(), reader.S16()});
                    const auto transitionCount = reader.U8();
                    objects_[objectIndex].details.push_back({"Transitions", std::to_string(transitionCount)});
                    machine.transitions.reserve(transitionCount);
                    for (std::uint32_t transition = 0; transition < transitionCount; ++transition)
                    {
                        BrxSmTransitionRecord record;
                        record.ownsAnimation = reader.U8() != 0;
                        record.fromOid = reader.S16();
                        record.toOid = reader.S16();
                        record.flags = reader.S32();
                        record.probability = reader.F32();
                        machine.transitions.push_back(record);
                    }
                    SkipOptionList(reader);
                    machine.serializedTransitionCursor = reader.S16();
                    for (auto &transition : machine.transitions)
                    {
                        if (!transition.ownsAnimation)
                            continue;
                        transition.animationObjectIndex = static_cast<std::int32_t>(objects_.size());
                        self(self, 1, objectIndex);
                        if (stopped)
                            break;
                    }
                    if (stopped)
                        continue;
                    stateMachines_.push_back(std::move(machine));
                }
                else if (usesLoader("LoadRchmFromBrx"))
                {
                    BrxRchmRecord reach;
                    reach.ownerObjectIndex = objectIndex;
                    for (auto &oid : reach.animationOids)
                        oid = reader.S16();
                    reach.gridResolution = reader.S16();
                    reach.geometryRadius = reader.F32();
                    const auto positionCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Grid positions", std::to_string(positionCount)});
                    reach.positions.reserve(positionCount);
                    for (std::uint32_t position = 0; position < positionCount; ++position)
                        reach.positions.push_back({reader.F32(), reader.F32(), reader.F32()});
                    const auto surfaceCount = reader.U16();
                    objects_[objectIndex].details.push_back({"Grid surfaces", std::to_string(surfaceCount)});
                    reach.totalEdgeCount = reader.U16();
                    reach.surfaces.reserve(surfaceCount);
                    for (std::uint32_t surface = 0; surface < surfaceCount; ++surface)
                    {
                        BrxRchmSurfaceRecord record;
                        for (auto &position : record.planePositions)
                            position = reader.U16();
                        record.radius = reader.F32();
                        record.center = {reader.F32(), reader.F32(), reader.F32()};
                        const auto surfaceEdgeCount = reader.U8();
                        record.edges.reserve(surfaceEdgeCount);
                        for (std::uint32_t edge = 0; edge < surfaceEdgeCount; ++edge)
                            record.edges.push_back({reader.U16(), reader.U16(), reader.U16()});
                        reach.surfaces.push_back(std::move(record));
                    }
                    reach.enabledPositions.reserve(positionCount);
                    for (std::uint32_t position = 0; position < positionCount; ++position)
                        reach.enabledPositions.push_back(reader.U8() != 0);
                    // ProjectCane reads both topology counts with S16Read.  A
                    // negative count would be handed to vector::resize and is
                    // malformed data, not a large unsigned topology.
                    const auto towerCountSigned = reader.S16();
                    if (towerCountSigned < 0)
                        throw std::runtime_error("RCHM has a negative tetrahedron count");
                    const auto towerCount = static_cast<std::uint16_t>(towerCountSigned);
                    objects_[objectIndex].details.push_back({"Towers", std::to_string(towerCount)});
                    reach.tetrahedra.reserve(towerCount);
                    for (std::uint32_t tower = 0; tower < towerCount; ++tower)
                        reach.tetrahedra.push_back({reader.S16(), reader.S16(), reader.S16(), reader.S16()});
                    const auto categoryBspCountSigned = reader.S16();
                    if (categoryBspCountSigned < 0)
                        throw std::runtime_error("RCHM has a negative category-BSP count");
                    const auto categoryBspCount = static_cast<std::uint16_t>(categoryBspCountSigned);
                    objects_[objectIndex].details.push_back({"Category BSP nodes", std::to_string(categoryBspCount)});
                    reach.bspNodes.reserve(categoryBspCount);
                    for (std::uint32_t bsp = 0; bsp < categoryBspCount; ++bsp)
                        reach.bspNodes.push_back({reader.S16(), reader.S16(), reader.S16()});
                    reach.radiusSquared = reader.F32();
                    SkipOptionList(reader);
                    reachMaps_.push_back(std::move(reach));
                }
                else if (loader.kind == LoaderKind::Alo || loader.kind == LoaderKind::So)
                {
                    parseAloPayload(loader.kind);
                    if (stopped)
                        continue;
                }
                else
                {
                    worldHeader_.objectParseDiagnostic =
                        "CID " + std::to_string(object.cid) + " (" + std::string(BrxDocument::ClassName(object.cid)) +
                        ") requires " + loader.functionName + " at payload offset " + std::to_string(object.dataOffset);
                    stopped = true;
                    continue;
                }

                objects_[objectIndex].endOffset = reader.Offset();
                objects_[objectIndex].loaderImplemented = true;
            }
            catch (const std::exception &exception)
            {
                worldHeader_.objectParseDiagnostic = "CID " + std::to_string(object.cid) +
                                                     " failed at payload offset " + std::to_string(reader.Offset()) +
                                                     ": " + exception.what();
                stopped = true;
            }
        }
    };
    parseObjectList(parseObjectList, worldHeader_.rootObjectCount, -1);
    if (!stopped)
        worldHeader_.objectParseDiagnostic = "All currently routed object loaders completed";
    // ProjectCane's LoadSwFromBrx aligns the binary stream to a 16-byte
    // boundary after the final world object and before LoadTexturesFromBrx.
    // Bitmap base offsets are relative to this aligned position.
    const auto texturePayloadOffset = (reader.Offset() + 15u) & ~std::size_t(15u);
    if (texturePayloadOffset > payload_.size())
        throw std::runtime_error("PCBRX texture payload alignment exceeds the file");
    if (!stopped)
    {
        for (auto &bitmap : bitmaps_)
        {
            const auto begin = texturePayloadOffset + bitmap.textureDataOffset;
            if (begin > payload_.size() || bitmap.pixelBytes > payload_.size() - begin)
                throw std::runtime_error("PCBRX texture descriptor points outside the payload");
            bitmap.pixels.assign(payload_.begin() + static_cast<std::ptrdiff_t>(begin),
                                 payload_.begin() + static_cast<std::ptrdiff_t>(begin + bitmap.pixelBytes));
        }
    }
    sections_.push_back({"Shaders and bitmaps", worldHeader_.shadersOffset,
                         worldHeader_.worldObjectsOffset - worldHeader_.shadersOffset, BrxSectionState::Parsed});
    sections_.push_back({"World objects", worldHeader_.worldObjectsOffset,
                         texturePayloadOffset - worldHeader_.worldObjectsOffset,
                         stopped ? BrxSectionState::Pending : BrxSectionState::Parsed});
    sections_.push_back({"Texture payload", texturePayloadOffset, payload_.size() - texturePayloadOffset,
                         stopped ? BrxSectionState::Pending : BrxSectionState::Parsed});
}
