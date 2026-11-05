#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

class BrxDocument;

bool ExportRiggedCharacterGlb(const BrxDocument &document, std::int32_t characterRoot,
                              const std::filesystem::path &path, std::string &error);
