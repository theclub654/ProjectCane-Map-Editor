// Included in BrxCompiler.cpp's internal namespace after WritePayloadFile.
bool DialogWaveFileMatches(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(bytes.size())) return false;
    input.seekg(0);
    std::array<std::uint8_t, 65536> block{};
    for (std::size_t offset = 0; offset < bytes.size();)
    {
        const auto size = (std::min)(block.size(), bytes.size() - offset);
        if (!input.read(reinterpret_cast<char *>(block.data()), static_cast<std::streamsize>(size)) ||
            std::memcmp(block.data(), bytes.data() + offset, size) != 0) return false;
        offset += size;
    }
    return true;
}

bool ExportDialogWaves(const BrxDocument &document, const std::filesystem::path &destination, std::string &error)
{
    struct Pending { const BrxDialogWaveAsset *asset; std::filesystem::path target; bool exists; };
    std::vector<Pending> pending;
    std::unordered_set<std::string> names;
    const auto lower = [](std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return value;
    };
    for (const auto &asset : document.DialogWaves())
    {
        if (!asset.bytes || asset.languageId > 4 ||
            std::none_of(document.Languages().begin(), document.Languages().end(),
                         [&](const auto &language) { return language.languageId == asset.languageId; }) ||
            !names.insert(std::to_string(asset.languageId) + ":" + lower(asset.resourceName)).second ||
            !BrxDocument::ValidateDialogVoiceResource(asset.resourceName, error) ||
            std::filesystem::path(asset.resourceName).extension() != ".wav" ||
            !BrxDocument::ValidateDialogWave(*asset.bytes, error))
        { if (error.empty()) error = "A staged dialogue WAV is invalid or ambiguous."; return false; }
        std::uint32_t crc = 0xffffffffu;
        for (const auto byte : *asset.bytes)
        { crc ^= byte; for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); }
        if (~crc != asset.crc) { error = "An imported WAV failed its integrity check."; return false; }
        auto root = destination.parent_path();
        if (root.empty()) root = ".";
        const std::string language = BrxDocument::LanguageName(asset.languageId);
        if (lower(root.filename().string()) != lower(language)) root /= language;
        const auto target = root / "Dialogs" / asset.resourceName;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(target, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
        { error = "Cannot inspect voice export path: " + target.string() + ": " + ec.message(); return false; }
        const bool exists = status.type() != std::filesystem::file_type::not_found;
        if (exists && (status.type() != std::filesystem::file_type::regular || !DialogWaveFileMatches(target, *asset.bytes)))
        { error = "Voice export would replace a different existing file; nothing was overwritten: " + target.string(); return false; }
        pending.push_back({&asset, target, exists});
    }
    // All collisions are checked before writing any clips. Immutable generated
    // names make identical re-exports harmless; copy_options::none also protects
    // against a file appearing between this check and publication.
    static std::atomic<std::uint64_t> sequence{0};
    for (const auto &item : pending)
    {
        if (item.exists) continue;
        std::error_code ec;
        std::filesystem::create_directories(item.target.parent_path(), ec);
        if (ec) { error = "Cannot create voice export folder: " + ec.message(); return false; }
        std::filesystem::path staging;
        for (unsigned attempt = 0; attempt < 64; ++attempt)
        {
            const auto name = ".cane-wave-stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                              "-" + std::to_string(++sequence);
            const auto candidate = item.target.parent_path() / name;
            if (std::filesystem::create_directory(candidate, ec)) { staging = candidate; break; }
            if (ec && ec != std::errc::file_exists) break;
            ec.clear();
        }
        if (staging.empty()) { error = "Cannot create a temporary voice export folder: " + ec.message(); return false; }
        const auto temporary = staging / "voice.wav";
        const auto cleanup = [&]()
        { std::error_code ignored; std::filesystem::remove(temporary, ignored); std::filesystem::remove(staging, ignored); };
        if (!WritePayloadFile(*item.asset->bytes, temporary, error)) { cleanup(); return false; }
        const bool copied = std::filesystem::copy_file(temporary, item.target, std::filesystem::copy_options::none, ec);
        cleanup();
        if (!copied || !DialogWaveFileMatches(item.target, *item.asset->bytes))
        { error = "Could not publish the dialogue WAV without replacing an existing file: " + item.target.string() + ": " + ec.message(); return false; }
    }
    return true;
}
