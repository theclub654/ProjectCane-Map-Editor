void AudioPreviewService::Pause()
{
    if (linuxAudio_)
    {
        SDL_PauseAudioDevice(linuxAudio_->device, 1);
        linuxAudio_->paused = true;
    }
}
void AudioPreviewService::Resume()
{
    if (linuxAudio_)
    {
        linuxAudio_->paused = false;
        SDL_PauseAudioDevice(linuxAudio_->device, 0);
    }
}
void AudioPreviewService::Seek(const float seconds)
{
    if (!linuxAudio_ || !std::isfinite(seconds)) return;
    auto &audio = *linuxAudio_;
    const std::size_t frameBytes = static_cast<std::size_t>(audio.spec.channels) * sizeof(float);
    const auto frames = audio.samples.size() / frameBytes;
    const double target = (std::min)(static_cast<double>(frames),
        (std::max)(0.0, static_cast<double>(seconds)) * audio.spec.freq);
    SDL_LockAudioDevice(audio.device);
    audio.position = static_cast<std::size_t>(target) * frameBytes;
    SDL_UnlockAudioDevice(audio.device);
}
void AudioPreviewService::SetVolume(const float volume)
{
    if (!std::isfinite(volume)) return;
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    if (linuxAudio_)
    {
        SDL_LockAudioDevice(linuxAudio_->device);
        linuxAudio_->volume = volume_;
        SDL_UnlockAudioDevice(linuxAudio_->device);
    }
}
void AudioPreviewService::Stop()
{
    linuxAudio_.reset();
    open_ = false;
    playingPath_.clear();
    waveform_.clear();
}
bool AudioPreviewService::IsPlaying()
{
    if (!linuxAudio_ || linuxAudio_->paused) return false;
    SDL_LockAudioDevice(linuxAudio_->device);
    const bool playing = linuxAudio_->position < linuxAudio_->samples.size();
    SDL_UnlockAudioDevice(linuxAudio_->device);
    return playing;
}
bool AudioPreviewService::IsPaused()
{ return linuxAudio_ && linuxAudio_->paused; }
float AudioPreviewService::PositionSeconds() const
{
    if (!linuxAudio_) return 0.0f;
    SDL_LockAudioDevice(linuxAudio_->device);
    const auto position = linuxAudio_->position;
    SDL_UnlockAudioDevice(linuxAudio_->device);
    return static_cast<float>(static_cast<double>(position) /
        (static_cast<double>(linuxAudio_->spec.freq) * linuxAudio_->spec.channels * sizeof(float)));
}
float AudioPreviewService::DurationSeconds() const
{
    if (!linuxAudio_) return 0.0f;
    return static_cast<float>(static_cast<double>(linuxAudio_->samples.size()) /
        (static_cast<double>(linuxAudio_->spec.freq) * linuxAudio_->spec.channels * sizeof(float)));
}
