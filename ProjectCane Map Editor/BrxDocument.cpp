#include "BrxDocument.h"
#include "CidAuthoringGuide.h"
#include "BrxLoader.h"
#include "EditorOptions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr std::uint32_t kPcbrMagic = 0x52424350u;
constexpr std::uint32_t kUnifiedMagic = 0x494e5550u;

[[nodiscard]] std::size_t ResolveGlobSourceRecordIndex(const std::size_t globRecordIndex,
                                                       const std::vector<BrxGlobRecord> &globs,
                                                       const std::vector<BrxGlobsetRecord> &globsets)
{
    if (globRecordIndex >= globs.size() || !globs[globRecordIndex].instanced)
        return static_cast<std::size_t>(-1);
    const auto sourceSlot = globs[globRecordIndex].sourceGlobIndex;
    if (sourceSlot < 0)
        return static_cast<std::size_t>(-1);
    for (const auto &globset : globsets)
    {
        const auto instance = std::find(globset.globIndices.begin(), globset.globIndices.end(), globRecordIndex);
        if (instance == globset.globIndices.end())
            continue;
        const auto localInstanceSlot = static_cast<std::size_t>(std::distance(globset.globIndices.begin(), instance));
        const auto localSourceSlot = static_cast<std::size_t>(sourceSlot);
        // Retail GLOBI records may only reference an earlier GLOB in the same
        // GLOBSET. sourceRecordIndex is editor cache data and is deliberately
        // not consulted here.
        if (localSourceSlot >= localInstanceSlot || localSourceSlot >= globset.globIndices.size())
            return static_cast<std::size_t>(-1);
        const auto sourceRecordIndex = globset.globIndices[localSourceSlot];
        return sourceRecordIndex < globs.size() ? sourceRecordIndex : static_cast<std::size_t>(-1);
    }
    return static_cast<std::size_t>(-1);
}

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
}

bool BrxDocument::OpenReadOnly(const std::filesystem::path &path, std::string &error)
{
    // Loading can fail after decompression or halfway through object parsing.
    // Keep the current authored document until the replacement is complete.
    BrxDocument replacement;
    if (!replacement.OpenReadOnlyInPlace(path, error))
        return false;
    *this = std::move(replacement);
    return true;
}

bool BrxDocument::OpenPayloadBytes(const std::vector<std::uint8_t> &bytes, std::string &error)
{
    BrxDocument replacement;
    if (!replacement.OpenReadOnlyInPlace("language.pcbrx", error, &bytes))
        return false;
    *this = std::move(replacement);
    return true;
}

bool BrxDocument::ExtractLocalizationFooter(const std::vector<std::uint8_t> &source,
                                            std::vector<std::uint8_t> &footer, std::string &error)
{
    footer.clear();
    error.clear();
    const auto readU32 = [&](const std::size_t offset)
    {
        return static_cast<std::uint32_t>(source[offset]) |
               (static_cast<std::uint32_t>(source[offset + 1]) << 8u) |
               (static_cast<std::uint32_t>(source[offset + 2]) << 16u) |
               (static_cast<std::uint32_t>(source[offset + 3]) << 24u);
    };
    if (source.size() < 8 || readU32(source.size() - 8) != 0x434f4c50u)
        return true;
    const auto size = readU32(source.size() - 4);
    const auto end = source.size() - 8;
    if (size > end || size < 4)
    {
        error = "The PLOC localization footer has an invalid size.";
        return false;
    }
    const auto begin = end - size;
    auto cursor = begin;
    const auto count = readU32(cursor);
    cursor += 4;
    if (count > 4096u)
    {
        error = "The PLOC localization footer exceeds ProjectCane's string-count limit.";
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (end - cursor < 4)
        {
            error = "The PLOC localization string table is truncated.";
            return false;
        }
        const auto length = readU32(cursor);
        cursor += 4;
        if (length > end - cursor)
        {
            error = "A PLOC localization string exceeds its table.";
            return false;
        }
        cursor += length;
    }
    if (cursor != end)
    {
        error = "The PLOC localization table has unread data.";
        return false;
    }
    footer.assign(source.begin() + static_cast<std::ptrdiff_t>(begin), source.end());
    return true;
}

bool BrxDocument::OpenReadOnlyInPlace(const std::filesystem::path &path, std::string &error,
                                     const std::vector<std::uint8_t> *sourceBytes)
{
    Close();
    try
    {
        std::vector<std::uint8_t> bytes;
        if (sourceBytes != nullptr)
            bytes = *sourceBytes;
        else
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("Unable to open the selected file");
            const auto end = file.tellg();
            if (end < 0 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Unable to determine a supported file size");
            bytes.resize(static_cast<std::size_t>(end));
            file.seekg(0);
            file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                throw std::runtime_error("Unable to read the complete file");
        }
        fileSize_ = bytes.size();
        if (bytes.size() < 8)
            throw std::runtime_error("The file is not a valid PCBRX container");

        Reader reader(bytes);
        if (reader.U32() != kPcbrMagic)
            throw std::runtime_error("The selected file does not have a PCBR header");

        version_ = reader.U32();
        if (version_ != 17 && version_ != 18 && version_ != 19)
            throw std::runtime_error("Unsupported PCBRX version " + std::to_string(version_));

        if (version_ == 18 || version_ == 19)
        {
            const bool unified = version_ == 19;
            if (reader.U32() != (unified ? kUnifiedMagic : 0x4b484350u))
                throw std::runtime_error("PCBRX is missing its language-container header");
            if (unified && reader.U32() != 1)
                throw std::runtime_error("Unsupported PCBRX v19 schema");

            const auto languageCount = reader.U32();
            defaultLanguage_ = unified ? reader.U32() : 0;
            chunkCount_ = reader.U32();
            if (languageCount == 0 || languageCount > 5 || (!unified && languageCount != 5))
                throw std::runtime_error("Invalid PCBRX language count");
            if (chunkCount_ > 1'000'000)
                throw std::runtime_error("Invalid PCBRX chunk count");

            languages_.reserve(languageCount);
            std::uint32_t languageMask = 0;
            for (std::uint32_t i = 0; i < languageCount; ++i)
            {
                BrxLanguagePayload language;
                language.languageId = unified ? reader.U32() : i;
                language.reconstructedSize = reader.U64();
                const auto referenceCount = reader.U32();
                // References are an ordered reconstruction recipe and may reuse
                // a deduplicated chunk, so this count can exceed chunkCount_.
                if (language.languageId > 4 || (languageMask & (1u << language.languageId)) != 0 ||
                    referenceCount == 0 || referenceCount > reader.Remaining() / sizeof(std::uint32_t))
                    throw std::runtime_error("Invalid PCBRX language descriptor");
                languageMask |= 1u << language.languageId;
                language.chunkIds.reserve(referenceCount);
                for (std::uint32_t reference = 0; reference < referenceCount; ++reference)
                {
                    const auto chunkId = reader.U32();
                    if (chunkId >= chunkCount_)
                        throw std::runtime_error("PCBRX language references an invalid chunk");
                    language.chunkIds.push_back(chunkId);
                }
                languages_.push_back(std::move(language));
            }

            std::vector<std::vector<std::uint8_t>> chunks;
            chunks.reserve(chunkCount_);
            for (std::uint32_t i = 0; i < chunkCount_; ++i)
            {
                const auto chunkSize = reader.U32();
                const auto expectedCrc = unified ? reader.U32() : 0;
                auto chunk = reader.Bytes(chunkSize);
                if (unified && Crc32(chunk) != expectedCrc)
                    throw std::runtime_error("PCBRX chunk " + std::to_string(i) + " failed CRC validation");
                chunks.push_back(std::move(chunk));
            }

            // Preserve every language, not only the one used by the viewport.
            for (auto &language : languages_)
            {
                if (language.reconstructedSize > std::numeric_limits<std::size_t>::max())
                    throw std::runtime_error("A language payload is too large");
                auto payload = std::make_shared<std::vector<std::uint8_t>>();
                payload->reserve(static_cast<std::size_t>(language.reconstructedSize));
                for (const auto id : language.chunkIds)
                {
                    if (payload->size() > language.reconstructedSize ||
                        chunks[id].size() > language.reconstructedSize - payload->size())
                        throw std::runtime_error("A language payload exceeds its declared size");
                    payload->insert(payload->end(), chunks[id].begin(), chunks[id].end());
                }
                if (payload->size() != language.reconstructedSize || payload->size() < 8)
                    throw std::runtime_error("A language payload has an invalid size");
                std::uint32_t magic{}, version{};
                std::memcpy(&magic, payload->data(), 4);
                std::memcpy(&version, payload->data() + 4, 4);
                if (magic != kPcbrMagic || version != 17)
                    throw std::runtime_error("A language payload is not PCBRX v17");
                language.payloadCrc = Crc32(*payload);
                language.payload = std::move(payload);
            }

            const auto selected =
                std::find_if(languages_.begin(), languages_.end(), [this](const BrxLanguagePayload &language)
                             { return language.languageId == defaultLanguage_; });
            if (selected == languages_.end())
                throw std::runtime_error("The default PCBRX language is missing");
            payload_ = *selected->payload; // Already reconstructed and checked above.
            payloadValidated_ = true;
        }
        else
        {
            if (version_ == 17)
            {
                payload_ = bytes;
                payloadValidated_ = true;
            }
        }

        if (payloadValidated_)
        {
            if (!ExtractLocalizationFooter(payload_, localizationFooter_, error))
                throw std::runtime_error(error);
            BrxLoader::ParseWorld(*this);
            for (std::size_t bitmapIndex = 0; bitmapIndex < bitmaps_.size(); ++bitmapIndex)
            {
                bitmaps_[bitmapIndex].editorResourceId = nextEditorResourceId_++;
                bitmaps_[bitmapIndex].editorName = "Bitmap " + std::to_string(bitmapIndex);
            }
            for (std::size_t shaderIndex = 0; shaderIndex < shaders_.size(); ++shaderIndex)
            {
                shaders_[shaderIndex].editorResourceId = nextEditorResourceId_++;
                shaders_[shaderIndex].editorName = "Material " + std::to_string(shaderIndex);
            }
            // Binary decoding still emits the legacy flattened packets. Fold
            // them into the source-level editor model once, then derive all
            // renderer packets from that model for the rest of the session.
            // These vectors are caches, not decoded source. Clear them before
            // association so a loader-created PROXY GLOB or restored editor
            // document cannot retain a stale packet and receive it twice.
            for (auto &glob : globs_)
            {
                glob.derivedBatches.clear();
                glob.derivedCelPackets.clear();
            }
            for (const auto &batch : renderBatches_)
            {
                auto glob = std::find_if(globs_.begin(), globs_.end(),
                                         [&](const BrxGlobRecord &candidate)
                                         {
                                             return candidate.renderGroup == batch.renderGroup &&
                                                    candidate.ownerObjectIndex == batch.ownerObjectIndex;
                                         });
                if (glob == globs_.end())
                {
                    BrxGlobRecord created;
                    created.renderGroup = batch.renderGroup;
                    created.ownerObjectIndex = batch.ownerObjectIndex;
                    // Flattened PROXY clones do not have separately serialized
                    // GLOB records. Preserve the cloned source GLOB's effective
                    // submission state when materializing an editor record;
                    // leaving these fields at their defaults changes ordinary
                    // cloned geometry to RP_DynamicTexture during the rebuild.
                    created.renderPhase = batch.renderPhase;
                    created.rotationTracking = batch.rotationTracking;
                    created.hasShaderAnimation = batch.hasShaderAnimation;
                    created.shaderAnimation = batch.shaderAnimation;
                    created.zoneMask = batch.zoneMask;
                    created.hasZoneMask = true;
                    created.center[0] = batch.sortCenter[0];
                    created.center[1] = batch.sortCenter[1];
                    created.center[2] = batch.sortCenter[2];
                    created.radius = batch.mrdRadius;
                    created.hasExplicitSortZ = batch.hasExplicitSortZ;
                    created.effectiveSortZ = batch.explicitSortZ;
                    created.fogScale = batch.fogScale;
                    created.maximumRenderDistance = batch.maximumRenderDistance;
                    globs_.push_back(std::move(created));
                    glob = std::prev(globs_.end());
                }
                // Serialized GLOBs already have one source record per SUBGLOB.
                // Synthetic PROXY-clone GLOBs do not, so reconstruct only the
                // missing records as their flattened batches are adopted.
                if (glob->sourceSubglobs.size() < glob->derivedBatches.size() + 1)
                {
                    BrxSubglobRecord source;
                    source.shaderIndex = batch.shaderIndex;
                    source.selfIllumination = batch.selfIllumination;
                    source.triangleCount = static_cast<std::uint16_t>(
                        (std::min)(batch.indexCount / 3u, static_cast<std::uint32_t>(0xffffu)));
                    glob->sourceSubglobs.push_back(source);
                }
                glob->derivedBatches.push_back(batch);
            }
            for (const auto &cel : celBatches_)
            {
                auto glob = std::find_if(globs_.begin(), globs_.end(),
                                         [&](const BrxGlobRecord &candidate)
                                         {
                                             return candidate.renderGroup == cel.renderGroup &&
                                                    candidate.ownerObjectIndex == cel.ownerObjectIndex;
                                         });
                if (glob == globs_.end())
                {
                    BrxGlobRecord created;
                    created.renderGroup = cel.renderGroup;
                    created.ownerObjectIndex = cel.ownerObjectIndex;
                    globs_.push_back(std::move(created));
                    glob = std::prev(globs_.end());
                }
                glob->derivedCelPackets.push_back(cel);
            }
            RebuildRenderPacketsFromGlobs();
        }

        path_ = std::filesystem::absolute(path);
        error.clear();
        return true;
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        Close();
        return false;
    }
}

