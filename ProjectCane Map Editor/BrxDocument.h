#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>
#include <glm/mat4x4.hpp>

class BrxLoader;

struct BrxLanguagePayload
{
    std::uint32_t languageId = 0;
    std::uint64_t reconstructedSize = 0;
    std::vector<std::uint32_t> chunkIds;
};

enum class BrxSectionState
{
    Parsed,
    Pending
};

struct BrxPayloadSection
{
    std::string name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    BrxSectionState state = BrxSectionState::Pending;
};

enum class BrxPropertyKind : std::uint8_t
{
    Bool,
    Float,
    Vector3,
    Vector4,
    Int32,
    Float2,
    UInt32,
    Int16,
    TypedInt16,
    Dynamic
};

// One concrete EOP value serialized on an object.  The CID tables in
// ProjectCane describe which EOPIDs are legal; this record preserves the
// values that are actually present in the BRX so the editor never fabricates
// defaults for omitted properties.
struct BrxPropertyRecord
{
    std::int16_t eopid = -1;
    BrxPropertyKind kind = BrxPropertyKind::Int16;
    std::uint32_t flags = 0;
    std::uint16_t argumentCount = 0;
    std::uint64_t offset = 0;
    std::uint32_t serializedSize = 0;
    bool editable = true;
    bool modified = false;
    bool boolValue = false;
    std::int32_t intValue = 0;
    std::uint32_t uintValue = 0;
    std::int16_t typeValue = 0;
    float floatValue[4]{};
    struct DynamicArgument
    {
        std::int16_t type = -1;
        std::int32_t intValue = 0;
        float floatValue[4]{};
    };
    std::vector<DynamicArgument> arguments;
};

// World options use the same EOP value encoding as object properties. Keeping
// the complete value is required for a genuine Scene World editor; the old
// offset-only record silently discarded almost every SW value during loading.
using BrxOptionRecord = BrxPropertyRecord;

struct BrxObjectDetail
{
    std::string name;
    std::string value;
};

enum class BrxProxyRootKind : std::uint8_t
{
    InlineSource,
    SourceReference
};

struct BrxProxyRootRecord
{
    BrxProxyRootKind kind = BrxProxyRootKind::InlineSource;
    std::int32_t sourceListIndex = -1;
    std::int32_t sourceProxyRecordIndex = -1;
    std::int32_t sourceProxyRootIndex = -1;
    std::int32_t instantiatedRootObjectIndex = -1;
    std::uint32_t objectBegin = 0;
    std::uint32_t objectCount = 0;
    // LoadProxyFromBrx reads this as U16. Keeping it signed silently turned
    // valid capacities 32768..65535 negative and made their sources unusable.
    std::uint16_t cloneCapacity = 1;
};

struct BrxProxyOverrideRecord
{
    std::int16_t targetOid = -1;
    std::int32_t targetObjectIndex = -1;
    std::int16_t replacementSpliceIndex = -1;
    std::uint32_t zoneMask = 0;
    std::int8_t mrdMode = 0;
    float maximumRenderDistance = 0.0f;
    std::int16_t inheritedSpliceIndex = -1;
    std::uint32_t inheritedZoneMask = 0;
    std::uint8_t inheritedMrdMode = 0;
    float inheritedMaximumRenderDistance = 0.0f;
    bool inheritedPropertiesCaptured = false;
    std::vector<BrxPropertyRecord> inheritedProperties;
    std::vector<BrxPropertyRecord> properties;
};

struct BrxProxyRecord
{
    std::int32_t ownerObjectIndex = -1;
    float localTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<BrxProxyRootRecord> roots;
    std::vector<BrxProxyOverrideRecord> overrides;
    std::vector<std::int32_t> attachedChildObjectIndices;
};

struct BrxSoBspNode
{
    std::uint16_t surfaceIndex = 0xffff;
    std::uint16_t negativeChild = 0xffff;
    std::uint16_t positiveChild = 0xffff;
};

struct BrxSoCameraGroup
{
    std::int16_t cameraStyle = -1;
    std::uint16_t globIndex = 0;
};

struct BrxSoHitSurfaceGroup
{
    std::int16_t globIndex = -1;
    std::int16_t subglobIndex = -1;
};

struct BrxSoGeometryInfo
{
    struct Edge
    {
        std::uint16_t firstPosition = 0;
        std::uint16_t secondPosition = 0;
        std::uint16_t adjacentSurface = 0xffff;
    };
    struct Surface
    {
        std::array<std::uint16_t, 3> positionIndices{};
        float radius = 0.0f;
        std::array<float, 3> center{};
        std::vector<Edge> edges;
    };
    float radius = 0.0f;
    std::uint16_t positionCount = 0;
    std::uint16_t surfaceCount = 0;
    std::uint16_t edgeCount = 0;
    // BSPC::cbspFull is the runtime pruning/intersection scratch capacity.
    // It is serialized independently of the active node count and may be
    // larger than bspNodes.size().
    std::uint16_t fullBspNodeCount = 0;
    float boundsMin[3]{};
    float boundsMax[3]{};
    std::vector<std::array<float, 3>> positions;
    std::vector<Surface> surfaces;
    std::vector<BrxSoBspNode> bspNodes;
};

struct BrxSoPayload
{
    bool present = false;
    // Import-provenance offsets retained only for diagnostics and migration of
    // old editor projects.  The BRX rebuilder never reads source bytes from
    // these ranges; it structurally emits every field below.
    std::uint64_t sourcePrefixOffset = 0;
    std::uint64_t sourcePrefixSize = 0;
    std::uint64_t massSourceOffset = 0;
    std::uint8_t physicsFlags = 0;
    bool fixedPhysics = false;
    float mass = 0.0f;
    float inertia[9]{};
    float centerOfMass[3]{};
    BrxSoGeometryInfo collisionGeometry;
    BrxSoGeometryInfo cameraGeometry;
    std::vector<BrxSoCameraGroup> cameraGroups;
    std::vector<std::int16_t> bspNodeToCameraGroup;
    std::vector<BrxSoHitSurfaceGroup> hitSurfaceGroups;
    std::vector<std::int16_t> surfaceToHitSurfaceGroup;
    bool modified = false;
};

struct BrxTnSurface
{
    float planeDistance = 0.0f;
    float normal[3]{};
};

struct BrxTnBspNode
{
    std::uint16_t surfaceIndex = 0xffff;
    std::int16_t negativeChild = -1;
    std::int16_t positiveChild = -1;
};

struct BrxTnCurvePoint
{
    float parameter = 0.0f;
    float position[3]{};
    float incomingTangent[3]{};
    float outgoingTangent[3]{};
};

