void AudioPreviewService::SetDialogLanguage(const std::int32_t language)
{
    const auto selected = language >= 0 && language < 5 ? language : -1;
    if (selected == dialogLanguage_) return;
    Stop();
    dialogLanguage_ = selected;
}

std::vector<std::filesystem::path> AudioPreviewService::LocalizedDialogCandidates(const std::string &resourceName) const
{
    static constexpr const char *names[]{"English", "French", "German", "Spanish", "Italian"};
    std::vector<std::filesystem::path> paths;
    if (dialogLanguage_ < 0 || dialogLanguage_ >= 5 || resourceName.empty()) return paths;
    auto file = ResourcePath(resourceName).filename();
    file.replace_extension(".wav");
    const std::string language = names[dialogLanguage_];
    const auto add = [&](const std::filesystem::path &path)
    {
        const auto normalized = path.lexically_normal();
        if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) paths.push_back(normalized);
    };
    for (auto root : {documentPath_.parent_path(), additionalSearchRoot_})
    {
        if (root.empty()) continue;
        const auto name = Lower(root.filename().string());
        if ((name == "dialogs" || name == "commentary") &&
            Lower(root.parent_path().filename().string()) == Lower(language))
        { add(root / file); continue; }
        if (name != Lower(language)) root /= language;
        add(root / "Dialogs" / file);
        add(root / "Commentary" / file);
    }
    return paths;
}

void AudioPreviewService::CleanupTemporaryWave()
{
    if (temporaryWaveDirectory_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(temporaryWaveDirectory_ / "preview.wav", ignored);
    std::filesystem::remove(temporaryWaveDirectory_, ignored);
    temporaryWaveDirectory_.clear();
}

bool AudioPreviewService::PlayDialogWaveBytes(const std::vector<std::uint8_t> &bytes, std::string &error)
{
    Stop();
    if (!BrxDocument::ValidateDialogWave(bytes, error)) return false;
    std::error_code ec;
    const auto root = std::filesystem::temp_directory_path(ec);
    if (ec) { error = "Cannot locate a temporary audio-preview folder: " + ec.message(); return false; }
    static std::atomic<std::uint64_t> sequence{0};
    std::filesystem::path directory;
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        const auto name = "cane-dialog-preview-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "-" + std::to_string(++sequence);
        const auto candidate = root / name;
        if (std::filesystem::create_directory(candidate, ec)) { directory = candidate; break; }
        if (ec && ec != std::errc::file_exists) break;
        ec.clear();
    }
    if (directory.empty()) { error = "Cannot create a temporary audio-preview folder: " + ec.message(); return false; }
    const auto path = directory / "preview.wav";
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    const bool written = static_cast<bool>(output);
    if (written && Play(path, error))
    { temporaryWaveDirectory_ = directory; return true; }
    if (!written) error = "Could not write the temporary audio preview.";
    std::filesystem::remove(path, ec);
    std::filesystem::remove(directory, ec);
    return false;
}
