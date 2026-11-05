#include "AudioPreviewService.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>
#include <shlobj.h>
#else
#include <SDL.h>
#include "PlatformDialogs.h"
#include <limits>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>

#ifndef _WIN32
#include "AudioPreviewLinuxState.inc"
#endif

namespace
{
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::filesystem::path ResourcePath(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    return std::filesystem::path(value);
}

std::unordered_map<std::string, std::vector<std::filesystem::path>> g_waveByName;
std::filesystem::path g_indexRoot;
#ifdef _WIN32
constexpr wchar_t kAlias[] = L"ProjectCaneDialogPreview";

std::wstring MciStatus(const wchar_t *field)
{
    wchar_t result[128]{};
    const std::wstring command = std::wstring(L"status ") + kAlias + L" " + field;
    return mciSendStringW(command.c_str(), result, static_cast<UINT>(std::size(result)), nullptr) == 0
               ? std::wstring(result)
               : std::wstring{};
}

float MillisecondsStatus(const wchar_t *field)
{
    const auto value = MciStatus(field);
    if (value.empty())
        return 0.0f;
    try
    {
        return static_cast<float>(std::stoll(value)) / 1000.0f;
    }
    catch (...)
    {
        return 0.0f;
    }
}
#endif

template <typename T> bool Read(std::ifstream &stream, T &value)
{
    return static_cast<bool>(stream.read(reinterpret_cast<char *>(&value), sizeof(value)));
}
}

AudioPreviewService::~AudioPreviewService()
{
    Stop();
}

void AudioPreviewService::SetDocumentPath(const std::filesystem::path &documentPath)
{
    if (documentPath_ == documentPath)
        return;
    Stop();
    documentPath_ = documentPath;
    RebuildIndex();
}

void AudioPreviewService::SetAdditionalSearchRoot(const std::filesystem::path &root)
{
    if (additionalSearchRoot_ == root)
        return;
    additionalSearchRoot_ = root;
    RebuildIndex();
}

std::filesystem::path AudioPreviewService::BrowseForAudioFolder()
{
#ifdef _WIN32
    BROWSEINFOW browser{};
    browser.lpszTitle = L"Choose the extracted dialogue WAV folder";
    browser.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    const PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browser);
    if (selected == nullptr)
        return {};
    wchar_t path[MAX_PATH]{};
    const bool resolved = SHGetPathFromIDListW(selected, path) != FALSE;
    CoTaskMemFree(selected);
    return resolved ? std::filesystem::path(path) : std::filesystem::path{};
#else
    return ChooseExportFolder(nullptr, L"Choose the extracted dialogue WAV folder");
#endif
}

void AudioPreviewService::RebuildIndex()
{
    g_waveByName.clear();
    g_indexRoot.clear();
    if (documentPath_.empty())
        return;

    const auto root = documentPath_.parent_path();
    if (root.empty() && additionalSearchRoot_.empty())
        return;
    g_indexRoot = root;
    const std::filesystem::path roots[]{root, additionalSearchRoot_};
    for (const auto &searchRoot : roots)
    {
        if (searchRoot.empty())
            continue;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator
                 iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             iterator != end; iterator.increment(error))
        {
            if (error)
            {
                error.clear();
                continue;
            }
            if (!iterator->is_regular_file(error) || error)
            {
                error.clear();
                continue;
            }
            const auto &path = iterator->path();
            if (Lower(path.extension().string()) != ".wav")
                continue;
            auto add = [&](const std::string &key)
            {
                auto &matches = g_waveByName[key];
                if (std::find(matches.begin(), matches.end(), path) == matches.end())
                    matches.push_back(path);
            };
            add(Lower(path.filename().string()));
            add(Lower(path.stem().string()));
        }
    }
}

