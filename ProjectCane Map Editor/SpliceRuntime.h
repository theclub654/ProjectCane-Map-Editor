#pragma once

#include "BrxDocument.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SpliceModuleAnalysis
{
    std::vector<std::uint32_t> eventIds;
    // Effective callback definitions after following imports. The pair is
    // (event symbol, defining splice module), in ProjectCane lookup order.
    std::vector<std::pair<std::uint32_t, std::int32_t>> eventOrigins;
    std::vector<std::int32_t> imports;
    std::vector<std::int32_t> builtinCalls;
    std::vector<std::int32_t> optionReads;
    std::vector<std::int32_t> optionWrites;
    std::vector<std::int32_t> basicTokens;
    std::vector<std::string> diagnostics;
    std::uint32_t nodeCount = 0;
    std::uint32_t maximumDepth = 0;
};

struct SpliceAuthoringResult
{
    bool succeeded = false;
    std::int32_t moduleIndex = -1;
    std::string message;
};

struct SpliceSandboxResult
{
    bool completed = false;
    bool limitReached = false;
    bool approximated = false;
    std::uint32_t instructions = 0;
    std::vector<std::string> trace;
    std::vector<std::string> nativeActions;
    std::vector<std::string> diagnostics;
};

struct SpliceSourceDiagnostic
{
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::string message;
};

struct SpliceSourceResult
{
    bool succeeded = false;
    std::uint32_t nodeCount = 0;
    std::uint32_t requiredSymbolCount = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> nodeLocations;
    std::vector<SpliceSourceDiagnostic> diagnostics;
};

[[nodiscard]] const char *SpliceEventName(std::uint32_t eventId);
[[nodiscard]] const char *SpliceBuiltinName(std::int32_t builtinId);
// Canonical, lossless-oriented textual view of the compact CPair graph. It is
// intentionally formatter-only until parser round-trip validation is proven.
[[nodiscard]] std::string FormatSpliceModuleSource(const BrxDocument &document, std::int32_t spliceIndex);
[[nodiscard]] SpliceSourceResult ValidateSpliceModuleSource(const std::string &source, std::int16_t selectedCid = -1,
                                                            std::uint32_t symbolBase = 0,
                                                            const BrxDocument *editorAliases = nullptr);
[[nodiscard]] SpliceSourceResult ApplySpliceModuleSource(BrxDocument &document, std::int32_t spliceIndex,
                                                         const std::string &source, std::int16_t selectedCid = -1);
[[nodiscard]] SpliceSourceResult VerifySpliceModuleSourceRoundTrip(const BrxDocument &document,
                                                                   std::int32_t spliceIndex,
                                                                   std::int16_t selectedCid = -1);
[[nodiscard]] SpliceModuleAnalysis AnalyzeSpliceModule(const BrxDocument &document, std::int32_t spliceIndex);
[[nodiscard]] SpliceSandboxResult EvaluateSpliceEventSandbox(const BrxDocument &document, std::int32_t spliceIndex,
                                                             std::uint32_t eventId, std::int32_t selfObjectIndex,
                                                             std::uint32_t instructionLimit = 10000);
[[nodiscard]] std::size_t SpliceModuleUserCount(const BrxDocument &document, std::int32_t spliceIndex);
[[nodiscard]] SpliceAuthoringResult CreateSpliceModule(BrxDocument &document, std::int32_t objectIndex);
[[nodiscard]] SpliceAuthoringResult AttachSpliceModule(BrxDocument &document, std::int32_t objectIndex,
                                                       std::int32_t spliceIndex);
[[nodiscard]] SpliceAuthoringResult DetachSpliceModule(BrxDocument &document, std::int32_t objectIndex);
[[nodiscard]] SpliceAuthoringResult MakeSpliceModuleUnique(BrxDocument &document, std::int32_t objectIndex);
[[nodiscard]] SpliceAuthoringResult AddSpliceEventStub(BrxDocument &document, std::int32_t objectIndex,
                                                       std::uint32_t eventId);
[[nodiscard]] SpliceAuthoringResult RemoveSpliceEvent(BrxDocument &document, std::int32_t objectIndex,
                                                      std::uint32_t eventId);
[[nodiscard]] SpliceAuthoringResult RepairSpliceImports(BrxDocument &document, std::int32_t spliceIndex);
[[nodiscard]] SpliceAuthoringResult AppendSpliceTopLevelNode(BrxDocument &document, std::int32_t spliceIndex,
                                                             std::uint8_t tag);
[[nodiscard]] SpliceAuthoringResult AddSpliceImport(BrxDocument &document, std::int32_t spliceIndex,
                                                    std::int32_t importedModuleIndex);
[[nodiscard]] SpliceAuthoringResult SetSpliceEventBooleanResult(BrxDocument &document, std::int32_t objectIndex,
                                                                std::uint32_t eventId, bool value);
[[nodiscard]] SpliceAuthoringResult PrependSpliceSetSelfBoolean(BrxDocument &document, std::int32_t objectIndex,
                                                                std::uint32_t eventId, std::int16_t eopid, bool value);
[[nodiscard]] SpliceAuthoringResult PrependSpliceSetSelfInteger(BrxDocument &document, std::int32_t objectIndex,
                                                                std::uint32_t eventId, std::int16_t eopid,
                                                                std::int32_t value);
[[nodiscard]] SpliceAuthoringResult PrependSpliceSetSelfFloat(BrxDocument &document, std::int32_t objectIndex,
                                                              std::uint32_t eventId, std::int16_t eopid, float value);
[[nodiscard]] SpliceAuthoringResult CreateAlternatingWarpTemplate(BrxDocument &document,
                                                                  std::int32_t controllerObjectIndex,
                                                                  std::int32_t firstWarpObjectIndex,
                                                                  std::int32_t secondWarpObjectIndex);
void CompactSpliceNodes(BrxDocument &document);