// TN does not use LoadAloFromBrx. Its transform, trigger BSP, and camera
// path/frame are a class-owned prefix before the ordinary option list.
struct BrxTnPayload
{
    bool present = false;
    std::vector<BrxTnSurface> surfaces;
    std::vector<BrxTnBspNode> bspNodes;
    std::int8_t curveKind = -1; // -1 static frame, 0 linear, 1 cubic
    bool curveClosed = false;
    std::vector<BrxTnCurvePoint> curvePoints;
    float staticFrame[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool modified = false;
};

enum class BrxDialogEventKind : std::int8_t
{
    PreloadAudio = 0,
    SpeakerLeft = 1,
    SpeakerRight = 2,
    Talk = 3,
    CallSplice = 4,
    ShowLetterbox = 5,
    HideLetterbox = 6,
    ShowMovie = 7
};

struct BrxDialogEvent
{
    BrxDialogEventKind kind = BrxDialogEventKind::Talk;
    std::string resourceName;
    std::int16_t primaryOid = -1;
    std::int16_t replacementOid = -1;
    // Retail uses negative values as "leave zoom unchanged" and "no timed pause".
    float zoom = -1.0f;
    float pauseSeconds = -1.0f;
    bool waitForInput = false;
    std::uint8_t promptKind = 0;
    std::int16_t speakerOid = -1;
    std::int16_t lipsyncAnimationOid = -1;
    std::int16_t focusOid = -1;
    std::int16_t anchorPointOid = -1;
    std::string subtitle;
    bool modified = false;
};

struct BrxDialogPayload
{
    bool present = false;
    std::vector<BrxDialogEvent> events;
    bool modified = false;
};

struct BrxAloxRecord
{
    std::uint32_t flags = 0;
    float preRotation[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float postRotation[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::int16_t rotationSourceOid = -1;
    // Runtime SetAloTransformBasis can install a foster basis that overlays
    // the serialized SCJ rotation-source pointer. This editor-only selection
    // previews that live state without pretending it is BRX-authored data.
    std::int32_t editorFosterObjectIndex = -1;
    // Stable identity for the editor-only foster preview.  The cached index is
    // only an acceleration hint and must be revalidated after structural edits.
    std::int16_t editorFosterOid = -1;
    std::int16_t editorFosterSpliceIndex = -1;
    std::uint64_t editorFosterObjectOffset = 0;
    std::int16_t ikTargetOid = -1;
    std::int16_t elbowOid = -1;
    float ikPosition[3]{};
    float wristPosition[3]{};
    float twistRadians = 0.0f;
    std::int16_t jointParentOid = -1;
    std::int16_t jointChildOid = -1;
    bool segmentScaleCompensation = false;
    bool modified = false;
};

struct BrxPoseCombinationRecord
{
    std::int16_t oid = -1;
    std::vector<float> weights;
};

struct BrxBitmapRecord
{
    std::uint64_t editorResourceId = 0;
    // Editor-only mutation generation. Raw pixels can be many megabytes, so
    // the per-frame dirty fingerprint tracks deliberate payload replacements
    // through this counter instead of rehashing every texel continuously.
    std::uint64_t editorContentRevision = 0;
    std::string editorName;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t zoneFlags = 0;
    std::uint32_t mipCount = 0;
    std::uint32_t pixelBytes = 0;
    std::uint32_t textureDataOffset = 0;
    std::uint32_t textureFlags = 0;
    std::uint32_t textureFormat = 0;
    std::vector<std::uint8_t> pixels;
};

enum class BrxShaderAnimationKind : std::int16_t
{
    None = 0,
    Loop = 1,
    PingPong = 2,
    Shuffle = 3,
    Hologram = 4,
    Eyes = 5,
    Scroller = 6,
    Circler = 7,
    Looker = 8
};

// Exact editable representation of ProjectCane's SAAF payload.  The six
// values are interpreted according to kind; keeping the common prefix here
// also lets unknown/reserved tail values round-trip once saving is enabled.
struct BrxShaderAnimationRecord
{
    BrxShaderAnimationKind kind = BrxShaderAnimationKind::None;
    std::int16_t shaderOid = -1;
    bool instanced = false;
    float values[6]{};
    // LOOP serializes this word as S16; PINGPONG serializes the same union
    // slot as U16.  Keep the editor value wide enough to represent both.
    std::int32_t initialFrame = 0;
    std::int16_t otherShaderOid = -1;
    std::uint32_t symmetryCount = 1;
    bool modified = false;
};

inline void ResetShaderAnimationPayload(BrxShaderAnimationRecord &animation, BrxShaderAnimationKind kind)
{
    animation.kind = kind;
    std::fill(std::begin(animation.values), std::end(animation.values), 0.0f);
    animation.initialFrame = 0;
    animation.otherShaderOid = -1;
    animation.symmetryCount = 1;
    switch (kind)
    {
    case BrxShaderAnimationKind::Loop:
    case BrxShaderAnimationKind::PingPong:
        animation.values[0] = animation.values[1] = 1.0f;
        break;
    case BrxShaderAnimationKind::Shuffle:
        animation.values[0] = animation.values[1] = 0.1f;
        break;
    case BrxShaderAnimationKind::Eyes:
        animation.values[0] = 0.2f;
        animation.values[1] = 1.0f;
        animation.values[2] = 2.0f;
        animation.values[3] = 0.1f;
        break;
    case BrxShaderAnimationKind::Scroller:
        animation.values[2] = animation.values[3] = 1.0f;
        break;
    case BrxShaderAnimationKind::Circler:
        animation.values[0] = 1.0f;
        animation.values[1] = 0.1f;
        break;
    case BrxShaderAnimationKind::Looker:
        animation.values[0] = animation.values[1] = 0.5f;
        animation.values[3] = animation.values[5] = 1.0f;
        break;
    default:
        break;
    }
    animation.modified = true;
}

struct BrxShaderRecord
{
    std::uint64_t editorResourceId = 0;
    std::string editorName;
    std::uint64_t offset = 0;
    std::uint64_t sourcePrefixOffset = 0;
    std::uint64_t sourcePrefixSize = 0;
    std::uint64_t sourceTailOffset = 0;
    std::uint64_t sourceTailSize = 0;
    std::int16_t kind = 0;
    std::uint8_t flags = 0;
    std::int16_t oid = -1;
    std::uint32_t zoneFlags = 0;
    std::int16_t alternateSaturationOid = -1;
    std::uint8_t renderPhase = 0;
    std::uint8_t textureStageCount = 0;
    // Cached serialized discriminator; keep the default synchronized with
    // animation.kind so a newly constructed material is internally valid.
    std::int16_t animationKind = static_cast<std::int16_t>(BrxShaderAnimationKind::None);
    BrxShaderAnimationRecord animation;
    float rgba[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float volumeRgba[4]{1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<std::int32_t> ambientBitmapIndices;
    std::vector<std::int32_t> diffuseBitmapIndices;
    std::vector<std::int32_t> saturateBitmapIndices;
    struct TextureStage
    {
        std::int16_t oid = -1;
        std::int16_t flags = 0;
        std::vector<std::int32_t> ambientBitmapIndices;
        std::vector<std::int32_t> diffuseBitmapIndices;
        std::vector<std::int32_t> saturateBitmapIndices;
    };
    std::vector<TextureStage> textureStages;
};

struct BrxFontGlyphRecord
{
    std::int16_t character = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t width = 0;
};

struct BrxFontRecord
{
    std::int32_t bitmapIndex = -1;
    std::uint8_t characterWidth = 0;
    std::uint8_t spaceWidth = 0;
    std::uint8_t height = 0;
    std::uint8_t kind = 0;
    float scale = 1.0f;
    std::vector<BrxFontGlyphRecord> glyphs;
};

struct BrxObjectRecord
{
    std::uint64_t offset = 0;
    std::int16_t cid = -1;
    std::int16_t oid = -1;
    std::int16_t spliceIndex = -1;
    std::int32_t parentIndex = -1;
    // PROXY roots are serialized beneath their wrapper but ProjectCane
    // reparents them to the wrapper's parent after applying the proxy.
    std::int32_t proxyRuntimeParentIndex = -1;
    std::int32_t proxyRecordIndex = -1;
    std::int32_t proxyRootIndex = -1;
    std::int32_t proxySourceObjectIndex = -1;
    bool proxyClone = false;
    std::uint64_t dataOffset = 0;
    std::uint64_t endOffset = 0;
    bool loaderImplemented = false;
    float localTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float worldTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::uint32_t firstRenderGroup = 0xffffffffu;
    std::uint32_t renderGroupCount = 0;
    std::uint8_t visibilityMode = 0;
    std::uint8_t zoneMode = 0;
    std::uint8_t mrdMode = 0;
    std::uint32_t zoneMask = 0;
    float maximumRenderDistance = 0.0f;
    float celBorderMaximumDistance = 0.0f;
    bool maximumRenderDistanceDefault = false;
    bool celBorderMaximumDistanceDefault = false;
    float renderRadiusSelf = 0.0f;
    float renderRadiusAll = 0.0f;
    bool hasAloPayload = false;
    std::uint64_t aloPayloadOffset = 0;
    std::uint64_t globsetSourceOffset = 0;
    std::uint64_t globsetSourceSize = 0;
    BrxSoPayload so;
    bool hasCnvoPayload = false;
    float cnvoAverageUVelocity = 0.0f;
    float cnvoAverageVVelocity = 0.0f;
    BrxTnPayload tn;
    BrxDialogPayload dialog;
    BrxAloxRecord alox;
    std::uint16_t globPoseCount = 0;
    std::vector<BrxPoseCombinationRecord> poseCombinations;
    bool hidden = false;
    std::vector<BrxPropertyRecord> properties;
    std::vector<BrxObjectDetail> details;
    std::string loaderName;
    // Editor authoring state lives at the end so legacy aggregate initializers
    // retain the serialized-field ordering of BrxObjectRecord.
    bool spliceModified = false;
    // Editor-only alias. The retail BRX writer intentionally never serializes
    // this string; .caneproject persistence owns it.
    std::string editorName;
};

struct BrxRenderVertex
{
    float position[3]{};
    float normal[3]{};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float uv[2]{};
    // Editor-only visualization channel; never serialized to PCBRX.
    float boneWeightPreview = 0.0f;
};

// Lossless PCBRX source vertex. BrxRenderVertex remains the compact,
// world-space renderer cache generated from this authored local-space data.
struct BrxSourceVertex
{
    float position[3]{};
    float normal[3]{};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float uv[2]{};
    std::uint16_t boneIndices[4]{};
    float boneWeights[4]{};
};

struct BrxSourceCelPoint
{
    float position[4]{};
    std::uint16_t boneIndices[4]{};
    float boneWeights[4]{};
};

struct BrxSourceCelRecord
{
    std::vector<BrxSourceCelPoint> points;
    // PCBRX stores one vec4 position delta per point for every GLOB pose.
    std::vector<std::array<float, 4>> posePositionDeltas;
};

struct BrxRenderBatch
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint16_t shaderIndex = 0xffff;
    std::uint8_t renderPhase = 0;
    float selfIllumination = 0.0f;
    float boundsCenter[3]{};
    float boundsRadius = 0.0f;
    float mrdRadius = 0.0f;
    std::uint32_t zoneMask = 0xffffffffu;
    std::uint32_t renderGroup = 0;
    // GLOB grfglob bits 0/1 select projected-shadow shader kinds 2/3.
    // Editor-only draw metadata; the authoritative serialized value remains
    // BrxGlobRecord::flags.
    std::uint16_t shadowReceiverMask = 0;
    // Submission state belongs to the owning GLOB, not an individual
    // SUBGLOB.  RenderAloGlobset/RenderMsGlobset create one RPL per GLOB.
    float sortCenter[3]{};
    float explicitSortZ = 0.0f;
    bool hasExplicitSortZ = false;
    bool translucentSort = false;
    std::uint8_t projectedFlags = 0;
    float fogScale = 1.0f;
    float maximumRenderDistance = 1.0e10f;
    // GLOB::fDynamic is initialized from GLOBSET::fRelight. Dynamic GLOBs use
    // center-based positional/frustum lighting and honor LIGHT's
    // fExcludeDynamicObjects flag, matching ProjectCane's glob.vert path.
    bool dynamicLighting = false;
    bool hasGleam = false;
    float gleamNormal[3]{};
    float gleamCurve[4]{};
    // GLOB::rtck and the submission matrix used by RenderAloGlobset. Geometry
    // is baked into world space by the editor, so rendering applies
    // AdjustAloRtckMat(base) * inverse(base) as a per-frame delta.
    std::uint8_t rotationTracking = 0;
    float rtckBaseMatrix[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::int32_t ownerObjectIndex = -1;
    bool hasShaderAnimation = false;
    BrxShaderAnimationRecord shaderAnimation;
    bool hasEditorInstanceTransform = false;
    float editorInstanceTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

struct BrxCelEdge
{
    // ProjectCane CELPOINT order: edge endpoints E0/E1 followed by the
    // opposite vertex from each adjacent face OA/OB.
    float points[4][4]{};
};

struct BrxCelBatch
{
    std::uint32_t firstEdge = 0;
    std::uint32_t edgeCount = 0;
    std::uint32_t renderGroup = 0;
    std::uint8_t renderPhase = 5;
    std::uint32_t zoneMask = 0xffffffffu;
    float center[3]{};
    float radius = 0.0f;
    // GLOB::sMRD is needed even for cel-only GLOBs that have no ordinary
    // triangle batch to inherit it from.
    float surfaceMaximumDistance = 1.0e10f;
    float maximumDistance = 2000.0f;
    float color[4]{16.0f / 255.0f, 16.0f / 255.0f, 16.0f / 255.0f, 1.0f};
    std::int32_t ownerObjectIndex = -1;
};

struct BrxSubglobRecord
{
    // Editor-only label; source SUBGLOB order remains the retail identity.
    std::string editorName;
    float center[3]{};
    float radius = 0.0f;
    std::uint16_t vertexCount = 0;
    std::uint16_t triangleCount = 0;
    std::uint16_t shaderIndex = 0xffff;
    std::uint16_t boneCount = 0;
    float selfIllumination = 0.0f;
    std::vector<BrxSourceVertex> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    // Indexed as pose * vertexCount + vertex.
    std::vector<std::array<float, 4>> posePositionDeltas;
    std::vector<std::array<float, 4>> poseNormalDeltas;
};

struct BrxGlobsetRecord
{
    std::int32_t ownerObjectIndex = -1;
    bool relight = false;
    // The retail loader currently ignores this byte, but retaining it makes
    // the document lossless and permits later cross-version analysis.
    std::uint8_t unknownHeaderByte = 0;
    std::vector<std::int16_t> boneOids;
    std::vector<float> poseWeights;
    std::vector<float> originalPoseWeights;
    std::vector<std::size_t> globIndices;
};

// Authoritative source representation of one ProjectCane GLOB. GPU packets
// below are derived caches, never the source of serialized field values.
struct BrxGlobRecord
{
    // Editor-only label; renderGroup remains the retail identity.
    std::string editorName;
    std::uint32_t renderGroup = 0;
    std::int32_t ownerObjectIndex = -1;
    std::uint16_t propertyMask = 0;
    bool instanced = false;
    std::int16_t sourceGlobIndex = -1;
    std::size_t sourceRecordIndex = static_cast<std::size_t>(-1);
    float instanceTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    // Authored local geometry -> loaded world transform. This keeps source
    // geometry editable without treating the world-space GPU cache as source.
    float sourceToWorld[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    // Loaded BRX instances already have their serialized matrix baked into the
    // editor's world-space vertex cache. Further editor movement is therefore
    // stored as a world-space delta instead of applying that matrix twice.
    bool hasEditorPlacementTransform = false;
    float editorPlacementTransform[16]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    bool hasZoneMask = false;
    std::uint32_t zoneMask = 0;
    bool hasSubglobRadiusScale = false;
    float subglobRadiusScale = 1.0f;
    bool hasExplicitSortZ = false;
    float authoredSortZ = 0.0f;
    float effectiveSortZ = 0.0f;
    bool hasFogScale = false;
    float fogScale = 1.0f;
    bool hasMaximumRenderDistance = false;
    float authoredMaximumRenderDistance = 1.0e10f;
    bool maximumRenderDistanceInfinite = false;
    float maximumRenderDistance = 1.0e10f;
    bool hasCelMaximumRenderDistance = false;
    float authoredCelMaximumRenderDistance = 2000.0f;
    bool celMaximumRenderDistanceInfinite = false;
    float celMaximumRenderDistance = 2000.0f;
    bool hasShaderAnimation = false;
    BrxShaderAnimationRecord shaderAnimation;
    bool hasGleam = false;
    float gleamNormal[3]{};
    float gleamCurve[4]{};
    bool hasWarp = false;
    std::int16_t warpControllerOid = -1;
    std::int8_t warpEnvelopeKind = -1;
    std::uint8_t warpMatrixCount = 0;
    bool warpPosition = false;
    bool warpUv = false;
    float center[3]{};
    float radius = 0.0f;
    std::int16_t oid = -1;
    std::uint8_t rotationTracking = 0;
    std::uint8_t renderPhase = 0;
    std::uint8_t flags = 0;
    std::vector<BrxSubglobRecord> sourceSubglobs;
    std::vector<BrxSourceCelRecord> sourceCelRecords;
    std::vector<BrxRenderBatch> derivedBatches;
    std::vector<BrxCelBatch> derivedCelPackets;
    bool modified = false;
    bool editorCreatedInstance = false;
};

struct BrxCollisionTriangle
{
    float positions[3][3]{};
    std::int32_t ownerObjectIndex = -1;
    std::uint32_t surfaceIndex = 0;
    bool cameraGeometry = false;
};

struct BrxLightRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int16_t kind = 0;
    bool dynamic = false;
    bool excludeDynamicObjects = false;
    float position[3]{};
    // EOPID 498 is retained separately from the renderer-space direction.
    // ProjectCane stores it as vecDirectionOrig, while BuildLightBlk derives
    // the submitted direction from the LIGHT object's transform.
    float authoredDirection[3]{0.0f, 0.0f, 1.0f};
    float direction[3]{0.0f, 0.0f, 1.0f};
    float highlightHsv[3]{};
    float midtoneStrength = 0.0f;
    float shadowStrength = 0.0f;
    float highlightAngle = 180.0f;
    float midtoneAngle = 240.0f;
    float shadowAngle = 180.0f;
    float falloff[2]{200.0f, 2000.0f};
    float coneAngle = 60.0f;
    float hotSpotAngle = 0.0f;
    float up[3]{0.0f, 0.0f, 1.0f};
};

struct BrxCameraRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int16_t targetOid = -1;
    float eye[3]{};
    float view[3]{0.0f, 1.0f, 0.0f};
    float up[3]{0.0f, 0.0f, 1.0f};
};

struct BrxCheckpointRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int16_t warpOid = -1;
};

struct BrxWarpRecord
{
    std::int32_t ownerObjectIndex = -1;
    float velocity[3]{};
    bool isDefault = false;
    bool isSpecialDefault = false;
    std::int16_t aliasOid = -1;
    float cameraInitialAngle = 0.0f;
    bool usesCameraInitialAngle = false;
    std::string destinationLevel;
    std::int16_t embeddedAnimationCount = 0;
    std::vector<std::int16_t> hiddenObjectOids;
};

struct BrxExitRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::string destinationLevel;
    std::string alternateLevel;
    std::int16_t embeddedAnimationCount = 0;
};

struct BrxScanRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::string text;
};

enum class BrxAsegEventLane : std::uint8_t
{
    Apply,
    Frame,
    Retract
};

struct BrxAsegScalarKey
{
    float time = 0.0f;
    float value = 0.0f;
    std::int8_t tangentInKind = 0;
    std::int8_t tangentOutKind = 0;
    float tangentIn = 0.0f;
    float tangentOut = 0.0f;
    float tangentInTime = 0.0f;
    float tangentOutTime = 0.0f;
};

struct BrxAsegVectorKey
{
    float time = 0.0f;
    std::array<float, 3> value{};
    std::array<float, 3> tangentIn{};
    std::array<float, 3> tangentOut{};
};

struct BrxAsegScalarController
{
    std::int8_t kind = -1;
    std::vector<BrxAsegScalarKey> keys;
};

struct BrxAsegControllerSummary
{
    std::int8_t kind = -1;
    std::uint16_t keyCount = 0;
    std::uint8_t componentMask = 0;
    std::array<float, 3> defaultValue{};
    std::array<BrxAsegScalarController, 3> components;
    std::vector<BrxAsegScalarKey> scalarKeys;
    std::vector<BrxAsegVectorKey> vectorKeys;
};

struct BrxAsegChannelRecord
{
    std::int16_t targetOid = -1;
    std::uint8_t flags = 0;
    BrxAsegControllerSummary position;
    BrxAsegControllerSummary rotation;
    BrxAsegControllerSummary scale;
    BrxAsegControllerSummary twist;
    std::vector<BrxAsegControllerSummary> poses;
    bool stripChannel = false;
};

// ASEG event payloads are deliberately kept as typed generic slots.  The
// event type defines their meaning, while retaining every serialized scalar
// lets the timeline inspect and eventually rewrite records without confusing
// runtime actions (Apply/Ensure/FindLabel) with authored event data.
struct BrxAsegEventRecord
{
    BrxAsegEventLane lane = BrxAsegEventLane::Frame;
    std::int8_t type = -1;
    std::int16_t frame = 0;
    std::array<std::int16_t, 4> shorts{-1, -1, -1, -1};
    std::array<std::int32_t, 2> integers{};
    std::array<float, 4> floats{};
    std::string text;
};

struct BrxAsegRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int16_t rootOid = -1;
    std::int16_t maximumFrame = 0;
    std::vector<std::int16_t> searchRootOids;
    std::vector<BrxAsegChannelRecord> channels;
    std::vector<BrxAsegEventRecord> events;
    bool runtimeBlendClass = false;
};

// A GLOB's serialized WRBG binding.  WR is the game's animated mesh/UV warp
// controller (CID 141); it is unrelated to the teleport WARP object (CID 116).
struct BrxGlobWarpRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::uint32_t renderGroup = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstCelEdge = 0;
    std::uint32_t celEdgeCount = 0;
    std::int16_t controllerOid = -1;
    std::int8_t envelopeKind = -1;
    float innerDistance = 0.0f;
    float innerWeight = 1.0f;
    float outerDistance = 0.0f;
    float outerWeight = 1.0f;
    float envelopeMatrix[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::uint8_t matrixCount = 0;
    bool deformPosition = false;
    bool deformUv = false;
    float objectToWorld[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

struct BrxLaserBeamRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int32_t shapeObjectIndex = -1;
    // CRV control vertices are serialized in the owning LASEN's local space.
    // Keeping them local is required for proxy clones, whose LASEN root gets a
    // different world transform while the cloned SHAPE retains the same CVs.
    std::vector<std::array<float, 3>> points;
};

struct BrxShapePathRecord
{
    std::int32_t ownerObjectIndex = -1;
    bool closed = false;
    bool hermite = false;
    // CRV stores an authored parameter and, for CRVC, incoming/outgoing
    // derivatives for every control vertex. Preserve all of it so traversal
    // previews and eventual export match the game rather than a polyline.
    std::vector<float> parameters;
    std::vector<std::array<float, 3>> points;
    std::vector<std::array<float, 3>> tangentsIn;
    std::vector<std::array<float, 3>> tangentsOut;
};

struct BrxPathZoneRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::uint16_t boundaryEdgeCount = 0;
    std::vector<std::array<float, 3>> vertices;
    // Serialized per-vertex edge lists are the graph consumed by PATHZONE A*.
    // Keep them rather than deriving connectivity only from triangle geometry.
    std::vector<std::vector<std::uint16_t>> vertexEdges;
    std::vector<std::array<std::uint16_t, 2>> edges;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    struct BspNode
    {
        std::array<float, 3> normal{};
        float distance = 0.0f;
        std::int16_t negativeChild = -1;
        std::int16_t positiveChild = -1;
    };
    std::vector<BspNode> bspNodes;
    bool modified = false;
};

struct BrxSmStateRecord
{
    std::int16_t oid = -1;
    std::int16_t automaticNextOid = -1;
};

struct BrxSmTransitionRecord
{
    bool ownsAnimation = false;
    std::int16_t fromOid = -1;
    std::int16_t toOid = -1;
    std::int32_t flags = 0;
    float probability = 0.0f;
    std::int32_t animationObjectIndex = -1;
};

struct BrxSmRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::vector<BrxSmStateRecord> states;
    std::vector<BrxSmTransitionRecord> transitions;
    std::int16_t serializedTransitionCursor = 0;
    bool modified = false;
};

