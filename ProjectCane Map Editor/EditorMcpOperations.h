#pragma once
#include "EditorMcpTools.h"
#include <limits>
#include <memory>

namespace EditorMcp
{
int Integer(const Json &arguments, const char *key, int minimum = 0,
            int maximum = (std::numeric_limits<int>::max)());
float Number(const Json &value);
std::uint32_t Mask(const Json &value);
std::filesystem::path InputPath(const Json &arguments);
int Owner(const Json &arguments, const BrxDocument &document);
Json Operations();
Json RunOperation(BrxDocument &document, const std::string &name, const Json &arguments);
Json PreviewChanges(const BrxDocument &before, const BrxDocument &after);

struct OperationPreview
{
    std::unique_ptr<BrxDocument> document;
    std::string revision, token, operation;
    std::chrono::steady_clock::time_point created;
    Json result;
    void Clear() { document.reset(); revision.clear(); token.clear(); operation.clear(); result = Json(); }
};
}
