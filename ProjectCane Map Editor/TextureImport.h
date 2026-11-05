#pragma once

#include <filesystem>
#include <cstdint>
#include <span>
#include <string>

struct BrxBitmapRecord;

// Exact PCBRX format values consumed by ProjectCane's texture loader.
enum class TextureImportFormat : std::uint32_t
{
    Rgba8 = 0,
    Bc1 = 1,
    Bc3 = 2
};

[[nodiscard]] bool LoadTextureImage(const std::filesystem::path &path, TextureImportFormat format,
                                   BrxBitmapRecord &bitmap, std::string &error);

[[nodiscard]] bool LoadTextureRgba(const std::filesystem::path &path, BrxBitmapRecord &bitmap, std::string &error);
[[nodiscard]] bool LoadTextureRgbaMemory(std::span<const std::uint8_t> encoded, const std::filesystem::path &sourceName,
                                         BrxBitmapRecord &bitmap, std::string &error);