struct BrxRchmBspNode
{
    std::int16_t surfaceIndex = -1;
    std::int16_t negativeChild = 0;
    std::int16_t positiveChild = 0;
};

struct BrxRchmEdgeRecord
{
    std::uint16_t firstPosition = 0;
    std::uint16_t secondPosition = 0;
    std::uint16_t adjacentSurface = 0;
};

struct BrxRchmSurfaceRecord
{
    std::array<std::uint16_t, 3> planePositions{};
    float radius = 0.0f;
    std::array<float, 3> center{};
    std::vector<BrxRchmEdgeRecord> edges;
};

struct BrxRchmRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::array<std::int16_t, 24> animationOids{};
    std::int16_t gridResolution = 0;
    float geometryRadius = 0.0f;
    std::vector<std::array<float, 3>> positions;
    std::uint16_t totalEdgeCount = 0;
    std::vector<BrxRchmSurfaceRecord> surfaces;
    std::vector<bool> enabledPositions;
    std::vector<std::array<std::int16_t, 4>> tetrahedra;
    std::vector<BrxRchmBspNode> bspNodes;
    float radiusSquared = 0.0f;
    bool modified = false;
};

struct BrxKeyholeRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::vector<std::array<float, 2>> positions;
    std::array<float, 2> minimum{};
    std::array<float, 2> maximum{};
    std::array<std::vector<std::array<std::uint16_t, 3>>, 5> layers;
    bool modified = false;
};

