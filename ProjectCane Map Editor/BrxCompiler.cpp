#include "BrxCompiler.h"

#include "BrxDocument.h"
#include "EditorOptions.h"
#include "ProxyRuntime.h"
#include "ValidationSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr std::uint32_t kPcbrMagic = 0x52424350u;

enum class CidWriterLayout : std::uint8_t
{
    Unsupported,
    Lo,
    Alo,
    So,
    Cnvo,
    Emitter,
    Proxy,
    Flash,
    Tn,
    Dialog,
    World,
    Shape,
    Xfm,
    Warp,
    Explg,
    ExPlo,
    Vol,
    Exit,
    Pnt,
    Scan,
    Aseg,
    Vismap,
    Sm,
    Pathzone,
    Rchm,
    Keyhole,
    RuntimeOnly
};

// This is the audited loader layout, not the C++ inheritance hierarchy.
// Specialized runtime wrappers that consume no additional bytes deliberately
// map to their shared SO/ALO layout.
CidWriterLayout WriterLayoutForCid(const std::int16_t cid)
{
    if (cid == 1 || cid == 109 || cid == 132 || cid == 133 || cid == 135 || cid == 137 || cid == 140 || cid == 141 ||
        (cid >= 143 && cid <= 145))
        return CidWriterLayout::Lo;
    if ((cid >= 3 && cid <= 56) || (cid >= 58 && cid <= 76))
        return CidWriterLayout::So;
    if (cid == 2 || (cid >= 77 && cid <= 79) || (cid >= 81 && cid <= 82) || (cid >= 84 && cid <= 95) ||
        (cid >= 97 && cid <= 98) || cid == 100 || (cid >= 102 && cid <= 107))
        return CidWriterLayout::Alo;
    switch (cid)
    {
    case 57:
        return CidWriterLayout::Cnvo;
    case 80:
        return CidWriterLayout::Emitter;
    case 83:
        return CidWriterLayout::Proxy;
    case 96:
        return CidWriterLayout::Flash;
    case 99:
        return CidWriterLayout::Tn;
    case 101:
        return CidWriterLayout::Dialog;
    // SW/CID 108 is the runtime world container loaded by LoadSwFromBrx; it
    // is not a legal nested/root object record in the world-object list.
    case 108:
        return CidWriterLayout::RuntimeOnly;
    case 110:
    case 111:
    case 112:
    case 113:
    case 114:
        return CidWriterLayout::Shape;
    case 115:
    case 117:
    case 118:
    case 119:
        return CidWriterLayout::Xfm;
    case 116:
        return CidWriterLayout::Warp;
    case 120:
        return CidWriterLayout::Explg;
    case 121:
    case 122:
        return CidWriterLayout::ExPlo;
    case 123:
    case 124:
        return CidWriterLayout::Vol;
    case 125:
        return CidWriterLayout::Exit;
    case 126:
    case 127:
    case 128:
    case 129:
        return CidWriterLayout::Pnt;
    case 130:
        return CidWriterLayout::Scan;
    case 131:
        return CidWriterLayout::Aseg;
    case 134:
        return CidWriterLayout::Vismap;
    case 136:
        return CidWriterLayout::Sm;
    case 138:
        return CidWriterLayout::Pathzone;
    case 139:
        return CidWriterLayout::Rchm;
    case 142:
        return CidWriterLayout::Keyhole;
    case 146:
    case 147:
        return CidWriterLayout::RuntimeOnly;
    default:
        return CidWriterLayout::Unsupported;
    }
}

bool VerifyCidWriterCoverage(const BrxDocument &document, std::string &error)
{
    static const auto runtimeLoaders = []
    {
        std::array<const char *, 148> result{};
#define CID_LOADER(id, kind, functionName) result.at(id) = functionName;
#include "CidLoaders.generated.inc"
#undef CID_LOADER
        return result;
    }();
    const auto customLoaderMatchesLayout = [](const CidWriterLayout layout, const char *loader)
    {
        if (loader == nullptr)
            return false;
        const auto is = [&](const char *expected) { return std::strcmp(loader, expected) == 0; };
        switch (layout)
        {
        case CidWriterLayout::Cnvo:
            return is("LoadCnvoFromBrx");
        case CidWriterLayout::Emitter:
            return is("LoadEmitterFromBrx");
        case CidWriterLayout::Proxy:
            return is("LoadProxyFromBrx");
        case CidWriterLayout::Flash:
            return is("LoadFlashFromBrx");
        case CidWriterLayout::Tn:
            return is("LoadTnFromBrx");
        case CidWriterLayout::Dialog:
            return is("LoadDialogFromBrx");
        case CidWriterLayout::Shape:
            return is("LoadShapeFromBrx");
        // HND's loader is a byte-identical wrapper around LoadXfmFromBrx.
        case CidWriterLayout::Xfm:
            return is("LoadXfmFromBrx") || is("LoadHndFromBrx");
        case CidWriterLayout::Warp:
            return is("LoadWarpFromBrx");
        case CidWriterLayout::Explg:
            return is("LoadExplgFromBrx");
        case CidWriterLayout::ExPlo:
            return is("LoadExploFromBrx");
        case CidWriterLayout::Vol:
            return is("LoadVolFromBrx");
        case CidWriterLayout::Exit:
            return is("LoadExitFromBrx");
        case CidWriterLayout::Pnt:
            return is("LoadPntFromBrx");
        case CidWriterLayout::Scan:
            return is("LoadScanFromBrx");
        case CidWriterLayout::Aseg:
            return is("LoadAsegFromBrx");
        case CidWriterLayout::Vismap:
            return is("LoadVismapFromBrx");
        case CidWriterLayout::Sm:
            return is("LoadSmFromBrx");
        case CidWriterLayout::Pathzone:
            return is("LoadPathZoneFromBrx");
        case CidWriterLayout::Rchm:
            return is("LoadRchmFromBrx");
        case CidWriterLayout::Keyhole:
            return is("LoadKeyholeFromBrx");
        case CidWriterLayout::RuntimeOnly:
            return is("LoadSwFromBrx");
        default:
            return true;
        }
    };
    for (std::int16_t cid = 1; cid <= 145; ++cid)
    {
        const auto *loader = runtimeLoaders[static_cast<std::size_t>(cid)];
        if (loader == nullptr)
        {
            error = "Runtime CID table has no loader for CID " + std::to_string(cid) + ".";
            return false;
        }
        const auto layout = WriterLayoutForCid(cid);
        if ((cid == 108 && layout != CidWriterLayout::RuntimeOnly) ||
            (cid != 108 && (layout == CidWriterLayout::Unsupported || layout == CidWriterLayout::RuntimeOnly)))
        {
            error = "Writer coverage is missing for runtime world CID " + std::to_string(cid) + ".";
            return false;
        }
        if ((std::strcmp(loader, "LoadLoFromBrx") == 0 && layout != CidWriterLayout::Lo) ||
            (std::strcmp(loader, "LoadAloFromBrx") == 0 && layout != CidWriterLayout::Alo) ||
            (std::strcmp(loader, "LoadSoFromBrx") == 0 && layout != CidWriterLayout::So))
        {
            error = "Shared writer layout disagrees with the runtime loader for CID " + std::to_string(cid) + ".";
            return false;
        }
        if (!customLoaderMatchesLayout(layout, loader))
        {
            error = "Custom writer layout disagrees with runtime loader " + std::string(loader) + " for CID " +
                    std::to_string(cid) + ".";
            return false;
        }
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto cid = document.Objects()[index].cid;
        const auto layout = WriterLayoutForCid(cid);
        if (layout == CidWriterLayout::RuntimeOnly)
        {
            error =
                "Object " + std::to_string(index) + " uses runtime-only CID " + std::to_string(cid) +
                (cid == 108
                     ? " (SW), whose data belongs to the world header rather than the object list."
                     : " (ASEGA/SMA), which is generated from authored ASEG/SM data and is not a world BRX record.");
            return false;
        }
        if (layout == CidWriterLayout::Unsupported)
        {
            error = "Object " + std::to_string(index) + " has unsupported CID " + std::to_string(cid) + ".";
            return false;
        }
    }
    return true;
}

std::uint64_t HashBytes(const std::vector<std::uint8_t> &bytes)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : bytes)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool FiniteFloats(const float *values, const std::size_t count)
{
    if (count == 0)
        return true;
    return std::all_of(values, values + count, [](const float value) { return std::isfinite(value); });
}

class PayloadWriter
{
  public:
    template <class T> void Pod(const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto *begin = reinterpret_cast<const std::uint8_t *>(&value);
        bytes_.insert(bytes_.end(), begin, begin + sizeof(value));
    }
    void Floats(const float *values, const std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
            Pod(values[index]);
    }
    void Bytes(const std::uint8_t *data, const std::size_t size)
    {
        bytes_.insert(bytes_.end(), data, data + size);
    }
    void Zeroes(const std::size_t size)
    {
        bytes_.insert(bytes_.end(), size, 0);
    }
    bool FixedString(const std::string &value, const std::size_t size)
    {
        if (value.size() >= size)
            return false;
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        bytes_.insert(bytes_.end(), size - value.size(), 0);
        return true;
    }
    bool StringU16(const std::string &value)
    {
        if (value.size() > (std::numeric_limits<std::uint16_t>::max)())
            return false;
        Pod(static_cast<std::uint16_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }
    [[nodiscard]] const std::vector<std::uint8_t> &Bytes() const
    {
        return bytes_;
    }

    bool MarkObjectWritten(const std::size_t objectIndex)
    {
        return writtenObjects_.insert(objectIndex).second;
    }
    [[nodiscard]] const std::unordered_set<std::size_t> &WrittenObjects() const
    {
        return writtenObjects_;
    }

    bool RegisterProxySource(const std::int32_t proxyRecordIndex, const std::int32_t proxyRootIndex,
                             std::uint16_t &sourceIndex)
    {
        if (proxyRecordIndex < 0 || proxyRootIndex < 0 || proxySourceCount_ >= 128u)
            return false;
        const auto key = ProxySourceKey(proxyRecordIndex, proxyRootIndex);
        if (proxySources_.find(key) != proxySources_.end())
            return false;
        sourceIndex = proxySourceCount_++;
        proxySources_.emplace(key, sourceIndex);
        return true;
    }

    [[nodiscard]] bool FindProxySource(const std::int32_t proxyRecordIndex, const std::int32_t proxyRootIndex,
                                       std::uint16_t &sourceIndex) const
    {
        const auto found = proxySources_.find(ProxySourceKey(proxyRecordIndex, proxyRootIndex));
        if (found == proxySources_.end())
            return false;
        sourceIndex = found->second;
        return true;
    }

  private:
    static std::uint64_t ProxySourceKey(const std::int32_t proxyRecordIndex, const std::int32_t proxyRootIndex)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(proxyRecordIndex)) << 32u) |
               static_cast<std::uint32_t>(proxyRootIndex);
    }

    std::vector<std::uint8_t> bytes_;
    std::unordered_map<std::uint64_t, std::uint16_t> proxySources_;
    std::unordered_set<std::size_t> writtenObjects_;
    std::uint16_t proxySourceCount_ = 0;
};

bool WriteDynamicArgument(PayloadWriter &writer, const BrxPropertyRecord::DynamicArgument &argument)
{
    writer.Pod(argument.type);
    switch (argument.type)
    {
    case 0:
        writer.Pod(static_cast<std::uint8_t>(argument.intValue));
        return true;
    case 1:
        writer.Floats(argument.floatValue, 1);
        return true;
    case 2:
    case 4:
    case 9:
    case 11:
        writer.Floats(argument.floatValue, 3);
        return true;
    case 3:
    case 12:
        writer.Floats(argument.floatValue, 4);
        return true;
    case 5:
    case 13:
        writer.Pod(static_cast<std::uint32_t>(argument.intValue));
        return true;
    case 10:
        writer.Floats(argument.floatValue, 2);
        return true;
    case 6:
    case 7:
    case 14:
    case 15:
    case 16:
    case 17:
    case 20:
        writer.Pod(static_cast<std::int16_t>(argument.intValue));
        return true;
    default:
        if ((static_cast<std::uint32_t>(argument.type) & 0x7fffffffu) >> 12 == 1)
        {
            writer.Pod(static_cast<std::int16_t>(argument.intValue));
            return true;
        }
        return false;
    }
}

bool WritePropertyValue(PayloadWriter &writer, const BrxPropertyRecord &property)
{
    if (property.kind == BrxPropertyKind::Dynamic)
    {
        if (property.arguments.size() != property.argumentCount)
            return false;
        for (const auto &argument : property.arguments)
            if (!WriteDynamicArgument(writer, argument))
                return false;
        return true;
    }
    switch (property.kind)
    {
    case BrxPropertyKind::Bool:
        writer.Pod(static_cast<std::uint8_t>(property.boolValue ? 1 : 0));
        return true;
    case BrxPropertyKind::Float:
        writer.Floats(property.floatValue, 1);
        return true;
    case BrxPropertyKind::Vector3:
        writer.Floats(property.floatValue, 3);
        return true;
    case BrxPropertyKind::Vector4:
        writer.Floats(property.floatValue, 4);
        return true;
    case BrxPropertyKind::Int32:
        writer.Pod(property.intValue);
        return true;
    case BrxPropertyKind::Float2:
        writer.Floats(property.floatValue, 2);
        return true;
    case BrxPropertyKind::UInt32:
        writer.Pod(property.uintValue);
        return true;
    case BrxPropertyKind::Int16:
        writer.Pod(static_cast<std::int16_t>(property.intValue));
        return true;
    case BrxPropertyKind::TypedInt16:
        writer.Pod(property.typeValue);
        writer.Pod(static_cast<std::int16_t>(property.intValue));
        return true;
    default:
        return false;
    }
}

bool ValidateOptionListForCid(const std::int16_t cid, const std::vector<BrxPropertyRecord> &properties,
                              std::string &error)
{
    const auto finiteFloats = [](const float *values, const std::size_t count)
    { return std::all_of(values, values + count, [](const float value) { return std::isfinite(value); }); };
    for (const auto &property : properties)
    {
        if (!IsEditorWritableOptionForCid(cid, property.eopid))
        {
            error = "CID " + std::to_string(cid) + " cannot serialize non-SetUser EOPID " +
                    std::to_string(property.eopid) + ".";
            return false;
        }
        const auto definition = EditorOptionDefinitionForCid(cid, property.eopid);
        if (!definition.present || definition.kind != property.kind)
        {
            error = "CID " + std::to_string(cid) + " EOPID " + std::to_string(property.eopid) +
                    " has a value encoding that disagrees with the runtime descriptor.";
            return false;
        }
        if (definition.argumentCount != property.argumentCount ||
            (property.kind == BrxPropertyKind::Dynamic && property.arguments.size() != property.argumentCount))
        {
            error = "CID " + std::to_string(cid) + " EOPID " + std::to_string(property.eopid) +
                    " has the wrong serialized argument count.";
            return false;
        }
        std::size_t floatCount = 0;
        switch (property.kind)
        {
        case BrxPropertyKind::Float:
            floatCount = 1;
            break;
        case BrxPropertyKind::Float2:
            floatCount = 2;
            break;
        case BrxPropertyKind::Vector3:
            floatCount = 3;
            break;
        case BrxPropertyKind::Vector4:
            floatCount = 4;
            break;
        case BrxPropertyKind::Int16:
        case BrxPropertyKind::TypedInt16:
            if (property.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                property.intValue > (std::numeric_limits<std::int16_t>::max)())
            {
                error = "CID " + std::to_string(cid) + " EOPID " + std::to_string(property.eopid) +
                        " exceeds signed 16-bit storage.";
                return false;
            }
            break;
        default:
            break;
        }
        if (floatCount != 0 && !finiteFloats(property.floatValue, floatCount))
        {
            error = "CID " + std::to_string(cid) + " EOPID " + std::to_string(property.eopid) +
                    " contains a non-finite value.";
            return false;
        }
        for (const auto &argument : property.arguments)
        {
            std::size_t argumentFloatCount = 0;
            switch (argument.type)
            {
            case 1:
                argumentFloatCount = 1;
                break;
            case 10:
                argumentFloatCount = 2;
                break;
            case 2:
            case 4:
            case 9:
            case 11:
                argumentFloatCount = 3;
                break;
            case 3:
            case 12:
                argumentFloatCount = 4;
                break;
            case 0:
                if (argument.intValue < 0 || argument.intValue > 255)
                {
                    error = "Dynamic SetUser byte argument exceeds U8 storage.";
                    return false;
                }
                break;
            case 6:
            case 7:
            case 14:
            case 15:
            case 16:
            case 17:
            case 20:
                if (argument.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                    argument.intValue > (std::numeric_limits<std::int16_t>::max)())
                {
                    error = "Dynamic SetUser argument exceeds signed 16-bit storage.";
                    return false;
                }
                break;
            default:
                if (((static_cast<std::uint32_t>(argument.type) & 0x7fffffffu) >> 12) == 1u &&
                    (argument.intValue < (std::numeric_limits<std::int16_t>::min)() ||
                     argument.intValue > (std::numeric_limits<std::int16_t>::max)()))
                {
                    error = "Dynamic typed argument exceeds signed 16-bit storage.";
                    return false;
                }
                break;
            }
            if (argumentFloatCount != 0 && !finiteFloats(argument.floatValue, argumentFloatCount))
            {
                error = "Dynamic SetUser argument contains a non-finite value.";
                return false;
            }
        }
    }
    return true;
}

bool WriteOptionList(PayloadWriter &writer, const std::vector<BrxPropertyRecord> &properties, std::string &error,
                     const bool omitNewRuntimeDefaults = true)
{
    for (const auto &property : properties)
    {
        // A newly enabled plain override that exactly equals a descriptor
        // default is represented by absence, matching SetLoDefaults. Source
        // records remain explicit even when they equal that default.
        if (omitNewRuntimeDefaults && property.offset == 0 && IsRuntimeDefaultValue(property))
            continue;
        writer.Pod(property.eopid);
        if (!WritePropertyValue(writer, property))
        {
            error = "EOPID " + std::to_string(property.eopid) + " uses an unsupported value encoding.";
            return false;
        }
    }
    writer.Pod(static_cast<std::int16_t>(-1));
    return true;
}