bool BrxDocument::LoadLanguageSubtitles(const std::uint32_t languageId, std::string &error)
{
    auto found = std::find_if(languages_.begin(), languages_.end(),
                               [&](const auto &language) { return language.languageId == languageId; });
    if (found == languages_.end() || !found->payload)
    {
        error = "This language has no preserved world payload.";
        return false;
    }
    if (found->subtitlesLoaded && found->voiceLinesLoaded)
        return true;
    BrxDocument localized;
    if (!localized.OpenPayloadBytes(*found->payload, error))
        return false;
    if (std::any_of(localized.Sections().begin(), localized.Sections().end(),
                    [](const auto &section) { return section.state == BrxSectionState::Pending; }))
    {
        error = "This language world was only partially decoded; subtitle editing is disabled to preserve its data.";
        return false;
    }
    std::vector<BrxLocalizedSubtitle> lines;
    std::vector<BrxLanguagePayload::VoiceLine> voices;
    for (std::size_t objectIndex = 0; objectIndex < localized.Objects().size(); ++objectIndex)
    {
        const auto &object = localized.Objects()[objectIndex];
        if (!object.dialog.present || object.proxyClone)
            continue;
        for (std::size_t eventIndex = 0; eventIndex < object.dialog.events.size(); ++eventIndex)
        {
            const auto &event = object.dialog.events[eventIndex];
            if (event.kind == BrxDialogEventKind::PreloadAudio)
            {
                BrxLanguagePayload::VoiceLine voice;
                voice.objectIndex = static_cast<std::uint32_t>(objectIndex);
                voice.eventIndex = static_cast<std::uint32_t>(eventIndex);
                voice.dialogOid = object.oid;
                voice.resourceName = voice.originalResourceName = event.resourceName;
                voices.push_back(std::move(voice));
            }
            if (event.kind != BrxDialogEventKind::Talk)
                continue;
            BrxLocalizedSubtitle line;
            line.objectIndex = static_cast<std::uint32_t>(objectIndex);
            line.eventIndex = static_cast<std::uint32_t>(eventIndex);
            line.dialogOid = object.oid;
            line.subtitle = line.originalSubtitle = event.subtitle;
            lines.push_back(std::move(line));
        }
    }
    for (const auto &edit : found->subtitles)
    {
        if (!edit.modified)
            continue;
        const auto line = std::find_if(lines.begin(), lines.end(), [&](const auto &candidate)
        {
            return candidate.objectIndex == edit.objectIndex && candidate.eventIndex == edit.eventIndex &&
                   candidate.dialogOid == edit.dialogOid;
        });
        if (line == lines.end() || edit.subtitle.size() > 65535u || edit.subtitle.find('\0') != std::string::npos)
        {
            error = "A localized subtitle no longer matches its preserved DIALOG event.";
            return false;
        }
        line->subtitle = edit.subtitle;
        line->modified = line->subtitle != line->originalSubtitle;
    }
    for (const auto &edit : found->voiceLines)
    {
        if (!edit.modified)
            continue;
        auto voice = std::find_if(voices.begin(), voices.end(), [&](const auto &candidate)
        {
            return candidate.objectIndex == edit.objectIndex && candidate.eventIndex == edit.eventIndex &&
                   candidate.dialogOid == edit.dialogOid;
        });
        if (voice == voices.end() || !ValidateDialogVoiceResource(edit.resourceName, error))
        {
            if (error.empty()) error = "A voice edit no longer matches its preserved Preload audio event.";
            return false;
        }
        voice->resourceName = edit.resourceName;
        voice->modified = voice->resourceName != voice->originalResourceName;
    }
    found->subtitles = std::move(lines);
    found->voiceLines = std::move(voices);
    found->subtitlesLoaded = true;
    found->voiceLinesLoaded = true;
    error.clear();
    return true;
}

