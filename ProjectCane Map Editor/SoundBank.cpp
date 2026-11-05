#include "SoundBank.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace
{
template <class T> bool Read(const std::vector<std::uint8_t> &bytes, const std::size_t offset, T &value)
{
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}
}

void SoundBank::Close()
{
    path_.clear();
    bytes_.clear();
    sounds_.clear();
    bankId_ = version_ = sampleOffset_ = sampleSize_ = 0;
}

bool SoundBank::Open(const std::filesystem::path &path, std::string &error)
{
    Close();
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open BNK: " + path.string();
        return false;
    }
    bytes_.assign(std::istreambuf_iterator<char>(stream), {});

    std::uint32_t fileType = 0, chunkCount = 0, metadataOffset = 0, metadataSize = 0;
    if (!Read(bytes_, 0, fileType) || fileType != 3 || !Read(bytes_, 4, chunkCount) || chunkCount < 2 ||
        !Read(bytes_, 8, metadataOffset) || !Read(bytes_, 12, metadataSize) || !Read(bytes_, 16, sampleOffset_) ||
        !Read(bytes_, 20, sampleSize_))
    {
        error = "Not a supported type-3 989SND effect bank.";
        Close();
        return false;
    }
    if (metadataOffset > bytes_.size() || metadataSize > bytes_.size() - metadataOffset ||
        sampleOffset_ > bytes_.size() || sampleSize_ > bytes_.size() - sampleOffset_)
    {
        error = "BNK chunk ranges extend beyond the file.";
        Close();
        return false;
    }

    constexpr std::uint32_t sblk =
        std::uint32_t('S') | (std::uint32_t('B') << 8) | (std::uint32_t('l') << 16) | (std::uint32_t('k') << 24);
    std::uint32_t id = 0, firstSound = 0, firstGrain = 0;
    std::uint16_t soundCount = 0;
    if (!Read(bytes_, metadataOffset, id) || id != sblk || !Read(bytes_, metadataOffset + 4, version_) ||
        !Read(bytes_, metadataOffset + 12, bankId_) || !Read(bytes_, metadataOffset + 22, soundCount) ||
        !Read(bytes_, metadataOffset + 28, firstSound) || !Read(bytes_, metadataOffset + 32, firstGrain))
    {
        error = "BNK has an invalid or truncated SBlk header.";
        Close();
        return false;
    }

    constexpr std::size_t soundRecordSize = 12, grainRecordSize = 0x28;
    const std::size_t soundTable = std::size_t(metadataOffset) + firstSound;
    if (soundTable > bytes_.size() || soundCount > (bytes_.size() - soundTable) / soundRecordSize)
    {
        error = "BNK sound table is outside the file.";
        Close();
        return false;
    }
    sounds_.reserve(soundCount);
    std::vector<std::uint32_t> grainOffsets;
    for (std::uint16_t i = 0; i < soundCount; ++i)
    {
        const auto record = soundTable + std::size_t(i) * soundRecordSize;
        SoundBankSound sound{};
        std::int8_t grainCount = 0;
        std::uint32_t relativeGrain = 0;
        if (!Read(bytes_, record, sound.volume) || !Read(bytes_, record + 1, sound.volumeGroup) ||
            !Read(bytes_, record + 2, sound.pan) || !Read(bytes_, record + 4, grainCount) || grainCount < 0 ||
            !Read(bytes_, record + 5, sound.instanceLimit) || !Read(bytes_, record + 6, sound.flags) ||
            !Read(bytes_, record + 8, relativeGrain))
        {
            error = "BNK contains a malformed sound record.";
            Close();
            return false;
        }
        sounds_.push_back(sound);
        grainOffsets.push_back(firstGrain + relativeGrain);
        sounds_.back().grains.reserve(static_cast<std::uint8_t>(grainCount));
        for (std::uint8_t g = 0; g < static_cast<std::uint8_t>(grainCount); ++g)
        {
            const std::size_t grainAt =
                std::size_t(metadataOffset) + grainOffsets.back() + std::size_t(g) * grainRecordSize;
            if (grainAt > std::size_t(metadataOffset) + metadataSize ||
                grainRecordSize > std::size_t(metadataOffset) + metadataSize - grainAt)
            {
                error = "BNK grain table is outside its metadata chunk.";
                Close();
                return false;
            }
            SoundBankGrain grain{};
            if (!Read(bytes_, grainAt, grain.type) || !Read(bytes_, grainAt + 4, grain.delay))
            {
                error = "BNK contains a truncated grain.";
                Close();
                return false;
            }
            std::memcpy(grain.data.data(), bytes_.data() + grainAt + 8, grain.data.size());
            if (grain.type == 1 || grain.type == 9)
            {
                grain.hasTone = true;
                const auto t = grainAt + 8;
                if (!Read(bytes_, t, grain.tone.priority) || !Read(bytes_, t + 1, grain.tone.volume) ||
                    !Read(bytes_, t + 2, grain.tone.centerNote) || !Read(bytes_, t + 3, grain.tone.centerFine) ||
                    !Read(bytes_, t + 4, grain.tone.pan) || !Read(bytes_, t + 6, grain.tone.mapLow) ||
                    !Read(bytes_, t + 7, grain.tone.mapHigh) || !Read(bytes_, t + 8, grain.tone.pitchBendLow) ||
                    !Read(bytes_, t + 9, grain.tone.pitchBendHigh) || !Read(bytes_, t + 10, grain.tone.adsr1) ||
                    !Read(bytes_, t + 12, grain.tone.adsr2) || !Read(bytes_, t + 14, grain.tone.flags) ||
                    !Read(bytes_, t + 16, grain.tone.sampleOffset) || !Read(bytes_, t + 20, grain.tone.reserved))
                {
                    error = "BNK contains a truncated tone grain.";
                    Close();
                    return false;
                }
            }
            sounds_.back().grains.push_back(grain);
        }
    }
    std::vector<std::uint32_t> starts;
    for (const auto &sound : sounds_)
        for (const auto &grain : sound.grains)
            if (grain.hasTone && grain.tone.sampleOffset < sampleSize_)
                starts.push_back(grain.tone.sampleOffset);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    for (auto &sound : sounds_)
        for (auto &grain : sound.grains)
            if (grain.hasTone)
            {
                const auto next = std::upper_bound(starts.begin(), starts.end(), grain.tone.sampleOffset);
                grain.tone.sampleEndOffset = next == starts.end() ? sampleSize_ : *next;
            }
    path_ = path;
    error.clear();
    return true;
}