struct BrxEffectCurvePoint
{
    float u = 0.0f;
    std::array<float, 3> position{};
    std::array<float, 3> tangentIn{};
    std::array<float, 3> tangentOut{};
};

struct BrxEffectMeshTriangle
{
    std::array<std::uint16_t, 3> indices{};
    float area = 0.0f;
};

struct BrxEffectRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::int16_t originKind = 0;
    std::int8_t curveKind = -1;
    bool curveClosed = false;
    std::vector<BrxEffectCurvePoint> curvePoints;
    std::vector<std::array<float, 3>> meshPositions;
    std::vector<BrxEffectMeshTriangle> meshTriangles;
    std::array<float, 3> meshCenter{};
    bool colorsAreRanges = false;
    std::vector<std::array<float, 4>> colors;
};

// Plane BSP used by VOL, RATHOLE, and EXIT. The retail point query transforms
// the test point into the owning object's local space and walks these planes,
// so keep the payload separate from SO triangle collision.
struct BrxVolumePlane
{
    float distance = 0.0f;
    std::array<float, 3> normal{};
};

struct BrxVolumeNode
{
    std::uint16_t surfaceIndex = 0;
    std::uint16_t negativeChild = 0xffff;
    std::uint16_t positiveChild = 0xffff;
};

struct BrxVolumeRecord
{
    std::int32_t ownerObjectIndex = -1;
    std::vector<BrxVolumePlane> planes;
    std::vector<BrxVolumeNode> nodes;
};