void BrxDocument::SynchronizeDerivedObjectRecord(const std::int32_t objectIndex)
{
    if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
        return;
    const auto &object = objects_[static_cast<std::size_t>(objectIndex)];
    if (object.cid == 81)
    {
        // Match BrxLoader's LIGHT branch. Geometry and EOPID 498 remain
        // independent: BuildLightBlk submits the object's world Z direction.
        BrxLightRecord light;
        light.ownerObjectIndex = objectIndex;
        for (const auto &property : object.properties)
        {
            if (property.kind == BrxPropertyKind::Dynamic)
                continue;
            switch (property.eopid)
            {
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
                std::copy_n(property.floatValue, 2, light.falloff);
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
        std::copy_n(object.worldTransform + 12, 3, light.position);
        glm::vec3 direction(object.worldTransform[8], object.worldTransform[9], object.worldTransform[10]);
        if (glm::dot(direction, direction) > 0.000001f)
            direction = glm::normalize(direction);
        std::copy_n(glm::value_ptr(direction), 3, light.direction);
        const auto found = std::find_if(lights_.begin(), lights_.end(), [&](const auto &candidate)
                                        { return candidate.ownerObjectIndex == objectIndex; });
        if (found == lights_.end())
            lights_.push_back(light);
        else
            *found = light;
    }
    else if (object.cid == 78)
    {
        BrxCameraRecord camera;
        camera.ownerObjectIndex = objectIndex;
        for (const auto &property : object.properties)
        {
            if (property.kind == BrxPropertyKind::Dynamic)
                continue;
            switch (property.eopid)
            {
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
        const auto found = std::find_if(cameras_.begin(), cameras_.end(), [&](const auto &candidate)
                                        { return candidate.ownerObjectIndex == objectIndex; });
        if (found == cameras_.end())
            cameras_.push_back(camera);
        else
            *found = camera;
    }
}

void BrxDocument::Close()
{
    path_.clear();
    version_ = 0;
    fileSize_ = 0;
    defaultLanguage_ = 0;
    chunkCount_ = 0;
    languages_.clear();
    localizationFooter_.clear();
    dialogWaves_.clear();
    payload_.clear();
    payloadValidated_ = false;
    worldHeader_ = {};
    sections_.clear();
    worldOptions_.clear();
    spliceNodes_.clear();
    spliceModules_.clear();
    bitmaps_.clear();
    shaders_.clear();
    fonts_.clear();
    objects_.clear();
    proxies_.clear();
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
    exits_.clear();
    scans_.clear();
    animationSegments_.clear();
    globWarps_.clear();
    laserBeams_.clear();
    shapePaths_.clear();
    pathZones_.clear();
    stateMachines_.clear();
    reachMaps_.clear();
    keyholes_.clear();
    effects_.clear();
    volumes_.clear();
    visibilityNodes_.clear();
    visibilityOneHopMasks_.clear();
    visibilityZoneNames_.clear();
    nextEditorResourceId_ = 1;
    std::fill(std::begin(renderBoundsMin_), std::end(renderBoundsMin_), 0.0f);
    std::fill(std::begin(renderBoundsMax_), std::end(renderBoundsMax_), 0.0f);
}

std::size_t BrxDocument::AddBitmap(BrxBitmapRecord bitmap)
{
    bitmap.editorResourceId = nextEditorResourceId_++;
    if (bitmap.editorName.empty())
        bitmap.editorName = "Bitmap " + std::to_string(bitmaps_.size());
    bitmaps_.push_back(std::move(bitmap));
    return bitmaps_.size() - 1;
}

std::size_t BrxDocument::DuplicateBitmap(const std::size_t bitmapIndex)
{
    if (bitmapIndex >= bitmaps_.size())
        return static_cast<std::size_t>(-1);
    BrxBitmapRecord bitmap = bitmaps_[bitmapIndex];
    bitmap.editorResourceId = nextEditorResourceId_++;
    bitmap.editorName += " Copy";
    bitmap.textureDataOffset = 0;
    bitmaps_.push_back(std::move(bitmap));
    return bitmaps_.size() - 1;
}

bool BrxDocument::DeleteBitmapAndRemap(const std::size_t bitmapIndex, const std::int32_t replacementIndex)
{
    if (bitmapIndex >= bitmaps_.size())
        return false;
    if (replacementIndex >= static_cast<std::int32_t>(bitmaps_.size()) ||
        replacementIndex == static_cast<std::int32_t>(bitmapIndex))
        return false;
    const auto remapChannel = [&](std::vector<std::int32_t> &channel)
    {
        for (auto &value : channel)
        {
            // A texture frame is an ambient/diffuse/saturate triplet. Removing
            // one channel entry shifts every following frame and destroys that
            // alignment, so an unassigned replacement must remain as -1.
            if (value == static_cast<std::int32_t>(bitmapIndex))
                value = replacementIndex;
            if (value > static_cast<std::int32_t>(bitmapIndex))
                --value;
        }
    };
    for (auto &shader : shaders_)
    {
        remapChannel(shader.ambientBitmapIndices);
        remapChannel(shader.diffuseBitmapIndices);
        remapChannel(shader.saturateBitmapIndices);
        for (auto &stage : shader.textureStages)
        {
            remapChannel(stage.ambientBitmapIndices);
            remapChannel(stage.diffuseBitmapIndices);
            remapChannel(stage.saturateBitmapIndices);
        }
        shader.textureStageCount = static_cast<std::uint8_t>((std::min)(std::size_t{255}, shader.textureStages.size()));
        if (!shader.textureStages.empty())
        {
            shader.ambientBitmapIndices = shader.textureStages.front().ambientBitmapIndices;
            shader.diffuseBitmapIndices = shader.textureStages.front().diffuseBitmapIndices;
            shader.saturateBitmapIndices = shader.textureStages.front().saturateBitmapIndices;
        }
    }
    for (auto &font : fonts_)
    {
        if (font.bitmapIndex == static_cast<std::int32_t>(bitmapIndex))
            font.bitmapIndex = replacementIndex;
        if (font.bitmapIndex > static_cast<std::int32_t>(bitmapIndex))
            --font.bitmapIndex;
    }
    bitmaps_.erase(bitmaps_.begin() + static_cast<std::ptrdiff_t>(bitmapIndex));
    return true;
}

std::size_t BrxDocument::DuplicateShader(const std::size_t shaderIndex)
{
    if (shaderIndex >= shaders_.size())
        return static_cast<std::size_t>(-1);
    BrxShaderRecord shader = shaders_[shaderIndex];
    shader.editorResourceId = nextEditorResourceId_++;
    shader.editorName += " Copy";
    shader.offset = 0;
    shaders_.push_back(std::move(shader));
    return shaders_.size() - 1;
}

std::size_t BrxDocument::AddShader(BrxShaderRecord shader)
{
    // The BRX stores the animation tag and texture-stage count separately from
    // their decoded payloads.  New/imported editor materials must initialize
    // those cached scalar fields just like LoadShadersFromBrx does, otherwise
    // the from-scratch writer correctly rejects an internally inconsistent
    // material before export.
    shader.animationKind = static_cast<std::int16_t>(shader.animation.kind);
    shader.textureStageCount = static_cast<std::uint8_t>((std::min)(std::size_t{255}, shader.textureStages.size()));
    if (!shader.textureStages.empty())
    {
        shader.ambientBitmapIndices = shader.textureStages.front().ambientBitmapIndices;
        shader.diffuseBitmapIndices = shader.textureStages.front().diffuseBitmapIndices;
        shader.saturateBitmapIndices = shader.textureStages.front().saturateBitmapIndices;
    }
    shader.editorResourceId = nextEditorResourceId_++;
    if (shader.editorName.empty())
        shader.editorName = "Material " + std::to_string(shaders_.size());
    shader.offset = 0;
    shaders_.push_back(std::move(shader));
    return shaders_.size() - 1;
}

bool BrxDocument::DeleteShaderAndRemap(const std::size_t shaderIndex, const std::int32_t replacementIndex)
{
    if (shaderIndex >= shaders_.size())
        return false;
    if (replacementIndex >= static_cast<std::int32_t>(shaders_.size()) ||
        replacementIndex == static_cast<std::int32_t>(shaderIndex))
        return false;
    const auto remap = [&](std::uint16_t &index)
    {
        if (index == shaderIndex)
            index = replacementIndex < 0 ? 0xffffu : static_cast<std::uint16_t>(replacementIndex);
        if (index != 0xffffu && index > shaderIndex)
            --index;
    };
    for (auto &glob : globs_)
    {
        for (auto &subglob : glob.sourceSubglobs)
            remap(subglob.shaderIndex);
        for (auto &batch : glob.derivedBatches)
            remap(batch.shaderIndex);
    }
    for (auto &batch : renderBatches_)
        remap(batch.shaderIndex);
    shaders_.erase(shaders_.begin() + static_cast<std::ptrdiff_t>(shaderIndex));
    RebuildRenderPacketsFromGlobs();
    return true;
}

void BrxDocument::RebuildRenderPacketsFromGlobs()
{
    renderBatches_.clear();
    celBatches_.clear();
    std::vector<bool> globRelight(globs_.size(), false);
    for (const auto &globset : globsets_)
        for (const auto globIndex : globset.globIndices)
            if (globIndex < globRelight.size())
                globRelight[globIndex] = globset.relight;
    // Keep the document-global source cache synchronized from the actual BRX
    // relationship before any preview code consumes inherited GLOB state.
    for (std::size_t globIndex = 0; globIndex < globs_.size(); ++globIndex)
    {
        auto &glob = globs_[globIndex];
        glob.sourceRecordIndex =
            glob.instanced ? ResolveGlobSourceRecordIndex(globIndex, globs_, globsets_) : static_cast<std::size_t>(-1);
    }
    for (std::size_t globIndex = 0; globIndex < globs_.size(); ++globIndex)
    {
        auto &glob = globs_[globIndex];
        const BrxGlobRecord *effectiveAnimationGlob = &glob;
        std::size_t inheritedAnimationDepth = 0;
        while (!effectiveAnimationGlob->hasShaderAnimation && effectiveAnimationGlob->instanced &&
               effectiveAnimationGlob->sourceRecordIndex < globs_.size() && ++inheritedAnimationDepth <= globs_.size())
        {
            const auto *source = &globs_[effectiveAnimationGlob->sourceRecordIndex];
            if (source == effectiveAnimationGlob)
                break;
            effectiveAnimationGlob = source;
        }
        const auto inheritedGlobFor = [&](const auto &ownsProperty)
        {
            const BrxGlobRecord *effective = &glob;
            std::size_t depth = 0;
            while (!ownsProperty(*effective) && effective->instanced && effective->sourceRecordIndex < globs_.size() &&
                   ++depth <= globs_.size())
            {
                const auto *source = &globs_[effective->sourceRecordIndex];
                if (source == effective)
                    break;
                effective = source;
            }
            return effective;
        };
        const auto *effectiveGeometryGlob =
            inheritedGlobFor([](const BrxGlobRecord &candidate) { return !candidate.instanced; });
        // LoadGlobsetFromBrx derives these flags from the non-instanced
        // geometry's shader list; instances copy them from their source.
        bool translucentSort = false;
        std::uint8_t projectedFlags = 0;
        for (const auto &subglob : effectiveGeometryGlob->sourceSubglobs)
        {
            if (subglob.shaderIndex >= shaders_.size())
                continue;
            const auto flags = shaders_[subglob.shaderIndex].flags;
            if ((effectiveGeometryGlob->renderPhase == 1 || effectiveGeometryGlob->renderPhase == 4 ||
                 effectiveGeometryGlob->renderPhase == 8) &&
                flags == 2)
                translucentSort = true;
            if (effectiveGeometryGlob->renderPhase == 6)
                projectedFlags = flags;
        }
        const auto *effectiveZoneGlob =
            inheritedGlobFor([](const BrxGlobRecord &candidate) { return candidate.hasZoneMask; });
        const auto *effectiveSortGlob =
            inheritedGlobFor([](const BrxGlobRecord &candidate) { return candidate.hasExplicitSortZ; });
        const auto *effectiveFogGlob =
            inheritedGlobFor([](const BrxGlobRecord &candidate) { return candidate.hasFogScale; });
        // Retail copies the already-clamped cel MRD at every instance hop,
        // then applies this record's overrides and clamps again. Skipping to
        // the nearest explicit cel property loses intermediate surface clamps.
        std::vector<const BrxGlobRecord *> distanceChain;
        const BrxGlobRecord *distanceGlob = &glob;
        for (std::size_t depth = 0; depth < globs_.size(); ++depth)
        {
            distanceChain.push_back(distanceGlob);
            if (!distanceGlob->instanced || distanceGlob->sourceRecordIndex >= globs_.size())
                break;
            distanceGlob = &globs_[distanceGlob->sourceRecordIndex];
        }
        float effectiveSurfaceMrd = 1.0e10f;
        float effectiveCelMrd = 2000.0f;
        for (auto record = distanceChain.rbegin(); record != distanceChain.rend(); ++record)
        {
            if ((*record)->hasMaximumRenderDistance)
                effectiveSurfaceMrd =
                    (*record)->maximumRenderDistanceInfinite ||
                            (*record)->authoredMaximumRenderDistance == (std::numeric_limits<float>::max)()
                        ? 1.0e10f
                        : (*record)->authoredMaximumRenderDistance;
            if ((*record)->hasCelMaximumRenderDistance)
                effectiveCelMrd =
                    (*record)->celMaximumRenderDistanceInfinite ||
                            (*record)->authoredCelMaximumRenderDistance == (std::numeric_limits<float>::max)()
                        ? 2000.0f
                        : (*record)->authoredCelMaximumRenderDistance;
            effectiveCelMrd = (std::min)(effectiveCelMrd, effectiveSurfaceMrd);
        }
        glob.maximumRenderDistance = effectiveSurfaceMrd;
        glob.celMaximumRenderDistance = effectiveCelMrd;
        const float authoredSort = effectiveSortGlob->hasExplicitSortZ ? effectiveSortGlob->authoredSortZ
                                                                       : (std::numeric_limits<float>::max)();
        if (!glob.hasExplicitSortZ)
            glob.authoredSortZ = authoredSort;
        glob.effectiveSortZ =
            authoredSort == (std::numeric_limits<float>::max)() ? authoredSort : authoredSort * std::abs(authoredSort);
        std::uint32_t effectiveZoneMask = effectiveZoneGlob->hasZoneMask ? effectiveZoneGlob->zoneMask : 0u;
        if (glob.ownerObjectIndex >= 0 && static_cast<std::size_t>(glob.ownerObjectIndex) < objects_.size())
        {
            const auto &owner = objects_[static_cast<std::size_t>(glob.ownerObjectIndex)];
            // LoadGlobsetFromBrx uses per-GLOB zones for merged static models.
            // Ordinary ALOs select object, GLOB, or disabled zone culling from
            // VISS 2, 1, or 0 respectively.
            if (owner.cid != 4)
            {
                if (owner.visibilityMode == 2)
                    effectiveZoneMask = owner.zoneMask;
                else if (owner.visibilityMode != 1)
                    effectiveZoneMask = 0u;
            }
        }
        float placedCenter[3]{};
        float placedRadius = 0.0f;
        if (!PlacedGlobBounds(globIndex, placedCenter, placedRadius))
            continue;
        glm::mat4 boundsToWorld(1.0f);
        if (glob.ownerObjectIndex >= 0 && static_cast<std::size_t>(glob.ownerObjectIndex) < objects_.size())
        {
            const auto &owner = objects_[static_cast<std::size_t>(glob.ownerObjectIndex)];
            if (owner.cid != 4 && !(owner.so.present && owner.so.fixedPhysics))
                boundsToWorld = glm::make_mat4(owner.worldTransform);
        }
        const glm::vec3 placedWorldCenter = glm::vec3(boundsToWorld *
            glm::vec4(placedCenter[0], placedCenter[1], placedCenter[2], 1.0f));
        const float placedWorldRadius = placedRadius * (std::max)({glm::length(glm::vec3(boundsToWorld[0])),
            glm::length(glm::vec3(boundsToWorld[1])), glm::length(glm::vec3(boundsToWorld[2]))});
        for (std::size_t subglobIndex = 0; subglobIndex < glob.derivedBatches.size(); ++subglobIndex)
        {
            auto batch = glob.derivedBatches[subglobIndex];
            batch.renderGroup = glob.renderGroup;
            batch.ownerObjectIndex = glob.ownerObjectIndex;
            batch.shadowReceiverMask = glob.flags & 0x3u;
            batch.renderPhase = glob.renderPhase;
            batch.rotationTracking = glob.rotationTracking;
            batch.dynamicLighting = globRelight[globIndex];
            batch.translucentSort = translucentSort;
            batch.projectedFlags = projectedFlags;
            // GLOBI records inherit their source GLOB's SAA unless the
            // instance serializes property bit 0x040. CloneGlob then gives
            // instanced animations private runtime state, but not different
            // authored parameters. Preserve that distinction here: the GLOB
            // record keeps only its authored override while render packets
            // receive the effective inherited animation.
            batch.hasShaderAnimation = effectiveAnimationGlob->hasShaderAnimation;
            batch.shaderAnimation = effectiveAnimationGlob->shaderAnimation;
            if (glob.editorCreatedInstance || glob.hasEditorPlacementTransform)
            {
                batch.hasEditorInstanceTransform = true;
                const float *placement =
                    glob.editorCreatedInstance ? glob.instanceTransform : glob.editorPlacementTransform;
                std::copy(placement, placement + 16, std::begin(batch.editorInstanceTransform));
                const auto transformPoint = [&](float *point)
                {
                    const float x = point[0], y = point[1], z = point[2];
                    point[0] = placement[0] * x + placement[4] * y + placement[8] * z + placement[12];
                    point[1] = placement[1] * x + placement[5] * y + placement[9] * z + placement[13];
                    point[2] = placement[2] * x + placement[6] * y + placement[10] * z + placement[14];
                };
                transformPoint(batch.boundsCenter);
                transformPoint(batch.sortCenter);
                const auto axisLength = [&](const int column)
                {
                    const int i = column * 4;
                    return std::sqrt(placement[i] * placement[i] + placement[i + 1] * placement[i + 1] +
                                     placement[i + 2] * placement[i + 2]);
                };
                const float scale = (std::max)({axisLength(0), axisLength(1), axisLength(2)});
                batch.boundsRadius *= scale;
                batch.mrdRadius *= scale;
            }
            // Native GLOB bounds are independent of the source geometry and
            // instance matrix. Use the placed current-GLOB bound for MRD,
            // MS frustum culling and the RTCK pivot, just as the game does.
            std::copy_n(&placedWorldCenter.x, 3, batch.sortCenter);
            batch.mrdRadius = placedWorldRadius;
            batch.hasExplicitSortZ = effectiveSortGlob->hasExplicitSortZ &&
                                     effectiveSortGlob->authoredSortZ != (std::numeric_limits<float>::max)();
            batch.explicitSortZ = glob.effectiveSortZ;
            batch.fogScale = effectiveFogGlob->fogScale;
            batch.maximumRenderDistance = effectiveSurfaceMrd;
            batch.hasGleam = glob.hasGleam;
            std::copy(std::begin(glob.gleamNormal), std::end(glob.gleamNormal), batch.gleamNormal);
            std::copy(std::begin(glob.gleamCurve), std::end(glob.gleamCurve), batch.gleamCurve);
            batch.zoneMask = effectiveZoneMask;
            if (subglobIndex < effectiveGeometryGlob->sourceSubglobs.size())
            {
                const auto &source = effectiveGeometryGlob->sourceSubglobs[subglobIndex];
                batch.shaderIndex = source.shaderIndex;
                batch.selfIllumination = source.selfIllumination;
            }
            renderBatches_.push_back(std::move(batch));
        }
        for (auto cel : glob.derivedCelPackets)
        {
            std::copy_n(&placedWorldCenter.x, 3, cel.center);
            cel.radius = placedWorldRadius;
            cel.renderGroup = glob.renderGroup;
            cel.ownerObjectIndex = glob.ownerObjectIndex;
            cel.surfaceMaximumDistance = effectiveSurfaceMrd;
            cel.maximumDistance = effectiveCelMrd;
            cel.zoneMask = effectiveZoneMask;
            if (glob.renderPhase == 7 || glob.renderPhase == 8 || glob.renderPhase == 9)
                cel.renderPhase = 9;
            else if (glob.renderPhase == 13 || glob.renderPhase == 14)
                cel.renderPhase = 14;
            else
                cel.renderPhase = 5;
            celBatches_.push_back(std::move(cel));
        }
    }
}

bool BrxDocument::ApplyGlobsetPosePreview(const std::size_t globsetIndex)
{
    if (globsetIndex >= globsets_.size())
        return false;
    const auto &globset = globsets_[globsetIndex];
    for (const std::size_t globIndex : globset.globIndices)
    {
        if (globIndex >= globs_.size())
            return false;
        auto &glob = globs_[globIndex];
        glm::mat4 sourceToWorld(1.0f);
        std::memcpy(glm::value_ptr(sourceToWorld), glob.sourceToWorld, sizeof(glob.sourceToWorld));
        // ProjectCane's GLOB vertex shader applies mat3(model) directly to
        // authored and pose normals. Match that retail convention exactly;
        // inverse-transpose would make scaled preview geometry shade
        // differently from the exported level.
        const glm::mat3 normalToWorld = glm::mat3(sourceToWorld);
        for (std::size_t subglobIndex = 0; subglobIndex < glob.sourceSubglobs.size(); ++subglobIndex)
        {
            if (subglobIndex >= glob.derivedBatches.size())
                return false;
            const auto &source = glob.sourceSubglobs[subglobIndex];
            const auto &batch = glob.derivedBatches[subglobIndex];
            std::vector<std::uint32_t> destination(source.vertices.size(), std::numeric_limits<std::uint32_t>::max());
            for (std::size_t triangleIndex = 0; triangleIndex < source.triangles.size(); ++triangleIndex)
            {
                const std::size_t indexOffset = static_cast<std::size_t>(batch.firstIndex) + triangleIndex * 3;
                if (indexOffset + 2 >= renderIndices_.size())
                    return false;
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint16_t localIndex = source.triangles[triangleIndex][corner];
                    if (localIndex >= destination.size())
                        return false;
                    destination[localIndex] = renderIndices_[indexOffset + corner];
                }
            }
            for (std::size_t vertexIndex = 0; vertexIndex < source.vertices.size(); ++vertexIndex)
            {
                const std::uint32_t destinationIndex = destination[vertexIndex];
                if (destinationIndex == std::numeric_limits<std::uint32_t>::max())
                    continue;
                if (destinationIndex >= renderVertices_.size())
                    return false;
                const auto &sourceVertex = source.vertices[vertexIndex];
                glm::vec3 position(sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2]);
                glm::vec3 normal(sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]);
                for (std::size_t pose = 0; pose < globset.poseWeights.size(); ++pose)
                {
                    // Serialized pose arrays match the runtime shader's
                    // vertex-major layout: vertex * poseCount + pose.
                    const std::size_t deltaIndex = vertexIndex * globset.poseWeights.size() + pose;
                    if (deltaIndex >= source.posePositionDeltas.size() || deltaIndex >= source.poseNormalDeltas.size())
                        return false;
                    const float weight = globset.poseWeights[pose];
                    const auto &dpos = source.posePositionDeltas[deltaIndex];
                    const auto &dnormal = source.poseNormalDeltas[deltaIndex];
                    position += glm::vec3(dpos[0], dpos[1], dpos[2]) * weight;
                    normal += glm::vec3(dnormal[0], dnormal[1], dnormal[2]) * weight;
                }
                position = glm::vec3(sourceToWorld * glm::vec4(position, 1.0f));
                normal = normalToWorld * normal;
                if (glm::dot(normal, normal) > 0.000001f)
                    normal = glm::normalize(normal);
                auto &vertex = renderVertices_[destinationIndex];
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                std::copy(std::begin(sourceVertex.color), std::end(sourceVertex.color), vertex.color);
                std::copy(std::begin(sourceVertex.uv), std::end(sourceVertex.uv), vertex.uv);
            }
        }
        for (std::size_t celIndex = 0; celIndex < glob.sourceCelRecords.size(); ++celIndex)
        {
            if (celIndex >= glob.derivedCelPackets.size())
                return false;
            const auto &source = glob.sourceCelRecords[celIndex];
            const auto &packet = glob.derivedCelPackets[celIndex];
            if (source.points.size() != static_cast<std::size_t>(packet.edgeCount) * 4)
                return false;
            if (static_cast<std::size_t>(packet.firstEdge) + packet.edgeCount > celEdges_.size())
                return false;
            for (std::size_t pointIndex = 0; pointIndex < source.points.size(); ++pointIndex)
            {
                glm::vec3 position(source.points[pointIndex].position[0], source.points[pointIndex].position[1],
                                   source.points[pointIndex].position[2]);
                for (std::size_t pose = 0; pose < globset.poseWeights.size(); ++pose)
                {
                    const std::size_t deltaIndex = pointIndex * globset.poseWeights.size() + pose;
                    if (deltaIndex >= source.posePositionDeltas.size())
                        return false;
                    const auto &delta = source.posePositionDeltas[deltaIndex];
                    position += glm::vec3(delta[0], delta[1], delta[2]) * globset.poseWeights[pose];
                }
                position = glm::vec3(sourceToWorld * glm::vec4(position, 1.0f));
                auto &destination =
                    celEdges_[static_cast<std::size_t>(packet.firstEdge) + pointIndex / 4].points[pointIndex & 3u];
                destination[0] = position.x;
                destination[1] = position.y;
                destination[2] = position.z;
                destination[3] = 1.0f;
            }
        }
    }
    return true;
}

bool BrxDocument::PlacedGlobBounds(const std::size_t globIndex, float *center, float &radius) const
{
    if (globIndex >= globs_.size() || center == nullptr)
        return false;
    const auto &glob = globs_[globIndex];
    std::copy_n(glob.center, 3, center);
    radius = glob.radius;
    const float empty = (std::numeric_limits<float>::lowest)();
    if (radius == empty && center[0] == empty && center[1] == empty && center[2] == empty)
        return true; // Preserve the native empty-bounds tuple.
    if (!glob.editorCreatedInstance && !glob.hasEditorPlacementTransform)
        return true;
    glm::mat4 boundsToWorld(1.0f);
    if (glob.ownerObjectIndex >= 0 && static_cast<std::size_t>(glob.ownerObjectIndex) < objects_.size())
    {
        const auto &owner = objects_[static_cast<std::size_t>(glob.ownerObjectIndex)];
        if (owner.cid != 4 && !(owner.so.present && owner.so.fixedPhysics))
            boundsToWorld = glm::make_mat4(owner.worldTransform);
    }
    const float determinant = glm::determinant(boundsToWorld);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
        return false;
    const auto placement = glm::make_mat4(glob.editorCreatedInstance
        ? glob.instanceTransform : glob.editorPlacementTransform);
    const glm::mat4 nativePlacement = glm::inverse(boundsToWorld) * placement * boundsToWorld;
    const glm::vec3 placed = glm::vec3(nativePlacement * glm::vec4(center[0], center[1], center[2], 1.0f));
    const float scale = (std::max)({glm::length(glm::vec3(nativePlacement[0])),
        glm::length(glm::vec3(nativePlacement[1])), glm::length(glm::vec3(nativePlacement[2]))});
    radius *= scale;
    if (!std::isfinite(placed.x) || !std::isfinite(placed.y) || !std::isfinite(placed.z) || !std::isfinite(radius))
        return false;
    std::copy_n(&placed.x, 3, center);
    return true;
}

bool BrxDocument::MaterializeGlobRenderGeometry(const std::size_t globIndex)
{
    if (globIndex >= globs_.size())
        return false;
    auto &glob = globs_[globIndex];
    // Loader-created GLOBs already own derived packets. Imported models and
    // reopened projects start with source data only. Populate renderer caches
    // without changing authored properties, instance matrices or dirty flags.
    if (glob.derivedBatches.empty() && !glob.sourceSubglobs.empty())
    {
        glm::mat4 sourceToWorld(1.0f);
        std::memcpy(glm::value_ptr(sourceToWorld), glob.sourceToWorld, sizeof(glob.sourceToWorld));
        const glm::mat3 normalToWorld = glm::mat3(sourceToWorld);
        const float radiusScale =
            (std::max)({glm::length(glm::vec3(sourceToWorld[0])), glm::length(glm::vec3(sourceToWorld[1])),
                        glm::length(glm::vec3(sourceToWorld[2]))});
        std::vector<BrxRenderVertex> authoredVertices;
        std::vector<std::uint32_t> authoredIndices;
        std::vector<BrxRenderBatch> authoredBatches;
        for (const auto &subglob : glob.sourceSubglobs)
        {
            if (subglob.vertices.size() > (std::numeric_limits<std::uint16_t>::max)() ||
                subglob.triangles.size() > (std::numeric_limits<std::uint16_t>::max)())
                return false;
            for (const auto &triangle : subglob.triangles)
                for (const auto vertexIndex : triangle)
                    if (vertexIndex >= subglob.vertices.size())
                        return false;
            const std::uint32_t vertexBase =
                static_cast<std::uint32_t>(renderVertices_.size() + authoredVertices.size());
            const std::uint32_t firstIndex = static_cast<std::uint32_t>(renderIndices_.size() + authoredIndices.size());
            for (const auto &source : subglob.vertices)
            {
                BrxRenderVertex destination;
                const glm::vec3 position = glm::vec3(
                    sourceToWorld * glm::vec4(source.position[0], source.position[1], source.position[2], 1.0f));
                glm::vec3 normal = normalToWorld * glm::vec3(source.normal[0], source.normal[1], source.normal[2]);
                if (glm::dot(normal, normal) > 0.000001f)
                    normal = glm::normalize(normal);
                else
                    normal = glm::vec3(0.0f, 1.0f, 0.0f);
                destination.position[0] = position.x;
                destination.position[1] = position.y;
                destination.position[2] = position.z;
                destination.normal[0] = normal.x;
                destination.normal[1] = normal.y;
                destination.normal[2] = normal.z;
                std::copy(std::begin(source.color), std::end(source.color), destination.color);
                std::copy(std::begin(source.uv), std::end(source.uv), destination.uv);
                authoredVertices.push_back(destination);
            }
            for (const auto &triangle : subglob.triangles)
                for (const auto vertexIndex : triangle)
                    authoredIndices.push_back(vertexBase + vertexIndex);
            BrxRenderBatch batch;
            batch.firstIndex = firstIndex;
            batch.indexCount = static_cast<std::uint32_t>(subglob.triangles.size() * 3u);
            batch.shaderIndex = subglob.shaderIndex;
            batch.selfIllumination = subglob.selfIllumination;
            const glm::vec3 center =
                glm::vec3(sourceToWorld * glm::vec4(subglob.center[0], subglob.center[1], subglob.center[2], 1.0f));
            batch.boundsCenter[0] = center.x;
            batch.boundsCenter[1] = center.y;
            batch.boundsCenter[2] = center.z;
            std::copy(std::begin(batch.boundsCenter), std::end(batch.boundsCenter), batch.sortCenter);
            batch.boundsRadius = subglob.radius * radiusScale;
            batch.mrdRadius = batch.boundsRadius;
            batch.zoneMask = glob.hasZoneMask ? glob.zoneMask : 0xffffffffu;
            batch.maximumRenderDistance = glob.maximumRenderDistance;
            batch.ownerObjectIndex = glob.ownerObjectIndex;
            std::copy_n(glob.sourceToWorld, 16, batch.rtckBaseMatrix);
            authoredBatches.push_back(batch);
        }
        renderVertices_.insert(renderVertices_.end(), authoredVertices.begin(), authoredVertices.end());
        renderIndices_.insert(renderIndices_.end(), authoredIndices.begin(), authoredIndices.end());
        glob.derivedBatches = std::move(authoredBatches);
    }
    if (glob.derivedCelPackets.empty() && !glob.sourceCelRecords.empty())
    {
        // SUBCEL stores four points per edge, in E0/E1/OA/OB order.
        // Retain a packet for empty records too: pose preview indexes the
        // source records and their derived packets in parallel.
        for (const auto &source : glob.sourceCelRecords)
            if (source.points.size() % 4 != 0)
                return false;
        const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
        for (const auto &source : glob.sourceCelRecords)
        {
            BrxCelBatch packet;
            packet.firstEdge = static_cast<std::uint32_t>(celEdges_.size());
            packet.edgeCount = static_cast<std::uint32_t>(source.points.size() / 4);
            packet.ownerObjectIndex = glob.ownerObjectIndex;
            for (std::size_t edgeIndex = 0; edgeIndex < packet.edgeCount; ++edgeIndex)
            {
                BrxCelEdge edge;
                for (std::size_t pointIndex = 0; pointIndex < 4; ++pointIndex)
                {
                    const auto &point = source.points[edgeIndex * 4 + pointIndex];
                    const glm::vec3 position = glm::vec3(sourceToWorld *
                        glm::vec4(point.position[0], point.position[1], point.position[2], 1.0f));
                    std::copy_n(&position.x, 3, edge.points[pointIndex]);
                    edge.points[pointIndex][3] = 1.0f;
                }
                celEdges_.push_back(edge);
            }
            glob.derivedCelPackets.push_back(packet);
        }
    }
    return true;
}

bool BrxDocument::RebuildProjectRenderGeometry()
{
    // Projects serialize source geometry, never document-global GPU offsets.
    // Discard every old cache (also for legacy projects opened over a BRX)
    // before reconstructing independent ranges from the saved placements.
    renderVertices_.clear();
    renderIndices_.clear();
    renderBatches_.clear();
    celEdges_.clear();
    celBatches_.clear();
    for (auto &glob : globs_)
    {
        glob.derivedBatches.clear();
        glob.derivedCelPackets.clear();
    }
    for (std::size_t index = 0; index < globs_.size(); ++index)
    {
        const auto firstVertex = renderVertices_.size();
        const auto firstEdge = celEdges_.size();
        if (!MaterializeGlobRenderGeometry(index))
            return false;
        // WRBG ranges also address the flattened caches, not source vertices.
        // Editor instances can previously have shared another GLOB's ranges.
        for (auto &binding : globWarps_)
            if (binding.ownerObjectIndex == globs_[index].ownerObjectIndex &&
                binding.renderGroup == globs_[index].renderGroup)
            {
                binding.firstVertex = static_cast<std::uint32_t>(firstVertex);
                binding.vertexCount = static_cast<std::uint32_t>(renderVertices_.size() - firstVertex);
                binding.firstCelEdge = static_cast<std::uint32_t>(firstEdge);
                binding.celEdgeCount = static_cast<std::uint32_t>(celEdges_.size() - firstEdge);
            }
    }
    std::unordered_set<std::size_t> posedGlobs;
    for (std::size_t index = 0; index < globsets_.size(); ++index)
    {
        const auto &globset = globsets_[index];
        if (globset.poseWeights.empty() ||
            std::any_of(globset.globIndices.begin(), globset.globIndices.end(),
                        [&](const auto globIndex) { return posedGlobs.contains(globIndex); }))
            continue;
        if (!ApplyGlobsetPosePreview(index))
            return false;
        posedGlobs.insert(globset.globIndices.begin(), globset.globIndices.end());
    }
    std::fill_n(renderBoundsMin_, 3, 0.0f);
    std::fill_n(renderBoundsMax_, 3, 0.0f);
    bool first = true;
    for (const auto &vertex : renderVertices_)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            renderBoundsMin_[axis] = first ? vertex.position[axis] :
                (std::min)(renderBoundsMin_[axis], vertex.position[axis]);
            renderBoundsMax_[axis] = first ? vertex.position[axis] :
                (std::max)(renderBoundsMax_[axis], vertex.position[axis]);
        }
        first = false;
    }
    RebuildRenderPacketsFromGlobs();
    return true;
}

