#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
class BrxDocument;
enum class ModelExportFormat
{
    Glb,
    Obj
};
struct ModelExportOptions
{
    ModelExportFormat format = ModelExportFormat::Glb;
    bool worldSpace = false;
};
bool ExportGlobModel(const BrxDocument &document, std::size_t globIndex, const std::filesystem::path &path,
                     const ModelExportOptions &options, std::string &error);
bool ExportGlobsetModel(const BrxDocument &document, std::size_t globsetIndex, const std::filesystem::path &path,
                        const ModelExportOptions &options, std::string &error);
bool ExportMapGlb(const BrxDocument &document, const std::filesystem::path &path, bool bakeLights, std::string &error);
bool ExportGlobGlb(const BrxDocument &document, std::size_t globIndex, const std::filesystem::path &path,
                   std::string &error);
bool ExportGlobsetGlb(const BrxDocument &document, std::size_t globsetIndex, const std::filesystem::path &path,
                      std::string &error);
bool ExportMapTextures(const BrxDocument &document, const std::filesystem::path &folder, std::size_t &written,
                       std::string &error);