struct BrxVisibilityNode
{
    float normal[3]{};
    float planeDistance = 0.0f;
    std::int32_t negativeChild = -1;
    std::uint32_t negativeZoneMask = 0;
    std::int32_t positiveChild = -1;
    std::uint32_t positiveZoneMask = 0;
};

struct BrxWorldHeader
{
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::string soundBankFile;
    std::uint64_t spliceMapOffset = 0;
    std::uint32_t spliceCount = 0;
    std::uint32_t symbolCount = 0;
    std::uint32_t extraOptionCount = 0;
    std::uint32_t expressionNodeCount = 0;
    std::uint64_t worldOptionsOffset = 0;
    std::uint64_t commentaryOffset = 0;
    std::string commentaryAudioFile;
    std::string commentarySpeakers;
    std::uint64_t musicOffset = 0;
    std::string musicFile;
    std::uint64_t shadersOffset = 0;
    std::uint16_t declaredShaderAnimationCount = 0;
    std::uint16_t fontCount = 0;
    std::uint64_t fontTableSourceOffset = 0;
    std::uint64_t fontTableSourceSize = 0;
    std::uint64_t worldObjectsOffset = 0;
    std::uint16_t rootObjectCount = 0;
    std::int16_t firstRootCid = -1;
    std::int16_t firstRootOid = -1;
    std::int16_t firstRootSpliceIndex = -1;
    std::string objectParseDiagnostic;
    std::uint32_t parsedGlobCount = 0;
    std::uint32_t parsedSubglobCount = 0;
    std::uint64_t parsedVertexCount = 0;
    std::uint64_t parsedTriangleCount = 0;
    std::uint64_t parsedCollisionVertexCount = 0;
    std::uint64_t parsedCollisionSurfaceCount = 0;
    std::uint64_t parsedBspNodeCount = 0;
    std::uint32_t visibilityNodeCount = 0;
    std::uint16_t visibilityZoneCount = 0;
    float worldMidtone = 0.0f;
    float worldShadow = 50.0f / 255.0f;
    float skyColor[4]{16.0f / 255.0f, 16.0f / 255.0f, 40.0f / 255.0f, 80.0f / 255.0f};
    float cameraFov = 1.0f;
    float cameraNearClip = 100.0f;
    float cameraFarClip = 20000.0f;
    float cameraMrdRatio = 1.0f;
    // InitCm defaults used when SW does not serialize fog overrides.
    float fogNear = 500.0f;
    float fogFar = 20000.0f;
    float fogMaximum = 0.5f;
    float fogColor[4]{};
};

