#include "ValidationSystem.h"

#include "BrxDocument.h"
#include "EditorOptions.h"
#include "ResolvedScene.h"
#include "SpliceRuntime.h"

#include <algorithm>
#include <unordered_set>

void ValidationSystem::Run(const BrxDocument &document, const ResolvedScene &scene)
{
    issues_.clear();
    std::unordered_set<std::int32_t> validatedSplices;
    for (std::size_t objectIndex = 0; objectIndex < document.Objects().size(); ++objectIndex)
    {
        const auto spliceIndex = document.Objects()[objectIndex].spliceIndex;
        if (spliceIndex < -1 ||
            (spliceIndex >= 0 && static_cast<std::size_t>(spliceIndex) >= document.SpliceModules().size()))
        {
            issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(objectIndex), -1, "Splice",
                               "Object references a missing splice module " + std::to_string(spliceIndex) + "."});
            continue;
        }
        if (spliceIndex >= 0 && validatedSplices.insert(spliceIndex).second)
        {
            const auto analysis = AnalyzeSpliceModule(document, spliceIndex);
            for (const auto &diagnostic : analysis.diagnostics)
                issues_.push_back(
                    {ValidationSeverity::Warning, static_cast<std::int32_t>(objectIndex), -1, "Splice", diagnostic});
        }
    }
    // LoadSwSpliceFromBrx evaluates every module, including those left behind
    // after an object is detached or deleted.
    for (std::size_t moduleIndex = 0; moduleIndex < document.SpliceModules().size(); ++moduleIndex)
    {
        if (!validatedSplices.insert(static_cast<std::int32_t>(moduleIndex)).second)
            continue;
        const auto analysis = AnalyzeSpliceModule(document, static_cast<std::int32_t>(moduleIndex));
        for (const auto &diagnostic : analysis.diagnostics)
            issues_.push_back({ValidationSeverity::Warning, -1, -1, "Splice",
                               "Unattached module " + std::to_string(moduleIndex) + ": " + diagnostic});
    }
    std::vector<std::size_t> lightRecordsPerObject(document.Objects().size(), 0);
    for (std::size_t lightIndex = 0; lightIndex < document.Lights().size(); ++lightIndex)
    {
        const auto &light = document.Lights()[lightIndex];
        if (light.ownerObjectIndex < 0 || static_cast<std::size_t>(light.ownerObjectIndex) >= document.Objects().size())
        {
            issues_.push_back({ValidationSeverity::Error, light.ownerObjectIndex, -1, "LIGHT",
                               "LIGHT record has no valid owning world object."});
            continue;
        }
        ++lightRecordsPerObject[static_cast<std::size_t>(light.ownerObjectIndex)];
        if (document.Objects()[static_cast<std::size_t>(light.ownerObjectIndex)].cid != 81)
            issues_.push_back({ValidationSeverity::Error, light.ownerObjectIndex, -1, "LIGHT",
                               "LIGHT record is owned by an object that is not CID 81."});
        if (lightIndex >= 256)
            issues_.push_back(
                {ValidationSeverity::Warning, light.ownerObjectIndex, -1, "LIGHT capacity",
                 "LIGHT is beyond ProjectCane's 256-slot active table and will not illuminate the scene."});
    }
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.cid == 81 && lightRecordsPerObject[index] == 0)
            issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(index), -1, "LIGHT",
                               "CID 81 object has no decoded LIGHT record."});
        else if (object.cid == 81 && lightRecordsPerObject[index] > 1)
            issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(index), -1, "LIGHT",
                               "CID 81 object owns multiple LIGHT records."});
        if (object.cid == 81 && object.parentIndex >= 0 &&
            static_cast<std::size_t>(object.parentIndex) < document.Objects().size() &&
            !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(object.parentIndex)].cid, 2))
            issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(index), -1, "LIGHT hierarchy",
                               "LIGHT parent is not ALO-compatible."});
        if (object.parentIndex == static_cast<std::int32_t>(index) || object.parentIndex < -1 ||
            object.parentIndex >= static_cast<std::int32_t>(document.Objects().size()))
            issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(index), -1, "Hierarchy",
                               "Object has an invalid or self-referencing parent."});
        std::unordered_set<std::int32_t> ancestors;
        for (auto parent = object.parentIndex; parent >= 0;)
        {
            if (!ancestors.insert(parent).second)
            {
                issues_.push_back({ValidationSeverity::Error, static_cast<std::int32_t>(index), -1, "Hierarchy",
                                   "Object hierarchy contains a cycle."});
                break;
            }
            if (static_cast<std::size_t>(parent) >= document.Objects().size())
                break;
            parent = document.Objects()[static_cast<std::size_t>(parent)].parentIndex;
        }
        for (const auto &optionIssue : ValidateBaseOptions(object.cid, object.properties))
        {
            const auto severity = optionIssue.severity == EditorOptionIssueSeverity::Error ? ValidationSeverity::Error
                                  : optionIssue.severity == EditorOptionIssueSeverity::Warning
                                      ? ValidationSeverity::Warning
                                      : ValidationSeverity::Info;
            issues_.push_back(
                {severity, static_cast<std::int32_t>(index), optionIssue.option, "Properties", optionIssue.message});
        }
    }
    for (const auto &reference : scene.References())
    {
        if (reference.targetObjectIndex < 0)
            issues_.push_back({ValidationSeverity::Error, reference.sourceObjectIndex, reference.eopid, "Reference",
                               "Referenced OID " + std::to_string(reference.targetOid) + " does not exist."});
        else if (!reference.compatible)
            issues_.push_back({ValidationSeverity::Error, reference.sourceObjectIndex, reference.eopid, "Reference",
                               "Referenced object has an incompatible CID."});
    }
}

std::size_t ValidationSystem::ErrorCount() const
{
    return static_cast<std::size_t>(std::count_if(issues_.begin(), issues_.end(), [](const ValidationIssue &issue)
                                                  { return issue.severity == ValidationSeverity::Error; }));
}

std::size_t ValidationSystem::WarningCount() const
{
    return static_cast<std::size_t>(std::count_if(issues_.begin(), issues_.end(), [](const ValidationIssue &issue)
                                                  { return issue.severity == ValidationSeverity::Warning; }));
}
