struct LinuxAudioState
{
    SDL_AudioDeviceID device = 0;
    SDL_AudioSpec spec{};
    std::vector<Uint8> samples;
    std::size_t position = 0;
    float volume = 1.0f;
    bool paused = false;
    bool initialized = false;
    ~LinuxAudioState()
    {
        // Closing waits for the callback before releasing its backing samples.
        if (device) SDL_CloseAudioDevice(device);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

static std::shared_ptr<LinuxAudioState> StartLinuxAudio(const std::filesystem::path &path,
                                                      const float volume, std::string &error)
{
    auto state = std::make_shared<LinuxAudioState>();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        error = std::string("Audio initialization failed: ") + SDL_GetError();
        return {};
    }
    state->initialized = true;
    SDL_AudioSpec input{};
    Uint8 *wav = nullptr;
    Uint32 length = 0;
    if (!SDL_LoadWAV(path.c_str(), &input, &wav, &length))
    {
        error = std::string("WAV decoder failed: ") + SDL_GetError();
        return {};
    }
    if (input.channels == 0 || input.freq <= 0)
    {
        SDL_FreeWAV(wav);
        error = "WAV audio has an invalid channel count or sample rate.";
        return {};
    }
    SDL_AudioCVT conversion{};
    const int conversionResult = SDL_BuildAudioCVT(&conversion, input.format, input.channels, input.freq,
                                                  AUDIO_F32SYS, input.channels, input.freq);
    if (conversionResult < 0 || length == 0 || conversion.len_mult <= 0 ||
        length > static_cast<Uint32>((std::numeric_limits<int>::max)() / conversion.len_mult))
    {
        SDL_FreeWAV(wav);
        error = "Unsupported or oversized WAV audio.";
        return {};
    }
    state->samples.resize(static_cast<std::size_t>(length) * conversion.len_mult);
    std::memcpy(state->samples.data(), wav, length);
    SDL_FreeWAV(wav);
    conversion.buf = state->samples.data();
    conversion.len = static_cast<int>(length);
    if (conversionResult != 0 && SDL_ConvertAudio(&conversion) != 0)
    {
        error = std::string("WAV conversion failed: ") + SDL_GetError();
        return {};
    }
    state->samples.resize(conversionResult == 0 ? length : static_cast<std::size_t>(conversion.len_cvt));
    state->spec.freq = input.freq;
    state->spec.channels = input.channels;
    state->spec.format = AUDIO_F32SYS;
    state->spec.samples = 1024;
    state->spec.userdata = state.get();
    state->spec.callback = +[](void *context, Uint8 *output, int count)
    {
        auto &audio = *static_cast<LinuxAudioState *>(context);
        std::memset(output, 0, static_cast<std::size_t>(count));
        const auto available = (std::min)(audio.samples.size() - audio.position, static_cast<std::size_t>(count));
        if (available)
        {
            SDL_MixAudioFormat(output, audio.samples.data() + audio.position, AUDIO_F32SYS,
                              static_cast<Uint32>(available), static_cast<int>(audio.volume * SDL_MIX_MAXVOLUME));
            audio.position += available;
        }
    };
    state->volume = volume;
    state->device = SDL_OpenAudioDevice(nullptr, 0, &state->spec, nullptr, 0);
    if (!state->device)
    {
        error = std::string("Audio device could not be opened: ") + SDL_GetError();
        return {};
    }
    SDL_PauseAudioDevice(state->device, 0);
    error.clear();
    return state;
}
