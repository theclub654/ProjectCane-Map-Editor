#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
class BrxDocument;
bool ExportMapGlb(const BrxDocument &document, const std::filesystem::path &path, bool bakeLights, std::string &error);
bool ExportGlobGlb(const BrxDocument &document, std::size_t globIndex, const std::filesystem::path &path,
                   std::string &error);
bool ExportGlobsetGlb(const BrxDocument &document, std::size_t globsetIndex, const std::filesystem::path &path,
                      std::string &error);
bool ExportMapTextures(const BrxDocument &document, const std::filesystem::path &folder, std::size_t &written,
                       std::string &error);
