#include "TextureImport.h"

#include "BrxDocument.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#else
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <fstream>
#include <iterator>
#endif

#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <array>
#include <utility>

// The project already supplies stb through its ProjectCane include path.
// Keep this implementation private to this translation unit.
#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>

namespace
{
#ifdef _WIN32
bool LoadTextureRgbaImpl(const std::filesystem::path &path, const std::span<const std::uint8_t> encoded,
                         const bool embedded, BrxBitmapRecord &bitmap, std::string &error)
{
    error.clear();
    if (embedded && (encoded.empty() || encoded.size() > (std::numeric_limits<DWORD>::max)()))
    {
        error = "Embedded image is empty or exceeds the Windows image decoder's size limit.";
        return false;
    }
    // IWICStream takes mutable backing memory. Keep a private buffer alive
    // through decoder release instead of retaining pointers into a freed FBX.
    std::vector<std::uint8_t> streamBytes;
    if (embedded)
        streamBytes.assign(encoded.begin(), encoded.end());
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initializeResult);
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    IWICStream *stream = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (embedded)
    {
        if (SUCCEEDED(result))
            result = factory->CreateStream(&stream);
        if (SUCCEEDED(result))
            result = stream->InitializeFromMemory(streamBytes.data(), static_cast<DWORD>(streamBytes.size()));
        if (SUCCEEDED(result))
            result = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    else if (SUCCEEDED(result))
        result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                    &decoder);
    if (SUCCEEDED(result))
        result = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(result))
        result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result))
        result = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
    UINT width = 0, height = 0;
    if (SUCCEEDED(result))
        result = converter->GetSize(&width, &height);
    if (SUCCEEDED(result) && (width == 0 || height == 0 || width > 32768u || height > 32768u))
    {
        error = "Texture dimensions must be between 1 and 32768 pixels for ProjectCane.";
        result = E_INVALIDARG;
    }
    if (SUCCEEDED(result) && width != 0 && height != 0)
    {
        const std::uint64_t byteCount = static_cast<std::uint64_t>(width) * height * 4;
        if (byteCount > std::numeric_limits<std::uint32_t>::max())
            result = E_OUTOFMEMORY;
        else
        {
            bitmap = {};
            bitmap.editorName = path.stem().string();
            bitmap.width = width;
            bitmap.height = height;
            bitmap.mipCount = 1;
            bitmap.pixelBytes = static_cast<std::uint32_t>(byteCount);
            bitmap.textureFlags = 0;
            bitmap.textureFormat = 0;
            bitmap.pixels.resize(static_cast<std::size_t>(byteCount));
            result = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), bitmap.pixels.data());
        }
    }
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (stream)
        stream->Release();
    if (factory)
        factory->Release();
    if (uninitialize)
        CoUninitialize();
    if (FAILED(result))
    {
        if (error.empty())
        {
            std::ostringstream code;
            code << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << static_cast<unsigned>(result);
            error = "Windows image decoder could not read " + std::string(embedded ? "embedded image " : "") +
                    path.string() + " (HRESULT 0x" + code.str() + ").";
        }
        return false;
    }
    error.clear();
    return true;
}
#else
#include "TextureImportLinux.inc"
#endif
}

bool LoadTextureImage(const std::filesystem::path &path, const TextureImportFormat format,
                      BrxBitmapRecord &bitmap, std::string &error)
{
    error.clear();
    if (format != TextureImportFormat::Rgba8 && format != TextureImportFormat::Bc1 &&
        format != TextureImportFormat::Bc3)
    {
        error = "Unsupported texture import format.";
        return false;
    }
    BrxBitmapRecord decoded;
    if (!LoadTextureRgba(path, decoded, error))
        return false;
    if (format != TextureImportFormat::Rgba8)
    {
        // Retail uploads BC1 as RGB, not RGBA/one-bit-alpha DXT1. Reject
        // transparency explicitly instead of silently dropping the alpha.
        if (format == TextureImportFormat::Bc1)
            for (std::size_t alpha = 3; alpha < decoded.pixels.size(); alpha += 4)
                if (decoded.pixels[alpha] != 255)
                {
                    error = "BC1 is opaque in ProjectCane. This image has transparency; choose BC3 or RGBA8.";
                    return false;
                }
        const std::size_t blockBytes = format == TextureImportFormat::Bc1 ? 8u : 16u;
        const std::size_t blockColumns = (decoded.width + 3u) / 4u;
        const std::size_t blockRows = (decoded.height + 3u) / 4u;
        std::vector<std::uint8_t> compressed(blockColumns * blockRows * blockBytes);
        std::size_t destination = 0;
        for (std::uint32_t blockY = 0; blockY < decoded.height; blockY += 4u)
            for (std::uint32_t blockX = 0; blockX < decoded.width; blockX += 4u)
            {
                alignas(4) std::array<unsigned char, 64> block{};
                for (std::uint32_t y = 0; y < 4u; ++y)
                    for (std::uint32_t x = 0; x < 4u; ++x)
                    {
                        // Replicate edge texels for partial 4x4 blocks. The
                        // original dimensions remain in the BRX descriptor.
                        const auto sourceX = (std::min)(blockX + x, decoded.width - 1u);
                        const auto sourceY = (std::min)(blockY + y, decoded.height - 1u);
                        const auto source = (static_cast<std::size_t>(sourceY) * decoded.width + sourceX) * 4u;
                        std::copy_n(decoded.pixels.data() + source, 4u, block.data() + (y * 4u + x) * 4u);
                    }
                stb_compress_dxt_block(compressed.data() + destination, block.data(),
                                       format == TextureImportFormat::Bc3 ? 1 : 0, STB_DXT_HIGHQUAL);
                destination += blockBytes;
            }
        decoded.pixels = std::move(compressed);
        decoded.pixelBytes = static_cast<std::uint32_t>(decoded.pixels.size());
        decoded.textureFormat = static_cast<std::uint32_t>(format);
    }
    bitmap = std::move(decoded);
    return true;
}

bool LoadTextureRgba(const std::filesystem::path &path, BrxBitmapRecord &bitmap, std::string &error)
{
    return LoadTextureRgbaImpl(path, {}, false, bitmap, error);
}

bool LoadTextureRgbaMemory(const std::span<const std::uint8_t> encoded, const std::filesystem::path &sourceName,
                           BrxBitmapRecord &bitmap, std::string &error)
{
    return LoadTextureRgbaImpl(sourceName, encoded, true, bitmap, error);
}
