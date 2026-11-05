#pragma once

#include "BrxDocument.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct EditorOptionDefinition
{
    BrxPropertyKind kind = BrxPropertyKind::Int16;
    std::uint32_t flags = 0;
    std::uint16_t argumentCount = 0;
    bool present = false;
    bool runtimeDefaultable = false;
    std::uint32_t runtimeDefaultBits = 0;
};

struct EditorPropertySection
{
    std::int16_t cid = 0;
    const char *name = "BASIC";
};

enum class EditorOptionGroup : std::uint8_t
{
    Identity,
    Transform,
    Rendering,
    Motion,
    LookAt,
    Effects,
    Audio,
    Interaction,
    Physics,
    Constraints,
    Collision,
    Runtime,
    CharacterAI,
    CharacterAnimation,
    CharacterCombat,
    General
};

enum class EditorOptionBehavior : std::uint8_t
{
    Stored,
    Derived,
    Query,
    Action,
    Expression
};

enum class EditorOptionIssueSeverity : std::uint8_t
{
    Info,
    Warning,
    Error
};

struct EditorOptionIssue
{
    EditorOptionIssueSeverity severity = EditorOptionIssueSeverity::Warning;
    std::int16_t option = -1;
    std::int16_t relatedOption = -1;
    std::string message;
};

[[nodiscard]] const char *PropertyName(std::int16_t eopid);
[[nodiscard]] const char *PropertyNameForCid(std::int16_t cid, std::int16_t eopid);
[[nodiscard]] const char *PropertyKindName(BrxPropertyKind kind);
[[nodiscard]] const std::array<EditorOptionDefinition, 1179> &EditorOptionDefinitions();
[[nodiscard]] EditorOptionDefinition EditorOptionDefinitionForCid(std::int16_t cid, std::int16_t eopid);
[[nodiscard]] bool IsRuntimeDefaultValue(const BrxPropertyRecord &property);
[[nodiscard]] const std::array<std::vector<std::int16_t>, 148> &CidOptionIds();
[[nodiscard]] std::vector<EditorPropertySection> PropertySectionsForCid(std::int16_t cid);
[[nodiscard]] std::int16_t PropertyOwnerCid(std::int16_t objectCid, std::int16_t eopid);
[[nodiscard]] bool CidDerivesFrom(std::int16_t cid, std::int16_t baseCid);
// InitPo enables playable registration, while InitStepguard disables it again.
// Every STEPGUARD-derived class inherits that concrete default unless EOPID 281
// is serialized as an explicit override.
[[nodiscard]] bool PoClassDefaultPlayable(std::int16_t cid);
[[nodiscard]] bool IsEditorWritableOption(std::int16_t eopid);
[[nodiscard]] bool IsEditorWritableOptionForCid(std::int16_t cid, std::int16_t eopid);
// SetUser descriptors backed by ProjectCane Add* functions. Every serialized
// occurrence appends/executes and must retain stream order and duplicates.
[[nodiscard]] bool IsAppendSetUserOption(std::int16_t eopid);
// Object-reference contracts reconstructed from BuildEopids setters and the
// corresponding post-load resolvers. Generic OID values (shader IDs, state
// labels, sound IDs, etc.) deliberately return false here.
[[nodiscard]] bool IsObjectReferenceOption(std::int16_t eopid);
[[nodiscard]] bool IsObjectNameFilterOption(std::int16_t eopid);
[[nodiscard]] bool SoClassFilterCanMatch(std::int16_t cid);
[[nodiscard]] bool ObjectReferenceAccepts(std::int16_t eopid, std::int16_t candidateCid);
[[nodiscard]] const char *ObjectReferenceTypeName(std::int16_t eopid);
[[nodiscard]] EditorOptionGroup OptionGroup(std::int16_t eopid);
[[nodiscard]] const char *OptionGroupName(EditorOptionGroup group);
[[nodiscard]] EditorOptionBehavior OptionBehavior(std::int16_t eopid);
[[nodiscard]] const char *OptionBehaviorName(EditorOptionBehavior behavior);
void ResetPropertyValue(BrxPropertyRecord &property);
[[nodiscard]] bool CanEnableBaseOption(std::int16_t cid, const std::vector<BrxPropertyRecord> &properties,
                                       std::int16_t option, std::string *reason = nullptr);
[[nodiscard]] std::vector<EditorOptionIssue> ValidateBaseOptions(std::int16_t cid,
                                                                 const std::vector<BrxPropertyRecord> &properties);