std::filesystem::path AudioPreviewService::ResolveDialogWave(const std::string &resourceName)
{
    if (resourceName.empty())
        return {};
    if (documentPath_.parent_path() != g_indexRoot)
        RebuildIndex();

    auto requested = ResourcePath(resourceName);
    requested.replace_extension(".wav");
    const auto direct = documentPath_.parent_path() / requested;
    std::error_code error;
    if (std::filesystem::is_regular_file(direct, error))
        return direct;

    const auto byName = g_waveByName.find(Lower(requested.filename().string()));
    if (byName != g_waveByName.end() && !byName->second.empty())
        return byName->second.front();
    const auto byStem = g_waveByName.find(Lower(requested.stem().string()));
    return byStem == g_waveByName.end() || byStem->second.empty() ? std::filesystem::path{} : byStem->second.front();
}

std::filesystem::path AudioPreviewService::ResolveEffectWave(const std::int32_t sfxId)
{
    if (sfxId < 0)
        return {};
    const std::string id = std::to_string(sfxId);
    const std::string candidates[]{"SFX_" + id, "SFX" + id, "sfx_" + id, "sound_" + id, "sound" + id, id};
    for (const auto &candidate : candidates)
        if (const auto path = ResolveDialogWave(candidate); !path.empty())
            return path;
    return {};
}

std::size_t AudioPreviewService::MatchCount(const std::string &resourceName) const
{
    auto requested = ResourcePath(resourceName);
    requested.replace_extension(".wav");
    const auto found = g_waveByName.find(Lower(requested.filename().string()));
    return found == g_waveByName.end() ? 0u : found->second.size();
}

bool AudioPreviewService::Play(const std::filesystem::path &wavePath, std::string &error)
{
    Stop();
    if (wavePath.empty())
    {
        error = "Dialogue WAV could not be found.";
        return false;
    }
#ifdef _WIN32
    const std::wstring command = std::wstring(L"open \"") + wavePath.wstring() + L"\" type waveaudio alias " + kAlias;
    const auto openError = mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    if (openError != 0)
    {
        error = "Windows could not play " + wavePath.filename().string() + ".";
        return false;
    }
    open_ = true;
    mciSendStringW((std::wstring(L"set ") + kAlias + L" time format milliseconds").c_str(), nullptr, 0, nullptr);
    SetVolume(volume_);
    if (mciSendStringW((std::wstring(L"play ") + kAlias).c_str(), nullptr, 0, nullptr) != 0)
    {
        Stop();
        error = "Windows could not start " + wavePath.filename().string() + ".";
        return false;
    }
#else
    linuxAudio_ = StartLinuxAudio(wavePath, volume_, error);
    if (!linuxAudio_)
        return false;
    open_ = true;
#endif
    playingPath_ = wavePath;
    waveform_.clear();
    std::ifstream stream(wavePath, std::ios::binary);
    char riff[4]{}, wave[4]{};
    std::uint32_t riffSize = 0;
    if (stream.read(riff, 4) && Read(stream, riffSize) && stream.read(wave, 4) && std::string(riff, 4) == "RIFF" &&
        std::string(wave, 4) == "WAVE")
    {
        std::uint16_t format = 0, channels = 0, bits = 0;
        std::vector<char> samples;
        const auto firstChunk = stream.tellg();
        stream.seekg(0, std::ios::end);
        const auto fileEnd = stream.tellg();
        stream.seekg(firstChunk);
        constexpr std::uint32_t maximumWaveformBytes = 64u * 1024u * 1024u;
        while (stream)
        {
            char id[4]{};
            std::uint32_t size = 0;
            if (!stream.read(id, 4) || !Read(stream, size))
                break;
            const auto payloadStart = stream.tellg();
            if (payloadStart < 0 || fileEnd < payloadStart ||
                static_cast<std::uint64_t>(size) + (size & 1u) >
                    static_cast<std::uint64_t>(fileEnd - payloadStart))
            {
                samples.clear();
                break;
            }
            if (std::string(id, 4) == "fmt " && size >= 16)
            {
                std::uint32_t rate = 0, bytes = 0;
                std::uint16_t align = 0;
                Read(stream, format);
                Read(stream, channels);
                Read(stream, rate);
                Read(stream, bytes);
                Read(stream, align);
                Read(stream, bits);
                stream.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
            }
            else if (std::string(id, 4) == "data")
            {
                if (size > maximumWaveformBytes)
                {
                    // Playback remains available; omit a large visualization
                    // rather than allocating directly from an untrusted size.
                    samples.clear();
                    stream.seekg(static_cast<std::streamoff>(size), std::ios::cur);
                }
                else
                {
                    samples.resize(size);
                    if (!stream.read(samples.data(), static_cast<std::streamsize>(size)))
                    {
                        samples.clear();
                        break;
                    }
                }
            }
            else
                stream.seekg(static_cast<std::streamoff>(size), std::ios::cur);
            if ((size & 1u) != 0)
                stream.seekg(1, std::ios::cur);
        }
        if (format == 1 && bits == 16 && channels > 0 && !samples.empty())
        {
            constexpr std::size_t bars = 256;
            waveform_.assign(bars, 0.0f);
            const std::size_t count = samples.size() / sizeof(std::int16_t);
            for (std::size_t i = 0; i < count; ++i)
            {
                std::int16_t sample = 0;
                std::memcpy(&sample, samples.data() + i * sizeof(sample), sizeof(sample));
                const std::size_t bar = std::min(bars - 1, i * bars / count);
                waveform_[bar] = std::max(waveform_[bar], std::abs(static_cast<float>(sample)) / 32768.0f);
            }
        }
    }
    error.clear();
    return true;
}

