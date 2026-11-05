bool LoadTextureRgbaImpl(const std::filesystem::path &path, const std::span<const std::uint8_t> encoded,
                         const bool embedded, BrxBitmapRecord &bitmap, std::string &error)
{
    error.clear();
    std::vector<std::uint8_t> fileBytes;
    if (!embedded)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0 || static_cast<std::uint64_t>(input.tellg()) > UINT32_MAX)
        {
            error = "Image could not be read or exceeds the decoder's input size limit: " + path.string();
            return false;
        }
        fileBytes.resize(static_cast<std::size_t>(input.tellg()));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size())))
        {
            error = "Image read was incomplete: " + path.string();
            return false;
        }
    }
    const auto bytes = embedded ? encoded : std::span<const std::uint8_t>(fileBytes);
    if (bytes.empty() || bytes.size() > UINT32_MAX)
    {
        error = "Embedded image is empty or exceeds the decoder's input size limit.";
        return false;
    }
    auto *loader = gdk_pixbuf_loader_new();
    bool invalidSize = false;
    g_signal_connect(loader, "size-prepared", G_CALLBACK(+[](GdkPixbufLoader *source, int width, int height, gpointer context)
    {
        auto &invalid = *static_cast<bool *>(context);
        invalid = invalid || width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
            static_cast<std::uint64_t>(width) * height * 4u > UINT32_MAX;
        if (invalid) gdk_pixbuf_loader_set_size(source, 1, 1);
    }), &invalidSize);
    GError *failure = nullptr;
    const bool written = gdk_pixbuf_loader_write(loader, bytes.data(), bytes.size(), &failure);
    const bool closed = gdk_pixbuf_loader_close(loader, written ? &failure : nullptr);
    auto *image = written && closed && !invalidSize ? gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
    if (!image)
    {
        error = invalidSize ? "Texture dimensions must be between 1 and 32768 pixels and fit the RGBA byte limit."
                            : "Image decoder could not read " + path.string() +
                              (failure ? ": " + std::string(failure->message) : ".");
        if (failure) g_error_free(failure);
        g_object_unref(loader);
        return false;
    }
    const int width = gdk_pixbuf_get_width(image), height = gdk_pixbuf_get_height(image);
    const int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
    if ((channels != 3 && channels != 4) || gdk_pixbuf_get_bits_per_sample(image) != 8)
    {
        error = "Image decoder returned an unsupported pixel layout.";
        g_object_unref(loader);
        return false;
    }
    BrxBitmapRecord decoded{};
    decoded.editorName = path.stem().string();
    decoded.width = static_cast<std::uint32_t>(width);
    decoded.height = static_cast<std::uint32_t>(height);
    decoded.mipCount = 1;
    decoded.pixelBytes = static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) * height * 4u);
    decoded.pixels.resize(decoded.pixelBytes);
    const auto *pixels = gdk_pixbuf_read_pixels(image);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto *source = pixels + static_cast<std::size_t>(y) * stride + x * channels;
            auto *destination = decoded.pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
            std::copy_n(source, 3, destination);
            destination[3] = channels == 4 ? source[3] : 255;
        }
    g_object_unref(loader);
    bitmap = std::move(decoded);
    return true;
}