bool SoundBank::ExportToneWave(const std::size_t soundIndex, const std::size_t grainIndex,
                               const std::filesystem::path &destination, std::string &error) const
{
    if (soundIndex >= sounds_.size() || grainIndex >= sounds_[soundIndex].grains.size() ||
        !sounds_[soundIndex].grains[grainIndex].hasTone)
    {
        error = "The selected grain is not a tone.";
        return false;
    }
    const auto &tone = sounds_[soundIndex].grains[grainIndex].tone;
    if (tone.sampleOffset >= sampleSize_ || tone.sampleEndOffset <= tone.sampleOffset ||
        tone.sampleEndOffset > sampleSize_)
    {
        error = "The tone has an invalid ADPCM sample range.";
        return false;
    }

    static constexpr int coefficients[16][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    std::vector<std::int16_t> pcm;
    const std::size_t begin = std::size_t(sampleOffset_) + tone.sampleOffset;
    const std::size_t end = std::size_t(sampleOffset_) + tone.sampleEndOffset;
    int history1 = 0, history2 = 0;
    bool sawEnd = false;
    for (std::size_t block = begin; block + 16 <= end; block += 16)
    {
        const std::uint8_t predictorShift = bytes_[block], flags = bytes_[block + 1];
        const int shift = predictorShift & 15, filter = (predictorShift >> 4) & 15;
        for (int sample = 0; sample < 28; ++sample)
        {
            const std::uint8_t packed = bytes_[block + 2 + sample / 2];
            int nibble = (sample & 1) ? packed >> 4 : packed & 15;
            if (nibble & 8)
                nibble -= 16;
            int value = (nibble * 4096) >> shift;
            value += (coefficients[filter][0] * history1 + coefficients[filter][1] * history2 + 32) >> 6;
            value = std::clamp(value, -32768, 32767);
            history2 = history1;
            history1 = value;
            pcm.push_back(static_cast<std::int16_t>(value));
        }
        if (flags & 1)
        {
            sawEnd = true;
            break;
        }
    }
    if (!sawEnd || pcm.empty())
    {
        error = "The ADPCM stream has no valid end block.";
        return false;
    }

    // The RIFF size includes 36 bytes beyond PCM data. Reject overflow before
    // opening/truncating the destination instead of silently shortening audio.
    constexpr std::size_t maximumPcmSamples =
        ((std::numeric_limits<std::uint32_t>::max)() - 36u) / sizeof(std::int16_t);
    if (pcm.size() > maximumPcmSamples)
    {
        error = "Decoded audio exceeds the RIFF/WAV size limit.";
        return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        error = "Could not create preview WAV.";
        return false;
    }
    constexpr std::uint32_t sampleRate = 48000, fmtSize = 16;
    constexpr std::uint16_t format = 1, channels = 1, bits = 16, blockAlign = 2;
    constexpr std::uint32_t byteRate = sampleRate * blockAlign;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t)), riffBytes = 36 + dataBytes;
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char *>(&riffBytes), 4);
    out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char *>(&fmtSize), 4);
    out.write(reinterpret_cast<const char *>(&format), 2);
    out.write(reinterpret_cast<const char *>(&channels), 2);
    out.write(reinterpret_cast<const char *>(&sampleRate), 4);
    out.write(reinterpret_cast<const char *>(&byteRate), 4);
    out.write(reinterpret_cast<const char *>(&blockAlign), 2);
    out.write(reinterpret_cast<const char *>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char *>(&dataBytes), 4);
    out.write(reinterpret_cast<const char *>(pcm.data()), dataBytes);
    out.close();
    if (!out)
    {
        error = "Writing the preview WAV failed.";
        return false;
    }
    error.clear();
    return true;
}