// Lossless in-memory form of ProjectCane's compact CPair splice stream.  The
// numeric tag and payload remain authoritative; editor analysis is layered on
// top and never rewrites unknown constructs.
struct BrxSpliceNode
{
    std::uint8_t tag = 0;
    std::int32_t childNode = -1;
    std::int32_t nextNode = -1;
    std::int32_t intValue = 0;
    float floatValue[16]{};
    std::uint64_t offset = 0;
    std::uint32_t serializedSize = 0;
    bool modified = false;
    bool editorCreated = false;
};

struct BrxSpliceModule
{
    std::int32_t rootNode = -1;
    std::uint64_t offset = 0;
    std::uint32_t serializedSize = 0;
    bool modified = false;
    bool editorCreated = false;
};

class BrxDocument
{
  public:
    bool OpenReadOnly(const std::filesystem::path &path, std::string &error);
    void Close();

    [[nodiscard]] bool IsOpen() const
    {
        return !path_.empty();
    }
    [[nodiscard]] const std::filesystem::path &Path() const
    {
        return path_;
    }
    [[nodiscard]] std::uint32_t Version() const
    {
        return version_;
    }
    [[nodiscard]] std::uint64_t FileSize() const
    {
        return fileSize_;
    }
    [[nodiscard]] std::uint32_t DefaultLanguage() const
    {
        return defaultLanguage_;
    }
    [[nodiscard]] std::uint32_t ChunkCount() const
    {
        return chunkCount_;
    }
    [[nodiscard]] const std::vector<BrxLanguagePayload> &Languages() const
    {
        return languages_;
    }
    [[nodiscard]] const std::vector<std::uint8_t> &Payload() const
    {
        return payload_;
    }
    [[nodiscard]] bool PayloadValidated() const
    {
        return payloadValidated_;
    }
    [[nodiscard]] const BrxWorldHeader &WorldHeader() const
    {
        return worldHeader_;
    }
    [[nodiscard]] BrxWorldHeader &EditableWorldHeader()
    {
        return worldHeader_;
    }
    [[nodiscard]] const std::vector<BrxPayloadSection> &Sections() const
    {
        return sections_;
    }
    [[nodiscard]] const std::vector<BrxOptionRecord> &WorldOptions() const
    {
        return worldOptions_;
    }
    [[nodiscard]] std::vector<BrxOptionRecord> &EditableWorldOptions()
    {
        return worldOptions_;
    }
    [[nodiscard]] const std::vector<BrxSpliceNode> &SpliceNodes() const
    {
        return spliceNodes_;
    }
    [[nodiscard]] const std::vector<BrxSpliceModule> &SpliceModules() const
    {
        return spliceModules_;
    }
    [[nodiscard]] std::vector<BrxSpliceNode> &EditableSpliceNodes()
    {
        return spliceNodes_;
    }
    [[nodiscard]] std::vector<BrxSpliceModule> &EditableSpliceModules()
    {
        return spliceModules_;
    }
    [[nodiscard]] const std::vector<BrxBitmapRecord> &Bitmaps() const
    {
        return bitmaps_;
    }
    [[nodiscard]] std::vector<BrxBitmapRecord> &EditableBitmaps()
    {
        return bitmaps_;
    }
    [[nodiscard]] const std::vector<BrxShaderRecord> &Shaders() const
    {
        return shaders_;
    }
    [[nodiscard]] std::vector<BrxShaderRecord> &EditableShaders()
    {
        return shaders_;
    }
    [[nodiscard]] const std::vector<BrxFontRecord> &Fonts() const
    {
        return fonts_;
    }
    [[nodiscard]] std::vector<BrxFontRecord> &EditableFonts()
    {
        return fonts_;
    }
    std::size_t AddBitmap(BrxBitmapRecord bitmap);
    // A staged snapshot must never rewind allocation after an intervening
    // create/delete or undo sequence. This counter is editor-only identity.
    void PreserveEditorResourceIdentityFloor(const BrxDocument &previous)
    {
        nextEditorResourceId_ = (std::max)(nextEditorResourceId_, previous.nextEditorResourceId_);
    }
    std::size_t DuplicateBitmap(std::size_t bitmapIndex);
    bool DeleteBitmapAndRemap(std::size_t bitmapIndex, std::int32_t replacementIndex);
    std::size_t AddShader(BrxShaderRecord shader);
    std::size_t DuplicateShader(std::size_t shaderIndex);
    bool DeleteShaderAndRemap(std::size_t shaderIndex, std::int32_t replacementIndex);
    [[nodiscard]] const std::vector<BrxObjectRecord> &Objects() const
    {
        return objects_;
    }
    [[nodiscard]] std::vector<BrxObjectRecord> &EditableObjects()
    {
        return objects_;
    }
    [[nodiscard]] std::string ObjectEditorLabel(std::int32_t objectIndex) const;
    [[nodiscard]] std::int32_t FindObjectByEditorName(const std::string &name) const;
    [[nodiscard]] const std::vector<BrxProxyRecord> &Proxies() const
    {
        return proxies_;
    }
    [[nodiscard]] std::vector<BrxProxyRecord> &EditableProxies()
    {
        return proxies_;
    }
    [[nodiscard]] const std::vector<BrxRenderVertex> &RenderVertices() const
    {
        return renderVertices_;
    }
    [[nodiscard]] const std::vector<std::uint32_t> &RenderIndices() const
    {
        return renderIndices_;
    }
    [[nodiscard]] const std::vector<BrxRenderBatch> &RenderBatches() const
    {
        return renderBatches_;
    }
    [[nodiscard]] std::vector<BrxRenderBatch> &EditableRenderBatches()
    {
        return renderBatches_;
    }
    [[nodiscard]] const std::vector<BrxCelEdge> &CelEdges() const
    {
        return celEdges_;
    }
    [[nodiscard]] const std::vector<BrxCelBatch> &CelBatches() const
    {
        return celBatches_;
    }
    [[nodiscard]] std::vector<BrxCelBatch> &EditableCelBatches()
    {
        return celBatches_;
    }
    [[nodiscard]] const std::vector<BrxGlobRecord> &Globs() const
    {
        return globs_;
    }
    [[nodiscard]] std::vector<BrxGlobRecord> &EditableGlobs()
    {
        return globs_;
    }
    [[nodiscard]] std::size_t GlobSourceRecordIndex(std::size_t globIndex) const;
    [[nodiscard]] std::string GlobEditorLabel(std::size_t globIndex) const;
    [[nodiscard]] std::string SubglobEditorLabel(std::size_t globIndex, std::size_t subglobIndex) const;
    [[nodiscard]] const std::vector<BrxGlobsetRecord> &Globsets() const
    {
        return globsets_;
    }
    [[nodiscard]] std::vector<BrxGlobsetRecord> &EditableGlobsets()
    {
        return globsets_;
    }
    bool CommitGlobEdit(std::size_t globIndex);
    // Native-coordinate bounds with editor-only placement applied exactly once.
    bool PlacedGlobBounds(std::size_t globIndex, float *center, float &radius) const;
    bool ApplyGlobGeometryTransform(std::size_t globIndex, const glm::mat4 &delta);
    std::size_t DuplicateGlobAsInstance(std::size_t sourceGlobRecordIndex);
    bool MakeGlobUnique(std::size_t globIndex, std::string *error = nullptr);
    bool ApplyGlobsetPosePreview(std::size_t globsetIndex);
    void RebuildRenderPacketsFromGlobs();
    [[nodiscard]] const std::vector<BrxCollisionTriangle> &CollisionTriangles() const
    {
        return collisionTriangles_;
    }
    [[nodiscard]] std::vector<BrxCollisionTriangle> &EditableCollisionTriangles()
    {
        return collisionTriangles_;
    }
    [[nodiscard]] const std::vector<BrxLightRecord> &Lights() const
    {
        return lights_;
    }
    [[nodiscard]] std::vector<BrxLightRecord> &EditableLights()
    {
        return lights_;
    }
    [[nodiscard]] const std::vector<BrxCameraRecord> &Cameras() const
    {
        return cameras_;
    }
    [[nodiscard]] std::vector<BrxCameraRecord> &EditableCameras()
    {
        return cameras_;
    }
    // Materialize the same LIGHT/CAMERA cache that BrxLoader builds on load.
    // Creation presets call this again after adding their authored options.
    void SynchronizeDerivedObjectRecord(std::int32_t objectIndex);
    [[nodiscard]] const std::vector<BrxCheckpointRecord> &Checkpoints() const
    {
        return checkpoints_;
    }
    [[nodiscard]] std::vector<BrxCheckpointRecord> &EditableCheckpoints()
    {
        return checkpoints_;
    }
    [[nodiscard]] const std::vector<BrxWarpRecord> &Warps() const
    {
        return warps_;
    }
    [[nodiscard]] std::vector<BrxWarpRecord> &EditableWarps()
    {
        return warps_;
    }
    [[nodiscard]] const std::vector<BrxExitRecord> &Exits() const
    {
        return exits_;
    }
    [[nodiscard]] std::vector<BrxExitRecord> &EditableExits()
    {
        return exits_;
    }
    [[nodiscard]] const std::vector<BrxScanRecord> &Scans() const
    {
        return scans_;
    }
    [[nodiscard]] std::vector<BrxScanRecord> &EditableScans()
    {
        return scans_;
    }
    [[nodiscard]] const std::vector<BrxAsegRecord> &AnimationSegments() const
    {
        return animationSegments_;
    }
    [[nodiscard]] std::vector<BrxAsegRecord> &EditableAnimationSegments()
    {
        return animationSegments_;
    }
    [[nodiscard]] const std::vector<BrxGlobWarpRecord> &GlobWarps() const
    {
        return globWarps_;
    }
    [[nodiscard]] std::vector<BrxGlobWarpRecord> &EditableGlobWarps()
    {
        return globWarps_;
    }
    [[nodiscard]] const std::vector<BrxLaserBeamRecord> &LaserBeams() const
    {
        return laserBeams_;
    }
    [[nodiscard]] std::vector<BrxLaserBeamRecord> &EditableLaserBeams()
    {
        return laserBeams_;
    }
    [[nodiscard]] const std::vector<BrxShapePathRecord> &ShapePaths() const
    {
        return shapePaths_;
    }
    [[nodiscard]] std::vector<BrxShapePathRecord> &EditableShapePaths()
    {
        return shapePaths_;
    }
    [[nodiscard]] const std::vector<BrxPathZoneRecord> &PathZones() const
    {
        return pathZones_;
    }
    [[nodiscard]] std::vector<BrxPathZoneRecord> &EditablePathZones()
    {
        return pathZones_;
    }
    [[nodiscard]] const std::vector<BrxSmRecord> &StateMachines() const
    {
        return stateMachines_;
    }
    [[nodiscard]] std::vector<BrxSmRecord> &EditableStateMachines()
    {
        return stateMachines_;
    }
    [[nodiscard]] const std::vector<BrxRchmRecord> &ReachMaps() const
    {
        return reachMaps_;
    }
    [[nodiscard]] std::vector<BrxRchmRecord> &EditableReachMaps()
    {
        return reachMaps_;
    }
    [[nodiscard]] const std::vector<BrxKeyholeRecord> &Keyholes() const
    {
        return keyholes_;
    }
    [[nodiscard]] std::vector<BrxKeyholeRecord> &EditableKeyholes()
    {
        return keyholes_;
    }
    [[nodiscard]] const std::vector<BrxEffectRecord> &Effects() const
    {
        return effects_;
    }
    [[nodiscard]] std::vector<BrxEffectRecord> &EditableEffects()
    {
        return effects_;
    }
    [[nodiscard]] const std::vector<BrxVolumeRecord> &Volumes() const
    {
        return volumes_;
    }
    [[nodiscard]] std::vector<BrxVolumeRecord> &EditableVolumes()
    {
        return volumes_;
    }
    [[nodiscard]] const std::vector<BrxVisibilityNode> &VisibilityNodes() const
    {
        return visibilityNodes_;
    }
    [[nodiscard]] const std::vector<std::uint32_t> &VisibilityOneHopMasks() const
    {
        return visibilityOneHopMasks_;
    }
    [[nodiscard]] std::vector<BrxVisibilityNode> &EditableVisibilityNodes()
    {
        return visibilityNodes_;
    }
    [[nodiscard]] std::vector<std::uint32_t> &EditableVisibilityOneHopMasks()
    {
        return visibilityOneHopMasks_;
    }
    [[nodiscard]] const std::vector<std::string> &VisibilityZoneNames() const
    {
        return visibilityZoneNames_;
    }
    [[nodiscard]] std::vector<std::string> &EditableVisibilityZoneNames()
    {
        return visibilityZoneNames_;
    }
    [[nodiscard]] std::string VisibilityZoneName(std::uint32_t zone) const;
    [[nodiscard]] const float *RenderBoundsMin() const
    {
        return renderBoundsMin_;
    }
    [[nodiscard]] const float *RenderBoundsMax() const
    {
        return renderBoundsMax_;
    }
    [[nodiscard]] bool HasRenderGeometry() const
    {
        return !renderIndices_.empty();
    }

