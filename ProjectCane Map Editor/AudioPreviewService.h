#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
struct LinuxAudioState;
#endif

class AudioPreviewService
{
  public:
    ~AudioPreviewService();

    void SetDocumentPath(const std::filesystem::path &documentPath);
    void SetAdditionalSearchRoot(const std::filesystem::path &root);
    [[nodiscard]] static std::filesystem::path BrowseForAudioFolder();
    [[nodiscard]] std::filesystem::path ResolveDialogWave(const std::string &resourceName);
    [[nodiscard]] std::filesystem::path ResolveEffectWave(std::int32_t sfxId);
    [[nodiscard]] std::size_t MatchCount(const std::string &resourceName) const;
    [[nodiscard]] bool Play(const std::filesystem::path &wavePath, std::string &error);
    void Pause();
    void Resume();
    void Seek(float seconds);
    void SetVolume(float volume);
    void Stop();

    [[nodiscard]] bool IsPlaying();
    [[nodiscard]] bool IsPaused();
    [[nodiscard]] float PositionSeconds() const;
    [[nodiscard]] float DurationSeconds() const;
    [[nodiscard]] const std::filesystem::path &PlayingPath() const
    {
        return playingPath_;
    }
    [[nodiscard]] const std::vector<float> &Waveform() const
    {
        return waveform_;
    }

  private:
    void RebuildIndex();

    std::filesystem::path documentPath_;
    std::filesystem::path additionalSearchRoot_;
    std::filesystem::path playingPath_;
    std::vector<float> waveform_;
    bool open_ = false;
    float volume_ = 1.0f;
#ifndef _WIN32
    std::shared_ptr<LinuxAudioState> linuxAudio_;
#endif
};
