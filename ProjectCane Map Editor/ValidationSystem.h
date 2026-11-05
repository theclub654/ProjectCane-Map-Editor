#pragma once

#include <cstdint>
#include <string>
#include <vector>

class BrxDocument;
class ResolvedScene;

enum class ValidationSeverity : std::uint8_t
{
    Info,
    Warning,
    Error
};

struct ValidationIssue
{
    ValidationSeverity severity = ValidationSeverity::Warning;
    std::int32_t objectIndex = -1;
    std::int16_t eopid = -1;
    std::string category;
    std::string message;
};

class ValidationSystem
{
  public:
    void Run(const BrxDocument &document, const ResolvedScene &scene);
    void Clear()
    {
        issues_.clear();
    }
    [[nodiscard]] const std::vector<ValidationIssue> &Issues() const
    {
        return issues_;
    }
    [[nodiscard]] std::size_t ErrorCount() const;
    [[nodiscard]] std::size_t WarningCount() const;

  private:
    std::vector<ValidationIssue> issues_;
};