    static const char *ClassName(std::int16_t cid);
    // Human-facing editor label. ClassName remains the exact serialized/runtime
    // class token and must continue to be used for game-data logic.
    static std::string EditorClassName(std::int16_t cid);
    static std::uint16_t AllowedPropertyCount(std::int16_t cid);

    static const char *LanguageName(std::uint32_t languageId);

  private:
    friend class BrxLoader;
    friend class EditorProject;
    bool MaterializeGlobRenderGeometry(std::size_t globIndex);
    bool RebuildProjectRenderGeometry();
    bool OpenReadOnlyInPlace(const std::filesystem::path &path, std::string &error);
    std::filesystem::path path_;
    std::uint32_t version_ = 0;
    std::uint64_t fileSize_ = 0;
    std::uint32_t defaultLanguage_ = 0;
    std::uint32_t chunkCount_ = 0;
    std::vector<BrxLanguagePayload> languages_;
    std::vector<std::uint8_t> payload_;
    bool payloadValidated_ = false;
    BrxWorldHeader worldHeader_;
    std::vector<BrxPayloadSection> sections_;
    std::vector<BrxOptionRecord> worldOptions_;
    std::vector<BrxSpliceNode> spliceNodes_;
    std::vector<BrxSpliceModule> spliceModules_;
    std::vector<BrxBitmapRecord> bitmaps_;
    std::vector<BrxShaderRecord> shaders_;
    std::vector<BrxFontRecord> fonts_;
    std::vector<BrxObjectRecord> objects_;
    std::vector<BrxProxyRecord> proxies_;
    std::vector<BrxRenderVertex> renderVertices_;
    std::vector<std::uint32_t> renderIndices_;
    std::vector<BrxRenderBatch> renderBatches_;
    std::vector<BrxCelEdge> celEdges_;
    std::vector<BrxCelBatch> celBatches_;
    std::vector<BrxGlobRecord> globs_;
    std::vector<BrxGlobsetRecord> globsets_;
    std::vector<BrxCollisionTriangle> collisionTriangles_;
    std::vector<BrxLightRecord> lights_;
    std::vector<BrxCameraRecord> cameras_;
    std::vector<BrxCheckpointRecord> checkpoints_;
    std::vector<BrxWarpRecord> warps_;
    std::vector<BrxExitRecord> exits_;
    std::vector<BrxScanRecord> scans_;
    std::vector<BrxAsegRecord> animationSegments_;
    std::vector<BrxGlobWarpRecord> globWarps_;
    std::vector<BrxLaserBeamRecord> laserBeams_;
    std::vector<BrxShapePathRecord> shapePaths_;
    std::vector<BrxPathZoneRecord> pathZones_;
    std::vector<BrxSmRecord> stateMachines_;
    std::vector<BrxRchmRecord> reachMaps_;
    std::vector<BrxKeyholeRecord> keyholes_;
    std::vector<BrxEffectRecord> effects_;
    std::vector<BrxVolumeRecord> volumes_;
    std::vector<BrxVisibilityNode> visibilityNodes_;
    std::vector<std::uint32_t> visibilityOneHopMasks_;
    // Editor-only labels; VISMAP retail data remains a 32-bit zone-mask system.
    std::vector<std::string> visibilityZoneNames_;
    float renderBoundsMin_[3]{};
    float renderBoundsMax_[3]{};
    std::uint64_t nextEditorResourceId_ = 1;
};
