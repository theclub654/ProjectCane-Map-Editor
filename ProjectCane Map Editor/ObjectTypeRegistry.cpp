#include "ObjectTypeRegistry.h"

#include "BrxDocument.h"

#include <algorithm>

PropertySchemaEntry PropertySchema::Describe(const std::int16_t cid, const std::int16_t eopid) const
{
    const auto definition = EditorOptionDefinitionForCid(cid, eopid);
    PropertySchemaEntry entry;
    entry.eopid = eopid;
    if (const char *name = PropertyNameForCid(cid, eopid); name != nullptr && name[0] != '\0')
        entry.displayName = name;
    else
        entry.displayName = "EOPID " + std::to_string(eopid);
    entry.kind = definition.kind;
    entry.group = OptionGroup(eopid);
    entry.behavior = OptionBehavior(eopid);
    entry.writable = IsEditorWritableOptionForCid(cid, eopid);
    entry.objectReference = IsObjectReferenceOption(eopid);
    if (entry.objectReference)
    {
        if (const char *referenceType = ObjectReferenceTypeName(eopid);
            referenceType != nullptr && referenceType[0] != '\0')
            entry.referenceType = referenceType;
        else
            entry.referenceType = "compatible object";
    }
    return entry;
}

std::vector<PropertySchemaEntry> PropertySchema::ForCid(const std::int16_t cid, const bool writableOnly) const
{
    std::vector<PropertySchemaEntry> result;
    if (cid < 0 || static_cast<std::size_t>(cid) >= CidOptionIds().size())
        return result;
    for (const auto eopid : CidOptionIds()[static_cast<std::size_t>(cid)])
    {
        auto entry = Describe(cid, eopid);
        if (!writableOnly || entry.writable)
            result.push_back(std::move(entry));
    }
    return result;
}

bool PropertySchema::ReferenceAccepts(const PropertySchemaEntry &property, const std::int16_t candidateCid) const
{
    return property.objectReference && ObjectReferenceAccepts(property.eopid, candidateCid);
}

void ObjectTypeRegistry::Rebuild()
{
    types_.clear();
    types_.reserve(CidOptionIds().size());
    for (std::size_t cidIndex = 0; cidIndex < CidOptionIds().size(); ++cidIndex)
    {
        const auto cid = static_cast<std::int16_t>(cidIndex);
        ObjectTypeDescriptor descriptor;
        descriptor.cid = cid;
        descriptor.name = BrxDocument::EditorClassName(cid);
        descriptor.inheritanceSections = PropertySectionsForCid(cid);
        descriptor.legalOptions = CidOptionIds()[cidIndex];
        for (const auto eopid : descriptor.legalOptions)
            if (IsEditorWritableOptionForCid(cid, eopid))
                descriptor.writableOptions.push_back(eopid);
        descriptor.hasAppearance = cid == 2 || CidDerivesFrom(cid, 2);
        descriptor.hasPhysics = cid == 3 || CidDerivesFrom(cid, 3);
        descriptor.hasGameplay = cid == 5 || CidDerivesFrom(cid, 5);
        descriptor.hasSpecializedInspector = descriptor.hasGameplay || cid == 7 || CidDerivesFrom(cid, 7) || cid == 8 ||
                                             CidDerivesFrom(cid, 8) || cid == 31 || CidDerivesFrom(cid, 31) ||
                                             cid == 32 || CidDerivesFrom(cid, 32) || cid == 56 || cid == 78 ||
                                             cid == 109 || (cid >= 110 && cid <= 114) || (cid >= 115 && cid <= 119) ||
                                             (cid >= 131 && cid <= 145) || cid == 146 || cid == 147;
        types_.push_back(std::move(descriptor));
    }
}

const ObjectTypeDescriptor *ObjectTypeRegistry::Find(const std::int16_t cid) const
{
    if (cid < 0 || static_cast<std::size_t>(cid) >= types_.size())
        return nullptr;
    return &types_[static_cast<std::size_t>(cid)];
}