bool BrxDocument::CommitGlobEdit(const std::size_t globIndex)
{
    if (!MaterializeGlobRenderGeometry(globIndex))
        return false;
    auto &glob = globs_[globIndex];
    const auto setPresenceBit = [&](const std::uint16_t bit, const bool present)
    {
        if (present)
            glob.propertyMask |= bit;
        else
            glob.propertyMask &= static_cast<std::uint16_t>(~bit);
    };
    setPresenceBit(0x002, glob.hasZoneMask);
    setPresenceBit(0x004, glob.hasExplicitSortZ);
    setPresenceBit(0x008, glob.hasFogScale);
    setPresenceBit(0x010, glob.hasMaximumRenderDistance);
    setPresenceBit(0x020, glob.hasCelMaximumRenderDistance);
    setPresenceBit(0x040, glob.hasShaderAnimation);
    setPresenceBit(0x080, glob.hasGleam);
    setPresenceBit(0x100, glob.hasWarp);
    setPresenceBit(0x200, glob.hasSubglobRadiusScale);
    auto warpBinding = std::find_if(
        globWarps_.begin(), globWarps_.end(), [&](const BrxGlobWarpRecord &binding)
        { return binding.ownerObjectIndex == glob.ownerObjectIndex && binding.renderGroup == glob.renderGroup; });
    if (!glob.hasWarp)
    {
        globWarps_.erase(std::remove_if(globWarps_.begin(), globWarps_.end(),
                                        [&](const BrxGlobWarpRecord &binding)
                                        {
                                            return binding.ownerObjectIndex == glob.ownerObjectIndex &&
                                                   binding.renderGroup == glob.renderGroup;
                                        }),
                         globWarps_.end());
    }
    else
    {
        const auto sourceRecordIndex = ResolveGlobSourceRecordIndex(globIndex, globs_, globsets_);
        if (warpBinding == globWarps_.end() && sourceRecordIndex < globs_.size())
        {
            const auto &source = globs_[sourceRecordIndex];
            const auto sourceBinding = std::find_if(globWarps_.begin(), globWarps_.end(),
                                                    [&](const BrxGlobWarpRecord &binding)
                                                    {
                                                        return binding.ownerObjectIndex == source.ownerObjectIndex &&
                                                               binding.renderGroup == source.renderGroup;
                                                    });
            if (sourceBinding != globWarps_.end())
            {
                BrxGlobWarpRecord binding = *sourceBinding;
                binding.ownerObjectIndex = glob.ownerObjectIndex;
                binding.renderGroup = glob.renderGroup;
                globWarps_.push_back(std::move(binding));
                warpBinding = std::prev(globWarps_.end());
            }
        }
        if (warpBinding == globWarps_.end())
        {
            BrxGlobWarpRecord binding;
            binding.ownerObjectIndex = glob.ownerObjectIndex;
            binding.renderGroup = glob.renderGroup;
            globWarps_.push_back(binding);
            warpBinding = std::prev(globWarps_.end());
        }
        if (warpBinding != globWarps_.end())
        {
            warpBinding->controllerOid = glob.warpControllerOid;
            warpBinding->envelopeKind = glob.warpEnvelopeKind;
            warpBinding->matrixCount = glob.warpMatrixCount;
            warpBinding->deformPosition = glob.warpPosition;
            warpBinding->deformUv = glob.warpUv;
        }
    }
    if (!glob.instanced)
    {
        if (!glob.hasExplicitSortZ)
            glob.effectiveSortZ = 0.0f;
        if (!glob.hasFogScale)
            glob.fogScale = 1.0f;
        if (!glob.hasMaximumRenderDistance)
            glob.maximumRenderDistance = 1.0e10f;
        if (!glob.hasCelMaximumRenderDistance)
            glob.celMaximumRenderDistance = 2000.0f;
    }
    if (glob.hasExplicitSortZ)
        glob.effectiveSortZ = glob.authoredSortZ == (std::numeric_limits<float>::max)()
                                  ? glob.authoredSortZ
                                  : glob.authoredSortZ * std::abs(glob.authoredSortZ);
    if (glob.hasMaximumRenderDistance)
        glob.maximumRenderDistance = glob.maximumRenderDistanceInfinite ||
                                             glob.authoredMaximumRenderDistance == (std::numeric_limits<float>::max)()
                                         ? 1.0e10f
                                         : glob.authoredMaximumRenderDistance;
    if (glob.hasCelMaximumRenderDistance)
        glob.celMaximumRenderDistance =
            glob.celMaximumRenderDistanceInfinite ||
                    glob.authoredCelMaximumRenderDistance == (std::numeric_limits<float>::max)()
                ? 2000.0f
                : glob.authoredCelMaximumRenderDistance;
    glob.celMaximumRenderDistance = (std::min)(glob.celMaximumRenderDistance, glob.maximumRenderDistance);
    glob.modified = true;
    RebuildRenderPacketsFromGlobs();
    return true;
}

