#pragma once
#include "EditorMcpTools.h"

namespace EditorMcp
{
Json FileOperations();
std::filesystem::path OutputPath(const Json &arguments);
std::filesystem::path ValidateFileRequest(const BrxDocument &document, const std::string &operation, const Json &arguments);
Json WriteFile(const BrxDocument &document, const std::string &operation, const Json &arguments);

struct FilePreview
{
    std::string operation, revision, token;
    Json arguments;
    std::chrono::steady_clock::time_point created;
    void Clear() { operation.clear(); revision.clear(); token.clear(); arguments = Json(); }
};
}