std::vector<std::size_t> AuthoredChildren(const BrxDocument &document, const std::size_t parentIndex)
{
    std::vector<std::size_t> children;
    const auto &objects = document.Objects();
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        bool attachedProxyChild = false;
        for (const auto &proxy : document.Proxies())
            if (std::find(proxy.attachedChildObjectIndices.begin(), proxy.attachedChildObjectIndices.end(),
                          static_cast<std::int32_t>(index)) != proxy.attachedChildObjectIndices.end())
            {
                attachedProxyChild = true;
                break;
            }
        // A nested PROXY wrapper starts a new proxy-root identity by design.
        // It is still an authored child of the object that contains the PROXY
        // record and must be emitted there.  Requiring every child to inherit
        // the containing root identity silently discarded nested PROXY trees
        // (for example VAULT's three tumbler assemblies and their SM OID 890).
        // Materialized proxy clones remain excluded below; only the wrapper
        // record itself is admitted across the root-identity boundary.
        const bool sameProxyRoot = objects[parentIndex].proxyRootIndex >= 0
                                       ? objects[index].proxyRootIndex == objects[parentIndex].proxyRootIndex
                                       : objects[index].proxyRootIndex < 0;
        const bool nestedProxyWrapper = objects[index].cid == 83 && objects[index].proxyRootIndex < 0;
        if (!attachedProxyChild && objects[index].parentIndex == static_cast<std::int32_t>(parentIndex) &&
            !objects[index].proxyClone && (sameProxyRoot || nestedProxyWrapper))
            children.push_back(index);
    }
    // Objects are decoded in stream order and editor-created objects are
    // appended in authored order. Source offsets are immutable import
    // diagnostics; sorting by them moves every new object (offset 0) ahead of
    // existing siblings and can place a PROXY reference before its source.
    return children;
}

bool WriteAlox(PayloadWriter &writer, const BrxAloxRecord &alox, std::string &error)
{
    if ((alox.flags & ~0xffu) != 0)
    {
        error = "ALO extension flags contain bits not consumed by LoadAloxFromBrx.";
        return false;
    }
    if (((alox.flags & 0x01u) != 0 && !FiniteFloats(alox.preRotation, 9)) ||
        ((alox.flags & 0x02u) != 0 && !FiniteFloats(alox.postRotation, 9)) ||
        ((alox.flags & 0x20u) != 0 && (!FiniteFloats(alox.ikPosition, 3) || !FiniteFloats(alox.wristPosition, 3) ||
                                       !std::isfinite(alox.twistRadians))))
    {
        error = "ALO extension payload contains a non-finite transform value.";
        return false;
    }
    writer.Pod(alox.flags);
    if ((alox.flags & 0x01u) != 0)
        writer.Floats(alox.preRotation, 9);
    if ((alox.flags & 0x02u) != 0)
        writer.Floats(alox.postRotation, 9);
    if ((alox.flags & 0x0cu) != 0)
        writer.Pod(alox.rotationSourceOid);
    if ((alox.flags & 0x10u) != 0)
        writer.Pod(alox.ikTargetOid);
    if ((alox.flags & 0x20u) != 0)
    {
        writer.Pod(alox.elbowOid);
        writer.Floats(alox.ikPosition, 3);
        writer.Floats(alox.wristPosition, 3);
        writer.Pod(alox.twistRadians);
    }
    if ((alox.flags & 0x40u) != 0)
    {
        writer.Pod(alox.jointParentOid);
        writer.Pod(alox.jointChildOid);
    }
    if ((alox.flags & 0x80u) != 0)
        writer.Pod(static_cast<std::uint8_t>(alox.segmentScaleCompensation ? 1 : 0));
    return true;
}

bool WriteEffectTail(PayloadWriter &writer, const BrxEffectRecord &effect, std::string &error)
{
    writer.Pod(effect.curveKind);
    if (effect.curveKind != -1)
    {
        if ((effect.curveKind != 0 && effect.curveKind != 1) ||
            effect.curvePoints.size() > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "Effect curve kind/count is invalid.";
            return false;
        }
        if (effect.curveKind == 1 && effect.curvePoints.empty())
        {
            error = "A cubic effect curve cannot be empty: ProjectCane measures control point zero while loading it.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(effect.curveClosed ? 1 : 0));
        writer.Pod(static_cast<std::uint8_t>(effect.curvePoints.size()));
        for (const auto &point : effect.curvePoints)
        {
            if (!std::isfinite(point.u) || !FiniteFloats(point.position.data(), 3) ||
                (effect.curveKind == 1 &&
                 (!FiniteFloats(point.tangentIn.data(), 3) || !FiniteFloats(point.tangentOut.data(), 3))))
            {
                error = "Effect curve contains a non-finite value.";
                return false;
            }
            writer.Pod(point.u);
            writer.Floats(point.position.data(), 3);
            if (effect.curveKind == 1)
            {
                writer.Floats(point.tangentIn.data(), 3);
                writer.Floats(point.tangentOut.data(), 3);
            }
        }
    }
    if (effect.originKind == 4)
    {
        if (effect.meshPositions.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            effect.meshTriangles.size() > (std::numeric_limits<std::uint16_t>::max)())
        {
            error = "Effect mesh count exceeds U16.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(effect.meshPositions.size()));
        for (const auto &position : effect.meshPositions)
        {
            if (!FiniteFloats(position.data(), 3))
            {
                error = "Effect mesh contains a non-finite position.";
                return false;
            }
            writer.Floats(position.data(), 3);
        }
        writer.Pod(static_cast<std::uint16_t>(effect.meshTriangles.size()));
        for (const auto &triangle : effect.meshTriangles)
        {
            if (triangle.indices[0] >= effect.meshPositions.size() ||
                triangle.indices[1] >= effect.meshPositions.size() ||
                triangle.indices[2] >= effect.meshPositions.size() || !std::isfinite(triangle.area) ||
                triangle.area < 0.0f)
            {
                error = "Effect mesh triangle index is invalid.";
                return false;
            }
            writer.Pod(triangle.indices[0]);
            writer.Pod(triangle.indices[1]);
            writer.Pod(triangle.indices[2]);
            writer.Pod(triangle.area);
        }
        if (!FiniteFloats(effect.meshCenter.data(), 3))
        {
            error = "Effect mesh center is non-finite.";
            return false;
        }
        writer.Floats(effect.meshCenter.data(), 3);
    }
    if (effect.colors.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "Effect color count exceeds U16.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(effect.colors.size()));
    if (!effect.colors.empty())
    {
        writer.Pod(static_cast<std::uint8_t>(effect.colorsAreRanges ? 1 : 0));
        for (const auto &color : effect.colors)
        {
            if (!FiniteFloats(color.data(), 4))
            {
                error = "Effect color contains a non-finite value.";
                return false;
            }
            writer.Floats(color.data(), 4);
        }
    }
    return true;
}

bool WriteVolumeTail(PayloadWriter &writer, const BrxVolumeRecord &volume, std::string &error)
{
    if (volume.planes.size() > (std::numeric_limits<std::uint16_t>::max)() ||
        volume.nodes.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u)
    {
        error = "Volume BSP count exceeds the retail signed child-index range.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(volume.planes.size()));
    for (const auto &plane : volume.planes)
    {
        if (!std::isfinite(plane.distance) || !FiniteFloats(plane.normal.data(), 3))
        {
            error = "Volume BSP contains a non-finite plane.";
            return false;
        }
        writer.Pod(plane.distance);
        writer.Floats(plane.normal.data(), 3);
    }
    writer.Pod(static_cast<std::uint16_t>(volume.nodes.size()));
    for (const auto &node : volume.nodes)
    {
        if (node.surfaceIndex >= volume.planes.size() ||
            (node.negativeChild != 0xffffu &&
             (node.negativeChild >= volume.nodes.size() ||
              node.negativeChild > static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))) ||
            (node.positiveChild != 0xffffu &&
             (node.positiveChild >= volume.nodes.size() ||
              node.positiveChild > static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))))
        {
            error = "Volume BSP contains an invalid surface/child index.";
            return false;
        }
        writer.Pod(node.surfaceIndex);
        writer.Pod(node.negativeChild);
        writer.Pod(node.positiveChild);
    }
    return true;
}

bool WriteSoGeometry(PayloadWriter &writer, const BrxSoGeometryInfo &geometry, std::string &error)
{
    // ReadGeom casts position and adjacent-surface indices through int16_t and
    // reads each surface's edge count through int8_t.  Values in the unsigned
    // half of those serialized fields therefore make the retail loader reject
    // otherwise well-formed geometry.
    if (geometry.positions.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
        geometry.surfaces.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
        geometry.bspNodes.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "SO geometry count exceeds the retail loader's index width.";
        return false;
    }
    std::size_t edgeCount = 0;
    std::unordered_set<std::uint32_t> physicalEdges;
    for (const auto &surface : geometry.surfaces)
    {
        if (surface.edges.size() > static_cast<std::size_t>((std::numeric_limits<std::int8_t>::max)()))
        {
            error = "SO surface edge count exceeds signed 8-bit storage.";
            return false;
        }
        edgeCount += surface.edges.size();
        for (const auto &edge : surface.edges)
        {
            const auto low = (std::min)(edge.firstPosition, edge.secondPosition);
            const auto high = (std::max)(edge.firstPosition, edge.secondPosition);
            const std::uint32_t key = (static_cast<std::uint32_t>(low) << 16u) | high;
            if (!physicalEdges.insert(key).second)
            {
                error = "SO geometry serializes the same physical edge more than once.";
                return false;
            }
        }
    }
    if (edgeCount > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "SO total edge count exceeds U16.";
        return false;
    }
    if (geometry.fullBspNodeCount < geometry.bspNodes.size())
    {
        error = "SO full BSP scratch count is smaller than the serialized BSP tree.";
        return false;
    }
    if (!std::isfinite(geometry.radius) || geometry.radius < 0.0f)
    {
        error = "SO geometry radius is invalid.";
        return false;
    }
    writer.Pod(geometry.radius);
    writer.Pod(static_cast<std::uint16_t>(geometry.positions.size()));
    for (const auto &position : geometry.positions)
    {
        if (!FiniteFloats(position.data(), 3))
        {
            error = "SO geometry contains a non-finite position.";
            return false;
        }
        writer.Floats(position.data(), 3);
    }
    writer.Pod(static_cast<std::uint16_t>(geometry.surfaces.size()));
    writer.Pod(static_cast<std::uint16_t>(edgeCount));
    for (const auto &surface : geometry.surfaces)
    {
        if (!std::isfinite(surface.radius) || surface.radius < 0.0f || !FiniteFloats(surface.center.data(), 3))
        {
            error = "SO surface bounds are invalid.";
            return false;
        }
        for (const auto position : surface.positionIndices)
        {
            if (position >= geometry.positions.size())
            {
                error = "SO surface references an invalid position.";
                return false;
            }
            writer.Pod(position);
        }
        const glm::vec3 a = glm::make_vec3(geometry.positions[surface.positionIndices[0]].data());
        const glm::vec3 b = glm::make_vec3(geometry.positions[surface.positionIndices[1]].data());
        const glm::vec3 c = glm::make_vec3(geometry.positions[surface.positionIndices[2]].data());
        const glm::vec3 cross = glm::cross(b - a, c - a);
        const float areaSquared = glm::dot(cross, cross);
        const float edgeScale =
            (std::max)({glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c), 1.0f});
        // ProjectCane's ReadGeom unconditionally normalizes this cross
        // product. A zero-area face therefore creates a NaN collision plane,
        // which can propagate into the player's world coordinates on contact.
        if (!std::isfinite(areaSquared) || areaSquared <= edgeScale * edgeScale * 1.0e-12f)
        {
            error = "SO geometry contains a degenerate surface; regenerate or simplify its collision.";
            return false;
        }
        writer.Pod(surface.radius);
        writer.Floats(surface.center.data(), 3);
        writer.Pod(static_cast<std::uint8_t>(surface.edges.size()));
        for (const auto &edge : surface.edges)
        {
            if (edge.firstPosition >= geometry.positions.size() || edge.secondPosition >= geometry.positions.size() ||
                edge.adjacentSurface >= geometry.surfaces.size() ||
                edge.adjacentSurface > static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))
            {
                error = "SO edge contains an invalid position/adjacency index.";
                return false;
            }
            writer.Pod(edge.firstPosition);
            writer.Pod(edge.secondPosition);
            writer.Pod(edge.adjacentSurface);
        }
    }
    writer.Pod(static_cast<std::uint16_t>(geometry.bspNodes.size()));
    writer.Pod(geometry.fullBspNodeCount);
    for (const auto &node : geometry.bspNodes)
    {
        if (node.surfaceIndex >= geometry.surfaces.size() ||
            (node.negativeChild != 0xffffu && node.negativeChild >= geometry.bspNodes.size()) ||
            (node.positiveChild != 0xffffu && node.positiveChild >= geometry.bspNodes.size()))
        {
            error = "SO BSP contains an invalid surface/child index.";
            return false;
        }
        writer.Pod(node.surfaceIndex);
        writer.Pod(node.negativeChild);
        writer.Pod(node.positiveChild);
    }
    return true;
}

bool WriteFixed32(PayloadWriter &writer, const std::string &value)
{
    const auto count = (std::min)(value.size(), std::size_t{32});
    writer.Bytes(reinterpret_cast<const std::uint8_t *>(value.data()), count);
    writer.Zeroes(32u - count);
    return true;
}

bool WriteAsegFrame(PayloadWriter &writer, const float seconds, std::string &error)
{
    if (!std::isfinite(seconds))
    {
        error = "ASEG key time is non-finite.";
        return false;
    }
    const auto frame = std::lround(static_cast<double>(seconds) * 60.0);
    if (frame < (std::numeric_limits<std::int16_t>::min)() || frame > (std::numeric_limits<std::int16_t>::max)())
    {
        error = "ASEG key time exceeds signed 16-bit frames.";
        return false;
    }
    writer.Pod(static_cast<std::int16_t>(frame));
    return true;
}