bool BrxDocument::ApplyGlobGeometryTransform(const std::size_t globIndex, const glm::mat4 &delta)
{
    if (globIndex >= globs_.size())
        return false;
    auto &glob = globs_[globIndex];
    if (glob.instanced)
        return true;

    const float determinant = glm::determinant(glm::mat3(delta));
    if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
        return false;
    const glm::mat3 normalMatrix = glm::mat3(delta);
    const float radiusScale = (std::max)(
        {glm::length(glm::vec3(delta[0])), glm::length(glm::vec3(delta[1])), glm::length(glm::vec3(delta[2]))});
    std::vector<bool> transformedVertices(renderVertices_.size(), false);

    for (auto &batch : glob.derivedBatches)
    {
        const std::size_t end = static_cast<std::size_t>(batch.firstIndex) + batch.indexCount;
        if (end > renderIndices_.size())
            return false;
        for (std::size_t index = batch.firstIndex; index < end; ++index)
        {
            const std::uint32_t vertexIndex = renderIndices_[index];
            if (vertexIndex >= renderVertices_.size())
                return false;
            if (transformedVertices[vertexIndex])
                continue;
            transformedVertices[vertexIndex] = true;
            auto &vertex = renderVertices_[vertexIndex];
            const glm::vec3 position =
                glm::vec3(delta * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
            glm::vec3 normal = normalMatrix * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            if (glm::dot(normal, normal) > 0.000001f)
                normal = glm::normalize(normal);
            vertex.position[0] = position.x;
            vertex.position[1] = position.y;
            vertex.position[2] = position.z;
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
        }
        const glm::vec3 center =
            glm::vec3(delta * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f));
        const glm::vec3 sortCenter =
            glm::vec3(delta * glm::vec4(batch.sortCenter[0], batch.sortCenter[1], batch.sortCenter[2], 1.0f));
        batch.boundsCenter[0] = center.x;
        batch.boundsCenter[1] = center.y;
        batch.boundsCenter[2] = center.z;
        batch.sortCenter[0] = sortCenter.x;
        batch.sortCenter[1] = sortCenter.y;
        batch.sortCenter[2] = sortCenter.z;
        batch.boundsRadius *= radiusScale;
        batch.mrdRadius *= radiusScale;
        const auto rtckBase = delta * glm::make_mat4(batch.rtckBaseMatrix);
        std::copy_n(glm::value_ptr(rtckBase), 16, batch.rtckBaseMatrix);
    }

    for (auto &cel : glob.derivedCelPackets)
    {
        const std::size_t end = static_cast<std::size_t>(cel.firstEdge) + cel.edgeCount;
        if (end > celEdges_.size())
            return false;
        for (std::size_t edgeIndex = cel.firstEdge; edgeIndex < end; ++edgeIndex)
            for (auto &point : celEdges_[edgeIndex].points)
            {
                const glm::vec3 transformed = glm::vec3(delta * glm::vec4(point[0], point[1], point[2], 1.0f));
                point[0] = transformed.x;
                point[1] = transformed.y;
                point[2] = transformed.z;
            }
        const glm::vec3 center = glm::vec3(delta * glm::vec4(cel.center[0], cel.center[1], cel.center[2], 1.0f));
        cel.center[0] = center.x;
        cel.center[1] = center.y;
        cel.center[2] = center.z;
        cel.radius *= radiusScale;
    }
    RebuildRenderPacketsFromGlobs();
    return true;
}

