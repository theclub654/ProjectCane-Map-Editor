#pragma once

#include <filesystem>
#include <string>

class BrxDocument;

[[nodiscard]] std::string BuildLevelDiagnosticDump(const BrxDocument &document);
bool WriteLevelDiagnosticDump(const BrxDocument &document, const std::filesystem::path &path, std::string &error);
