#pragma once
#include "EditorMcpBridge.h"
#include "BrxDocument.h"

class EditorWorkspace;
namespace EditorMcp
{
using Json = nlohmann::json;
std::string Text(const char *value);
std::string Utf8Path(const std::filesystem::path &path);
std::filesystem::path PathFromUtf8(const std::string &text);
Json FloatBits(const float *values, std::size_t count);
Json Property(const BrxPropertyRecord &property, std::int16_t cid);
Json Object(const BrxDocument &document, std::size_t index, bool full);
Json Inspect(const Json &request, const BrxDocument &document, EditorWorkspace &workspace);
}