bool BrxDocument::PrepareStaticGlobCopy(const std::size_t source, const std::int32_t target,
                                      BrxGlobRecord &copy, std::size_t &targetSet, std::string &error) const
{
    const auto fail = [&](const char *message) { error = message; return false; };
    if (source >= globs_.size() || target < 0 || static_cast<std::size_t>(target) >= objects_.size())
        return fail("Select an existing GLOB and destination object.");
    const auto &destination = objects_[static_cast<std::size_t>(target)];
    const auto &original = globs_[source];
    if (original.ownerObjectIndex == target)
        return fail("Use Duplicate GLOB as instance to copy within the same owning object.");
    if (original.ownerObjectIndex < 0 || static_cast<std::size_t>(original.ownerObjectIndex) >= objects_.size())
        return fail("The source GLOB has no valid owner.");
    const auto &sourceOwner = objects_[static_cast<std::size_t>(original.ownerObjectIndex)];
    if (!CidDerivesFrom(destination.cid, 2) || destination.cid == 108 || destination.cid == 109)
        return fail("The destination must serialize an authored ALO-derived GLOBSET.");
    if (destination.proxyClone || destination.proxyRecordIndex >= 0 ||
        sourceOwner.proxyClone || sourceOwner.proxyRecordIndex >= 0)
        return fail("Make linked PROXY hierarchies unique before copying GLOBs between owners.");
    const BrxGlobsetRecord *sourceSet = nullptr;
    targetSet = globsets_.size();
    std::size_t targetCount = 0;
    for (std::size_t index = 0; index < globsets_.size(); ++index)
    {
        const auto &set = globsets_[index];
        if (std::find(set.globIndices.begin(), set.globIndices.end(), source) != set.globIndices.end())
        {
            if (sourceSet != nullptr || set.ownerObjectIndex != original.ownerObjectIndex)
                return fail("The source GLOBSET ownership is invalid.");
            sourceSet = &set;
        }
        if (set.ownerObjectIndex == target) { targetSet = index; ++targetCount; }
    }
    if (sourceSet == nullptr || targetCount != 1)
        return fail("Both objects must have an existing, unambiguous native GLOBSET.");
    const auto &destinationSet = globsets_[targetSet];
    if (!sourceSet->boneOids.empty() || !sourceSet->poseWeights.empty() ||
        !destinationSet.boneOids.empty() || !destinationSet.poseWeights.empty() || destination.globPoseCount != 0)
        return fail("This static-copy operation does not transfer bone bindings or pose tables.");
    if (destinationSet.globIndices.size() >= (std::numeric_limits<std::uint16_t>::max)())
        return fail("The destination GLOBSET is full.");
    std::size_t geometry = source;
    for (std::size_t guard = 0; globs_[geometry].instanced; ++guard)
    {
        if (guard >= globs_.size()) return fail("The source instance chain is cyclic.");
        geometry = ResolveGlobSourceRecordIndex(geometry, globs_, globsets_);
        if (geometry >= globs_.size()) return fail("The source instance chain is invalid.");
    }
    copy = original;
    copy.sourceSubglobs = globs_[geometry].sourceSubglobs;
    copy.sourceCelRecords = globs_[geometry].sourceCelRecords;
    if (copy.sourceSubglobs.empty()) return fail("The source GLOB has no static visual geometry.");
    if (copy.sourceSubglobs.size() > 65535 || copy.sourceCelRecords.size() > 65535)
        return fail("The source part/outline counts exceed native U16 limits.");
    // RTCK is serialized on every GLOB. GLEAM and WRBG also belong to the
    // selected instance; LoadGlobsetFromBrx does not inherit those pointers.
    if (original.hasWarp || original.hasGleam || original.rotationTracking != 0 ||
        std::any_of(globWarps_.begin(), globWarps_.end(), [&](const BrxGlobWarpRecord &binding)
            { return binding.ownerObjectIndex == original.ownerObjectIndex && binding.renderGroup == original.renderGroup; }))
        return fail("Warped, GLEAM or camera-tracking GLOBs require their original setup and cannot use static copy.");
    // SAA, unlike those per-instance fields, can be inherited from a source.
    for (std::size_t index = source, guard = 0; guard < globs_.size(); ++guard)
    {
        const auto &record = globs_[index];
        if (record.hasShaderAnimation)
            return fail("The GLOB inherits shader animation and cannot use static copy.");
        if (!record.instanced) break;
        index = ResolveGlobSourceRecordIndex(index, globs_, globsets_);
    }
    const auto finite = [](const glm::mat4 &m)
    { return std::all_of(glm::value_ptr(m), glm::value_ptr(m) + 16, [](float v) { return std::isfinite(v); }); };
    const auto targetWorld = glm::make_mat4(destination.worldTransform);
    auto sourceWorld = glm::make_mat4(original.sourceToWorld);
    if (original.editorCreatedInstance)
        sourceWorld = glm::make_mat4(original.instanceTransform) * sourceWorld;
    else if (original.hasEditorPlacementTransform)
        sourceWorld = glm::make_mat4(original.editorPlacementTransform) * sourceWorld;
    const float targetDeterminant = glm::determinant(glm::mat3(targetWorld));
    if (!finite(targetWorld) || !finite(sourceWorld) || !std::isfinite(targetDeterminant) ||
        std::abs(targetDeterminant) <= 1.0e-8f)
        return fail("The source or destination transform is non-finite or non-invertible.");
    const auto affine = [](const glm::mat4 &matrix)
    { return matrix[0][3] == 0.0f && matrix[1][3] == 0.0f && matrix[2][3] == 0.0f && matrix[3][3] == 1.0f; };
    if (!affine(targetWorld) || !affine(sourceWorld))
        return fail("Static copy requires native affine object transforms.");
    const auto bake = glm::inverse(targetWorld) * sourceWorld;
    if (!finite(bake)) return fail("The copy cannot be represented in destination-local space.");
    // Retail sphere scaling uses the largest basis length. That is a safe
    // bound for rotation/scale, but not a sheared destination frame.
    const glm::vec3 axes[]{glm::normalize(glm::vec3(targetWorld[0])), glm::normalize(glm::vec3(targetWorld[1])),
                           glm::normalize(glm::vec3(targetWorld[2]))};
    if (std::abs(glm::dot(axes[0], axes[1])) > 0.0001f || std::abs(glm::dot(axes[0], axes[2])) > 0.0001f ||
        std::abs(glm::dot(axes[1], axes[2])) > 0.0001f)
        return fail("The destination has a sheared transform; static copy cannot preserve safe retail bounds for it.");
    const auto transform = [&](float *values, bool direction)
    {
        const auto v = direction ? glm::mat3(bake) * glm::make_vec3(values)
                                 : glm::vec3(bake * glm::vec4(glm::make_vec3(values), 1.0f));
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
        std::copy_n(&v.x, 3, values);
        return true;
    };
    glm::vec3 lower((std::numeric_limits<float>::max)()), upper((std::numeric_limits<float>::lowest)());
    bool hasVertices = false;
    for (auto &part : copy.sourceSubglobs)
    {
        if (part.boneCount != 0 || !part.posePositionDeltas.empty() || !part.poseNormalDeltas.empty())
            return fail("The source contains skinning or pose data.");
        if (part.shaderIndex >= shaders_.size() || shaders_[part.shaderIndex].animation.kind != BrxShaderAnimationKind::None)
            return fail("Assign valid, non-animated materials before using static copy.");
        if (!std::isfinite(part.selfIllumination)) return fail("The source material brightness is non-finite.");
        if (part.vertices.size() > 65535 || part.triangles.size() > 65535)
            return fail("The source geometry exceeds native U16 limits.");
        for (const auto &triangle : part.triangles)
            for (const auto index : triangle)
                if (index >= part.vertices.size()) return fail("The source contains an invalid triangle.");
        glm::vec3 partLower((std::numeric_limits<float>::max)()), partUpper((std::numeric_limits<float>::lowest)());
        for (auto &vertex : part.vertices)
        {
            if (!std::all_of(std::begin(vertex.color), std::end(vertex.color), [](float v) { return std::isfinite(v); }) ||
                !std::all_of(std::begin(vertex.uv), std::end(vertex.uv), [](float v) { return std::isfinite(v); }))
                return fail("The source contains non-finite colors or texture coordinates.");
            if (std::any_of(std::begin(vertex.boneWeights), std::end(vertex.boneWeights), [](float v) { return v != 0.0f; }))
                return fail("The source contains weighted vertices.");
            if (!transform(vertex.position, false) || !transform(vertex.normal, true))
                return fail("The transformed geometry exceeds finite numeric limits.");
            partLower = glm::min(partLower, glm::make_vec3(vertex.position));
            partUpper = glm::max(partUpper, glm::make_vec3(vertex.position));
            hasVertices = true;
        }
        if (part.vertices.empty()) return fail("The source contains an empty model part.");
        const auto center = (partLower + partUpper) * 0.5f;
        std::copy_n(&center.x, 3, part.center);
        part.radius = glm::length(partUpper - center);
        lower = glm::min(lower, partLower); upper = glm::max(upper, partUpper);
    }
    if (!hasVertices) return fail("The source has no vertices.");
    for (auto &cel : copy.sourceCelRecords)
    {
        if (!cel.posePositionDeltas.empty() || cel.points.size() % 4 != 0)
            return fail("The source outline contains unsupported pose data or invalid edges.");
        for (auto &point : cel.points)
            if (std::any_of(std::begin(point.boneWeights), std::end(point.boneWeights), [](float v) { return v != 0.0f; }) ||
                !transform(point.position, false)) return fail("The source outline cannot be copied as static geometry.");
    }
    auto center = (lower + upper) * 0.5f;
    copy.radius = glm::length(upper - center);
    // Fixed SO and MS culling spheres are world-space; their vertices remain local.
    if (destination.cid == 4 || (destination.so.present && destination.so.fixedPhysics))
    {
        center = glm::vec3(targetWorld * glm::vec4(center, 1.0f));
        copy.radius *= (std::max)({glm::length(glm::vec3(targetWorld[0])), glm::length(glm::vec3(targetWorld[1])), glm::length(glm::vec3(targetWorld[2]))});
    }
    else
    {
        // RenderAloGlobset transforms a non-fixed center, but passes sRadius
        // directly to SphereInFrustum/FInsideCmMrd without scaling it.
        const float worldScale = (std::max)({glm::length(glm::vec3(targetWorld[0])),
            glm::length(glm::vec3(targetWorld[1])), glm::length(glm::vec3(targetWorld[2]))});
        copy.radius = (std::max)(copy.radius, copy.radius * worldScale);
    }
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) || !std::isfinite(copy.radius))
        return fail("The copied bounds exceed finite numeric limits.");
    std::copy_n(&center.x, 3, copy.center);
    copy.ownerObjectIndex = target;
    copy.instanced = copy.editorCreatedInstance = copy.hasEditorPlacementTransform = false;
    copy.sourceGlobIndex = -1; copy.sourceRecordIndex = static_cast<std::size_t>(-1);
    const glm::mat4 identity(1.0f);
    std::copy_n(glm::value_ptr(identity), 16, copy.instanceTransform);
    std::copy_n(glm::value_ptr(identity), 16, copy.editorPlacementTransform);
    std::copy_n(glm::value_ptr(targetWorld), 16, copy.sourceToWorld);
    copy.derivedBatches.clear(); copy.derivedCelPackets.clear();
    // Materialize inherited static values before removing the instance chain.
    const auto inherited = [&](const auto &owns)
    {
        std::size_t index = source;
        for (std::size_t guard = 0; guard < globs_.size(); ++guard)
        {
            if (owns(globs_[index]) || !globs_[index].instanced) return &globs_[index];
            index = ResolveGlobSourceRecordIndex(index, globs_, globsets_);
        }
        return &globs_[source];
    };
    const auto zones = inherited([](const auto &g) { return g.hasZoneMask; });
    copy.hasZoneMask = zones->hasZoneMask; copy.zoneMask = zones->zoneMask;
    const auto sort = inherited([](const auto &g) { return g.hasExplicitSortZ; });
    copy.hasExplicitSortZ = sort->hasExplicitSortZ; copy.authoredSortZ = sort->authoredSortZ;
    const auto fog = inherited([](const auto &g) { return g.hasFogScale; });
    copy.hasFogScale = fog->hasFogScale; copy.fogScale = fog->fogScale;
    const auto radiusScale = inherited([](const auto &g) { return g.hasSubglobRadiusScale; });
    copy.hasSubglobRadiusScale = radiusScale->hasSubglobRadiusScale;
    copy.subglobRadiusScale = radiusScale->subglobRadiusScale;
    copy.hasMaximumRenderDistance = copy.hasCelMaximumRenderDistance = true;
    copy.maximumRenderDistanceInfinite = copy.celMaximumRenderDistanceInfinite = false;
    copy.authoredMaximumRenderDistance = original.maximumRenderDistance;
    copy.authoredCelMaximumRenderDistance = original.celMaximumRenderDistance;
    copy.propertyMask = 0; copy.modified = true;
    copy.oid = -1; // Visual copy does not duplicate an owner's named GLOB identity.
    copy.editorName = GlobEditorLabel(source) + " (Copy)";
    copy.renderGroup = 0;
    for (const auto &record : globs_)
    {
        if (record.renderGroup >= (std::numeric_limits<std::uint32_t>::max)() - 1u)
            return fail("There is no free render-group identity.");
        copy.renderGroup = (std::max)(copy.renderGroup, record.renderGroup + 1);
    }
    auto worldCenter = glm::make_vec3(copy.center);
    float worldRadius = copy.radius;
    if (destination.cid != 4 && !(destination.so.present && destination.so.fixedPhysics))
    {
        worldCenter = glm::vec3(targetWorld * glm::vec4(worldCenter, 1.0f));
        // sRadius was already expanded above when scale > 1. The hierarchy
        // sphere is serialized in world units and must not apply scale twice.
        const float scale = (std::max)({glm::length(glm::vec3(targetWorld[0])),
            glm::length(glm::vec3(targetWorld[1])), glm::length(glm::vec3(targetWorld[2]))});
        worldRadius *= (std::min)(1.0f, scale);
    }
    std::vector<bool> visited(objects_.size(), false);
    for (std::int32_t owner = target; owner >= 0; owner = objects_[owner].parentIndex)
    {
        if (static_cast<std::size_t>(owner) >= objects_.size() || visited[owner])
            return fail("The destination hierarchy has an invalid parent or cycle.");
        visited[owner] = true;
        const auto origin = glm::vec3(glm::make_mat4(objects_[owner].worldTransform)[3]);
        if (!std::isfinite(glm::length(worldCenter - origin) + worldRadius))
            return fail("The copied visual exceeds finite hierarchy bounds.");
    }
    error.clear();
    return true;
}

bool BrxDocument::PreviewStaticGlobCopy(std::size_t source, std::int32_t target, std::string &error) const
{
    BrxGlobRecord copy; std::size_t set;
    return PrepareStaticGlobCopy(source, target, copy, set, error);
}

