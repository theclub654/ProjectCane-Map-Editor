#pragma once

#include <filesystem>
#include <string>
#include <vector>

class BrxDocument;
class ValidationSystem;

struct BrxCompilePlan
{
    bool validationPassed = false;
    bool cidLayoutsAudited = false;
    bool writerAvailable = false;
    std::vector<std::string> blockers;
};

class BrxCompiler
{
  public:
    [[nodiscard]] BrxCompilePlan Analyze(const BrxDocument &document, const ValidationSystem &validation) const;
    [[nodiscard]] bool WriteRebuiltPayload(const BrxDocument &document, const ValidationSystem &validation,
                                           const std::filesystem::path &destination, std::string &error) const;
};
