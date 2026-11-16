// Included by BrxDocument.cpp: native PAL dialogue audio authoring helpers.
bool BrxDocument::ValidateDialogVoiceResource(const std::string &name, std::string &error)
{
    if (name.empty() || name.size() > 32u || name == "." || name == ".." || name.back() == '.' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(), [](const unsigned char ch)
        { return ch < 0x20u || ch > 0x7eu || std::string("<>:\"/\\|?*").find(ch) != std::string::npos; }))
    {
        error = "Use a bare audio filename of 1-32 printable ASCII bytes, without folders or reserved filename characters.";
        return false;
    }
    error.clear();
    return true;
}

bool BrxDocument::ValidateDialogWave(const std::vector<std::uint8_t> &bytes, std::string &error)
{
    const auto invalid = [&]()
    {
        error = "ProjectCane requires a valid PCM WAV: 8/16/24/32-bit integer samples (classic PCM or extensible PCM), with consistent sample rate, block alignment and data size.";
        return false;
    };
    if (bytes.size() < 12 || bytes.size() > 256u * 1024u * 1024u ||
        std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return invalid();
    const auto u16 = [&](std::size_t at) { return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8u); };
    const auto u32 = [&](std::size_t at)
    {
        return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8u) |
               (std::uint32_t(bytes[at + 2]) << 16u) | (std::uint32_t(bytes[at + 3]) << 24u);
    };
    bool formatFound = false, dataFound = false;
    unsigned channels = 0, bits = 0, block = 0;
    std::uint32_t rate = 0, average = 0, dataSize = 0;
    for (std::size_t offset = 12; offset + 8 <= bytes.size();)
    {
        const auto size = u32(offset + 4);
        const auto payload = offset + 8;
        if (size > bytes.size() - payload) return invalid();
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && size >= 16)
        {
            const auto tag = u16(payload);
            channels = u16(payload + 2); rate = u32(payload + 4); average = u32(payload + 8);
            block = u16(payload + 12); bits = u16(payload + 14);
            formatFound = tag == 1;
            if (tag == 0xfffeu && size >= 40)
            {
                static constexpr std::uint8_t pcmSubtype[16]{1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};
                formatFound = u16(payload + 16) >= 22 &&
                    (u16(payload + 18) == 0 || u16(payload + 18) <= bits) &&
                    std::memcmp(bytes.data() + payload + 24, pcmSubtype, 16) == 0;
            }
        }
        else if (std::memcmp(bytes.data() + offset, "data", 4) == 0)
        { dataSize = size; dataFound = size != 0; }
        offset = payload + size + (size & 1u);
    }
    // sound.cpp::LoadPcmWave / XAudio2's documented format limits.
    if (!formatFound || !dataFound || channels < 1 || channels > 64 || rate < 1000 || rate > 200000 ||
        (bits != 8 && bits != 16 && bits != 24 && bits != 32) || block == 0 ||
        block != channels * (bits / 8u) || average != rate * block || dataSize % block != 0)
        return invalid();
    error.clear();
    return true;
}

bool BrxDocument::ImportDialogWave(std::uint32_t languageId, const std::filesystem::path &path,
                                  std::string &resourceName, std::string &error)
{
    if (std::none_of(languages_.begin(), languages_.end(),
                     [&](const auto &language) { return language.languageId == languageId; }))
    { error = "This PAL language is not present in the level."; return false; }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto length = input ? input.tellg() : std::streampos{-1};
    if (length < 12 || length > std::streamoff{256u * 1024u * 1024u})
    { error = "The selected WAV is unreadable or exceeds the 256 MiB import limit."; return false; }
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes->data()), static_cast<std::streamsize>(bytes->size())) ||
        !ValidateDialogWave(*bytes, error))
    { if (error.empty()) error = "The selected WAV could not be read completely."; return false; }
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto byte : *bytes) { hash ^= byte; hash *= 1099511628211ull; }
    for (unsigned attempt = 0; attempt < 256; ++attempt)
    {
        std::string hex(16, '0');
        const auto value = hash ^ (std::uint64_t(attempt) * 0x9e3779b97f4a7c15ull);
        for (unsigned i = 0; i < 16; ++i) hex[15-i] = "0123456789abcdef"[(value >> (i * 4u)) & 15u];
        const auto name = "cane_" + std::to_string(languageId) + "_" + hex + ".wav";
        const auto existing = std::find_if(dialogWaves_.begin(), dialogWaves_.end(), [&](const auto &asset)
        { return asset.languageId == languageId && asset.resourceName == name; });
        if (existing != dialogWaves_.end())
        {
            if (existing->bytes && *existing->bytes == *bytes)
            { resourceName = name; error.clear(); return true; }
            continue;
        }
        dialogWaves_.push_back({languageId, name, bytes, Crc32(*bytes)});
        resourceName = name;
        error.clear();
        return true;
    }
    error = "Could not allocate a unique imported voice filename.";
    return false;
}