bool WriteAsegScalar(PayloadWriter &writer, const BrxAsegControllerSummary &controller, std::string &error)
{
    writer.Pod(controller.kind);
    if (controller.kind == 3 || controller.kind == 4)
        return true;
    if (controller.kind < 0 || controller.kind > 2 ||
        controller.scalarKeys.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "ASEG scalar controller kind/count is invalid.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(controller.scalarKeys.size()));
    for (const auto &key : controller.scalarKeys)
    {
        if (!std::isfinite(key.value) || !std::isfinite(key.tangentIn) || !std::isfinite(key.tangentOut) ||
            !std::isfinite(key.tangentInTime) || !std::isfinite(key.tangentOutTime))
        {
            error = "ASEG scalar controller contains a non-finite value.";
            return false;
        }
        if (!WriteAsegFrame(writer, key.time, error))
            return false;
        writer.Pod(key.value);
        if (controller.kind == 0)
        {
            writer.Pod(key.tangentInKind);
            writer.Pod(key.tangentOutKind);
            writer.Pod(key.tangentIn);
            writer.Pod(key.tangentOut);
        }
        else if (controller.kind == 1)
        {
            writer.Pod(key.tangentInKind);
            writer.Pod(key.tangentOutKind);
            writer.Pod(key.tangentInTime);
            writer.Pod(key.value - key.tangentIn);
            writer.Pod(key.tangentOutTime);
            writer.Pod(key.tangentOut - key.value);
        }
    }
    return true;
}

bool WriteAsegVector(PayloadWriter &writer, const BrxAsegControllerSummary &controller, std::string &error)
{
    writer.Pod(controller.kind);
    if (controller.kind == 2)
        return true;
    if (controller.kind == 0)
    {
        if ((controller.componentMask & ~0x07u) != 0 || !FiniteFloats(controller.defaultValue.data(), 3))
        {
            error = "ASEG component controller mask/default is invalid.";
            return false;
        }
        writer.Pod(controller.componentMask);
        writer.Floats(controller.defaultValue.data(), 3);
        for (std::size_t component = 0; component < 3; ++component)
            if ((controller.componentMask & (1u << component)) != 0)
            {
                BrxAsegControllerSummary scalar;
                scalar.kind = controller.components[component].kind;
                scalar.scalarKeys = controller.components[component].keys;
                if (!WriteAsegScalar(writer, scalar, error))
                    return false;
            }
        return true;
    }
    if (controller.kind != 1 || controller.vectorKeys.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "ASEG vector controller kind/count is invalid.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(controller.vectorKeys.size()));
    for (const auto &key : controller.vectorKeys)
    {
        if (!FiniteFloats(key.value.data(), 3) || !FiniteFloats(key.tangentIn.data(), 3) ||
            !FiniteFloats(key.tangentOut.data(), 3))
        {
            error = "ASEG vector controller contains a non-finite value.";
            return false;
        }
        if (!WriteAsegFrame(writer, key.time, error))
            return false;
        writer.Floats(key.value.data(), 3);
        writer.Floats(key.tangentIn.data(), 3);
        writer.Floats(key.tangentOut.data(), 3);
    }
    return true;
}

bool WriteAsegEvent(PayloadWriter &writer, const BrxAsegEventRecord &event, std::string &error)
{
    if (!FiniteFloats(event.floats.data(), event.floats.size()))
    {
        error = "ASEG event contains a non-finite value.";
        return false;
    }
    writer.Pod(event.type);
    if (event.lane == BrxAsegEventLane::Frame)
        writer.Pod(event.frame);
    switch (event.type)
    {
    case -1:
        break;
    case 0:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        writer.Pod(event.integers[0]);
        writer.Pod(event.integers[1]);
        writer.Pod(event.floats[0]);
        writer.Pod(event.floats[1]);
        writer.Pod(event.floats[2]);
        break;
    case 4:
    case 7:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.floats[0]);
        break;
    case 5:
    case 33:
    case 57:
    case 58:
        writer.Pod(event.floats[0]);
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
        writer.Pod(event.shorts[0]);
        break;
    case 9:
    case 10:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.floats[0]);
        break;
    case 16:
        if (event.shorts[1] < (std::numeric_limits<std::int8_t>::min)() ||
            event.shorts[1] > (std::numeric_limits<std::int8_t>::max)() ||
            event.shorts[2] < (std::numeric_limits<std::int8_t>::min)() ||
            event.shorts[2] > (std::numeric_limits<std::int8_t>::max)())
        {
            error = "ASEG interact force/torque count exceeds S8.";
            return false;
        }
        writer.Pod(event.shorts[0]);
        writer.Pod(static_cast<std::int8_t>(event.shorts[1]));
        writer.Pod(static_cast<std::int8_t>(event.shorts[2]));
        break;
    case 19:
    case 20:
        if (event.shorts[1] < 0 || event.shorts[1] > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "ASEG puppet snap flag exceeds U8.";
            return false;
        }
        writer.Pod(event.shorts[0]);
        writer.Pod(static_cast<std::uint8_t>(event.shorts[1]));
        break;
    case 21:
    case 23:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.floats[0]);
        writer.Pod(event.floats[1]);
        break;
    case 25:
    case 26:
        // Wipe loads into a 32-byte C array and immediately passes data() as a
        // C string, so unlike ReadFixedString32 it requires an in-field null.
        if (event.text.size() >= 32u || event.text.find('\0') != std::string::npos)
        {
            error = "ASEG wipe level name must be null-terminated inside 32 bytes.";
            return false;
        }
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        WriteFixed32(writer, event.text);
        break;
    case 27:
        if (event.shorts[0] < 0 || event.shorts[0] > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "ASEG default-exit inhibit flag exceeds U8.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(event.shorts[0]));
        writer.Pod(event.shorts[1]);
        break;
    case 28:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        writer.Pod(event.floats[0]);
        writer.Pod(event.floats[1]);
        break;
    case 37:
    case 38:
    case 45:
    case 47:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        break;
    case 40:
        writer.Pod(static_cast<std::uint16_t>(event.shorts[0]));
        break;
    case 44:
    case 51:
        if (event.text.size() > 32u)
        {
            error = "ASEG fixed resource name exceeds 32 bytes.";
            return false;
        }
        WriteFixed32(writer, event.text);
        break;
    case 46:
    case 52:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        writer.Pod(event.shorts[2]);
        break;
    case 48:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        writer.Pod(event.shorts[2]);
        writer.Pod(event.floats[0]);
        break;
    case 54:
        writer.Pod(event.shorts[0]);
        writer.Pod(event.shorts[1]);
        if (event.text.size() > (std::numeric_limits<std::uint16_t>::max)())
        {
            error = "ASEG credit string exceeds U16 bytes.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(event.text.size()));
        writer.Bytes(reinterpret_cast<const std::uint8_t *>(event.text.data()), event.text.size());
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
        error = "ASEG event type is unsupported.";
        return false;
    }
    return true;
}

bool WriteAsegPayload(PayloadWriter &writer, const BrxAsegRecord &segment,
                      const std::vector<BrxPropertyRecord> &properties, std::string &error)
{
    if (segment.searchRootOids.size() > (std::numeric_limits<std::uint16_t>::max)() || segment.channels.size() > 127u)
    {
        error = "ASEG search-root/channel count is invalid.";
        return false;
    }
    writer.Pod(segment.rootOid);
    writer.Pod(segment.maximumFrame);
    writer.Pod(static_cast<std::uint16_t>(segment.searchRootOids.size()));
    for (const auto oid : segment.searchRootOids)
        writer.Pod(oid);
    writer.Pod(static_cast<std::int8_t>(segment.channels.size()));
    for (const auto &channel : segment.channels)
    {
        writer.Pod(channel.targetOid);
        writer.Pod(channel.flags);
        if ((channel.flags & 0x01u) != 0 && !WriteAsegVector(writer, channel.position, error))
            return false;
        if ((channel.flags & 0x02u) != 0 && !WriteAsegVector(writer, channel.rotation, error))
            return false;
        if ((channel.flags & 0x04u) != 0 && !WriteAsegVector(writer, channel.scale, error))
            return false;
        if ((channel.flags & 0x08u) != 0 && !WriteAsegScalar(writer, channel.twist, error))
            return false;
        if ((channel.flags & 0x10u) != 0)
        {
            if (channel.poses.size() > (std::numeric_limits<std::uint8_t>::max)())
            {
                error = "ASEG pose-controller count exceeds U8.";
                return false;
            }
            writer.Pod(static_cast<std::uint8_t>(channel.poses.size()));
            for (const auto &pose : channel.poses)
            {
                if (pose.kind == -1)
                    writer.Pod(static_cast<std::int8_t>(-1));
                else if (!WriteAsegScalar(writer, pose, error))
                    return false;
            }
        }
        if ((channel.flags & 0xc0u) != 0)
        {
            error = "ASEG channel contains unsupported flag bits.";
            return false;
        }
    }
    for (const auto lane : {BrxAsegEventLane::Apply, BrxAsegEventLane::Frame, BrxAsegEventLane::Retract})
    {
        const auto count = std::count_if(segment.events.begin(), segment.events.end(),
                                         [&](const auto &event) { return event.lane == lane; });
        if (count > 127)
        {
            error = "ASEG event-lane count exceeds signed S8.";
            return false;
        }
        writer.Pod(static_cast<std::int8_t>(count));
        for (const auto &event : segment.events)
            if (event.lane == lane && !WriteAsegEvent(writer, event, error))
                return false;
    }
    return WriteOptionList(writer, properties, error);
}

bool WriteShaderAnimation(PayloadWriter &writer, const BrxShaderAnimationRecord &animation, std::string &error);

// CBinaryInputStream::ReadMatrix4 reads the retail BRX MATRIX4 encoding as
// four consecutive three-float column vectors (48 bytes).  The fourth lane
// of each in-memory GLM column is synthesized as 0, 0, 0, 1 and is not
// present in the file.
void WriteBrxMatrix4(PayloadWriter &writer, const float *matrix)
{
    for (std::size_t column = 0; column < 4; ++column)
        writer.Floats(matrix + column * 4, 3);
}

bool WriteGlobset(PayloadWriter &writer, const BrxDocument &document, const std::size_t objectIndex, std::string &error)
{
    const auto globsetCount =
        std::count_if(document.Globsets().begin(), document.Globsets().end(), [&](const auto &globset)
                      { return globset.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
    if (globsetCount != 1)
    {
        error = "ALO must own exactly one GLOBSET record.";
        return false;
    }
    const auto found = std::find_if(document.Globsets().begin(), document.Globsets().end(), [&](const auto &globset)
                                    { return globset.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
    if (found == document.Globsets().end())
    {
        error = "ALO GLOBSET record is missing.";
        return false;
    }
    const auto &globset = *found;
    if (objectIndex >= document.Objects().size() ||
        document.Objects()[objectIndex].globPoseCount != globset.poseWeights.size())
    {
        error = "ALO pose count does not match its GLOBSET pose table.";
        return false;
    }
    if (globset.boneOids.size() > (std::numeric_limits<std::uint8_t>::max)() ||
        globset.poseWeights.size() > (std::numeric_limits<std::uint8_t>::max)() ||
        globset.globIndices.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "GLOBSET bone/pose/GLOB count exceeds its field width.";
        return false;
    }
    if (std::unordered_set<std::size_t>(globset.globIndices.begin(), globset.globIndices.end()).size() !=
        globset.globIndices.size())
    {
        error = "GLOBSET contains duplicate GLOB records.";
        return false;
    }
    writer.Pod(static_cast<std::uint8_t>(globset.relight ? 1 : 0));
    // LoadGlobsetFromBrx reads and discards this byte. A from-scratch export
    // emits the canonical zero value instead of propagating source filler.
    writer.Pod(static_cast<std::uint8_t>(0));
    writer.Pod(static_cast<std::uint8_t>(globset.boneOids.size()));
    for (const auto oid : globset.boneOids)
        writer.Pod(oid);
    writer.Pod(static_cast<std::uint8_t>(globset.poseWeights.size()));
    for (const auto weight : globset.poseWeights)
    {
        if (!std::isfinite(weight))
        {
            error = "GLOBSET pose table contains a non-finite weight.";
            return false;
        }
        writer.Pod(weight);
    }
    writer.Pod(static_cast<std::uint16_t>(globset.globIndices.size()));
    for (std::size_t localGlobIndex = 0; localGlobIndex < globset.globIndices.size(); ++localGlobIndex)
    {
        const auto globalGlobIndex = globset.globIndices[localGlobIndex];
        if (globalGlobIndex >= document.Globs().size())
        {
            error = "GLOBSET references a missing GLOB record.";
            return false;
        }
        const auto &glob = document.Globs()[globalGlobIndex];
        if (glob.ownerObjectIndex != static_cast<std::int32_t>(objectIndex))
        {
            error = "GLOBSET references a GLOB owned by another object.";
            return false;
        }
        if (glob.rotationTracking >= 6 && glob.rotationTracking != 0xffu)
        {
            error = "GLOB has an invalid rotation-tracking mode.";
            return false;
        }
        if (glob.renderPhase >= 18 && glob.renderPhase != 0xffu)
        {
            error = "GLOB has an invalid render phase.";
            return false;
        }
        // Rebuild known bits from semantic editor state. A source record may
        // physically contain a FLT_MAX sort override that has no runtime
        // effect; a from-scratch export canonicalizes that to an omitted field.
        const bool serializeSort = glob.hasExplicitSortZ;
        std::uint16_t properties = 0;
        if (glob.instanced)
            properties |= 0x001u;
        if (glob.hasZoneMask)
            properties |= 0x002u;
        if (serializeSort)
            properties |= 0x004u;
        if (glob.hasFogScale)
            properties |= 0x008u;
        if (glob.hasMaximumRenderDistance)
            properties |= 0x010u;
        if (glob.hasCelMaximumRenderDistance)
            properties |= 0x020u;
        if (glob.hasShaderAnimation)
            properties |= 0x040u;
        if (glob.hasGleam)
            properties |= 0x080u;
        if (glob.hasWarp)
            properties |= 0x100u;
        if (glob.hasSubglobRadiusScale)
            properties |= 0x200u;
        writer.Pod(properties);
        if (glob.instanced)
        {
            if (glob.sourceGlobIndex < 0 || static_cast<std::size_t>(glob.sourceGlobIndex) >= localGlobIndex)
            {
                error = "Instanced GLOB has an invalid earlier-source index.";
                return false;
            }
            // BRX serializes the local source slot, not the editor's cached
            // document-global sourceRecordIndex.  The latter is derived state
            // and may be stale in an older project after object/GLOB deletion;
            // never let that cache block or alter the authoritative stream.
            writer.Pod(glob.sourceGlobIndex);
            constexpr std::array<std::size_t, 12> matrixIndices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            for (const auto index : matrixIndices)
                if (!std::isfinite(glob.instanceTransform[index]))
                {
                    error = "Instanced GLOB transform contains a non-finite value.";
                    return false;
                }
            WriteBrxMatrix4(writer, glob.instanceTransform);
        }
        if (glob.hasZoneMask)
            writer.Pod(glob.zoneMask);
        if (glob.hasSubglobRadiusScale)
        {
            if (!std::isfinite(glob.subglobRadiusScale) || glob.subglobRadiusScale < 0.0f)
            {
                error = "GLOB SUBGLOB radius scale is invalid.";
                return false;
            }
            writer.Pod(glob.subglobRadiusScale);
        }
        if (serializeSort)
        {
            if (!std::isfinite(glob.authoredSortZ) && glob.authoredSortZ != (std::numeric_limits<float>::max)())
            {
                error = "GLOB sort value is invalid.";
                return false;
            }
            writer.Pod(glob.authoredSortZ);
        }
        if (glob.hasFogScale)
        {
            if (!std::isfinite(glob.fogScale))
            {
                error = "GLOB fog scale is non-finite.";
                return false;
            }
            writer.Pod(glob.fogScale);
        }
        if (glob.hasMaximumRenderDistance)
        {
            if (!glob.maximumRenderDistanceInfinite && !std::isfinite(glob.authoredMaximumRenderDistance))
            {
                error = "GLOB maximum render distance is non-finite.";
                return false;
            }
            writer.Pod(glob.maximumRenderDistanceInfinite ? (std::numeric_limits<float>::max)()
                                                          : glob.authoredMaximumRenderDistance);
        }
        if (glob.hasCelMaximumRenderDistance)
        {
            if (!glob.celMaximumRenderDistanceInfinite && !std::isfinite(glob.authoredCelMaximumRenderDistance))
            {
                error = "GLOB cel-border render distance is non-finite.";
                return false;
            }
            writer.Pod(glob.celMaximumRenderDistanceInfinite ? (std::numeric_limits<float>::max)()
                                                             : glob.authoredCelMaximumRenderDistance);
        }
        if (glob.hasShaderAnimation && !WriteShaderAnimation(writer, glob.shaderAnimation, error))
            return false;
        if (glob.hasGleam)
        {
            if (!FiniteFloats(glob.gleamNormal, 3) || !FiniteFloats(glob.gleamCurve, 4))
            {
                error = "GLOB gleam payload contains a non-finite value.";
                return false;
            }
            writer.Floats(glob.gleamNormal, 3);
            writer.Floats(glob.gleamCurve, 4);
        }
        if (glob.hasWarp)
        {
            const auto warpCount = std::count_if(document.GlobWarps().begin(), document.GlobWarps().end(),
                                                 [&](const auto &candidate)
                                                 {
                                                     return candidate.ownerObjectIndex == glob.ownerObjectIndex &&
                                                            candidate.renderGroup == glob.renderGroup;
                                                 });
            if (warpCount != 1)
            {
                error = "Warped GLOB must own exactly one WRBG record.";
                return false;
            }
            const auto warp = std::find_if(document.GlobWarps().begin(), document.GlobWarps().end(),
                                           [&](const auto &candidate)
                                           {
                                               return candidate.ownerObjectIndex == glob.ownerObjectIndex &&
                                                      candidate.renderGroup == glob.renderGroup;
                                           });
            if (warp == document.GlobWarps().end())
            {
                error = "GLOB WRBG record is missing.";
                return false;
            }
            if (glob.warpEnvelopeKind < -1 || glob.warpEnvelopeKind >= 7 || glob.warpMatrixCount > 4 ||
                warp->matrixCount > 4 || warp->controllerOid != glob.warpControllerOid ||
                warp->envelopeKind != glob.warpEnvelopeKind || warp->matrixCount != glob.warpMatrixCount ||
                warp->deformPosition != glob.warpPosition || warp->deformUv != glob.warpUv)
            {
                error = "GLOB and WRBG authored fields disagree.";
                return false;
            }
            writer.Pod(glob.warpControllerOid);
            writer.Pod(glob.warpEnvelopeKind);
            if (glob.warpEnvelopeKind != -1)
            {
                if (!std::isfinite(warp->innerDistance) || !std::isfinite(warp->innerWeight) ||
                    !std::isfinite(warp->outerDistance) || !std::isfinite(warp->outerWeight))
                {
                    error = "GLOB warp envelope contains a non-finite value.";
                    return false;
                }
                constexpr std::array<std::size_t, 12> matrixIndices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
                for (const auto index : matrixIndices)
                    if (!std::isfinite(warp->envelopeMatrix[index]))
                    {
                        error = "GLOB warp envelope matrix contains a non-finite value.";
                        return false;
                    }
                writer.Pod(warp->innerDistance);
                writer.Pod(warp->innerWeight);
                writer.Pod(warp->outerDistance);
                writer.Pod(warp->outerWeight);
                WriteBrxMatrix4(writer, warp->envelopeMatrix);
            }
            writer.Pod(glob.warpMatrixCount);
            writer.Pod(static_cast<std::uint8_t>(glob.warpPosition ? 1 : 0));
            writer.Pod(static_cast<std::uint8_t>(glob.warpUv ? 1 : 0));
        }
        const float emptyBoundsSentinel = (std::numeric_limits<float>::lowest)();
        const bool emptyBounds = glob.radius == emptyBoundsSentinel && glob.center[0] == emptyBoundsSentinel &&
                                 glob.center[1] == emptyBoundsSentinel && glob.center[2] == emptyBoundsSentinel;
        // Retail PCBRX uses (-FLT_MAX, -FLT_MAX, -FLT_MAX), radius -FLT_MAX
        // for GLOB slots that have no calculated geometry bounds. ProjectCane
        // reads and retains this finite sentinel verbatim. Admit only the full
        // sentinel tuple; arbitrary negative radii remain malformed.
        if (!FiniteFloats(glob.center, 3) || !std::isfinite(glob.radius) || (glob.radius < 0.0f && !emptyBounds))
        {
            error = "GLOB render group " + std::to_string(glob.renderGroup) + " has invalid bounds.";
            return false;
        }
        float placedCenter[3]{glob.center[0], glob.center[1], glob.center[2]};
        float placedRadius = glob.radius;
        // Native instances serialize their changed matrix above, but retail
        // does not derive GLOB bounds from it. Export the matching bound too.
        // Direct geometry retains its own existing baking/serialization path.
        if (glob.instanced && !document.PlacedGlobBounds(globalGlobIndex, placedCenter, placedRadius))
        {
            error = "GLOB placement produces invalid culling bounds.";
            return false;
        }
        writer.Floats(placedCenter, 3);
        writer.Pod(placedRadius);
        writer.Pod(glob.oid);
        writer.Pod(glob.rotationTracking);
        writer.Pod(glob.renderPhase);
        writer.Pod(glob.flags);
        if (glob.instanced)
            continue;
        if (glob.sourceSubglobs.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            glob.sourceCelRecords.size() > (std::numeric_limits<std::uint16_t>::max)())
        {
            error = "GLOB SUBGLOB/SUBCEL count exceeds U16.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(glob.sourceSubglobs.size()));
        for (const auto &subglob : glob.sourceSubglobs)
        {
            if (subglob.vertices.size() > (std::numeric_limits<std::uint16_t>::max)() ||
                subglob.triangles.size() > (std::numeric_limits<std::uint16_t>::max)() ||
                subglob.posePositionDeltas.size() != subglob.vertices.size() * globset.poseWeights.size() ||
                subglob.poseNormalDeltas.size() != subglob.vertices.size() * globset.poseWeights.size())
            {
                error = "SUBGLOB count/pose-delta contract is invalid.";
                return false;
            }
            if (subglob.boneCount > globset.boneOids.size())
            {
                error = "SUBGLOB bone count exceeds its GLOBSET bone table.";
                return false;
            }
            if (!FiniteFloats(subglob.center, 3) || !std::isfinite(subglob.radius) || subglob.radius < 0.0f ||
                !std::isfinite(subglob.selfIllumination))
            {
                error = "SUBGLOB bounds/material values are invalid.";
                return false;
            }
            writer.Floats(subglob.center, 3);
            writer.Pod(subglob.radius);
            writer.Pod(static_cast<std::uint16_t>(subglob.vertices.size()));
            writer.Pod(static_cast<std::uint16_t>(subglob.triangles.size()));
            if (subglob.shaderIndex >= document.Shaders().size())
            {
                error = "SUBGLOB references an invalid shader.";
                return false;
            }
            writer.Pod(subglob.shaderIndex);
            writer.Pod(subglob.boneCount);
            writer.Pod(subglob.selfIllumination);
            for (const auto &vertex : subglob.vertices)
            {
                if (!FiniteFloats(vertex.position, 3) || !FiniteFloats(vertex.normal, 3) ||
                    !FiniteFloats(vertex.color, 4) || !FiniteFloats(vertex.uv, 2) ||
                    !FiniteFloats(vertex.boneWeights, 4))
                {
                    error = "SUBGLOB vertex contains a non-finite value.";
                    return false;
                }
                for (std::size_t influence = 0; influence < 4; ++influence)
                    if (vertex.boneWeights[influence] != 0.0f &&
                        vertex.boneIndices[influence] >= globset.boneOids.size())
                    {
                        error = "SUBGLOB vertex references an invalid bone.";
                        return false;
                    }
                writer.Floats(vertex.position, 3);
                writer.Floats(vertex.normal, 3);
                writer.Floats(vertex.color, 4);
                writer.Floats(vertex.uv, 2);
                for (const auto bone : vertex.boneIndices)
                    writer.Pod(bone);
                writer.Floats(vertex.boneWeights, 4);
            }
            for (const auto &triangle : subglob.triangles)
            {
                if (triangle[0] >= subglob.vertices.size() || triangle[1] >= subglob.vertices.size() ||
                    triangle[2] >= subglob.vertices.size())
                {
                    error = "SUBGLOB triangle references an invalid vertex.";
                    return false;
                }
                writer.Pod(triangle[0]);
                writer.Pod(triangle[1]);
                writer.Pod(triangle[2]);
            }
            for (std::size_t delta = 0; delta < subglob.posePositionDeltas.size(); ++delta)
            {
                if (!FiniteFloats(subglob.posePositionDeltas[delta].data(), 4) ||
                    !FiniteFloats(subglob.poseNormalDeltas[delta].data(), 4))
                {
                    error = "SUBGLOB pose delta contains a non-finite value.";
                    return false;
                }
                writer.Floats(subglob.posePositionDeltas[delta].data(), 4);
                writer.Floats(subglob.poseNormalDeltas[delta].data(), 4);
            }
        }
        writer.Pod(static_cast<std::uint16_t>(glob.sourceCelRecords.size()));
        for (const auto &cel : glob.sourceCelRecords)
        {
            if (cel.points.size() % 4u != 0 || cel.points.size() / 4u > (std::numeric_limits<std::uint32_t>::max)() ||
                cel.posePositionDeltas.size() != cel.points.size() * globset.poseWeights.size())
            {
                error = "SUBCEL point/pose-delta contract is invalid.";
                return false;
            }
            writer.Pod(static_cast<std::uint32_t>(cel.points.size() / 4u));
            for (const auto &point : cel.points)
            {
                if (!FiniteFloats(point.position, 4) || !FiniteFloats(point.boneWeights, 4))
                {
                    error = "SUBCEL point contains a non-finite value.";
                    return false;
                }
                for (std::size_t influence = 0; influence < 4; ++influence)
                    if (point.boneWeights[influence] != 0.0f && point.boneIndices[influence] >= globset.boneOids.size())
                    {
                        error = "SUBCEL point references an invalid bone.";
                        return false;
                    }
                writer.Floats(point.position, 4);
                for (const auto bone : point.boneIndices)
                    writer.Pod(bone);
                writer.Floats(point.boneWeights, 4);
            }
            for (const auto &delta : cel.posePositionDeltas)
            {
                if (!FiniteFloats(delta.data(), 4))
                {
                    error = "SUBCEL pose delta contains a non-finite value.";
                    return false;
                }
                writer.Floats(delta.data(), 4);
            }
        }
    }
    return true;
}

bool WriteAnyObject(PayloadWriter &writer, const BrxDocument &document, std::size_t objectIndex, std::string &error);

const BrxObjectRecord &ObjectSourceStateForSerialization(const BrxDocument &document, const std::size_t objectIndex,
                                                         std::optional<BrxObjectRecord> &storage)
{
    const auto &object = document.Objects()[objectIndex];
    const BrxProxyOverrideRecord *inherited = nullptr;
    const auto findCapturedState = [&](const BrxProxyRecord &proxy)
    {
        const auto found =
            std::find_if(proxy.overrides.begin(), proxy.overrides.end(), [&](const BrxProxyOverrideRecord &candidate)
                         { return candidate.targetObjectIndex == static_cast<std::int32_t>(objectIndex); });
        return found == proxy.overrides.end() ? nullptr : &*found;
    };

    // A nested PROXY is decoded and applies its own override while its outer
    // source is still loading.  Its captured state is therefore earlier than
    // an outer packet even though the outer proxy record was allocated first.
    // Prefer the object's nearest proxy identity, then fall back to an outer
    // packet when only the outer instance targets this nested member.
    if (object.proxyRecordIndex >= 0 && static_cast<std::size_t>(object.proxyRecordIndex) < document.Proxies().size())
        inherited = findCapturedState(document.Proxies()[static_cast<std::size_t>(object.proxyRecordIndex)]);
    if (inherited == nullptr)
    {
        for (const auto &proxy : document.Proxies())
        {
            inherited = findCapturedState(proxy);
            if (inherited != nullptr)
                break;
        }
    }
    if (inherited == nullptr)
        return object;

    // LoadProxyFromBrx applies an instance's override after its reusable root
    // has been decoded. Overrides from an outer PROXY may target an object whose
    // nearest proxyRecordIndex belongs to a nested PROXY, so limiting this
    // lookup to the nearest owner alone bakes an outer-only instance state into
    // the nested reusable source. The nearest-first search above preserves a
    // nested packet's earlier raw state while still finding outer-only packets.
    storage = object;
    storage->spliceIndex = inherited->inheritedSpliceIndex;
    storage->zoneMask = inherited->inheritedZoneMask;
    storage->mrdMode = inherited->inheritedMrdMode;
    storage->maximumRenderDistance = inherited->inheritedMaximumRenderDistance;
    if (inherited->inheritedPropertiesCaptured)
        storage->properties = inherited->inheritedProperties;
    return *storage;
}

bool WriteSharedObject(PayloadWriter &writer, const BrxDocument &document, const std::size_t objectIndex,
                       const bool includeHeader, std::string &error)
{
    const auto &objects = document.Objects();
    if (objectIndex >= objects.size())
    {
        error = "Object index is outside the document.";
        return false;
    }
    std::optional<BrxObjectRecord> sourceState;
    const auto &object = ObjectSourceStateForSerialization(document, objectIndex, sourceState);
    const auto layout = WriterLayoutForCid(object.cid);
    const auto sharedLayout = layout == CidWriterLayout::Cnvo
                                  ? CidWriterLayout::So
                                  : (layout == CidWriterLayout::Emitter || layout == CidWriterLayout::Dialog ||
                                             layout == CidWriterLayout::Flash
                                         ? CidWriterLayout::Alo
                                         : layout);
    if (sharedLayout != CidWriterLayout::Lo && sharedLayout != CidWriterLayout::Alo &&
        sharedLayout != CidWriterLayout::So)
    {
        error = "CID " + std::to_string(object.cid) + " is not a shared LO/ALO/SO layout.";
        return false;
    }
    if (includeHeader)
    {
        writer.Pod(object.cid);
        writer.Pod(object.oid);
        writer.Pod(object.spliceIndex);
    }
    if (sharedLayout == CidWriterLayout::Lo)
        return WriteOptionList(writer, object.properties, error);

    if (sharedLayout == CidWriterLayout::So)
    {
        if (!object.so.present)
        {
            error = "SO payload is unavailable for CID " + std::to_string(object.cid) + ".";
            return false;
        }
        if (!std::isfinite(object.so.mass) || !FiniteFloats(object.so.inertia, 9) ||
            !FiniteFloats(object.so.centerOfMass, 3))
        {
            error = "SO rigid-body payload contains a non-finite value.";
            return false;
        }
        // LoadSoFromBrx consumes only bit zero. Do not preserve ignored source
        // bits in a structured rebuild.
        writer.Pod(static_cast<std::uint8_t>(object.so.fixedPhysics ? 1u : 0u));
        if (!WriteSoGeometry(writer, object.so.collisionGeometry, error))
            return false;
        writer.Pod(object.so.mass);
        writer.Floats(object.so.inertia, 9);
        writer.Floats(object.so.centerOfMass, 3);
        if (object.so.cameraGroups.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) ||
            object.so.collisionGeometry.bspNodes.size() > (std::numeric_limits<std::uint32_t>::max)() ||
            object.so.hitSurfaceGroups.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()))
        {
            error = "SO camera/hit-surface group count exceeds the runtime's signed mapping range.";
            return false;
        }
        const auto globset =
            std::find_if(document.Globsets().begin(), document.Globsets().end(), [&](const auto &candidate)
                         { return candidate.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (globset == document.Globsets().end())
        {
            error = "SO GLOBSET record is missing.";
            return false;
        }
        if (object.offset == 0 && object.so.fixedPhysics && object.so.collisionGeometry.surfaces.empty())
        {
            const bool ownsVisibleGeometry = std::any_of(
                globset->globIndices.begin(), globset->globIndices.end(), [&](const std::size_t globIndex)
                { return globIndex < document.Globs().size() && !document.Globs()[globIndex].sourceSubglobs.empty(); });
            if (ownsVisibleGeometry)
            {
                error =
                    "Editor-created fixed SO owns a visible model but has no gameplay collision; regenerate collision before export.";
                return false;
            }
        }
        for (const auto &group : object.so.cameraGroups)
            if (group.globIndex >= globset->globIndices.size())
            {
                error = "SO camera group references an invalid local GLOB.";
                return false;
            }
        // ProjectCane stores these entries as signed 16-bit values and CM's
        // camera-occlusion pass deliberately ignores every value outside
        // [0, cnpg). Retail data may therefore use negative or positive
        // out-of-range inert sentinels; preserve them instead of rejecting a
        // payload the game accepts. The int16_t element type enforces width.
        for (const auto &group : object.so.hitSurfaceGroups)
        {
            if (group.globIndex < 0 || static_cast<std::size_t>(group.globIndex) >= globset->globIndices.size())
            {
                error = "SO hit-surface group references an invalid local GLOB.";
                return false;
            }
            const auto documentGlobIndex = globset->globIndices[static_cast<std::size_t>(group.globIndex)];
            if (documentGlobIndex >= document.Globs().size())
            {
                error = "SO hit-surface group references a missing GLOB record.";
                return false;
            }
            const BrxGlobRecord *geometryGlob = &document.Globs()[documentGlobIndex];
            std::size_t instanceDepth = 0;
            while (geometryGlob->instanced)
            {
                if (++instanceDepth > globset->globIndices.size() || geometryGlob->sourceGlobIndex < 0 ||
                    static_cast<std::size_t>(geometryGlob->sourceGlobIndex) >= globset->globIndices.size())
                {
                    error = "SO hit-surface group uses an instance with an invalid or cyclic source GLOB chain.";
                    return false;
                }
                const auto sourceDocumentIndex =
                    globset->globIndices[static_cast<std::size_t>(geometryGlob->sourceGlobIndex)];
                if (sourceDocumentIndex >= document.Globs().size())
                {
                    error = "SO hit-surface group cannot resolve its instance source geometry.";
                    return false;
                }
                geometryGlob = &document.Globs()[sourceDocumentIndex];
            }
            if (group.subglobIndex < 0 ||
                static_cast<std::size_t>(group.subglobIndex) >= geometryGlob->sourceSubglobs.size())
            {
                error = "SO hit-surface group references an invalid SUBGLOB.";
                return false;
            }
        }
        writer.Pod(static_cast<std::uint16_t>(object.so.cameraGroups.size()));
        for (const auto &group : object.so.cameraGroups)
        {
            writer.Pod(group.cameraStyle);
            writer.Pod(group.globIndex);
        }
        // ClsgClipEdgeToBsp indexes this table by gameplay BSP-node index.
        // Collision rebuilding can change that node count, so emit a safe
        // parallel table: retain mappings for surviving nodes, initialize new
        // nodes to the runtime's inert -1 value, and omit stale tail entries.
        writer.Pod(static_cast<std::uint32_t>(object.so.collisionGeometry.bspNodes.size()));
        for (std::size_t nodeIndex = 0; nodeIndex < object.so.collisionGeometry.bspNodes.size(); ++nodeIndex)
        {
            const std::int16_t group = nodeIndex < object.so.bspNodeToCameraGroup.size()
                                           ? object.so.bspNodeToCameraGroup[nodeIndex]
                                           : static_cast<std::int16_t>(-1);
            writer.Pod(group);
        }
        writer.Pod(static_cast<std::uint32_t>(object.so.hitSurfaceGroups.size()));
        for (const auto &group : object.so.hitSurfaceGroups)
        {
            writer.Pod(group.globIndex);
            writer.Pod(group.subglobIndex);
        }
        if (!object.so.hitSurfaceGroups.empty())
        {
            if (object.so.surfaceToHitSurfaceGroup.size() != object.so.collisionGeometry.surfaces.size() + 1u)
            {
                error = "SO surface-to-hit-group map has the wrong length.";
                return false;
            }
            if (object.so.surfaceToHitSurfaceGroup.front() != 0 ||
                object.so.surfaceToHitSurfaceGroup.back() !=
                    static_cast<std::int32_t>(object.so.hitSurfaceGroups.size()) ||
                !std::is_sorted(object.so.surfaceToHitSurfaceGroup.begin(), object.so.surfaceToHitSurfaceGroup.end()))
            {
                error = "SO surface-to-hit-group map is not a valid monotonic range table.";
                return false;
            }
            for (const auto group : object.so.surfaceToHitSurfaceGroup)
                if (group < 0 || group > static_cast<std::int32_t>(object.so.hitSurfaceGroups.size()) ||
                    group > (std::numeric_limits<std::int16_t>::max)())
                {
                    error = "SO surface-to-hit-group map contains an invalid range endpoint.";
                    return false;
                }
            // LoadSoFromBrx consumes this table with S16Read(), not S32Read().
            // BrxSoGeometryInfo retains a wider editor-side type for safe
            // arithmetic, but the authored stream is exactly two bytes per
            // surface endpoint.
            for (const auto group : object.so.surfaceToHitSurfaceGroup)
                writer.Pod(static_cast<std::int16_t>(group));
        }
        else if (!object.so.surfaceToHitSurfaceGroup.empty())
        {
            error = "SO has a surface map without hit-surface groups.";
            return false;
        }
        if (!WriteSoGeometry(writer, object.so.cameraGeometry, error))
            return false;
    }

    constexpr std::array<std::size_t, 12> serializedTransformIndices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    for (const auto index : serializedTransformIndices)
        if (!std::isfinite(object.localTransform[index]))
        {
            error = "ALO local transform contains a non-finite value.";
            return false;
        }
    if (object.zoneMode >= 3u || object.visibilityMode >= 3u || object.mrdMode >= 3u)
    {
        error = "ALO zone/visibility/MRD mode is outside the runtime enum range.";
        return false;
    }
    if ((!object.maximumRenderDistanceDefault && !std::isfinite(object.maximumRenderDistance)) ||
        (!object.celBorderMaximumDistanceDefault && !std::isfinite(object.celBorderMaximumDistance)) ||
        !std::isfinite(object.renderRadiusSelf) || object.renderRadiusSelf < 0.0f ||
        !std::isfinite(object.renderRadiusAll) || object.renderRadiusAll < 0.0f)
    {
        error = "ALO render bounds contain an invalid value.";
        return false;
    }
    for (const auto index : {0, 1, 2, 4, 5, 6, 8, 9, 10})
        writer.Pod(object.localTransform[index]);
    writer.Pod(object.localTransform[12]);
    writer.Pod(object.localTransform[13]);
    writer.Pod(object.localTransform[14]);
    writer.Pod(object.zoneMode);
    writer.Pod(object.visibilityMode);
    writer.Pod(object.mrdMode);
    writer.Pod(object.zoneMask);
    writer.Pod(object.maximumRenderDistanceDefault ? (std::numeric_limits<float>::max)()
                                                   : object.maximumRenderDistance);
    writer.Pod(object.celBorderMaximumDistanceDefault ? (std::numeric_limits<float>::max)()
                                                      : object.celBorderMaximumDistance);
    writer.Pod(object.renderRadiusSelf);
    writer.Pod(object.renderRadiusAll);
    if (!WriteOptionList(writer, object.properties, error))
        return false;
    if (!WriteGlobset(writer, document, objectIndex, error))
        return false;
    if (!WriteAlox(writer, object.alox, error))
        return false;
    if (object.poseCombinations.size() > (std::numeric_limits<std::uint8_t>::max)())
    {
        error = "ALO pose-combination count exceeds U8.";
        return false;
    }
    writer.Pod(static_cast<std::uint8_t>(object.poseCombinations.size()));
    for (const auto &combination : object.poseCombinations)
    {
        if (combination.weights.size() != object.globPoseCount)
        {
            error = "ALO pose-combination weight count does not match the GLOBSET.";
            return false;
        }
        if (!FiniteFloats(combination.weights.data(), combination.weights.size()))
        {
            error = "ALO pose-combination contains a non-finite weight.";
            return false;
        }
        writer.Pod(combination.oid);
        for (const auto weight : combination.weights)
            writer.Pod(weight);
    }
    const auto children = AuthoredChildren(document, objectIndex);
    if (children.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "ALO child count exceeds U16.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(children.size()));
    for (const auto child : children)
        if (!WriteAnyObject(writer, document, child, error))
            return false;
    if (layout == CidWriterLayout::Cnvo)
    {
        if (!object.hasCnvoPayload)
        {
            error = "CNVO trailing payload is unavailable.";
            return false;
        }
        if (!std::isfinite(object.cnvoAverageUVelocity) || !std::isfinite(object.cnvoAverageVVelocity))
        {
            error = "CNVO velocity payload contains a non-finite value.";
            return false;
        }
        writer.Pod(object.cnvoAverageUVelocity);
        writer.Pod(object.cnvoAverageVVelocity);
    }
    else if (layout == CidWriterLayout::Emitter)
    {
        const auto found = std::find_if(document.Effects().begin(), document.Effects().end(), [&](const auto &effect)
                                        { return effect.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Effects().end())
        {
            error = "EMITTER effect record is missing.";
            return false;
        }
        if (!WriteEffectTail(writer, *found, error))
            return false;
    }
    else if (layout == CidWriterLayout::Flash)
    {
        const auto found = std::find_if(document.Volumes().begin(), document.Volumes().end(), [&](const auto &volume)
                                        { return volume.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Volumes().end())
        {
            error = "FLASH TBSP record is missing.";
            return false;
        }
        if (!WriteVolumeTail(writer, *found, error))
            return false;
    }
    else if (layout == CidWriterLayout::Dialog)
    {
        if (!object.dialog.present || object.dialog.events.size() > 127u)
        {
            error = "DIALOG event payload/count is invalid.";
            return false;
        }
        writer.Pod(static_cast<std::int8_t>(object.dialog.events.size()));
        for (const auto &event : object.dialog.events)
        {
            const auto kind = static_cast<std::int8_t>(event.kind);
            writer.Pod(kind);
            switch (kind)
            {
            case 0:
            case 7:
            {
                if (event.resourceName.size() > 32u)
                {
                    error = "DIALOG fixed resource name exceeds 32 bytes.";
                    return false;
                }
                const auto count = event.resourceName.size();
                writer.Bytes(reinterpret_cast<const std::uint8_t *>(event.resourceName.data()), count);
                writer.Zeroes(32u - count);
                break;
            }
            case 1:
            case 2:
                writer.Pod(event.primaryOid);
                writer.Pod(event.replacementOid);
                break;
            case 3:
                if (!std::isfinite(event.zoom) || !std::isfinite(event.pauseSeconds))
                {
                    error = "DIALOG speech event contains a non-finite value.";
                    return false;
                }
                if (event.promptKind >= 15u)
                {
                    error = "DIALOG prompt kind is outside the retail DPK range.";
                    return false;
                }
                writer.Pod(event.zoom);
                writer.Pod(event.pauseSeconds);
                writer.Pod(static_cast<std::uint8_t>(event.waitForInput ? 1 : 0));
                writer.Pod(event.promptKind);
                writer.Pod(event.speakerOid);
                writer.Pod(event.lipsyncAnimationOid);
                writer.Pod(event.focusOid);
                writer.Pod(event.anchorPointOid);
                if (event.subtitle.size() > (std::numeric_limits<std::uint16_t>::max)())
                {
                    error = "DIALOG subtitle exceeds U16 bytes.";
                    return false;
                }
                writer.Pod(static_cast<std::uint16_t>(event.subtitle.size()));
                writer.Bytes(reinterpret_cast<const std::uint8_t *>(event.subtitle.data()), event.subtitle.size());
                break;
            case 4:
                writer.Pod(event.primaryOid);
                break;
            case 5:
            case 6:
                break;
            default:
                error = "DIALOG event kind is invalid.";
                return false;
            }
        }
    }
    return true;
}

const BrxShapePathRecord *ShapeForObject(const BrxDocument &document, const std::size_t objectIndex)
{
    const auto found = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(), [&](const auto &shape)
                                    { return shape.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
    return found == document.ShapePaths().end() ? nullptr : &*found;
}

bool WriteShapeCurve(PayloadWriter &writer, const BrxShapePathRecord &shape, std::string &error)
{
    if (shape.points.size() > (std::numeric_limits<std::uint8_t>::max)())
    {
        error = "SHAPE control-point count exceeds U8.";
        return false;
    }
    if (shape.hermite && shape.points.empty())
    {
        error = "A cubic SHAPE cannot be empty: ProjectCane measures control point zero while loading it.";
        return false;
    }
    if (shape.parameters.size() != shape.points.size() ||
        (shape.hermite &&
         (shape.tangentsIn.size() != shape.points.size() || shape.tangentsOut.size() != shape.points.size())))
    {
        error = "SHAPE curve arrays have inconsistent lengths.";
        return false;
    }
    writer.Pod(static_cast<std::uint8_t>(shape.hermite ? 1 : 0));
    writer.Pod(static_cast<std::uint8_t>(shape.closed ? 1 : 0));
    writer.Pod(static_cast<std::uint8_t>(shape.points.size()));
    for (std::size_t index = 0; index < shape.points.size(); ++index)
    {
        if (!std::isfinite(shape.parameters[index]) || !FiniteFloats(shape.points[index].data(), 3) ||
            (shape.hermite &&
             (!FiniteFloats(shape.tangentsIn[index].data(), 3) || !FiniteFloats(shape.tangentsOut[index].data(), 3))))
        {
            error = "SHAPE curve contains a non-finite value.";
            return false;
        }
        writer.Pod(shape.parameters[index]);
        writer.Floats(shape.points[index].data(), 3);
        if (shape.hermite)
        {
            writer.Floats(shape.tangentsIn[index].data(), 3);
            writer.Floats(shape.tangentsOut[index].data(), 3);
        }
    }
    return true;
}

bool WriteSimpleCustomObject(PayloadWriter &writer, const BrxDocument &document, const std::size_t objectIndex,
                             std::string &error)
{
    std::optional<BrxObjectRecord> sourceState;
    const auto &object = ObjectSourceStateForSerialization(document, objectIndex, sourceState);
    const auto layout = WriterLayoutForCid(object.cid);
    writer.Pod(object.cid);
    writer.Pod(object.oid);
    writer.Pod(object.spliceIndex);
    const auto writeMatrixPosition = [&]() -> bool
    {
        constexpr std::array<std::size_t, 12> indices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (const auto index : indices)
            if (!std::isfinite(object.localTransform[index]))
            {
                error = "Object transform contains a non-finite value.";
                return false;
            }
        for (const auto index : {0, 1, 2, 4, 5, 6, 8, 9, 10})
            writer.Pod(object.localTransform[index]);
        writer.Pod(object.localTransform[12]);
        writer.Pod(object.localTransform[13]);
        writer.Pod(object.localTransform[14]);
        return true;
    };
    if (layout == CidWriterLayout::Proxy)
    {
        const auto found = std::find_if(document.Proxies().begin(), document.Proxies().end(), [&](const auto &proxy)
                                        { return proxy.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Proxies().end())
        {
            error = "PROXY record is missing.";
            return false;
        }
        const auto currentProxyRecordIndex =
            static_cast<std::int32_t>(std::distance(document.Proxies().begin(), found));
        if (!writeMatrixPosition())
            return false;
        if (found->roots.size() > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "PROXY root count exceeds U8.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(found->roots.size()));
        for (std::size_t proxyRootIndex = 0; proxyRootIndex < found->roots.size(); ++proxyRootIndex)
        {
            const auto &root = found->roots[proxyRootIndex];
            if (root.kind == BrxProxyRootKind::SourceReference)
            {
                std::int32_t sourceProxyRecordIndex = root.sourceProxyRecordIndex;
                std::int32_t sourceProxyRootIndex = root.sourceProxyRootIndex;
                // The materialized clone retains a direct link to the source
                // object. Resolve through that link first: proxy record-vector
                // indices are editor bookkeeping and can be compacted by a
                // hierarchy deletion, while the source object identity is the
                // relationship the runtime reference actually represents.
                if (root.instantiatedRootObjectIndex >= 0 &&
                    static_cast<std::size_t>(root.instantiatedRootObjectIndex) < document.Objects().size())
                {
                    const auto &cloneRoot =
                        document.Objects()[static_cast<std::size_t>(root.instantiatedRootObjectIndex)];
                    const auto sourceObjectIndex = cloneRoot.proxySourceObjectIndex;
                    if (sourceObjectIndex >= 0)
                    {
                        std::uint32_t sourceMatches = 0;
                        for (std::size_t candidateProxy = 0; candidateProxy < document.Proxies().size();
                             ++candidateProxy)
                        {
                            const auto &candidate = document.Proxies()[candidateProxy];
                            if (candidate.ownerObjectIndex >= 0 &&
                                static_cast<std::size_t>(candidate.ownerObjectIndex) < document.Objects().size() &&
                                document.Objects()[static_cast<std::size_t>(candidate.ownerObjectIndex)].proxyClone)
                                continue;
                            for (std::size_t candidateRoot = 0; candidateRoot < candidate.roots.size(); ++candidateRoot)
                            {
                                const auto &candidateSource = candidate.roots[candidateRoot];
                                if (candidateSource.kind != BrxProxyRootKind::InlineSource ||
                                    candidateSource.instantiatedRootObjectIndex != sourceObjectIndex)
                                    continue;
                                sourceProxyRecordIndex = static_cast<std::int32_t>(candidateProxy);
                                sourceProxyRootIndex = static_cast<std::int32_t>(candidateRoot);
                                ++sourceMatches;
                            }
                        }
                        if (sourceMatches > 1)
                        {
                            error = "PROXY source object resolves to multiple inline source definitions.";
                            return false;
                        }
                    }
                }
                std::uint16_t rebuiltSourceIndex = 0;
                if (!writer.FindProxySource(sourceProxyRecordIndex, sourceProxyRootIndex, rebuiltSourceIndex))
                {
                    error = "PROXY source reference precedes or cannot identify its inline source.";
                    return false;
                }
                writer.Pod(static_cast<std::int16_t>(-1));
                writer.Pod(rebuiltSourceIndex);
                continue;
            }
            if (root.instantiatedRootObjectIndex < 0 ||
                static_cast<std::size_t>(root.instantiatedRootObjectIndex) >= document.Objects().size())
            {
                error = "PROXY inline source root is invalid.";
                return false;
            }
            const auto rootIndex = static_cast<std::size_t>(root.instantiatedRootObjectIndex);
            const auto &rootObject = document.Objects()[rootIndex];
            if (rootObject.proxyClone || rootObject.proxyRecordIndex != object.proxyRecordIndex ||
                rootObject.proxyRootIndex != static_cast<std::int32_t>(proxyRootIndex))
            {
                error = "PROXY inline source identity is inconsistent.";
                return false;
            }
            if (root.cloneCapacity == 0)
            {
                error = "PROXY inline source clone capacity must be at least one.";
                return false;
            }
            if (!WriteAnyObject(writer, document, rootIndex, error))
                return false;
            writer.Pod(root.cloneCapacity);
            std::uint16_t rebuiltSourceIndex = 0;
            // AddSwProxySource numbers sources in physical stream order.  The
            // document-side source identity cached on a root can become stale
            // after deleting an earlier PROXY, so register the authoritative
            // owner/root location that is being emitted now.
            if (!writer.RegisterProxySource(currentProxyRecordIndex, static_cast<std::int32_t>(proxyRootIndex),
                                            rebuiltSourceIndex))
            {
                error =
                    "PROXY inline source has a duplicate/invalid source identity or exceeds ProjectCane's 128-slot source pool.";
                return false;
            }
        }
        if (found->overrides.size() > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "PROXY override count exceeds U8.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(found->overrides.size()));
        for (const auto &overrideRecord : found->overrides)
        {
            // The serialized OID is authoritative.  Resolve it every time
            // using the retail PXR rules: a nested PROXY OID can intentionally
            // yield that PROXY's instantiated root, whose own OID differs.
            const auto targetObjectIndex =
                ProxyRuntime::ResolveOverrideTarget(document, currentProxyRecordIndex, overrideRecord.targetOid);
            if (targetObjectIndex < 0 || static_cast<std::size_t>(targetObjectIndex) >= document.Objects().size())
            {
                error = "PROXY override target identity is invalid.";
                return false;
            }
            // ResolveOverrideTarget already proves membership using the exact
            // root search and PXR substitution path.  Rechecking the returned
            // PXR root as an ordinary hash candidate would incorrectly reject
            // retail nested-PROXY overrides such as Eye of the Storm's emitter
            // wrappers.
            if (overrideRecord.replacementSpliceIndex < -1 ||
                (overrideRecord.replacementSpliceIndex >= 0 &&
                 static_cast<std::size_t>(overrideRecord.replacementSpliceIndex) >= document.SpliceModules().size()))
            {
                error = "PROXY override references a splice outside the rebuilt splice map.";
                return false;
            }
            writer.Pod(overrideRecord.targetOid);
            writer.Pod(overrideRecord.replacementSpliceIndex);
            writer.Pod(overrideRecord.zoneMask);
            // LoadProxyFromBrx consumes an unrestricted signed command byte.
            // Only value 2 owns a following F32; every other value preserves
            // the target's inherited MRD state.  Retail maps use additional
            // raw values, so normalizing this field would corrupt alignment.
            writer.Pod(overrideRecord.mrdMode);
            if (overrideRecord.mrdMode == 2)
            {
                if (!std::isfinite(overrideRecord.maximumRenderDistance))
                {
                    error = "PROXY MRD override contains a non-finite value.";
                    return false;
                }
                writer.Pod(overrideRecord.maximumRenderDistance);
            }
            if (!ValidateOptionListForCid(document.Objects()[static_cast<std::size_t>(targetObjectIndex)].cid,
                                          overrideRecord.properties, error))
                return false;
            // An absent PROXY option inherits the reusable source; it does not
            // reset the instance to SetLoDefaults. Therefore an explicitly
            // authored default-valued override must remain in the stream.
            if (!WriteOptionList(writer, overrideRecord.properties, error, false))
                return false;
        }
        if (found->roots.size() == 1)
        {
            if (found->attachedChildObjectIndices.size() > (std::numeric_limits<std::uint16_t>::max)())
            {
                error = "PROXY attached-child count exceeds U16.";
                return false;
            }
            writer.Pod(static_cast<std::uint16_t>(found->attachedChildObjectIndices.size()));
            for (const auto child : found->attachedChildObjectIndices)
            {
                if (child < 0 || static_cast<std::size_t>(child) >= document.Objects().size() ||
                    document.Objects()[static_cast<std::size_t>(child)].proxyClone)
                {
                    error = "PROXY attached-child index is invalid.";
                    return false;
                }
                if (!WriteAnyObject(writer, document, static_cast<std::size_t>(child), error))
                    return false;
            }
        }
        else if (!found->attachedChildObjectIndices.empty())
        {
            error = "Only a single-root PROXY may own attached children.";
            return false;
        }
        return true;
    }
    if (layout == CidWriterLayout::Xfm)
    {
        if (!writeMatrixPosition())
            return false;
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Pnt || layout == CidWriterLayout::Scan)
    {
        if (!std::isfinite(object.localTransform[12]) || !std::isfinite(object.localTransform[13]) ||
            !std::isfinite(object.localTransform[14]))
        {
            error = "PNT/SCAN position contains a non-finite value.";
            return false;
        }
        writer.Pod(object.localTransform[12]);
        writer.Pod(object.localTransform[13]);
        writer.Pod(object.localTransform[14]);
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        if (layout == CidWriterLayout::Scan)
        {
            const auto found =
                std::find_if(document.Scans().begin(), document.Scans().end(), [&](const auto &scan)
                             { return scan.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
            if (found == document.Scans().end())
            {
                error = "SCAN text record is missing.";
                return false;
            }
            if (found->text.size() > (std::numeric_limits<std::uint16_t>::max)())
            {
                error = "SCAN text exceeds U16 bytes.";
                return false;
            }
            writer.Pod(static_cast<std::uint16_t>(found->text.size()));
            writer.Bytes(reinterpret_cast<const std::uint8_t *>(found->text.data()), found->text.size());
        }
        return true;
    }
    if (layout == CidWriterLayout::Shape)
    {
        const auto *shape = ShapeForObject(document, objectIndex);
        if (shape == nullptr)
        {
            error = "SHAPE curve record is missing.";
            return false;
        }
        if (!WriteShapeCurve(writer, *shape, error))
            return false;
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Vol)
    {
        if (!writeMatrixPosition())
            return false;
        const auto found = std::find_if(document.Volumes().begin(), document.Volumes().end(), [&](const auto &volume)
                                        { return volume.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Volumes().end())
        {
            error = "VOL BSP record is missing.";
            return false;
        }
        if (!WriteVolumeTail(writer, *found, error))
            return false;
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Exit)
    {
        const auto found = std::find_if(document.Exits().begin(), document.Exits().end(), [&](const auto &exit)
                                        { return exit.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Exits().end())
        {
            error = "EXIT record is missing.";
            return false;
        }
        if (found->destinationLevel.size() >= 32u || found->alternateLevel.size() >= 32u)
        {
            error = "EXIT level names must be null-terminated inside their fixed 32-byte fields.";
            return false;
        }
        WriteFixed32(writer, found->destinationLevel);
        WriteFixed32(writer, found->alternateLevel);
        if (!writeMatrixPosition())
            return false;
        const auto volume = std::find_if(document.Volumes().begin(), document.Volumes().end(), [&](const auto &value)
                                         { return value.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (volume == document.Volumes().end())
        {
            error = "EXIT volume record is missing.";
            return false;
        }
        if (!WriteVolumeTail(writer, *volume, error) || !WriteOptionList(writer, object.properties, error))
            return false;
        const auto children = AuthoredChildren(document, objectIndex);
        if (found->embeddedAnimationCount < 0 ||
            found->embeddedAnimationCount > (std::numeric_limits<std::int16_t>::max)() ||
            static_cast<std::size_t>(found->embeddedAnimationCount) != children.size())
        {
            error = "EXIT embedded ASEG count does not match its authored children.";
            return false;
        }
        writer.Pod(found->embeddedAnimationCount);
        for (const auto child : children)
        {
            if (document.Objects()[child].cid != 131)
            {
                error = "EXIT contains a non-ASEG embedded child.";
                return false;
            }
            if (!WriteAnyObject(writer, document, child, error))
                return false;
        }
        return true;
    }
    if (layout == CidWriterLayout::ExPlo)
    {
        if (!writeMatrixPosition())
            return false;
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        const auto found = std::find_if(document.Effects().begin(), document.Effects().end(), [&](const auto &effect)
                                        { return effect.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Effects().end())
        {
            error = "EXPLO effect record is missing.";
            return false;
        }
        return WriteEffectTail(writer, *found, error);
    }
    if (layout == CidWriterLayout::Aseg)
    {
        const auto found = std::find_if(document.AnimationSegments().begin(), document.AnimationSegments().end(),
                                        [&](const auto &segment)
                                        { return segment.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.AnimationSegments().end())
        {
            error = "ASEG source record is missing.";
            return false;
        }
        return WriteAsegPayload(writer, *found, object.properties, error);
    }
    if (layout == CidWriterLayout::Sm)
    {
        const auto found =
            std::find_if(document.StateMachines().begin(), document.StateMachines().end(), [&](const auto &machine)
                         { return machine.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.StateMachines().end())
        {
            error = "SM state-machine record is missing.";
            return false;
        }
        if (found->states.size() > (std::numeric_limits<std::uint8_t>::max)() ||
            found->transitions.size() > (std::numeric_limits<std::uint8_t>::max)())
        {
            error = "SM state/transition count exceeds U8.";
            return false;
        }
        std::unordered_set<std::int16_t> stateOids;
        for (const auto &state : found->states)
            if (!stateOids.insert(state.oid).second)
            {
                error = "SM contains duplicate state OIDs.";
                return false;
            }
        for (const auto &state : found->states)
            if (state.automaticNextOid != -1 && stateOids.find(state.automaticNextOid) == stateOids.end())
            {
                error = "SM automatic-next OID does not identify one of its states.";
                return false;
            }
        for (const auto &transition : found->transitions)
            if (stateOids.find(transition.fromOid) == stateOids.end() ||
                stateOids.find(transition.toOid) == stateOids.end())
            {
                error = "SM transition endpoint does not identify one of its states.";
                return false;
            }
            else if (!std::isfinite(transition.probability))
            {
                error = "SM transition probability is non-finite.";
                return false;
            }
        std::unordered_set<std::int32_t> ownedAnimations;
        for (const auto &transition : found->transitions)
        {
            if (!transition.ownsAnimation)
                continue;
            if (!ownedAnimations.insert(transition.animationObjectIndex).second)
            {
                error = "SM transitions share the same embedded ASEG object.";
                return false;
            }
        }
        const auto authoredChildren = AuthoredChildren(document, objectIndex);
        if (authoredChildren.size() != ownedAnimations.size() ||
            std::any_of(authoredChildren.begin(), authoredChildren.end(), [&](const std::size_t child)
                        { return ownedAnimations.find(static_cast<std::int32_t>(child)) == ownedAnimations.end(); }))
        {
            error = "SM authored children do not exactly match its embedded transition ASEGs.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(found->states.size()));
        for (const auto &state : found->states)
        {
            writer.Pod(state.oid);
            writer.Pod(state.automaticNextOid);
        }
        writer.Pod(static_cast<std::uint8_t>(found->transitions.size()));
        for (const auto &transition : found->transitions)
        {
            writer.Pod(static_cast<std::uint8_t>(transition.ownsAnimation ? 1 : 0));
            writer.Pod(transition.fromOid);
            writer.Pod(transition.toOid);
            writer.Pod(transition.flags);
            writer.Pod(transition.probability);
        }
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        writer.Pod(found->serializedTransitionCursor);
        for (const auto &transition : found->transitions)
        {
            if (!transition.ownsAnimation)
                continue;
            if (transition.animationObjectIndex < 0 ||
                static_cast<std::size_t>(transition.animationObjectIndex) >= document.Objects().size())
            {
                error = "SM owned animation index is invalid.";
                return false;
            }
            const auto animationIndex = static_cast<std::size_t>(transition.animationObjectIndex);
            if (document.Objects()[animationIndex].parentIndex != static_cast<std::int32_t>(objectIndex) ||
                document.Objects()[animationIndex].cid != 131)
            {
                error = "SM owned animation is not an ASEG child of the machine.";
                return false;
            }
            if (!WriteAnyObject(writer, document, animationIndex, error))
                return false;
        }
        return true;
    }
    if (layout == CidWriterLayout::Warp)
    {
        if (!writeMatrixPosition())
            return false;
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        const auto found = std::find_if(document.Warps().begin(), document.Warps().end(), [&](const auto &warp)
                                        { return warp.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Warps().end())
        {
            error = "WARP record is missing.";
            return false;
        }
        const auto children = AuthoredChildren(document, objectIndex);
        if (found->embeddedAnimationCount < 0 ||
            found->embeddedAnimationCount > (std::numeric_limits<std::int16_t>::max)() ||
            static_cast<std::size_t>(found->embeddedAnimationCount) != children.size())
        {
            error = "WARP embedded ASEG count does not match its authored children.";
            return false;
        }
        writer.Pod(found->embeddedAnimationCount);
        for (const auto child : children)
        {
            const auto &nested = document.Objects()[child];
            if (nested.cid != 131)
            {
                error = "WARP contains a non-ASEG embedded child.";
                return false;
            }
            if (!WriteAnyObject(writer, document, child, error))
                return false;
        }
        if (found->destinationLevel.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            found->hiddenObjectOids.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()))
        {
            error = "WARP destination/hide-list count exceeds its field width.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(found->destinationLevel.size()));
        writer.Bytes(reinterpret_cast<const std::uint8_t *>(found->destinationLevel.data()),
                     found->destinationLevel.size());
        writer.Pod(static_cast<std::int16_t>(found->hiddenObjectOids.size()));
        for (const auto oid : found->hiddenObjectOids)
            writer.Pod(oid);
        return true;
    }
    if (layout == CidWriterLayout::Explg)
    {
        if (!writeMatrixPosition())
            return false;
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        const auto children = AuthoredChildren(document, objectIndex);
        if (children.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()))
        {
            error = "EXPLG inline-object count exceeds S16.";
            return false;
        }
        writer.Pod(static_cast<std::int16_t>(children.size()));
        for (const auto child : children)
        {
            if (!WriteAnyObject(writer, document, child, error))
                return false;
        }
        return true;
    }
    if (layout == CidWriterLayout::Pathzone)
    {
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        const auto found = std::find_if(document.PathZones().begin(), document.PathZones().end(), [&](const auto &zone)
                                        { return zone.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.PathZones().end())
        {
            error = "PATHZONE record is missing.";
            return false;
        }
        const auto &zone = *found;
        if (zone.vertices.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            zone.edges.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            zone.triangles.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            zone.bspNodes.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            zone.boundaryEdgeCount > zone.edges.size() || zone.vertexEdges.size() != zone.vertices.size())
        {
            error = "PATHZONE count/array contract is invalid.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(zone.vertices.size()));
        writer.Pod(zone.boundaryEdgeCount);
        writer.Pod(static_cast<std::uint16_t>(zone.edges.size() - zone.boundaryEdgeCount));
        writer.Pod(static_cast<std::uint16_t>(zone.triangles.size()));
        for (std::size_t vertex = 0; vertex < zone.vertices.size(); ++vertex)
        {
            if (!FiniteFloats(zone.vertices[vertex].data(), 3))
            {
                error = "PATHZONE contains a non-finite vertex.";
                return false;
            }
            writer.Floats(zone.vertices[vertex].data(), 3);
            if (zone.vertexEdges[vertex].size() > 127u)
            {
                error = "PATHZONE vertex adjacency exceeds signed S8.";
                return false;
            }
            writer.Pod(static_cast<std::int8_t>(zone.vertexEdges[vertex].size()));
            for (const auto edge : zone.vertexEdges[vertex])
            {
                if (edge >= zone.edges.size())
                {
                    error = "PATHZONE adjacency edge is invalid.";
                    return false;
                }
                writer.Pod(edge);
            }
        }
        for (const auto &edge : zone.edges)
        {
            if (edge[0] >= zone.vertices.size() || edge[1] >= zone.vertices.size())
            {
                error = "PATHZONE edge vertex is invalid.";
                return false;
            }
            writer.Pod(edge[0]);
            writer.Pod(edge[1]);
        }
        for (const auto &triangle : zone.triangles)
        {
            if (triangle[0] >= zone.vertices.size() || triangle[1] >= zone.vertices.size() ||
                triangle[2] >= zone.vertices.size())
            {
                error = "PATHZONE triangle vertex is invalid.";
                return false;
            }
            writer.Pod(triangle[0]);
            writer.Pod(triangle[1]);
            writer.Pod(triangle[2]);
        }
        writer.Pod(static_cast<std::uint16_t>(zone.bspNodes.size()));
        for (const auto &node : zone.bspNodes)
        {
            const auto validBranch = [&](const std::int16_t branch)
            {
                if (branch == -1)
                    return true;
                if (branch >= 0)
                    return static_cast<std::size_t>(branch) < zone.bspNodes.size();
                const auto triangle = -static_cast<std::int32_t>(branch) - 2;
                return triangle >= 0 && static_cast<std::size_t>(triangle) < zone.triangles.size();
            };
            if (!validBranch(node.negativeChild) || !validBranch(node.positiveChild))
            {
                error = "PATHZONE BSP contains an invalid node/triangle branch.";
                return false;
            }
            if (!FiniteFloats(node.normal.data(), 3) || !std::isfinite(node.distance))
            {
                error = "PATHZONE BSP contains a non-finite plane.";
                return false;
            }
            writer.Floats(node.normal.data(), 3);
            writer.Pod(node.distance);
            writer.Pod(node.negativeChild);
            writer.Pod(node.positiveChild);
        }
        return true;
    }
    if (layout == CidWriterLayout::Rchm)
    {
        const auto found = std::find_if(document.ReachMaps().begin(), document.ReachMaps().end(), [&](const auto &reach)
                                        { return reach.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.ReachMaps().end())
        {
            error = "RCHM record is missing.";
            return false;
        }
        const auto &reach = *found;
        if (reach.positions.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
            reach.surfaces.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u ||
            reach.tetrahedra.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) ||
            reach.bspNodes.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) ||
            reach.enabledPositions.size() != reach.positions.size())
        {
            error = "RCHM count/array contract is invalid.";
            return false;
        }
        for (const auto oid : reach.animationOids)
            writer.Pod(oid);
        if (!std::isfinite(reach.geometryRadius) || reach.geometryRadius < 0.0f ||
            !std::isfinite(reach.radiusSquared) || reach.radiusSquared < 0.0f)
        {
            error = "RCHM bounds contain an invalid value.";
            return false;
        }
        writer.Pod(reach.gridResolution);
        writer.Pod(reach.geometryRadius);
        writer.Pod(static_cast<std::uint16_t>(reach.positions.size()));
        for (const auto &position : reach.positions)
        {
            if (!FiniteFloats(position.data(), 3))
            {
                error = "RCHM contains a non-finite position.";
                return false;
            }
            writer.Floats(position.data(), 3);
        }
        writer.Pod(static_cast<std::uint16_t>(reach.surfaces.size()));
        writer.Pod(reach.totalEdgeCount);
        std::uint32_t edgeTotal = 0;
        for (const auto &surface : reach.surfaces)
        {
            if (surface.edges.size() > static_cast<std::size_t>((std::numeric_limits<std::int8_t>::max)()))
            {
                error = "RCHM surface exceeds the retail signed 8-bit edge count.";
                return false;
            }
            if (!std::isfinite(surface.radius) || surface.radius < 0.0f || !FiniteFloats(surface.center.data(), 3))
            {
                error = "RCHM surface bounds are invalid.";
                return false;
            }
            for (const auto position : surface.planePositions)
                if (position >= reach.positions.size())
                {
                    error = "RCHM surface plane references an invalid position.";
                    return false;
                }
            for (const auto position : surface.planePositions)
                writer.Pod(position);
            writer.Pod(surface.radius);
            writer.Floats(surface.center.data(), 3);
            writer.Pod(static_cast<std::uint8_t>(surface.edges.size()));
            edgeTotal += static_cast<std::uint32_t>(surface.edges.size());
            for (const auto &edge : surface.edges)
            {
                if (edge.firstPosition >= reach.positions.size() || edge.secondPosition >= reach.positions.size() ||
                    edge.adjacentSurface >= reach.surfaces.size() ||
                    edge.adjacentSurface > static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()))
                {
                    error = "RCHM edge references an invalid position or adjacent surface.";
                    return false;
                }
                writer.Pod(edge.firstPosition);
                writer.Pod(edge.secondPosition);
                writer.Pod(edge.adjacentSurface);
            }
        }
        if (edgeTotal != reach.totalEdgeCount)
        {
            error = "RCHM total edge count does not match its surfaces.";
            return false;
        }
        for (const bool enabled : reach.enabledPositions)
            writer.Pod(static_cast<std::uint8_t>(enabled ? 1 : 0));
        writer.Pod(static_cast<std::int16_t>(reach.tetrahedra.size()));
        for (const auto &tetrahedron : reach.tetrahedra)
            for (const auto vertex : tetrahedron)
            {
                if (vertex < 0 || static_cast<std::size_t>(vertex) >= reach.positions.size())
                {
                    error = "RCHM tetrahedron references an invalid position.";
                    return false;
                }
                writer.Pod(vertex);
            }
        writer.Pod(static_cast<std::int16_t>(reach.bspNodes.size()));
        for (const auto &node : reach.bspNodes)
        {
            if (node.surfaceIndex < 0 || static_cast<std::size_t>(node.surfaceIndex) >= reach.surfaces.size())
            {
                error = "RCHM BSP references an invalid surface.";
                return false;
            }
            const auto validBranch = [&](const std::int16_t branch)
            {
                if (branch == 0)
                    return true;
                if (branch > 0)
                    return static_cast<std::size_t>(branch - 1) < reach.bspNodes.size();
                return static_cast<std::size_t>(~branch) < reach.tetrahedra.size();
            };
            if (!validBranch(node.negativeChild) || !validBranch(node.positiveChild))
            {
                error = "RCHM BSP contains an invalid node/tetrahedron branch.";
                return false;
            }
            writer.Pod(node.surfaceIndex);
            writer.Pod(node.negativeChild);
            writer.Pod(node.positiveChild);
        }
        writer.Pod(reach.radiusSquared);
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Tn)
    {
        const auto &tn = object.tn;
        if (!tn.present || tn.surfaces.size() > (std::numeric_limits<std::uint16_t>::max)() ||
            tn.bspNodes.size() > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) + 1u)
        {
            error = "TN payload/count is invalid.";
            return false;
        }
        if (!writeMatrixPosition())
            return false;
        writer.Pod(static_cast<std::uint16_t>(tn.surfaces.size()));
        for (const auto &surface : tn.surfaces)
        {
            if (!std::isfinite(surface.planeDistance) || !FiniteFloats(surface.normal, 3))
            {
                error = "TN TBSP contains a non-finite plane.";
                return false;
            }
            writer.Pod(surface.planeDistance);
            writer.Floats(surface.normal, 3);
        }
        writer.Pod(static_cast<std::uint16_t>(tn.bspNodes.size()));
        for (const auto &node : tn.bspNodes)
        {
            const auto validChild = [&](const std::uint16_t child)
            {
                return child == 0xffffu ||
                       (child <= static_cast<std::uint16_t>((std::numeric_limits<std::int16_t>::max)()) &&
                        child < tn.bspNodes.size());
            };
            if (node.surfaceIndex >= tn.surfaces.size() || !validChild(node.negativeChild) ||
                !validChild(node.positiveChild))
            {
                error = "TN TBSP contains an invalid surface/child index.";
                return false;
            }
            writer.Pod(node.surfaceIndex);
            writer.Pod(node.negativeChild);
            writer.Pod(node.positiveChild);
        }
        writer.Pod(tn.curveKind);
        if (tn.curveKind == -1)
        {
            constexpr std::array<std::size_t, 12> frameIndices{0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
            for (const auto index : frameIndices)
                if (!std::isfinite(tn.staticFrame[index]))
                {
                    error = "TN static frame contains a non-finite value.";
                    return false;
                }
            for (const auto index : {0, 1, 2, 4, 5, 6, 8, 9, 10})
                writer.Pod(tn.staticFrame[index]);
            writer.Pod(tn.staticFrame[12]);
            writer.Pod(tn.staticFrame[13]);
            writer.Pod(tn.staticFrame[14]);
        }
        else
        {
            if ((tn.curveKind != 0 && tn.curveKind != 1) ||
                tn.curvePoints.size() > (std::numeric_limits<std::uint8_t>::max)())
            {
                error = "TN curve kind/count is invalid.";
                return false;
            }
            if (tn.curvePoints.empty())
            {
                error = "A TN path cannot be empty: ProjectCane reads its last cumulative distance while loading it.";
                return false;
            }
            writer.Pod(static_cast<std::uint8_t>(tn.curveClosed ? 1 : 0));
            writer.Pod(static_cast<std::uint8_t>(tn.curvePoints.size()));
            for (const auto &point : tn.curvePoints)
            {
                if (!std::isfinite(point.parameter) || !FiniteFloats(point.position, 3) ||
                    (tn.curveKind == 1 &&
                     (!FiniteFloats(point.incomingTangent, 3) || !FiniteFloats(point.outgoingTangent, 3))))
                {
                    error = "TN curve contains a non-finite value.";
                    return false;
                }
                writer.Pod(point.parameter);
                writer.Floats(point.position, 3);
                if (tn.curveKind == 1)
                {
                    writer.Floats(point.incomingTangent, 3);
                    writer.Floats(point.outgoingTangent, 3);
                }
            }
        }
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Vismap)
    {
        const auto &nodes = document.VisibilityNodes();
        const auto &masks = document.VisibilityOneHopMasks();
        if (nodes.size() > (std::numeric_limits<std::uint16_t>::max)() || masks.size() > 32u)
        {
            error = "VISMAP node/zone count is invalid.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(nodes.size()));
        for (const auto &node : nodes)
        {
            const auto validChild = [&](const std::int32_t child)
            { return child == -1 || (child >= 0 && static_cast<std::size_t>(child) < nodes.size()); };
            if (!validChild(node.negativeChild) || !validChild(node.positiveChild))
            {
                error = "VISMAP contains an invalid BSP child index.";
                return false;
            }
            // LoadVismapFromBrx reads raw F32 plane components without a
            // finiteness/unit-normal restriction. Preserve the authored values;
            // diagnostics can still flag unusable authoring geometry.
            writer.Floats(node.normal, 3);
            writer.Pod(node.planeDistance);
            writer.Pod(node.negativeChild);
            writer.Pod(node.negativeZoneMask);
            writer.Pod(node.positiveChild);
            writer.Pod(node.positiveZoneMask);
        }
        writer.Pod(static_cast<std::uint16_t>(masks.size()));
        for (const auto mask : masks)
            writer.Pod(mask);
        return WriteOptionList(writer, object.properties, error);
    }
    if (layout == CidWriterLayout::Keyhole)
    {
        if (!WriteOptionList(writer, object.properties, error))
            return false;
        const auto found = std::find_if(document.Keyholes().begin(), document.Keyholes().end(), [&](const auto &keyhole)
                                        { return keyhole.ownerObjectIndex == static_cast<std::int32_t>(objectIndex); });
        if (found == document.Keyholes().end() || found->positions.size() > (std::numeric_limits<std::uint16_t>::max)())
        {
            error = "KEYHOLE record/count is invalid.";
            return false;
        }
        writer.Pod(static_cast<std::uint16_t>(found->positions.size()));
        for (const auto &point : found->positions)
        {
            if (!FiniteFloats(point.data(), 2))
            {
                error = "KEYHOLE contains a non-finite position.";
                return false;
            }
            writer.Pod(point[0]);
            writer.Pod(-point[1]);
        }
        if (!FiniteFloats(found->minimum.data(), 2) || !FiniteFloats(found->maximum.data(), 2) ||
            found->minimum[0] > found->maximum[0] || found->minimum[1] > found->maximum[1])
        {
            error = "KEYHOLE bounds are invalid.";
            return false;
        }
        writer.Pod(found->minimum[0]);
        writer.Pod(found->minimum[1]);
        writer.Pod(found->maximum[0]);
        writer.Pod(found->maximum[1]);
        for (const auto &layer : found->layers)
        {
            if (layer.size() > (std::numeric_limits<std::uint16_t>::max)())
            {
                error = "KEYHOLE layer triangle count exceeds U16.";
                return false;
            }
            writer.Pod(static_cast<std::uint16_t>(layer.size()));
            for (const auto &triangle : layer)
            {
                if (triangle[0] >= found->positions.size() || triangle[1] >= found->positions.size() ||
                    triangle[2] >= found->positions.size())
                {
                    error = "KEYHOLE triangle index is invalid.";
                    return false;
                }
                writer.Pod(triangle[0]);
                writer.Pod(triangle[1]);
                writer.Pod(triangle[2]);
            }
        }
        return true;
    }
    error = "Object is not a simple custom payload.";
    return false;
}

bool WriteAnyObject(PayloadWriter &writer, const BrxDocument &document, const std::size_t objectIndex,
                    std::string &error)
{
    if (objectIndex >= document.Objects().size())
    {
        error = "Object index is outside the document.";
        return false;
    }
    if (!writer.MarkObjectWritten(objectIndex))
    {
        const auto &duplicate = document.Objects()[objectIndex];
        error = "Authored object was serialized more than once: CID " + std::to_string(duplicate.cid) + " OID " +
                std::to_string(duplicate.oid) + ".";
        return false;
    }
    const auto layout = WriterLayoutForCid(document.Objects()[objectIndex].cid);
    if (layout == CidWriterLayout::Lo || layout == CidWriterLayout::Alo || layout == CidWriterLayout::So ||
        layout == CidWriterLayout::Cnvo || layout == CidWriterLayout::Emitter || layout == CidWriterLayout::Flash ||
        layout == CidWriterLayout::Dialog)
        return WriteSharedObject(writer, document, objectIndex, true, error);
    return WriteSimpleCustomObject(writer, document, objectIndex, error);
}

bool WriteSpliceMap(PayloadWriter &writer, const BrxDocument &document, std::string &error)
{
    const auto &nodes = document.SpliceNodes();
    const auto &modules = document.SpliceModules();
    if (nodes.size() > 10'000'000u)
    {
        error = "Splice node count exceeds the editor loader's expression-table limit.";
        return false;
    }
    if (modules.size() > 1'000'000u || document.WorldHeader().symbolCount > 1'000'000u ||
        document.WorldHeader().extraOptionCount > 1'000'000u)
    {
        error = "Splice module/symbol/extra-option count exceeds the loader safety limit.";
        return false;
    }
    writer.Pod(static_cast<std::uint32_t>(modules.size()));
    writer.Pod(document.WorldHeader().symbolCount);
    writer.Pod(document.WorldHeader().extraOptionCount);
    std::vector<bool> active(nodes.size(), false);
    std::vector<bool> referenced(nodes.size(), false);
    const auto writeNode = [&](auto &&self, const std::int32_t nodeIndex, const std::uint32_t depth) -> bool
    {
        if (nodeIndex == -1)
        {
            writer.Pod(static_cast<std::uint8_t>(0xff));
            return true;
        }
        if (nodeIndex < -1 || static_cast<std::size_t>(nodeIndex) >= nodes.size())
        {
            error = "Splice expression references an invalid node.";
            return false;
        }
        if (depth > 4096u || active[static_cast<std::size_t>(nodeIndex)])
        {
            error = "Splice expression contains a cycle or excessive nesting.";
            return false;
        }
        if (referenced[static_cast<std::size_t>(nodeIndex)])
        {
            error =
                "Splice expression graph shares node " + std::to_string(nodeIndex) +
                " from more than one child/next/root link. ProjectCane's recursive stream is a tree and cannot preserve shared node identity.";
            return false;
        }
        const auto &node = nodes[static_cast<std::size_t>(nodeIndex)];
        if (node.nextNode < -1 || node.childNode < -1 || (node.tag != 30u && node.childNode != -1))
        {
            error = "Splice expression contains an invalid link or a non-PAIR child payload.";
            return false;
        }
        if (node.tag >= 36u)
        {
            error = "Splice expression contains an invalid ProjectCane tag.";
            return false;
        }
        active[static_cast<std::size_t>(nodeIndex)] = true;
        referenced[static_cast<std::size_t>(nodeIndex)] = true;
        const bool hasNext = node.nextNode >= 0;
        writer.Pod(static_cast<std::uint8_t>(node.tag | (hasNext ? 0x80u : 0u)));
        switch (node.tag)
        {
        case 20:
            writer.Pod(node.intValue);
            break;
        case 21:
            if (!std::isfinite(node.floatValue[0]))
            {
                error = "Splice F32 literal is non-finite.";
                return false;
            }
            writer.Pod(node.floatValue[0]);
            break;
        case 28:
            if (node.intValue < 0 || static_cast<std::uint32_t>(node.intValue) >= document.WorldHeader().symbolCount)
            {
                error = "Splice expression references a symbol outside symidMax.";
                return false;
            }
            writer.Pod(static_cast<std::uint32_t>(node.intValue));
            break;
        case 27:
            writer.Pod(static_cast<std::uint32_t>(node.intValue != 0));
            break;
        case 29:
            if (node.intValue < 0 || node.intValue >= 125)
            {
                error = "Splice expression references an invalid ProjectCane built-in.";
                return false;
            }
            writer.Pod(static_cast<std::uint32_t>(node.intValue));
            break;
        case 32:
            if (node.intValue != 0)
            {
                error = "Splice BASIC literal is a nonzero raw pointer token; use find-object instead.";
                return false;
            }
            writer.Pod(static_cast<std::uint32_t>(node.intValue));
            break;
        case 22:
        case 24:
        case 26:
            if (!std::all_of(node.floatValue, node.floatValue + 3,
                             [](const float value) { return std::isfinite(value); }))
            {
                error = "Splice three-component literal is non-finite.";
                return false;
            }
            writer.Floats(node.floatValue, 3);
            break;
        case 23:
            if (!std::all_of(node.floatValue, node.floatValue + 16,
                             [](const float value) { return std::isfinite(value); }))
            {
                error = "Splice matrix literal is non-finite.";
                return false;
            }
            writer.Floats(node.floatValue, 16);
            break;
        case 25:
            if (!std::all_of(node.floatValue, node.floatValue + 2,
                             [](const float value) { return std::isfinite(value); }))
            {
                error = "Splice limit literal is non-finite.";
                return false;
            }
            writer.Floats(node.floatValue, 2);
            break;
        case 30:
            if (!self(self, node.childNode, depth + 1u))
                return false;
            break;
        default:
            if (node.childNode >= 0)
            {
                error = "Only a PAIR splice node may own a child expression.";
                return false;
            }
            break;
        }
        if (hasNext && !self(self, node.nextNode, depth + 1u))
            return false;
        active[static_cast<std::size_t>(nodeIndex)] = false;
        return true;
    };
    for (const auto &module : modules)
        if (!writeNode(writeNode, module.rootNode, 0))
            return false;
    if (std::find(referenced.begin(), referenced.end(), false) != referenced.end())
    {
        error = "Splice graph contains an authored node unreachable from every module.";
        return false;
    }
    return true;
}

bool WriteShaderAnimation(PayloadWriter &writer, const BrxShaderAnimationRecord &animation, std::string &error)
{
    const auto kind = static_cast<std::int16_t>(animation.kind);
    if (kind < 0 || kind > 8)
    {
        error = "Shader animation kind is invalid.";
        return false;
    }
    writer.Pod(kind);
    if (kind == 0)
        return true;
    writer.Pod(animation.shaderOid);
    writer.Pod(static_cast<std::uint16_t>(animation.instanced ? 1 : 0));
    float values[6]{};
    // Every SAA subtype owns a 24-byte body, but several loaders consume only
    // a prefix and discard the rest as padding.  Rebuild semantic fields and
    // canonicalize all ignored bytes instead of propagating decoded filler.
    std::size_t semanticFloatCount = 0;
    switch (animation.kind)
    {
    case BrxShaderAnimationKind::Loop:
    case BrxShaderAnimationKind::PingPong:
    case BrxShaderAnimationKind::Eyes:
    case BrxShaderAnimationKind::Scroller:
    case BrxShaderAnimationKind::Circler:
        semanticFloatCount = 4;
        break;
    case BrxShaderAnimationKind::Shuffle:
        semanticFloatCount = 2;
        break;
    case BrxShaderAnimationKind::Hologram:
        semanticFloatCount = 1;
        break;
    case BrxShaderAnimationKind::Looker:
        semanticFloatCount = 6;
        break;
    default:
        break;
    }
    for (std::size_t index = 0; index < semanticFloatCount; ++index)
    {
        if (!std::isfinite(animation.values[index]))
        {
            error = "Shader animation contains a non-finite semantic value.";
            return false;
        }
        values[index] = animation.values[index];
    }
    if (animation.kind == BrxShaderAnimationKind::Loop || animation.kind == BrxShaderAnimationKind::PingPong)
    {
        const auto minimumFrame = animation.kind == BrxShaderAnimationKind::Loop
                                      ? static_cast<std::int32_t>((std::numeric_limits<std::int16_t>::min)())
                                      : 0;
        const auto maximumFrame = animation.kind == BrxShaderAnimationKind::Loop
                                      ? static_cast<std::int32_t>((std::numeric_limits<std::int16_t>::max)())
                                      : static_cast<std::int32_t>((std::numeric_limits<std::uint16_t>::max)());
        if (animation.initialFrame < minimumFrame || animation.initialFrame > maximumFrame)
        {
            error = "Shader animation initial frame exceeds its retail field width.";
            return false;
        }
        const std::uint32_t word = static_cast<std::uint16_t>(animation.initialFrame);
        std::memcpy(&values[4], &word, sizeof(word));
    }
    else if (animation.kind == BrxShaderAnimationKind::Eyes)
    {
        const std::uint32_t word = static_cast<std::uint16_t>(animation.otherShaderOid);
        std::memcpy(&values[4], &word, sizeof(word));
    }
    if (animation.kind == BrxShaderAnimationKind::Hologram)
    {
        if (animation.symmetryCount == 0)
        {
            error = "Hologram shader animation requires a nonzero symmetry count.";
            return false;
        }
        std::memcpy(&values[1], &animation.symmetryCount, sizeof(animation.symmetryCount));
    }
    writer.Floats(values, 6);
    return true;
}

bool WriteResourceTable(PayloadWriter &writer, const BrxDocument &document, std::vector<std::uint8_t> &texturePayload,
                        std::string &error)
{
    const auto &bitmaps = document.Bitmaps();
    const auto &shaders = document.Shaders();
    const auto &fonts = document.Fonts();
    if (bitmaps.size() > 1'000'000u || shaders.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "Bitmap/shader count exceeds the loader or field-width limit.";
        return false;
    }
    writer.Pod(static_cast<std::uint32_t>(bitmaps.size()));
    std::size_t textureSize = 0;
    std::vector<std::uint32_t> rebuiltTextureOffsets;
    rebuiltTextureOffsets.reserve(bitmaps.size());
    for (const auto &bitmap : bitmaps)
    {
        if (bitmap.width == 0 || bitmap.height == 0 || bitmap.width > 32768u || bitmap.height > 32768u)
        {
            error = "Bitmap dimensions are outside the range accepted by LoadTexturesFromBrx.";
            return false;
        }
        if (bitmap.mipCount == 0 || bitmap.mipCount > 32u)
        {
            error = "Bitmap mip count is outside the range accepted by LoadTexturesFromBrx.";
            return false;
        }
        if (bitmap.textureFormat > 2u)
        {
            error = "Bitmap uses an unsupported PCBRX texture format.";
            return false;
        }
        std::size_t expectedPixelBytes = 0;
        std::uint32_t mipWidth = bitmap.width;
        std::uint32_t mipHeight = bitmap.height;
        for (std::uint32_t mip = 0; mip < bitmap.mipCount; ++mip)
        {
            const std::size_t levelBytes = bitmap.textureFormat == 0u
                                               ? static_cast<std::size_t>(mipWidth) * mipHeight * 4u
                                               : static_cast<std::size_t>((mipWidth + 3u) / 4u) *
                                                     ((mipHeight + 3u) / 4u) * (bitmap.textureFormat == 1u ? 8u : 16u);
            if (expectedPixelBytes > (std::numeric_limits<std::size_t>::max)() - levelBytes)
            {
                error = "Bitmap mip-chain size overflows the host size type.";
                return false;
            }
            expectedPixelBytes += levelBytes;
            mipWidth = (std::max)(1u, mipWidth >> 1u);
            mipHeight = (std::max)(1u, mipHeight >> 1u);
        }
        if (expectedPixelBytes != bitmap.pixelBytes)
        {
            error = "Bitmap pixel size does not match the mip chain expected by LoadTexturesFromBrx.";
            return false;
        }
        if ((bitmap.textureFlags & (1u << 2u)) != 0u && bitmap.mipCount != 1u)
        {
            error = "Runtime mip generation requires exactly one authored source mip.";
            return false;
        }
        if (bitmap.pixels.size() != bitmap.pixelBytes)
        {
            error = "Bitmap pixels do not match the authored mip-chain size.";
            return false;
        }
        if (textureSize > (std::numeric_limits<std::uint32_t>::max)() - bitmap.pixelBytes)
        {
            error = "Rebuilt texture payload exceeds the PCBRX U32 offset range.";
            return false;
        }
        rebuiltTextureOffsets.push_back(static_cast<std::uint32_t>(textureSize));
        writer.Pod(bitmap.width);
        writer.Pod(bitmap.height);
        writer.Pod(bitmap.zoneFlags);
        writer.Pod(bitmap.mipCount);
        writer.Pod(bitmap.pixelBytes);
        writer.Pod(rebuiltTextureOffsets.back());
        // ProjectCane consumes only bits 0..2.  Canonical structured output
        // drops ignored extractor/source bits instead of blocking the world or
        // reproducing opaque state.
        writer.Pod(bitmap.textureFlags & 0x07u);
        writer.Pod(bitmap.textureFormat);
        textureSize += bitmap.pixelBytes;
    }
    texturePayload.clear();
    texturePayload.reserve(textureSize);
    for (const auto &bitmap : bitmaps)
        texturePayload.insert(texturePayload.end(), bitmap.pixels.begin(), bitmap.pixels.end());
    writer.Pod(static_cast<std::uint16_t>(shaders.size()));
    const auto animationCount = std::count_if(shaders.begin(), shaders.end(), [](const auto &shader)
                                              { return shader.animation.kind != BrxShaderAnimationKind::None; });
    if (animationCount > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "Shader animation count exceeds U16.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(animationCount));
    for (const auto &shader : shaders)
    {
        if (shader.kind < 0 || shader.kind >= 11)
        {
            error = "Shader kind is outside the retail SHDK range.";
            return false;
        }
        if (shader.renderPhase >= 18)
        {
            error = "Shader render phase is outside the retail RP range.";
            return false;
        }
        if (shader.textureStages.size() != shader.textureStageCount)
        {
            error = "Shader texture-stage count does not match its decoded stage records.";
            return false;
        }
        if (shader.animationKind != static_cast<std::int16_t>(shader.animation.kind))
        {
            error = "Shader animation discriminator and payload kind disagree.";
            return false;
        }
        if (!FiniteFloats(shader.rgba, 4) || !FiniteFloats(shader.volumeRgba, 4))
        {
            error = "Shader color payload contains a non-finite value.";
            return false;
        }
        writer.Pod(static_cast<std::uint8_t>(shader.kind));
        writer.Pod(shader.flags);
        writer.Pod(shader.oid);
        writer.Floats(shader.rgba, 4);
        writer.Floats(shader.volumeRgba, 4);
        writer.Pod(shader.zoneFlags);
        writer.Pod(shader.alternateSaturationOid);
        writer.Pod(shader.renderPhase);
        writer.Pod(shader.textureStageCount);
        if (!WriteShaderAnimation(writer, shader.animation, error))
            return false;
        for (std::size_t stageIndex = 0; stageIndex < shader.textureStages.size(); ++stageIndex)
        {
            const auto &stage = shader.textureStages[stageIndex];
            // textureStages is the exact serialized representation. The
            // top-level arrays are a renderer/editor cache of stage zero and
            // must never override the authoritative stage record at export.
            const auto &ambient = stage.ambientBitmapIndices;
            const auto &diffuse = stage.diffuseBitmapIndices;
            const auto &saturate = stage.saturateBitmapIndices;
            const auto frameCount = ambient.size();
            if (frameCount > (std::numeric_limits<std::uint16_t>::max)() || diffuse.size() != frameCount ||
                saturate.size() != frameCount)
            {
                error = "Shader texture-stage frame arrays exceed the loader limit or are inconsistent.";
                return false;
            }
            writer.Pod(stage.oid);
            writer.Pod(stage.flags);
            writer.Pod(static_cast<std::uint16_t>(frameCount));
            for (std::size_t frame = 0; frame < frameCount; ++frame)
            {
                const auto validBitmap = [&](const std::int32_t index)
                { return index == -1 || (index >= 0 && static_cast<std::size_t>(index) < bitmaps.size()); };
                if (!validBitmap(ambient[frame]) || !validBitmap(diffuse[frame]) || !validBitmap(saturate[frame]))
                {
                    error = "Shader texture stage references an invalid bitmap index.";
                    return false;
                }
                writer.Pod(ambient[frame]);
                writer.Pod(diffuse[frame]);
                writer.Pod(saturate[frame]);
            }
        }
    }
    if (fonts.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "Font count exceeds U16.";
        return false;
    }
    writer.Pod(static_cast<std::uint16_t>(fonts.size()));
    for (const auto &font : fonts)
    {
        if (font.glyphs.size() > (std::numeric_limits<std::uint16_t>::max)())
        {
            error = "Font glyph count exceeds the loader's U16 runtime count.";
            return false;
        }
        if (font.bitmapIndex < -1 ||
            (font.bitmapIndex >= 0 && static_cast<std::size_t>(font.bitmapIndex) >= bitmaps.size()))
        {
            error = "Font references an invalid bitmap index.";
            return false;
        }
        if (!std::isfinite(font.scale))
        {
            error = "Font scale is non-finite.";
            return false;
        }
        writer.Pod(font.bitmapIndex);
        writer.Pod(font.characterWidth);
        writer.Pod(font.spaceWidth);
        writer.Pod(font.height);
        writer.Pod(font.kind);
        writer.Pod(font.scale);
        writer.Pod(static_cast<std::uint32_t>(font.glyphs.size()));
        for (const auto &glyph : font.glyphs)
        {
            writer.Pod(glyph.character);
            writer.Pod(glyph.x);
            writer.Pod(glyph.y);
            writer.Pod(glyph.width);
        }
    }
    return true;
}

bool BuildRebuiltPayload(const BrxDocument &document, std::vector<std::uint8_t> &rebuilt, std::string &error)
{
    error.clear();
    rebuilt.clear();
    if (document.WorldHeader().magic != kPcbrMagic || document.WorldHeader().version != 17)
    {
        error = "The document is not configured as a PCBRX v17 world.";
        return false;
    }
    // General editor validation includes gameplay diagnostics such as missing
    // runtime/global OIDs.  Those are useful authoring warnings, but are not a
    // reason to disable a structurally valid BRX round trip.  Enforce the
    // hierarchy invariants required by this serializer here instead.
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.proxyClone)
            continue;
        if (!ValidateOptionListForCid(object.cid, object.properties, error))
            return false;
        if (object.spliceIndex < -1 || (object.spliceIndex >= 0 && static_cast<std::size_t>(object.spliceIndex) >=
                                                                       document.SpliceModules().size()))
        {
            error = "World object has a splice index outside the rebuilt splice map: CID " +
                    std::to_string(object.cid) + " OID " + std::to_string(object.oid) + ".";
            return false;
        }
        if (object.parentIndex < -1 || object.parentIndex >= static_cast<std::int32_t>(document.Objects().size()))
        {
            error = "World object hierarchy contains an invalid parent index.";
            return false;
        }
        std::unordered_set<std::size_t> ancestors;
        std::int32_t parent = object.parentIndex;
        while (parent >= 0)
        {
            const auto parentIndex = static_cast<std::size_t>(parent);
            if (parentIndex >= document.Objects().size() || !ancestors.insert(parentIndex).second)
            {
                error = "World object hierarchy contains a parent cycle.";
                return false;
            }
            parent = document.Objects()[parentIndex].parentIndex;
        }
    }
    PayloadWriter world;
    std::vector<std::size_t> roots;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.parentIndex < 0 && !object.proxyClone)
            roots.push_back(index);
    }
    // Keep document/authored order. Original offsets cease to define ordering
    // after creation, duplication, deletion, or reparenting.
    if (roots.size() > (std::numeric_limits<std::uint16_t>::max)())
    {
        error = "World root-object count exceeds U16.";
        return false;
    }
    world.Pod(static_cast<std::uint16_t>(roots.size()));
    for (const auto root : roots)
        if (!WriteAnyObject(world, document, root, error))
            return false;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.proxyClone)
            continue;
        if (world.WrittenObjects().find(index) == world.WrittenObjects().end())
        {
            error = "Authored object is unreachable from the serialized world: index " + std::to_string(index) +
                    ", CID " + std::to_string(object.cid) + ", OID " + std::to_string(object.oid) + ".";
            return false;
        }
    }

    PayloadWriter prefix;
    prefix.Pod(kPcbrMagic);
    prefix.Pod(static_cast<std::uint32_t>(17));
    const auto &worldHeader = document.WorldHeader();
    const std::array<std::string_view, 7> validSoundBanks = {
        "Common.bnk", "Intro.bnk", "Underwater.bnk", "Muggshot.bnk", "Voodoo.bnk", "Snow.bnk", "Clockwerk.bnk"};
    if (!worldHeader.soundBankFile.empty() &&
        std::find(validSoundBanks.begin(), validSoundBanks.end(), std::string_view(worldHeader.soundBankFile)) ==
            validSoundBanks.end())
    {
        error = "Sound-bank filename is not one of the seven banks accepted by LoadSwFromBrx.";
        return false;
    }
    const auto validMusicName = [](const std::string &value)
    {
        if (value.empty())
            return true;
        constexpr std::string_view prefix = "MUSID_";
        constexpr std::string_view suffix = ".mus";
        if (value.size() <= prefix.size() + suffix.size() ||
            value.compare(0, prefix.size(), prefix.data(), prefix.size()) != 0 ||
            value.compare(value.size() - suffix.size(), suffix.size(), suffix.data(), suffix.size()) != 0)
            return false;
        for (std::size_t index = prefix.size(); index < value.size() - suffix.size(); ++index)
            if (!std::isxdigit(static_cast<unsigned char>(value[index])))
                return false;
        return true;
    };
    // The runtime treats all three fixed fields as C strings.  A 32-byte
    // value would leave no terminator and make strcmp/strlen walk into the
    // following BRX section.
    if (worldHeader.soundBankFile.size() >= 32u || worldHeader.commentaryAudioFile.size() >= 32u ||
        worldHeader.musicFile.size() >= 32u)
    {
        error = "A world fixed filename lacks room for its required null terminator.";
        return false;
    }
    if (worldHeader.soundBankFile.find('\0') != std::string::npos ||
        worldHeader.commentaryAudioFile.find('\0') != std::string::npos ||
        worldHeader.musicFile.find('\0') != std::string::npos)
    {
        error = "A world fixed filename contains an embedded null byte.";
        return false;
    }
    if (!validMusicName(worldHeader.musicFile))
    {
        error = "Music filename must use the exact MUSID_<hex>.mus form accepted by LoadSwFromBrx.";
        return false;
    }
    if (!prefix.FixedString(worldHeader.soundBankFile, 32))
    {
        error = "Sound-bank filename does not fit its fixed 32-byte field.";
        return false;
    }
    if (!ValidateOptionListForCid(108, document.WorldOptions(), error) || !WriteSpliceMap(prefix, document, error) ||
        !WriteOptionList(prefix, document.WorldOptions(), error))
        return false;
    if (!prefix.FixedString(worldHeader.commentaryAudioFile, 32))
    {
        error = "Commentary filename does not fit its fixed 32-byte field.";
        return false;
    }
    if (!prefix.StringU16(worldHeader.commentarySpeakers))
    {
        error = "Commentary speaker text exceeds U16 bytes.";
        return false;
    }
    if (!prefix.FixedString(worldHeader.musicFile, 32))
    {
        error = "Music filename does not fit its fixed 32-byte field.";
        return false;
    }
    PayloadWriter resources;
    std::vector<std::uint8_t> rebuiltTextures;
    if (!WriteResourceTable(resources, document, rebuiltTextures, error))
        return false;
    rebuilt.insert(rebuilt.end(), prefix.Bytes().begin(), prefix.Bytes().end());
    rebuilt.insert(rebuilt.end(), resources.Bytes().begin(), resources.Bytes().end());
    rebuilt.insert(rebuilt.end(), world.Bytes().begin(), world.Bytes().end());
    rebuilt.resize((rebuilt.size() + 15u) & ~std::size_t(15u), 0);
    rebuilt.insert(rebuilt.end(), rebuiltTextures.begin(), rebuiltTextures.end());
    return true;
}

bool WritePayloadFile(const std::vector<std::uint8_t> &payload, const std::filesystem::path &destination,
                      std::string &error)
{
    if (payload.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        error = "The rebuilt payload is too large to write.";
        return false;
    }
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not create " + destination.string();
        return false;
    }
    output.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
    output.close();
    if (!output)
    {
        error = "The complete payload could not be written.";
        return false;
    }
    std::ifstream verify(destination, std::ios::binary | std::ios::ate);
    if (!verify || verify.tellg() != static_cast<std::streamoff>(payload.size()))
    {
        error = "The exported payload failed its size verification.";
        return false;
    }
    std::vector<std::uint8_t> written(payload.size());
    verify.seekg(0);
    verify.read(reinterpret_cast<char *>(written.data()), static_cast<std::streamsize>(written.size()));
    if (!verify || written != payload || HashBytes(written) != HashBytes(payload))
    {
        error = "The exported payload failed byte-for-byte verification.";
        return false;
    }
    return true;
}

std::vector<std::string> BuildObjectHierarchySignatures(const BrxDocument &document)
{
    std::vector<std::string> signatures;
    signatures.reserve(document.Objects().size());
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto &object = document.Objects()[objectIndex];
        std::string signature = std::to_string(object.cid) + ":" + std::to_string(object.oid) + ":" +
                                std::to_string(object.spliceIndex) + (object.proxyClone ? "|clone" : "|authored");
        std::int32_t parent = object.parentIndex;
        std::size_t depth = 0;
        while (parent >= 0 && depth++ <= document.Objects().size())
        {
            if (static_cast<std::size_t>(parent) >= document.Objects().size())
            {
                signature += "|bad-parent";
                break;
            }
            const auto &parentObject = document.Objects()[static_cast<std::size_t>(parent)];
            signature += "/" + std::to_string(parentObject.cid) + ":" + std::to_string(parentObject.oid) + ":" +
                         std::to_string(parentObject.spliceIndex);
            parent = parentObject.parentIndex;
        }
        if (object.proxyClone && object.proxySourceObjectIndex >= 0 &&
            static_cast<std::size_t>(object.proxySourceObjectIndex) < document.Objects().size())
        {
            const auto &source = document.Objects()[static_cast<std::size_t>(object.proxySourceObjectIndex)];
            signature += "|source=" + std::to_string(source.cid) + ":" + std::to_string(source.oid) + ":" +
                         std::to_string(source.spliceIndex);
        }
        signatures.push_back(std::move(signature));
    }
    std::sort(signatures.begin(), signatures.end());
    return signatures;
}
}

BrxCompilePlan BrxCompiler::Analyze(const BrxDocument &document, const ValidationSystem &validation) const
{
    BrxCompilePlan plan;
    plan.validationPassed = validation.ErrorCount() == 0;
    std::string cidCoverageError;
    plan.cidLayoutsAudited = VerifyCidWriterCoverage(document, cidCoverageError);
    std::vector<std::uint8_t> rebuilt;
    std::string rebuiltError;
    plan.writerAvailable = plan.cidLayoutsAudited && BuildRebuiltPayload(document, rebuilt, rebuiltError);
    if (!plan.cidLayoutsAudited && !cidCoverageError.empty())
        plan.blockers.emplace_back("CID writer layout audit: " + cidCoverageError);
    if (!plan.writerAvailable && !rebuiltError.empty())
        plan.blockers.emplace_back("Edited BRX writer: " + rebuiltError);
    return plan;
}

bool BrxCompiler::WriteRebuiltPayload(const BrxDocument &document, const ValidationSystem &validation,
                                      const std::filesystem::path &destination, std::string &error) const
{
    error.clear();
    (void)validation;
    if (destination.empty())
    {
        error = "No output path was selected.";
        return false;
    }
    std::vector<std::uint8_t> rebuilt;
    if (!BuildRebuiltPayload(document, rebuilt, error))
        return false;
    auto validationPath = destination;
    validationPath += ".pcbrx-validation-" + std::to_string(HashBytes(rebuilt)) + ".tmp";
    std::error_code cleanupError;
    std::filesystem::remove(validationPath, cleanupError);
    if (!WritePayloadFile(rebuilt, validationPath, error))
        return false;
    const auto discardValidationFile = [&]()
    {
        std::error_code ignored;
        std::filesystem::remove(validationPath, ignored);
    };
    BrxDocument reopened;
    std::string reopenError;
    if (!reopened.OpenReadOnly(validationPath, reopenError))
    {
        discardValidationFile();
        error = "The rebuilt BRX could not be reopened: " + reopenError;
        return false;
    }
    if (!reopened.PayloadValidated())
    {
        reopened.Close();
        discardValidationFile();
        error = "The reopened rebuilt BRX did not validate its payload.";
        return false;
    }
    // PROXY loading materializes clone-capacity/runtime instances that do not
    // exist as independent records in the BRX stream.  Comparing the editor's
    // expanded object vectors therefore rejects valid files whenever reopening
    // regenerates a different number/order of those bookkeeping objects.
    //
    // Validate the serialized contract instead: a successfully reopened file
    // must compile to exactly the same bytes.  This catches dropped authored
    // records, broken hierarchy framing, bad counts, and unstable references,
    // while correctly ignoring loader-only PROXY materialization.
    std::vector<std::uint8_t> canonicalRebuild;
    std::string canonicalError;
    if (!BuildRebuiltPayload(reopened, canonicalRebuild, canonicalError))
    {
        reopened.Close();
        discardValidationFile();
        error = "The reopened rebuilt BRX could not be serialized again: " + canonicalError;
        return false;
    }
    if (canonicalRebuild != rebuilt)
    {
        reopened.Close();
        discardValidationFile();
        error = "The rebuilt BRX is not serialization-stable after reopening (first pass " +
                std::to_string(rebuilt.size()) + " bytes, second pass " + std::to_string(canonicalRebuild.size()) +
                " bytes).";
        return false;
    }
    reopened.Close();
    // The staging file has proved that these exact bytes reopen correctly.
    // Write and byte-verify the requested path directly instead of renaming
    // the open/closed staging path.  Windows can reject that rename when a
    // shell extension, antivirus scanner, or stale file handle observes the
    // temporary file, which previously left the user with no exported BRX.
    if (!WritePayloadFile(rebuilt, destination, error))
    {
        discardValidationFile();
        return false;
    }
    discardValidationFile();
    return true;
}
