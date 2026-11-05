#pragma once

#include "EditorOptions.h"

#include <cstdint>
#include <string>
#include <vector>

struct ObjectTypeDescriptor
{
    std::int16_t cid = -1;
    std::string name;
    std::vector<EditorPropertySection> inheritanceSections;
    std::vector<std::int16_t> legalOptions;
    std::vector<std::int16_t> writableOptions;
    bool hasAppearance = false;
    bool hasPhysics = false;
    bool hasGameplay = false;
    bool hasSpecializedInspector = false;
};

struct PropertySchemaEntry
{
    std::int16_t eopid = -1;
    std::string displayName;
    BrxPropertyKind kind = BrxPropertyKind::Int16;
    EditorOptionGroup group = EditorOptionGroup::General;
    EditorOptionBehavior behavior = EditorOptionBehavior::Stored;
    bool writable = false;
    bool objectReference = false;
    std::string referenceType;
};

class PropertySchema
{
  public:
    [[nodiscard]] PropertySchemaEntry Describe(std::int16_t cid, std::int16_t eopid) const;
    [[nodiscard]] std::vector<PropertySchemaEntry> ForCid(std::int16_t cid, bool writableOnly = true) const;
    [[nodiscard]] bool ReferenceAccepts(const PropertySchemaEntry &property, std::int16_t candidateCid) const;
};

class ObjectTypeRegistry
{
  public:
    void Rebuild();
    [[nodiscard]] const ObjectTypeDescriptor *Find(std::int16_t cid) const;
    [[nodiscard]] const std::vector<ObjectTypeDescriptor> &Types() const
    {
        return types_;
    }
    [[nodiscard]] const PropertySchema &Properties() const
    {
        return propertySchema_;
    }

  private:
    std::vector<ObjectTypeDescriptor> types_;
    PropertySchema propertySchema_;
};
