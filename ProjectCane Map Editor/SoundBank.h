#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SoundBankTone
{
    std::int8_t priority = 0, volume = 0, centerNote = 0;
    std::uint8_t centerFine = 0;
    std::int16_t pan = 0;
    std::int8_t mapLow = 0, mapHigh = 0, pitchBendLow = 0, pitchBendHigh = 0;
    std::uint16_t adsr1 = 0, adsr2 = 0, flags = 0;
    std::uint32_t sampleOffset = 0, sampleEndOffset = 0, reserved = 0;
};

struct SoundBankGrain
{
    std::uint32_t type = 0;
    std::int32_t delay = 0;
    std::array<std::uint8_t, 32> data{};
    bool hasTone = false;
    SoundBankTone tone{};
};

struct SoundBankSound
{
    std::int8_t volume = 0, volumeGroup = 0, instanceLimit = 0;
    std::int16_t pan = 0;
    std::uint16_t flags = 0;
    std::vector<SoundBankGrain> grains;
};

class SoundBank
{
  public:
    bool Open(const std::filesystem::path &path, std::string &error);
    void Close();
    [[nodiscard]] bool IsOpen() const
    {
        return !path_.empty();
    }
    [[nodiscard]] const std::filesystem::path &Path() const
    {
        return path_;
    }
    [[nodiscard]] std::uint32_t BankId() const
    {
        return bankId_;
    }
    [[nodiscard]] std::uint32_t Version() const
    {
        return version_;
    }
    [[nodiscard]] std::uint32_t SampleOffset() const
    {
        return sampleOffset_;
    }
    [[nodiscard]] std::uint32_t SampleSize() const
    {
        return sampleSize_;
    }
    [[nodiscard]] const std::vector<SoundBankSound> &Sounds() const
    {
        return sounds_;
    }
    bool ExportToneWave(std::size_t soundIndex, std::size_t grainIndex, const std::filesystem::path &destination,
                        std::string &error) const;

  private:
    std::filesystem::path path_;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t bankId_ = 0, version_ = 0, sampleOffset_ = 0, sampleSize_ = 0;
    std::vector<SoundBankSound> sounds_;
};
