#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class BrxDocument;

class EditorProject
{
  public:
    // v17 is the first self-contained authored snapshot. v18 adds editor-only
    // object names, v19 adds editor-only VISMAP zone names, and v20 adds
    // editor-only GLOB/SUBGLOB names. It carries every
    // decoded/editor-owned record needed by the structured BRX compiler and
    // no longer needs a matching source BRX to fill omitted arrays.
    // v22 separates EXPLG's embedded context OID from the ordinary splice
    // index stored in other LO headers.
    static constexpr std::uint32_t CurrentVersion = 21;

    [[nodiscard]] static std::filesystem::path DefaultPathFor(const BrxDocument &document);
    static bool Save(const std::filesystem::path &path, const BrxDocument &document, std::string &error);
    static bool Load(const std::filesystem::path &path, BrxDocument &document, std::string &error);
    static bool SaveHierarchy(const std::filesystem::path &path, const BrxDocument &document,
                              std::int32_t rootObjectIndex, std::string &error);
    static bool LoadHierarchy(const std::filesystem::path &path, BrxDocument &destination,
                              std::int32_t parentObjectIndex, std::int32_t &importedRoot, std::string &error);
    static bool ReplaceHierarchy(const std::filesystem::path &path, BrxDocument &destination,
                                 std::int32_t destinationRoot, std::int32_t &replacementRoot, std::string &error);
    static bool SavePortableProxy(const std::filesystem::path &path, const BrxDocument &document,
                                  std::int32_t selectedObjectIndex, std::string &error);
    static bool LoadPortableProxy(const std::filesystem::path &path, BrxDocument &destination,
                                  std::int32_t parentObjectIndex, std::int32_t &importedProxy, std::string &error);
};