std::size_t BrxDocument::CopyStaticGlobToObject(std::size_t source, std::int32_t target, std::string &error)
{
    BrxGlobRecord copy; std::size_t set;
    if (!PrepareStaticGlobCopy(source, target, copy, set, error)) return static_cast<std::size_t>(-1);
    const auto added = globs_.size();
    const auto vertexCount = renderVertices_.size(), indexCount = renderIndices_.size(), edgeCount = celEdges_.size();
    globs_.push_back(std::move(copy));
    globsets_[set].globIndices.push_back(added);
    if (!CommitGlobEdit(added))
    {
        globsets_[set].globIndices.pop_back(); globs_.pop_back();
        renderVertices_.resize(vertexCount); renderIndices_.resize(indexCount); celEdges_.resize(edgeCount);
        RebuildRenderPacketsFromGlobs();
        error = "The copied GLOB could not build native render packets.";
        return static_cast<std::size_t>(-1);
    }
    // Include the new, potentially offset visual in ALO hierarchy pruning.
    glm::vec3 worldCenter = glm::make_vec3(globs_[added].center);
    float worldRadius = globs_[added].radius;
    const auto world = glm::make_mat4(objects_[target].worldTransform);
    if (objects_[target].cid != 4 && !(objects_[target].so.present && objects_[target].so.fixedPhysics))
    {
        worldCenter = glm::vec3(world * glm::vec4(worldCenter, 1.0f));
        const float scale = (std::max)({glm::length(glm::vec3(world[0])),
            glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
        worldRadius *= (std::min)(1.0f, scale);
    }
    for (std::int32_t owner = target, guard = 0; owner >= 0 && static_cast<std::size_t>(owner) < objects_.size() &&
         static_cast<std::size_t>(guard++) < objects_.size(); owner = objects_[owner].parentIndex)
    {
        auto &object = objects_[owner];
        const float radius = glm::length(worldCenter - glm::vec3(glm::make_mat4(object.worldTransform)[3])) + worldRadius;
        object.renderRadiusAll = (std::max)(object.renderRadiusAll, radius);
        if (owner == target) object.renderRadiusSelf = (std::max)(object.renderRadiusSelf, radius);
    }
    return added;
}

std::size_t BrxDocument::DuplicateGlobAsInstance(const std::size_t sourceGlobRecordIndex)
{
    if (sourceGlobRecordIndex >= globs_.size())
        return static_cast<std::size_t>(-1);

    const auto &source = globs_[sourceGlobRecordIndex];
    BrxGlobRecord instance = source;
    instance.renderGroup = 0;
    for (const auto &candidate : globs_)
        instance.renderGroup = (std::max)(instance.renderGroup, candidate.renderGroup + 1);
    if (!source.editorName.empty())
        instance.editorName = source.editorName + "_Instance_" + std::to_string(instance.renderGroup);
    instance.instanced = true;
    instance.editorCreatedInstance = true;
    // derivedBatches already contain the loaded placement. A new editor
    // instance therefore starts with only the source's editor-side delta;
    // copying a loaded instance's serialized matrix would apply it twice.
    const glm::mat4 identity(1.0f);
    if (!source.editorCreatedInstance)
    {
        const float *placement =
            source.hasEditorPlacementTransform ? source.editorPlacementTransform : glm::value_ptr(identity);
        std::copy(placement, placement + 16, std::begin(instance.instanceTransform));
    }
    instance.hasEditorPlacementTransform = false;
    std::copy(glm::value_ptr(identity), glm::value_ptr(identity) + 16, std::begin(instance.editorPlacementTransform));
    instance.sourceRecordIndex = sourceGlobRecordIndex;
    instance.sourceGlobIndex = -1;
    for (auto &globset : globsets_)
    {
        const auto found = std::find(globset.globIndices.begin(), globset.globIndices.end(), sourceGlobRecordIndex);
        if (found == globset.globIndices.end())
            continue;
        const auto sourceSlot = static_cast<std::size_t>(std::distance(globset.globIndices.begin(), found));
        if (sourceSlot > static_cast<std::size_t>((std::numeric_limits<std::int16_t>::max)()) ||
            globset.globIndices.size() >= (std::numeric_limits<std::uint16_t>::max)())
            return static_cast<std::size_t>(-1);
        instance.sourceGlobIndex = static_cast<std::int16_t>(sourceSlot);
        globset.globIndices.push_back(globs_.size());
        break;
    }
    if (instance.sourceGlobIndex < 0)
        return static_cast<std::size_t>(-1);
    instance.propertyMask = 0x001;
    instance.hasZoneMask = false;
    instance.hasSubglobRadiusScale = false;
    instance.hasExplicitSortZ = false;
    instance.hasFogScale = false;
    instance.hasMaximumRenderDistance = false;
    instance.hasCelMaximumRenderDistance = false;
    instance.hasShaderAnimation = false;
    instance.hasGleam = false;
    // WRBG is legal on an instanced GLOB and is part of the model's authored
    // behavior. DuplicateGlobAsInstance clones the matching binding below, so
    // retain the semantic presence bit instead of creating contradictory
    // "not warped, but owns WRBG" state.
    instance.hasWarp = source.hasWarp;
    instance.modified = true;
    // BrxGlobRecord was copied from the selected source, including its current
    // instance transform. A regular authored GLOB carries identity here, while
    // duplicating an existing instance preserves its rendered placement. In
    // either case the duplicate initially overlaps exactly with its source and
    // can then be moved independently by the editor GLOB gizmo.
    const std::uint32_t instanceRenderGroup = instance.renderGroup;
    const auto sourceWarp = std::find_if(
        globWarps_.begin(), globWarps_.end(), [&](const BrxGlobWarpRecord &binding)
        { return binding.ownerObjectIndex == source.ownerObjectIndex && binding.renderGroup == source.renderGroup; });
    if (sourceWarp != globWarps_.end())
    {
        BrxGlobWarpRecord binding = *sourceWarp;
        binding.renderGroup = instanceRenderGroup;
        globWarps_.push_back(std::move(binding));
    }
    globs_.push_back(std::move(instance));
    RebuildRenderPacketsFromGlobs();
    return globs_.size() - 1;
}

bool BrxDocument::MakeGlobUnique(const std::size_t globIndex, std::string *error)
{
    const auto fail = [&](const char *message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (globIndex >= globs_.size() || !globs_[globIndex].instanced)
        return fail("Select an instanced GLOB to make unique.");

    auto &glob = globs_[globIndex];
    const auto inherited = [&](const auto &ownsProperty) -> const BrxGlobRecord *
    {
        std::size_t recordIndex = globIndex;
        for (std::size_t depth = 0; depth < globs_.size(); ++depth)
        {
            const auto &record = globs_[recordIndex];
            if (ownsProperty(record) || !record.instanced)
                return &record;
            recordIndex = ResolveGlobSourceRecordIndex(recordIndex, globs_, globsets_);
            if (recordIndex >= globs_.size())
                return nullptr;
        }
        return nullptr;
    };
    const auto *geometry = inherited([](const BrxGlobRecord &record) { return !record.instanced; });
    if (!geometry)
        return fail("The shared source geometry could not be resolved.");
    BrxGlobRecord detached = glob;
    detached.sourceSubglobs = geometry->sourceSubglobs;
    detached.sourceCelRecords = geometry->sourceCelRecords;
    const auto *zones = inherited([](const BrxGlobRecord &record) { return record.hasZoneMask; });
    const auto *sort = inherited([](const BrxGlobRecord &record) { return record.hasExplicitSortZ; });
    const auto *fog = inherited([](const BrxGlobRecord &record) { return record.hasFogScale; });
    const auto *animation = inherited([](const BrxGlobRecord &record) { return record.hasShaderAnimation; });
    if (!zones || !sort || !fog || !animation)
        return fail("The instance has an invalid source relationship.");
    detached.hasZoneMask = zones->hasZoneMask;
    detached.zoneMask = zones->zoneMask;
    detached.hasExplicitSortZ = sort->hasExplicitSortZ;
    detached.authoredSortZ = sort->authoredSortZ;
    detached.hasFogScale = fog->hasFogScale;
    detached.fogScale = fog->fogScale;
    detached.hasShaderAnimation = animation->hasShaderAnimation;
    detached.shaderAnimation = animation->shaderAnimation;
    // Preserve the resolved per-hop MRDs after removing the source chain.
    if (!detached.hasMaximumRenderDistance)
    {
        detached.hasMaximumRenderDistance = true;
        detached.maximumRenderDistanceInfinite = false;
        detached.authoredMaximumRenderDistance = glob.maximumRenderDistance;
    }
    if (!detached.hasCelMaximumRenderDistance)
    {
        detached.hasCelMaximumRenderDistance = true;
        detached.celMaximumRenderDistanceInfinite = false;
        detached.authoredCelMaximumRenderDistance = glob.celMaximumRenderDistance;
    }
    glm::mat4 bakeTransform(1.0f);
    if (glob.editorCreatedInstance)
        std::memcpy(glm::value_ptr(bakeTransform), glob.instanceTransform, sizeof(glob.instanceTransform));
    else if (glob.hasEditorPlacementTransform)
        std::memcpy(glm::value_ptr(bakeTransform), glob.editorPlacementTransform,
                    sizeof(glob.editorPlacementTransform));
    const glm::mat3 normalTransform = glm::mat3(bakeTransform);
    const float bakeScale =
        (std::max)({glm::length(glm::vec3(bakeTransform[0])), glm::length(glm::vec3(bakeTransform[1])),
                    glm::length(glm::vec3(bakeTransform[2]))});
    if (glob.ownerObjectIndex < 0 || static_cast<std::size_t>(glob.ownerObjectIndex) >= objects_.size())
        return fail("The GLOB has no valid owning object.");
    const auto &owner = objects_[static_cast<std::size_t>(glob.ownerObjectIndex)];
    const glm::mat4 ownerWorld = glm::make_mat4(owner.worldTransform);
    const glm::mat4 inverseOwner = glm::inverse(ownerWorld);
    const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
    const glm::mat4 identity(1.0f);
    const bool unchangedOwnerSpace =
        std::equal(glm::value_ptr(bakeTransform), glm::value_ptr(bakeTransform) + 16, glm::value_ptr(identity)) &&
        std::equal(glm::value_ptr(sourceToWorld), glm::value_ptr(sourceToWorld) + 16, glm::value_ptr(ownerWorld));
    const glm::mat4 localBake = unchangedOwnerSpace ? identity : inverseOwner * bakeTransform * sourceToWorld;
    const auto finiteMatrix = [](const glm::mat4 &matrix)
    {
        return std::all_of(glm::value_ptr(matrix), glm::value_ptr(matrix) + 16,
                           [](const float value) { return std::isfinite(value); });
    };
    if (!finiteMatrix(ownerWorld) || !finiteMatrix(bakeTransform) || !finiteMatrix(localBake))
        return fail("This placement cannot be converted to the owning object's space.");
    const bool changesSource =
        !std::equal(glm::value_ptr(localBake), glm::value_ptr(localBake) + 16, glm::value_ptr(identity));
    bool weighted = false;
    for (const auto &subglob : detached.sourceSubglobs)
        for (const auto &vertex : subglob.vertices)
            for (const float weight : vertex.boneWeights)
                weighted |= weight != 0.0f;
    for (const auto &cel : detached.sourceCelRecords)
        for (const auto &point : cel.points)
            for (const float weight : point.boneWeights)
                weighted |= weight != 0.0f;
    if (changesSource && (weighted || glob.hasWarp))
        return fail(
            "This rigged or deformed instance has a placement that Make unique cannot preserve safely yet. Keep it instanced.");
    const glm::mat3 localVectorBake(localBake);
    const float localScale = (std::max)({glm::length(glm::vec3(localBake[0])), glm::length(glm::vec3(localBake[1])),
                                         glm::length(glm::vec3(localBake[2]))});
    const auto transformSource = [&](float *value, const bool direction)
    {
        const glm::vec3 old(value[0], value[1], value[2]);
        const glm::vec3 transformed = direction ? localVectorBake * old : glm::vec3(localBake * glm::vec4(old, 1.0f));
        if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.z))
            return false;
        value[0] = transformed.x;
        value[1] = transformed.y;
        value[2] = transformed.z;
        return true;
    };
    if (changesSource)
    {
        for (auto &subglob : detached.sourceSubglobs)
        {
            if (!transformSource(subglob.center, false))
                return fail("The detached bounds exceed the supported numeric range.");
            subglob.radius *= localScale;
            if (!std::isfinite(subglob.radius))
                return fail("The detached bounds exceed the supported numeric range.");
            for (auto &vertex : subglob.vertices)
                if (!transformSource(vertex.position, false) || !transformSource(vertex.normal, true))
                    return fail("The detached geometry exceeds the supported numeric range.");
            for (auto &delta : subglob.posePositionDeltas)
                if (!transformSource(delta.data(), true))
                    return fail("A detached pose exceeds the supported numeric range.");
            for (auto &delta : subglob.poseNormalDeltas)
                if (!transformSource(delta.data(), true))
                    return fail("A detached pose exceeds the supported numeric range.");
        }
        for (auto &cel : detached.sourceCelRecords)
        {
            for (auto &point : cel.points)
                if (!transformSource(point.position, false))
                    return fail("The detached outline exceeds the supported numeric range.");
            for (auto &delta : cel.posePositionDeltas)
                if (!transformSource(delta.data(), true))
                    return fail("A detached outline pose exceeds the supported numeric range.");
        }
    }
    const glm::mat4 boundsBake = owner.cid == 4 || (owner.so.present && owner.so.fixedPhysics) ? bakeTransform
                                 : unchangedOwnerSpace                                         ? identity
                                                       : inverseOwner * bakeTransform * ownerWorld;
    const float emptyBound = (std::numeric_limits<float>::lowest)();
    const bool emptyBounds = detached.radius == emptyBound && detached.center[0] == emptyBound &&
                             detached.center[1] == emptyBound && detached.center[2] == emptyBound;
    if (!emptyBounds)
    {
        const glm::vec3 center =
            glm::vec3(boundsBake * glm::vec4(detached.center[0], detached.center[1], detached.center[2], 1.0f));
        const float scale = (std::max)({glm::length(glm::vec3(boundsBake[0])), glm::length(glm::vec3(boundsBake[1])),
                                        glm::length(glm::vec3(boundsBake[2]))});
        detached.radius *= scale;
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(detached.radius))
            return fail("The detached bounds exceed the supported numeric range.");
        detached.center[0] = center.x;
        detached.center[1] = center.y;
        detached.center[2] = center.z;
    }

    // Validate every shared range before appending anything, keeping failure
    // atomic if a malformed or partially decoded GLOB reaches this command.
    for (const auto &batch : glob.derivedBatches)
    {
        const std::size_t end = static_cast<std::size_t>(batch.firstIndex) + batch.indexCount;
        if (end > renderIndices_.size())
            return fail("A shared mesh has an invalid triangle range.");
        for (std::size_t index = batch.firstIndex; index < end; ++index)
            if (renderIndices_[index] >= renderVertices_.size())
                return fail("A shared mesh references an invalid vertex.");
    }
    for (const auto &cel : glob.derivedCelPackets)
        if (static_cast<std::size_t>(cel.firstEdge) + cel.edgeCount > celEdges_.size())
            return fail("A shared outline has an invalid edge range.");
    std::unordered_set<std::uint16_t> shadersToCopy;
    for (const auto &subglob : detached.sourceSubglobs)
    {
        if (subglob.shaderIndex >= shaders_.size())
            return fail("Assign a valid material to every part before making this GLOB unique.");
        shadersToCopy.insert(subglob.shaderIndex);
    }
    if (shaders_.size() + shadersToCopy.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
        return fail("There is no room in the retail material table for private materials.");

    // All source conversions above are staged. Unsupported rig/WRBG placement
    // and numeric failures leave the document unchanged.
    glob = std::move(detached);
    const std::uint32_t privateVertexBegin = static_cast<std::uint32_t>(renderVertices_.size());
    const std::uint32_t privateCelEdgeBegin = static_cast<std::uint32_t>(celEdges_.size());

    // An authored instance points at shared source ranges. Give every packet
    // private vertices and indices, baking the editor placement into those
    // vertices so the detached GLOB remains exactly where it was displayed.
    for (auto &batch : glob.derivedBatches)
    {
        const std::size_t sourceIndexEnd = static_cast<std::size_t>(batch.firstIndex) + batch.indexCount;
        if (sourceIndexEnd > renderIndices_.size())
            return false;

        std::unordered_map<std::uint32_t, std::uint32_t> remap;
        remap.reserve(batch.indexCount);
        const std::uint32_t newFirstIndex = static_cast<std::uint32_t>(renderIndices_.size());
        for (std::size_t sourceIndex = batch.firstIndex; sourceIndex < sourceIndexEnd; ++sourceIndex)
        {
            const std::uint32_t oldVertexIndex = renderIndices_[sourceIndex];
            if (oldVertexIndex >= renderVertices_.size())
                return false;
            const auto [entry, inserted] =
                remap.emplace(oldVertexIndex, static_cast<std::uint32_t>(renderVertices_.size()));
            if (inserted)
            {
                BrxRenderVertex vertex = renderVertices_[oldVertexIndex];
                const glm::vec3 position = glm::vec3(
                    bakeTransform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
                glm::vec3 normal = normalTransform * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                if (glm::dot(normal, normal) > 0.000001f)
                    normal = glm::normalize(normal);
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                renderVertices_.push_back(vertex);
            }
            renderIndices_.push_back(entry->second);
        }
        batch.firstIndex = newFirstIndex;
        const auto bakePoint = [&](float *point)
        {
            const glm::vec3 value = glm::vec3(bakeTransform * glm::vec4(point[0], point[1], point[2], 1.0f));
            point[0] = value.x;
            point[1] = value.y;
            point[2] = value.z;
        };
        bakePoint(batch.boundsCenter);
        bakePoint(batch.sortCenter);
        batch.boundsRadius *= bakeScale;
        batch.mrdRadius *= bakeScale;
        const auto rtckBase = bakeTransform * glm::make_mat4(batch.rtckBaseMatrix);
        std::copy_n(glm::value_ptr(rtckBase), 16, batch.rtckBaseMatrix);
        batch.hasEditorInstanceTransform = false;
        const glm::mat4 identity(1.0f);
        std::memcpy(batch.editorInstanceTransform, glm::value_ptr(identity), sizeof(batch.editorInstanceTransform));
    }

    // Cel edges are shared independently from triangle geometry, so detach and
    // bake their ranges as well.
    for (auto &cel : glob.derivedCelPackets)
    {
        const std::size_t sourceEdgeEnd = static_cast<std::size_t>(cel.firstEdge) + cel.edgeCount;
        if (sourceEdgeEnd > celEdges_.size())
            return false;
        const std::uint32_t newFirstEdge = static_cast<std::uint32_t>(celEdges_.size());
        for (std::size_t sourceEdge = cel.firstEdge; sourceEdge < sourceEdgeEnd; ++sourceEdge)
        {
            BrxCelEdge edge = celEdges_[sourceEdge];
            for (auto &point : edge.points)
            {
                const glm::vec3 value = glm::vec3(bakeTransform * glm::vec4(point[0], point[1], point[2], 1.0f));
                point[0] = value.x;
                point[1] = value.y;
                point[2] = value.z;
            }
            celEdges_.push_back(edge);
        }
        cel.firstEdge = newFirstEdge;
        const glm::vec3 center =
            glm::vec3(bakeTransform * glm::vec4(cel.center[0], cel.center[1], cel.center[2], 1.0f));
        cel.center[0] = center.x;
        cel.center[1] = center.y;
        cel.center[2] = center.z;
        cel.radius *= bakeScale;
    }

    // Authoritative SUBGLOB/SUBCEL arrays now include the instance placement in
    // owner-local space. A non-instanced BRX record has no matrix of its own.

    // Detaching a model must not leave material edits coupled to the source.
    // Duplicate each distinct SHD record used by this GLOB and remap both the
    // authoritative SUBGLOB metadata and generated packets to the private copy.
    std::unordered_map<std::uint16_t, std::uint16_t> privateShaders;
    for (auto &subglob : glob.sourceSubglobs)
    {
        if (subglob.shaderIndex >= shaders_.size())
            continue;
        auto found = privateShaders.find(subglob.shaderIndex);
        if (found == privateShaders.end())
        {
            BrxShaderRecord shader = shaders_[subglob.shaderIndex];
            shader.offset = 0;
            shader.editorResourceId = nextEditorResourceId_++;
            shader.editorName =
                (shader.editorName.empty() ? "Material " + std::to_string(subglob.shaderIndex) : shader.editorName) +
                " (Unique)";
            const std::uint16_t privateIndex = static_cast<std::uint16_t>(shaders_.size());
            shaders_.push_back(std::move(shader));
            found = privateShaders.emplace(subglob.shaderIndex, privateIndex).first;
        }
        subglob.shaderIndex = found->second;
    }
    for (std::size_t packet = 0; packet < glob.derivedBatches.size() && packet < glob.sourceSubglobs.size(); ++packet)
        glob.derivedBatches[packet].shaderIndex = glob.sourceSubglobs[packet].shaderIndex;

    for (auto &binding : globWarps_)
    {
        if (binding.renderGroup != glob.renderGroup)
            continue;
        binding.firstVertex = privateVertexBegin;
        binding.vertexCount = static_cast<std::uint32_t>(renderVertices_.size()) - privateVertexBegin;
        binding.firstCelEdge = privateCelEdgeBegin;
        binding.celEdgeCount = static_cast<std::uint32_t>(celEdges_.size()) - privateCelEdgeBegin;
        std::memcpy(binding.objectToWorld, glm::value_ptr(ownerWorld), sizeof(binding.objectToWorld));
    }

    std::memcpy(glob.sourceToWorld, glm::value_ptr(ownerWorld), sizeof(glob.sourceToWorld));
    std::memcpy(glob.instanceTransform, glm::value_ptr(identity), sizeof(glob.instanceTransform));
    std::memcpy(glob.editorPlacementTransform, glm::value_ptr(identity), sizeof(glob.editorPlacementTransform));
    glob.hasEditorPlacementTransform = false;
    glob.instanced = false;
    glob.editorCreatedInstance = false;
    glob.sourceGlobIndex = -1;
    glob.sourceRecordIndex = static_cast<std::size_t>(-1);
    glob.propertyMask &= static_cast<std::uint16_t>(~0x001u);
    if (glob.hasZoneMask)
        glob.propertyMask |= 0x002u;
    if (glob.hasExplicitSortZ)
        glob.propertyMask |= 0x004u;
    if (glob.hasFogScale)
        glob.propertyMask |= 0x008u;
    if (glob.hasMaximumRenderDistance)
        glob.propertyMask |= 0x010u;
    if (glob.hasCelMaximumRenderDistance)
        glob.propertyMask |= 0x020u;
    if (glob.hasShaderAnimation)
        glob.propertyMask |= 0x040u;
    glob.modified = true;
    RebuildRenderPacketsFromGlobs();
    if (error)
        error->clear();
    return true;
}

const char *BrxDocument::LanguageName(std::uint32_t languageId)
{
    switch (languageId)
    {
    case 0:
        return "English";
    case 1:
        return "French";
    case 2:
        return "German";
    case 3:
        return "Spanish";
    case 4:
        return "Italian";
    default:
        return "Unknown";
    }
}

const char *BrxDocument::ClassName(std::int16_t cid)
{
    static constexpr const char *names[] = {"BASIC",     "LO",      "ALO",     "SO",        "MS",
                                            "PO",        "STEP",    "JT",      "STEPGUARD", "SMARTGUARD",
                                            "GOMER",     "UBG",     "MBG",     "BHG",       "MURRAY",
                                            "PUFFC",     "CRFOD",   "CRFODB",  "CRFODK",    "RUBY",
                                            "TANK",      "ROV",     "TURRET",  "VAULT",     "PUFFER",
                                            "SUV",       "LGN",     "JETPACK", "RYTHM",     "MGV",
                                            "RIPG",      "WATER",   "BRK",     "BREAK",     "ALBRK",
                                            "DARTGUN",   "SWP",     "FRAGILE", "ZAPBREAK",  "BRKP",
                                            "BUTTON",    "VOLBTN",  "JLOVOL",  "BONE",      "SPRIZE",
                                            "SCPRIZE",   "CLUE",    "ALARM",   "SENSOR",    "LASEN",
                                            "CAMSEN",    "PRSEN",   "BARRIER", "IKH",       "TZP",
                                            "VOLZP",     "CHKPNT",  "CNVO",    "HBSK",      "BOMB",
                                            "MISSILE",   "ACCMISS", "TARMISS", "MINE",      "FLY",
                                            "RAT",       "ROH",     "ROC",     "ROST",      "ROP",
                                            "DART",      "JLO",     "PUFFT",   "MRKV",      "CLKWORK",
                                            "CLKWRKDMG", "DECOY",   "BLIPG",   "CAMERA",    "LBONE",
                                            "EMITTER",   "LIGHT",   "SCH",     "PROXY",     "SKY",
                                            "DPRIZE",    "CHARM",   "LIFETKN", "COIN",      "KEY",
                                            "GOLD",      "BOOST",   "LOCK",    "LOCKG",     "TAIL",
                                            "ROB",       "FLASH",   "DYSH",    "WAYPOINT",  "TN",
                                            "JLOC",      "DIALOG",  "SPEAKER", "WM",        "CRFODKSPAWN",
                                            "HUBSEL",    "HUBPROG", "UNK11",   "SW",        "CM",
                                            "SHAPE",     "HSHAPE",  "PIPE",    "RAIL",      "LANDING",
                                            "XFM",       "WARP",    "TARGET",  "HND",       "EXPL",
                                            "EXPLG",     "EXPLO",   "EXPLS",   "VOL",       "RATHOLE",
                                            "EXIT",      "PNT",     "HPNT",    "JMT",       "SPIRE",
                                            "SCAN",      "ASEG",    "ASEGBL",  "VISZONE",   "VISMAP",
                                            "FRZG",      "SM",      "SGG",     "PATHZONE",  "RCHM",
                                            "RWM",       "WR",      "KEYHOLE", "JSG",       "RYTHMSEQUENCE",
                                            "THNDFLASH", "ASEGA",   "SMA"};
    static_assert(std::size(names) == 148);
    return cid >= 0 && static_cast<std::size_t>(cid) < std::size(names) ? names[cid] : "Unknown CID";
}

std::string BrxDocument::EditorClassName(const std::int16_t cid)
{
    const auto *guide = FindCidAuthoringGuide(cid);
    const char *friendlyName = guide ? guide->name : nullptr;
    const char *runtimeName = ClassName(cid);
    if (friendlyName == nullptr)
        return runtimeName;
    return std::string(friendlyName) + " (" + runtimeName + ")";
}

std::string BrxDocument::ObjectEditorLabel(const std::int32_t objectIndex) const
{
    if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
        return "Invalid object";
    const auto &object = objects_[static_cast<std::size_t>(objectIndex)];
    if (!object.editorName.empty())
        return object.editorName;
    return EditorClassName(object.cid) + "_" + std::to_string(object.oid);
}

std::int32_t BrxDocument::FindObjectByEditorName(const std::string &name) const
{
    const auto equalIgnoringCase = [](const std::string &left, const std::string &right)
    {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
                                                         [](const char a, const char b)
                                                         {
                                                             return std::tolower(static_cast<unsigned char>(a)) ==
                                                                    std::tolower(static_cast<unsigned char>(b));
                                                         });
    };
    std::int32_t match = -1;
    for (std::size_t index = 0; index < objects_.size(); ++index)
    {
        if (objects_[index].editorName.empty() || !equalIgnoringCase(objects_[index].editorName, name))
            continue;
        if (match >= 0)
            return -2;
        match = static_cast<std::int32_t>(index);
    }
    return match;
}

std::string BrxDocument::VisibilityZoneName(const std::uint32_t zone) const
{
    if (zone < visibilityZoneNames_.size() && !visibilityZoneNames_[zone].empty())
        return visibilityZoneNames_[zone];
    return "Zone " + std::to_string(zone);
}

std::string BrxDocument::GlobEditorLabel(const std::size_t globIndex) const
{
    if (globIndex >= globs_.size())
        return "Invalid GLOB";
    const auto &glob = globs_[globIndex];
    if (!glob.editorName.empty())
        return glob.editorName;
    return "GLOB " + std::to_string(glob.renderGroup);
}

std::size_t BrxDocument::GlobSourceRecordIndex(const std::size_t globIndex) const
{
    return ResolveGlobSourceRecordIndex(globIndex, globs_, globsets_);
}

std::string BrxDocument::SubglobEditorLabel(const std::size_t globIndex, const std::size_t subglobIndex) const
{
    if (globIndex >= globs_.size())
        return "Invalid SUBGLOB";
    const auto &glob = globs_[globIndex];
    if (subglobIndex < glob.sourceSubglobs.size() && !glob.sourceSubglobs[subglobIndex].editorName.empty())
        return glob.sourceSubglobs[subglobIndex].editorName;
    return "SUBGLOB " + std::to_string(subglobIndex);
}

std::uint16_t BrxDocument::AllowedPropertyCount(const std::int16_t cid)
{
    // g_mpcidarysplopt from ProjectCane/spliceobj.cpp.  Counts include the
    // inherited LO/ALO/SO entries exactly as PeopidFind sees them.
    static constexpr std::uint16_t counts[] = {
        2,   21,  127, 149, 149, 153, 156, 170, 198, 211, 208, 208, 203, 201, 201, 198, 198, 199, 198, 207, 158, 154,
        160, 158, 156, 176, 177, 156, 155, 159, 149, 160, 179, 179, 180, 186, 184, 183, 184, 153, 165, 163, 167, 149,
        152, 152, 155, 158, 163, 167, 167, 179, 153, 149, 156, 156, 152, 150, 152, 169, 175, 178, 179, 169, 167, 160,
        150, 153, 149, 149, 155, 156, 149, 152, 154, 151, 152, 127, 133, 127, 200, 149, 127, 127, 128, 133, 133, 134,
        134, 133, 134, 133, 127, 130, 135, 160, 128, 127, 130, 163, 136, 140, 139, 127, 149, 127, 127, 127, 81,  39,
        21,  28,  30,  28,  21,  25,  35,  28,  35,  25,  27,  44,  93,  26,  26,  34,  23,  30,  25,  25,  23,  33,
        33,  21,  21,  22,  23,  23,  22,  27,  62,  41,  21,  38,  27,  29,  0,   11};
    static_assert(std::size(counts) == 148);
    return cid >= 0 && static_cast<std::size_t>(cid) < std::size(counts) ? counts[cid] : 0;
}

#include "BrxDocumentAudio.inc"