#ifdef _WIN32
void AudioPreviewService::Pause()
{
    if (open_)
        mciSendStringW((std::wstring(L"pause ") + kAlias).c_str(), nullptr, 0, nullptr);
}

void AudioPreviewService::Resume()
{
    if (open_)
        mciSendStringW((std::wstring(L"resume ") + kAlias).c_str(), nullptr, 0, nullptr);
}

void AudioPreviewService::Seek(const float seconds)
{
    if (!open_)
        return;
    const bool resume = MciStatus(L"mode") == L"playing";
    const auto milliseconds = static_cast<long long>(std::max(0.0f, seconds) * 1000.0f);
    const auto command = std::wstring(L"seek ") + kAlias + L" to " + std::to_wstring(milliseconds);
    mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    if (resume)
        mciSendStringW((std::wstring(L"play ") + kAlias).c_str(), nullptr, 0, nullptr);
}

void AudioPreviewService::SetVolume(const float volume)
{
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (!open_)
        return;
    const auto command =
        std::wstring(L"setaudio ") + kAlias + L" volume to " + std::to_wstring(static_cast<int>(volume_ * 1000.0f));
    mciSendStringW(command.c_str(), nullptr, 0, nullptr);
}

void AudioPreviewService::Stop()
{
    if (open_)
    {
        mciSendStringW((std::wstring(L"stop ") + kAlias).c_str(), nullptr, 0, nullptr);
        mciSendStringW((std::wstring(L"close ") + kAlias).c_str(), nullptr, 0, nullptr);
    }
    open_ = false;
    playingPath_.clear();
    waveform_.clear();
}

bool AudioPreviewService::IsPlaying()
{
    return open_ && MciStatus(L"mode") == L"playing";
}

bool AudioPreviewService::IsPaused()
{
    return open_ && MciStatus(L"mode") == L"paused";
}

float AudioPreviewService::PositionSeconds() const
{
    return open_ ? MillisecondsStatus(L"position") : 0.0f;
}

float AudioPreviewService::DurationSeconds() const
{
    return open_ ? MillisecondsStatus(L"length") : 0.0f;
}
#else
#include "AudioPreviewLinuxMethods.inc"
#endif
