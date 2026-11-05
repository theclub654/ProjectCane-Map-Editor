#include "EmitterPreviewRuntime.h"
#include "ObjectTypeRegistry.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace
{
const BrxPropertyRecord *Property(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                    [=](const BrxPropertyRecord &value) { return value.eopid == eopid; });
    return found == object.properties.end() ? nullptr : &*found;
}

float Scalar(const BrxObjectRecord &object, const std::int16_t eopid, const float fallback)
{
    const auto *value = Property(object, eopid);
    return value && std::isfinite(value->floatValue[0]) ? value->floatValue[0] : fallback;
}

glm::vec2 Range(const BrxObjectRecord &object, const std::int16_t eopid, const glm::vec2 fallback)
{
    const auto *value = Property(object, eopid);
    return value && std::isfinite(value->floatValue[0]) && std::isfinite(value->floatValue[1])
               ? glm::vec2(value->floatValue[0], value->floatValue[1])
               : fallback;
}

glm::vec3 Vector(const BrxObjectRecord &object, const std::int16_t eopid, const glm::vec3 fallback = {})
{
    const auto *value = Property(object, eopid);
    return value && std::isfinite(value->floatValue[0]) && std::isfinite(value->floatValue[1]) &&
                   std::isfinite(value->floatValue[2])
               ? glm::vec3(value->floatValue[0], value->floatValue[1], value->floatValue[2])
               : fallback;
}

std::int32_t Integer(const BrxObjectRecord &object, const std::int16_t eopid, const std::int32_t fallback)
{
    const auto *value = Property(object, eopid);
    if (!value)
        return fallback;
    if (value->kind == BrxPropertyKind::Bool)
        return value->boolValue ? 1 : 0;
    if (value->kind == BrxPropertyKind::Int16 || value->kind == BrxPropertyKind::Int32 ||
        value->kind == BrxPropertyKind::TypedInt16)
        return value->intValue;
    if (value->kind == BrxPropertyKind::UInt32)
        return static_cast<std::int32_t>(value->uintValue);
    return fallback;
}

const BrxObjectRecord *FindObject(const BrxDocument &document, const std::int16_t oid, const BrxObjectRecord &owner)
{
    if (oid < 0)
        return nullptr;
    const auto ownerIndex = static_cast<std::int32_t>(&owner - document.Objects().data());
    const BrxObjectRecord *fallback = nullptr;
    const BrxObjectRecord *spliceFallback = nullptr;
    const BrxObjectRecord *proxyFallback = nullptr;
    const BrxObjectRecord *globalFallback = nullptr;
    const BrxObjectRecord *globalSpliceFallback = nullptr;
    for (const auto &candidate : document.Objects())
    {
        if (candidate.oid != oid)
            continue;
        // FindObject(ProjectCane) searches from the requesting hierarchy/PXR
        // context. LoadProxyFromBrx also reparents resolved source children to
        // their runtime owner. Prefer that exact relationship before duplicate
        // OID proxy fallbacks; otherwise a later PROXY placement can bind the
        // first source instance's SHAPE (the water-tower aura failure).
        if (candidate.parentIndex == ownerIndex)
            return &candidate;
        if (!fallback)
            fallback = &candidate;
        if (!spliceFallback && candidate.spliceIndex == owner.spliceIndex)
            spliceFallback = &candidate;
        if (!globalFallback && owner.proxyRecordIndex < 0 && candidate.proxyRecordIndex < 0)
            globalFallback = &candidate;
        if (!globalSpliceFallback && owner.proxyRecordIndex < 0 && candidate.proxyRecordIndex < 0 &&
            candidate.spliceIndex == owner.spliceIndex)
            globalSpliceFallback = &candidate;
        if (owner.proxyRecordIndex >= 0 && candidate.proxyRecordIndex == owner.proxyRecordIndex)
        {
            if (!proxyFallback)
                proxyFallback = &candidate;
            if (candidate.proxyRootIndex == owner.proxyRootIndex)
                return &candidate;
        }
    }
    if (proxyFallback)
        return proxyFallback;
    if (owner.proxyRecordIndex < 0 && globalFallback)
    {
        if (globalSpliceFallback)
            return globalSpliceFallback;
        return globalFallback;
    }
    return spliceFallback ? spliceFallback : fallback;
}

std::int32_t FindObjectIndex(const BrxDocument &document, const std::int16_t oid, const BrxObjectRecord &owner)
{
    const auto *found = FindObject(document, oid, owner);
    return found == nullptr ? -1 : static_cast<std::int32_t>(found - document.Objects().data());
}

std::uint64_t EmitterGroupKey(const BrxObjectRecord &object)
{
    const auto oid = static_cast<std::int16_t>(Integer(object, 361, -1));
    if (oid < 0)
        return 0;
    // EnsureEmitterGroup looks up/creates the synthetic master with
    // PloFindSwChild(psw, oidGroup, nullptr).  Its identity is therefore the
    // authored group OID across the complete world, not the PROXY instance
    // containing a member.  Splitting this key by proxy context prevents the
    // authoritative member template (including its curve) from reaching the
    // other transforms submitted by DrawBlipg.
    return 1ull | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(oid)) << 1);
}

std::int32_t ResolveEmitterConfiguration(const BrxDocument &document, const std::int32_t placementIndex)
{
    if (placementIndex < 0 || static_cast<std::size_t>(placementIndex) >= document.Objects().size())
        return placementIndex;
    const auto &placement = document.Objects()[static_cast<std::size_t>(placementIndex)];
    const std::uint64_t wantedKey = EmitterGroupKey(placement);
    if (wantedKey == 0)
        return placementIndex;

    std::int32_t first = -1;
    std::int32_t firstViable = -1;
    for (std::size_t offset = 0; offset < document.Objects().size(); ++offset)
    {
        const auto &candidate = document.Objects()[offset];
        if (!CidDerivesFrom(candidate.cid, 80) || EmitterGroupKey(candidate) != wantedKey)
            continue;
        const auto candidateIndex = static_cast<std::int32_t>(offset);
        if (first < 0)
            first = candidateIndex;
        if (firstViable < 0 && Integer(candidate, 348, -1) != 0 && Integer(candidate, 355, 1) != 0)
            firstViable = candidateIndex;
    }
    return firstViable >= 0 ? firstViable : (first >= 0 ? first : placementIndex);
}

float RandomRange(std::mt19937 &random, glm::vec2 range)
{
    if (range.x > range.y)
        std::swap(range.x, range.y);
    return std::uniform_real_distribution<float>(range.x, range.y)(random);
}

float NormalizeAngle(const float angle)
{
    return angle < -glm::pi<float>() || angle > glm::pi<float>()
               ? std::fmod(std::fmod(angle + glm::pi<float>(), glm::two_pi<float>()) + glm::two_pi<float>(),
                           glm::two_pi<float>()) -
                     glm::pi<float>()
               : angle;
}

glm::vec3 SafeNormal(const glm::vec3 value)
{
    return glm::dot(value, value) < 0.0001f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(value);
}

std::int16_t StockRenderOidForRip(const int ripType)
{
    // LoadSwObjectsFromBrx maps stock OIDs 0x0c..0x2a directly to
    // psw->aploStock[0..30]. These RIP initializers install the listed stock
    // render object before an explicit EMITTER render object can override it.
    switch (ripType)
    {
    case 2: // Droplet
    case 15:
        return 13; // Debris: stock[1]
    case 4:
        return 14; // Ripple: stock[2]
    case 5:
        return 15; // Puff: stock[3]
    case 8:
        return 18; // Flake: stock[6]
    case 10:
        return 17; // Burst: stock[5]
    case 12:
        return 20; // Fireball: stock[8]
    case 13:
        return 21; // Smoke cloud: stock[9]
    default:
        return -1;
    }
}

void NormalBasis(const glm::vec3 normal, glm::vec3 &tangent, glm::vec3 &bitangent)
{
    const glm::vec3 n = SafeNormal(normal);
    const glm::vec3 helper = std::abs(n.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    tangent = SafeNormal(glm::cross(helper, n));
    bitangent = SafeNormal(glm::cross(n, tangent));
}

glm::vec3 IntegrateAcceleration(const glm::vec3 velocity, const glm::vec3 acceleration, const float damping,
                                glm::vec3 curlNormal, const float curlSpeed, const float seconds)
{
    curlNormal = SafeNormal(curlNormal);
    const glm::mat3 rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), curlSpeed * seconds, curlNormal));
    // ProjectCane's CalculateEmitdvMatrix returns R + (R * D) * dt, where
    // D has damping on the diagonal and acceleration in its affine column.
    return rotation * (velocity + (velocity * damping + acceleration) * seconds);
}

void AdvanceSpline(glm::vec3 &position, glm::vec3 &velocity, const glm::vec3 targetPosition,
                   const glm::vec3 targetVelocity, const float remaining, const float seconds)
{
    if (remaining <= 0.000001f)
    {
        position = targetPosition;
        velocity = targetVelocity;
        return;
    }
    const float t = std::clamp(seconds / remaining, 0.0f, 1.0f);
    const float u = 1.0f - t;
    const glm::vec3 start = position;
    const glm::vec3 startVelocity = velocity;
    position = start * (u * u * u + 3.0f * t * u * u) + startVelocity * (t * u * u * remaining) -
               targetVelocity * (t * t * u * remaining) + targetPosition * (t * t * t + 3.0f * t * t * u);
    const glm::vec3 delta = targetPosition - start;
    velocity = delta * ((t * (6.0f - 6.0f * t)) / remaining) + startVelocity * (1.0f - t * (4.0f - 3.0f * t)) +
               targetVelocity * (t * (3.0f * t - 2.0f));
}
}

void EmitterPreviewRuntime::SetSeed(const std::uint32_t seed)
{
    if (seed_ == seed)
        return;
    seed_ = seed;
    particles_.clear();
    groups_.clear();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::SetScope(const EmitterPreviewScope scope)
{
    if (scope_ == scope)
        return;
    scope_ = scope;
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::InvalidateSnapshot()
{
    particles_.clear();
    groups_.clear();
    resolvedWorld_.clear();
    linearVelocity_.clear();
    angularVelocity_.clear();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::SetSpatialContext(std::vector<glm::mat4> resolvedWorld,
                                              std::vector<glm::vec3> linearVelocity,
                                              std::vector<glm::vec3> angularVelocity, const glm::mat4 &cameraWorld)
{
    resolvedWorld_ = std::move(resolvedWorld);
    linearVelocity_ = std::move(linearVelocity);
    angularVelocity_ = std::move(angularVelocity);
    cameraWorld_ = cameraWorld;
}

void EmitterPreviewRuntime::Restart()
{
    if (scope_ == EmitterPreviewScope::Selected && selectedEmitter_ >= 0)
    {
        groups_.erase(selectedEmitter_);
        RebuildParticleView();
        snapshotDirty_ = true;
        return;
    }
    particles_.clear();
    groups_.clear();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::Restart(const std::int32_t objectIndex)
{
    if (objectIndex < 0)
        return;
    groups_.erase(objectIndex);
    RebuildParticleView();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::RestartAll()
{
    particles_.clear();
    groups_.clear();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::ResetSession()
{
    particles_.clear();
    groups_.clear();
    activation_.clear();
    timedPauseRemaining_.clear();
    resolvedWorld_.clear();
    linearVelocity_.clear();
    angularVelocity_.clear();
    playing_ = false;
    scope_ = EmitterPreviewScope::Visible;
    selectedEmitter_ = -1;
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::Stop()
{
    playing_ = false;
    // Keep the current world snapshots.  Stopping playback is an editor
    // transport operation, not a visibility operation.
    for (auto &[objectIndex, group] : groups_)
        group.staticSnapshotBuilt = !group.particles.empty();
    RebuildParticleView();
}

void EmitterPreviewRuntime::SetActivation(const std::int32_t objectIndex, const EmitterPreviewActivation activation)
{
    if (objectIndex < 0)
        return;
    timedPauseRemaining_.erase(objectIndex);
    if (activation == EmitterPreviewActivation::Authored)
        activation_.erase(objectIndex);
    else
        activation_[objectIndex] = activation;
    if (activation == EmitterPreviewActivation::Disabled)
        groups_.erase(objectIndex);
    else if (activation == EmitterPreviewActivation::Enabled)
    {
        auto &group = groups_[objectIndex];
        group.pauseRemaining = 0.0f;
        group.initialized = false;
    }
    RebuildParticleView();
    snapshotDirty_ = true;
}

void EmitterPreviewRuntime::PauseFor(const std::int32_t objectIndex, const float seconds)
{
    if (objectIndex < 0 || !std::isfinite(seconds))
        return;
    if (seconds <= 0.0f)
    {
        SetActivation(objectIndex, EmitterPreviewActivation::Enabled);
        return;
    }
    activation_[objectIndex] = EmitterPreviewActivation::Paused;
    timedPauseRemaining_[objectIndex] = seconds;
}

EmitterPreviewActivation EmitterPreviewRuntime::Activation(const std::int32_t objectIndex) const
{
    const auto found = activation_.find(objectIndex);
    return found == activation_.end() ? EmitterPreviewActivation::Authored : found->second;
}

float EmitterPreviewRuntime::TimedPauseRemaining(const std::int32_t objectIndex) const
{
    const auto found = timedPauseRemaining_.find(objectIndex);
    return found == timedPauseRemaining_.end() ? 0.0f : found->second;
}

void EmitterPreviewRuntime::Step(const BrxDocument &document, const std::int32_t objectIndex, const float seconds)
{
    if (objectIndex < 0)
        return;
    auto &group = groups_[objectIndex];
    if (group.particles.empty() && group.emittedCount == 0)
        group.random.seed(seed_ ^ static_cast<std::uint32_t>(objectIndex * 0x9e3779b9u));
    Simulate(document, objectIndex, seconds, group, ResolveEmitterConfiguration(document, objectIndex));
    RebuildParticleView();
}

void EmitterPreviewRuntime::Update(const BrxDocument &document, const std::vector<std::int32_t> &objectIndices,
                                   const float seconds)
{
    if (!playing_ && !snapshotDirty_)
        return;
    // Preserve the detailed single-emitter audition while bounding the full
    // scene preview. Without a global authoring budget, retail levels with
    // hundreds of placements can allocate 4096 particles for every EMITTER.
    constexpr std::size_t sceneParticleBudget = 65536;
    particleLimitPerEmitter_ = objectIndices.empty()
                                   ? 64
                                   : std::clamp(sceneParticleBudget / objectIndices.size(),
                                                static_cast<std::size_t>(64), static_cast<std::size_t>(4096));
    // Group members are placement records. ProjectCane creates one synthetic
    // master from the first member with a given group OID in the world, emits one
    // shared particle buffer from that template, then DrawBlipg submits it at
    // every member transform. A placement member can therefore author a zero
    // count and still display the master effect (notably the burning ship).
    std::unordered_map<std::uint64_t, std::int32_t> groupTemplates;
    for (std::size_t candidateOffset = 0; candidateOffset < document.Objects().size(); ++candidateOffset)
    {
        const auto candidateIndex = static_cast<std::int32_t>(candidateOffset);
        const auto &candidate = document.Objects()[candidateOffset];
        if (!CidDerivesFrom(candidate.cid, 80))
            continue;
        const std::uint64_t key = EmitterGroupKey(candidate);
        if (key == 0)
            continue;
        const auto [found, inserted] = groupTemplates.emplace(key, candidateIndex);
        if (!inserted)
        {
            const auto &current = document.Objects()[static_cast<std::size_t>(found->second)];
            const bool currentCanEmit = Integer(current, 348, -1) != 0 && Integer(current, 355, 1) != 0;
            const bool candidateCanEmit = Integer(candidate, 348, -1) != 0 && Integer(candidate, 355, 1) != 0;
            // Loader/PROXY expansion order is not ProjectCane's SW update
            // order. If a zero-count placement marker appears first, retain
            // the first viable emitting member as the synthetic template.
            if (!currentCanEmit && candidateCanEmit)
                found->second = candidateIndex;
        }
    }
    std::unordered_map<std::int32_t, bool> active;
    active.reserve(objectIndices.size());
    for (const std::int32_t objectIndex : objectIndices)
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= document.Objects().size() ||
            !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(objectIndex)].cid, 80))
            continue;
        active.emplace(objectIndex, true);
        const auto &placementObject = document.Objects()[static_cast<std::size_t>(objectIndex)];
        const std::uint64_t key = EmitterGroupKey(placementObject);
        const auto templateIt = key == 0 ? groupTemplates.end() : groupTemplates.find(key);
        const std::int32_t configurationObjectIndex =
            templateIt == groupTemplates.end() ? objectIndex : templateIt->second;
        const bool inserted = !groups_.contains(objectIndex);
        auto &group = groups_[objectIndex];
        if (group.particles.size() > particleLimitPerEmitter_)
            group.particles.resize(particleLimitPerEmitter_);
        if (inserted)
        {
            // Retail grouped emitters own one synthetic particle buffer and
            // submit it at every member. Give every editor-side placement of
            // that logical group the same deterministic stream so their
            // frozen copies reproduce the shared pattern instead of looking
            // like unrelated effects.
            const std::uint64_t randomIdentity =
                key == 0 ? static_cast<std::uint64_t>(static_cast<std::uint32_t>(objectIndex)) : key;
            group.random.seed(seed_ ^ static_cast<std::uint32_t>(randomIdentity) ^
                              static_cast<std::uint32_t>(randomIdentity >> 32));
        }
        const bool authoredAutoPause =
            Integer(document.Objects()[static_cast<std::size_t>(configurationObjectIndex)], 349, 0) != 0 &&
            Activation(objectIndex) == EmitterPreviewActivation::Authored;
        // Playback is global only in Visible/All authoring modes.  Selected
        // audition must be enforced here as well as by the caller's filtered
        // object list: grouped emitters and PROXY-expanded placements can
        // legitimately share one configuration template, but that must not
        // make every placement advance when the user presses Play selected.
        const bool advanceThisEmitter =
            playing_ && (scope_ != EmitterPreviewScope::Selected || objectIndex == selectedEmitter_);
        if (advanceThisEmitter && !authoredAutoPause)
        {
            group.staticSnapshotBuilt = false;
            Simulate(document, objectIndex, seconds * speed_, group, configurationObjectIndex);
            if (auto timed = timedPauseRemaining_.find(objectIndex); timed != timedPauseRemaining_.end())
            {
                timed->second -= seconds * speed_;
                if (timed->second <= 0.0f)
                {
                    timedPauseRemaining_.erase(timed);
                    activation_[objectIndex] = EmitterPreviewActivation::Enabled;
                }
            }
        }
        else if (!group.staticSnapshotBuilt && Activation(objectIndex) != EmitterPreviewActivation::Disabled)
        {
            // Authoring mode still needs a representative steady-state frame.
            // Stopping as soon as the first particle appeared froze continuous
            // fire after roughly one spawn, while ProjectCane displays the
            // accumulated overlap of many generations. Warm a bounded six
            // seconds and retain the densest frame; this also preserves finite
            // bursts that have expired by the end of the warm-up window.
            const auto previousActivation = Activation(objectIndex);
            activation_[objectIndex] = EmitterPreviewActivation::Enabled;
            GroupState densest = group;
            for (int tick = 0; tick < 60; ++tick)
            {
                Simulate(document, objectIndex, 0.1f, group, configurationObjectIndex);
                if (group.particles.size() > densest.particles.size())
                    densest = group;
            }
            group = std::move(densest);
            if (previousActivation == EmitterPreviewActivation::Authored)
                activation_.erase(objectIndex);
            else
                activation_[objectIndex] = previousActivation;
            group.staticSnapshotBuilt = true;
        }
    }
    // Selected playback is layered over the frozen world preview. Do not
    // discard inactive groups merely because only one emitter is advancing.
    if (scope_ != EmitterPreviewScope::Selected)
        std::erase_if(groups_, [&](const auto &entry) { return !active.contains(entry.first); });
    RebuildParticleView();
    snapshotDirty_ = false;
}

void EmitterPreviewRuntime::Simulate(const BrxDocument &document, const std::int32_t objectIndex, float seconds,
                                     GroupState &group, const std::int32_t configurationObjectIndex)
{
    if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= document.Objects().size())
        return;
    const auto &placementObject = document.Objects()[static_cast<std::size_t>(objectIndex)];
    if (!CidDerivesFrom(placementObject.cid, 80))
        return;
    const std::int32_t configurationIndex =
        configurationObjectIndex >= 0 && static_cast<std::size_t>(configurationObjectIndex) < document.Objects().size()
            ? configurationObjectIndex
            : objectIndex;
    const auto &object = document.Objects()[static_cast<std::size_t>(configurationIndex)];
    seconds = std::clamp(seconds, 0.0f, 0.1f);

    const auto referenceOid = static_cast<std::int16_t>(Integer(object, 357, -1));
    const auto referenceIndex = FindObjectIndex(document, referenceOid, placementObject);
    const auto *referenceObject =
        referenceIndex >= 0 ? &document.Objects()[static_cast<std::size_t>(referenceIndex)] : nullptr;
    const auto resolvedTransform = [&](const std::int32_t index)
    {
        if (index >= 0 && static_cast<std::size_t>(index) < resolvedWorld_.size())
            return resolvedWorld_[static_cast<std::size_t>(index)];
        return index >= 0 && static_cast<std::size_t>(index) < document.Objects().size()
                   ? glm::make_mat4(document.Objects()[static_cast<std::size_t>(index)].worldTransform)
                   : glm::mat4(1.0f);
    };
    glm::mat4 world = resolvedTransform(referenceIndex >= 0 ? referenceIndex : objectIndex);
    const bool groupedPlacement = Integer(placementObject, 361, -1) >= 0;
    if (groupedPlacement && referenceIndex < 0)
    {
        // DrawBlipg places a shared local particle buffer at each member's
        // world position using the member parent's world basis. It does not
        // apply the member's own authored rotation. Matching that rule keeps
        // grouped flames and smoke upright at every placement.
        const glm::mat4 placementWorld = resolvedTransform(objectIndex);
        const glm::mat4 parentWorld = resolvedTransform(placementObject.parentIndex);
        world = glm::mat4(1.0f);
        world[0] = glm::vec4(glm::vec3(parentWorld[0]), 0.0f);
        world[1] = glm::vec4(glm::vec3(parentWorld[1]), 0.0f);
        world[2] = glm::vec4(glm::vec3(parentWorld[2]), 0.0f);
        world[3] = glm::vec4(glm::vec3(placementWorld[3]), 1.0f);
    }

    // SetEmitterOidShape replaces the inline CRV and uses the SHAPE parent's
    // transform as EMITO::paloReference. Resolve the OID in the same PROXY /
    // splice context as the other EMITTER object references.
    const auto shapeOid = static_cast<std::int16_t>(Integer(object, 370, -1));
    auto shapeObjectIndex = FindObjectIndex(document, shapeOid, placementObject);
    const BrxShapePathRecord *externalShape = nullptr;
    if (shapeObjectIndex >= 0)
    {
        const auto found =
            std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                         [&](const BrxShapePathRecord &shape) { return shape.ownerObjectIndex == shapeObjectIndex; });
        if (found != document.ShapePaths().end())
        {
            externalShape = &*found;
            const auto parentIndex = document.Objects()[static_cast<std::size_t>(shapeObjectIndex)].parentIndex;
            world = resolvedTransform(parentIndex);
        }
    }
    const glm::vec3 acceleration = Vector(object, 379, {0.0f, 0.0f, -1500.0f});
    // EMITPK_Nil/default resolves to BLIP unless a concrete RIP type was
    // authored. Treating -1 as "nonzero" incorrectly turned default emitters
    // into invisible model particles.
    const bool modelParticle = Integer(object, 391, 0) == 1 || Integer(object, 392, -1) != -1;
    const glm::vec3 scaleClq =
        modelParticle ? Vector(object, 395, {1.0f, 0.0f, 0.0f}) : Vector(object, 399, {1.0f, 0.0f, 0.0f});
    const glm::vec3 alphaClq = Vector(object, 390, {1.0f, 0.0f, -1.0f});
    const glm::vec3 colorClq = Vector(object, 401, {0.0f, 0.0f, 0.0f});
    const glm::vec3 textureClq = Vector(object, 400, {0.0f, 0.0f, 0.0f});
    const float damping = Scalar(object, 380, -1.0f);
    const float curlSpeed = Scalar(object, 381, 0.0f);
    glm::vec3 curlNormal = Vector(object, 382, {0.0f, 0.0f, 1.0f});
    const int curlSpace = (std::max)(0, Integer(object, 383, 0));
    if (curlSpace == 1 && referenceObject)
        curlNormal = glm::mat3(world) * curlNormal;
    const int motionKind = (std::max)(0, Integer(object, 405, 0)); // BLIPMK_Accel by default.
    const int movementKind = (std::max)(0, Integer(object, 407, 0));
    for (auto &particle : group.particles)
    {
        particle.age += seconds;
        particle.previousPosition = particle.position;
        const float u = particle.age / (std::max)(particle.lifetime, 0.0001f);
        particle.scale = particle.baseScale * (scaleClq.x + u * (scaleClq.y + u * scaleClq.z));
        const float colorU =
            particle.interpolateColor ? std::clamp(colorClq.x + u * (colorClq.y + u * colorClq.z), 0.0f, 1.0f) : 0.0f;
        particle.color = glm::mix(particle.colorStart, particle.colorEnd, colorU);
        particle.color.a *= std::clamp(alphaClq.x + u * (alphaClq.y + u * alphaClq.z), 0.0f, 1.0f);
        float textureValue = textureClq.x + u * (textureClq.y + u * textureClq.z);
        if (Integer(object, 402, 0) != 0 && group.textureFrameCount > 1)
            textureValue = u * static_cast<float>(group.textureFrameCount);
        if (const float shaderLoop = Scalar(object, 403, 0.0f); std::abs(shaderLoop) > 0.0001f)
            textureValue = particle.age * static_cast<float>(group.textureFrameCount) / shaderLoop;
        const int textureTableCount = (std::max)(1, static_cast<int>(group.textureFrameCount));
        int packetFrameSigned = static_cast<int>(particle.initialTextureFrame) + static_cast<int>(textureValue);
        packetFrameSigned = ((packetFrameSigned % textureTableCount) + textureTableCount) % textureTableCount;
        const std::uint16_t packetFrame = static_cast<std::uint16_t>(packetFrameSigned);
        particle.textureFrame =
            packetFrame < group.textureFrameMap.size() ? group.textureFrameMap[packetFrame] : packetFrame;
        if (motionKind == 0)
            particle.velocity = IntegrateAcceleration(particle.velocity, acceleration, particle.velocityDamping,
                                                      curlNormal, curlSpeed, seconds);
        if (motionKind == 2 && particle.hasSplineTarget)
            AdvanceSpline(particle.position, particle.velocity, particle.splineTargetPosition,
                          particle.splineTargetVelocity,
                          (std::max)(particle.lifetime - particle.age + seconds, seconds), seconds);
        else
            particle.position += particle.velocity * seconds;
        if (movementKind == 1 && glm::dot(particle.velocity, particle.velocity) > 0.0001f)
            particle.roll = std::atan2(glm::dot(particle.velocity, glm::vec3(cameraWorld_[1])),
                                       glm::dot(particle.velocity, glm::vec3(cameraWorld_[0])));
        if (movementKind == 2)
            particle.roll = NormalizeAngle(particle.roll + particle.angularSpeed * seconds);
        if (particle.modelParticle)
        {
            // ProjectRipTransform rotates the complete RIP basis, not a
            // billboard-style Z roll. Preserve that distinction so debris and
            // shrapnel previews remain rigid model particles.
            const float angularSpeed = glm::length(particle.angularVelocity);
            if (angularSpeed > 0.0001f)
                particle.orientation = glm::mat3(glm::rotate(glm::mat4(1.0f), angularSpeed * seconds,
                                                             particle.angularVelocity / angularSpeed)) *
                                       particle.orientation;
        }
    }
    std::erase_if(group.particles,
                  [](const EmitterPreviewParticle &particle) { return particle.age >= particle.lifetime; });

    const EmitterPreviewActivation activation = Activation(objectIndex);
    const bool enabled = activation == EmitterPreviewActivation::Enabled ||
                         (activation == EmitterPreviewActivation::Authored && Integer(object, 355, 1) != 0);
    const int authoredCount = Integer(object, 348, -1);
    if (!enabled || activation == EmitterPreviewActivation::Paused || authoredCount == 0)
        return;
    const glm::vec2 rateRange = Range(object, 351, {10.0f, 10.0f});
    const bool autoPause = Integer(object, 349, 0) != 0;
    const float pauseProbability = Scalar(object, 353, 0.0f);
    int emissionMode = Integer(object, 350, -1);
    const float constantCount = Scalar(object, 352, 0.0f);
    if (emissionMode < 0)
        emissionMode = (pauseProbability == 0.0f && !autoPause) ? (constantCount == 0.0f ? 0 : 1) : 3;
    if (!group.initialized)
    {
        group.currentRate = (std::max)(0.0f, RandomRange(group.random, rateRange));
        if (emissionMode == 3)
            group.currentRate *= 60.0f;
        group.rateRecalcRemaining = (emissionMode == 0 || emissionMode == 1) ? 1.0f : FLT_MAX;
        if (autoPause && activation != EmitterPreviewActivation::Enabled)
            group.pauseRemaining = FLT_MAX;
        else if ((emissionMode == 2 || emissionMode == 3) &&
                 std::uniform_real_distribution<float>(0.0f, 1.0f)(group.random) <= pauseProbability)
            group.pauseRemaining = RandomRange(group.random, Range(object, 354, {0.0f, 0.0f}));
        group.initialized = true;
    }
    if (group.pauseRemaining > 0.0f)
    {
        group.pauseRemaining = (std::min)(group.pauseRemaining, FLT_MAX - 1.0f) - seconds;
        return;
    }
    if (group.rateRecalcRemaining != FLT_MAX)
    {
        group.rateRecalcRemaining -= seconds;
        if (group.rateRecalcRemaining <= 0.0f)
        {
            group.currentRate = (std::max)(0.0f, RandomRange(group.random, rateRange));
            group.rateRecalcRemaining += 1.0f;
        }
    }
    float density = 1.0f;
    const auto effect =
        std::find_if(document.Effects().begin(), document.Effects().end(),
                     [&](const BrxEffectRecord &value) { return value.ownerObjectIndex == configurationIndex; });
    // ProjectCane assigns EMITTER origin kind to EOPID 366. The decoded tail
    // retains the same value so newly edited and untouched effects agree.
    const int authoredOriginKind =
        Property(object, 366) ? Integer(object, 366, 0) : (effect != document.Effects().end() ? effect->originKind : 0);
    const int originKind = (std::max)(0, authoredOriginKind);
    if (originKind == 1 && Integer(object, 356, 0) != 0)
    {
        const glm::vec3 size = glm::abs(Vector(object, 369) - Vector(object, 368));
        density = constantCount == 0.0f ? size.y * size.z * 0.0001f : size.x * size.y * size.z * 0.000001f;
    }
    int spawnCount = 0;
    if (emissionMode == 1)
        spawnCount =
            (std::max)(0, static_cast<int>(constantCount * density) - static_cast<int>(group.particles.size()));
    else if (emissionMode == 2)
    {
        group.spawnAccumulator += seconds * 60.0f;
        const int tickCount = static_cast<int>(group.spawnAccumulator);
        group.spawnAccumulator -= static_cast<float>(tickCount);
        for (int tick = 0; tick < tickCount; ++tick)
        {
            spawnCount += static_cast<int>(group.currentRate);
            group.currentRate = (std::max)(0.0f, RandomRange(group.random, rateRange));
            if (autoPause || std::uniform_real_distribution<float>(0.0f, 1.0f)(group.random) <= pauseProbability)
            {
                group.pauseRemaining =
                    autoPause ? FLT_MAX : RandomRange(group.random, Range(object, 354, {0.0f, 0.0f}));
                break;
            }
        }
    }
    else
    {
        group.spawnAccumulator += seconds * group.currentRate * density;
        const int ticks = static_cast<int>(group.spawnAccumulator);
        group.spawnAccumulator -= static_cast<float>(ticks);
        spawnCount = ticks;
        if (emissionMode == 3 && spawnCount > 1 &&
            (autoPause || std::uniform_real_distribution<float>(0.0f, 1.0f)(group.random) <= pauseProbability))
        {
            group.pauseRemaining = autoPause ? FLT_MAX : RandomRange(group.random, Range(object, 354, {0.0f, 0.0f}));
            group.currentRate = (std::max)(0.0f, RandomRange(group.random, rateRange)) * 60.0f;
        }
    }
    if (authoredCount >= 0 && emissionMode != 1)
        spawnCount = (std::min)(spawnCount, (std::max)(0, authoredCount - group.emittedCount));
    spawnCount =
        (std::min)(spawnCount, static_cast<int>(particleLimitPerEmitter_) - static_cast<int>(group.particles.size()));
    if (spawnCount == 0 && emissionMode != 1 && authoredCount >= 0 && group.emittedCount >= authoredCount &&
        group.particles.empty() && looping_)
    {
        group.emittedCount = 0;
        group.spawnAccumulator = 0.0f;
    }
    group.emittedCount += spawnCount;

    // During emission ProjectCane temporarily uses the emitter itself only
    // when no explicit reference was authored.  An explicit reference drives
    // both spawn-position conversion and non-World velocity conversion.
    const glm::vec3 localOrigin = Vector(object, 367);
    const glm::vec3 boxMin = Vector(object, 368);
    const glm::vec3 boxMax = Vector(object, 369);
    const int directionKind = (std::max)(0, Integer(object, 371, 0));
    // InitEmitb uses g_normalX.  Using +Z here creates the characteristic
    // vertical fire column whenever an emitter omits its direction option.
    const glm::vec3 authoredDirection = Vector(object, 372, {1.0f, 0.0f, 0.0f});
    const glm::vec2 speedRange = Range(object, 377, {750.0f, 750.0f});
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float verticalScale = Scalar(object, 378, 1.0f);
    const glm::vec2 tiltRange = Range(object, 384, {0.0f, 0.0f});
    const int authoredParticlesPerRing = Integer(object, 385, 200);
    const int particlesPerRing =
        authoredParticlesPerRing < 1 ? spawnCount : (std::min)(authoredParticlesPerRing, spawnCount);
    const int ringCount = particlesPerRing > 0 ? (spawnCount - 1 + particlesPerRing) / particlesPerRing : 0;
    const float tiltSpan = NormalizeAngle(tiltRange.y - tiltRange.x);
    const float tiltStep = ringCount > 0 ? tiltSpan / static_cast<float>(ringCount) : 0.0f;
    const float panStep = particlesPerRing > 0 ? glm::two_pi<float>() / static_cast<float>(particlesPerRing) : 0.0f;
    const float panStart = unit(group.random) * glm::two_pi<float>();
    const float randomRadius = Scalar(object, 386, 1.0f);
    const int velocitySpace = (std::max)(0, Integer(object, 376, 0));
    const float lifetime = (std::max)(0.01f, Scalar(object, 387, 1.0f));
    // InitEmitb leaves oidShader at the zero-initialized value.  An omitted
    // EOPID 398 therefore resolves to SHD OID 0, not OID_Nil.
    group.shaderOid = static_cast<std::int16_t>(Integer(object, 398, 0));
    group.shaderColor = glm::vec4(1.0f);
    group.additive = false;
    group.textureFrameCount = 1;
    group.textureFrameMap.assign(1, 0);
    auto shader = std::find_if(document.Shaders().begin(), document.Shaders().end(),
                               [&](const BrxShaderRecord &value) { return value.oid == group.shaderOid; });
    // SetBlipgShader falls back to g_ashd[0] when PshdFindShader fails.
    if (shader == document.Shaders().end())
        shader = std::find_if(document.Shaders().begin(), document.Shaders().end(),
                              [](const BrxShaderRecord &value) { return value.oid == 0; });
    if (shader != document.Shaders().end())
    {
        group.additive = (shader->flags & 1u) != 0;
        // SetBlipgShader seeds the BLIP color table from SHD::rgba whenever
        // no explicit emitter color table was serialized.
        group.shaderColor = {shader->rgba[0], shader->rgba[1], shader->rgba[2], shader->rgba[3]};
        const auto &frames = shader->kind == 0 ? shader->ambientBitmapIndices : shader->diffuseBitmapIndices;
        const std::size_t sourceFrameCount = (std::min)(std::size_t{32}, frames.size());
        if (sourceFrameCount > 0)
        {
            std::size_t paddedCount = 1;
            while (paddedCount < sourceFrameCount)
                paddedCount <<= 1;
            group.textureFrameCount = static_cast<std::uint16_t>(paddedCount);
            group.textureFrameMap.clear();
            group.textureFrameMap.reserve(paddedCount);
            const std::size_t duplicateCount = paddedCount - sourceFrameCount;
            for (std::size_t frame = 0; frame < sourceFrameCount; ++frame)
            {
                group.textureFrameMap.push_back(static_cast<std::uint16_t>(frame));
                if (frame < duplicateCount && group.textureFrameMap.size() < paddedCount)
                    group.textureFrameMap.push_back(static_cast<std::uint16_t>(frame));
            }
            while (group.textureFrameMap.size() < paddedCount)
                group.textureFrameMap.push_back(static_cast<std::uint16_t>(sourceFrameCount - 1));
        }
    }

    for (int index = 0; index < spawnCount; ++index)
    {
        EmitterPreviewParticle particle;
        particle.ownerObjectIndex = objectIndex;
        particle.modelParticle = modelParticle;
        particle.movementKind = static_cast<std::uint8_t>(movementKind);
        particle.flyingScale = Scalar(object, 408, 0.0f);
        particle.velocityDamping = damping;
        if (modelParticle)
        {
            particle.ripType = static_cast<std::int8_t>(std::clamp(Integer(object, 392, 0), 0, 27));
            const int authoredTrailType = Integer(object, 393, -1);
            particle.trailType =
                static_cast<std::int8_t>(authoredTrailType == 10 || authoredTrailType == 13 ? authoredTrailType : -1);
            const auto renderOid = static_cast<std::int16_t>(Integer(object, 358, -1));
            particle.renderObjectIndex = FindObjectIndex(document, renderOid, placementObject);
            if (particle.renderObjectIndex < 0 && renderOid < 0)
            {
                const auto stockOid = StockRenderOidForRip(particle.ripType);
                if (stockOid >= 0)
                    particle.renderObjectIndex = FindObjectIndex(document, stockOid, placementObject);
            }
            const auto nextRenderOid = static_cast<std::int16_t>(Integer(object, 360, -1));
            particle.nextRenderObjectIndex = FindObjectIndex(document, nextRenderOid, placementObject);
            particle.orientation = glm::mat3(world);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float axisLength = glm::length(particle.orientation[axis]);
                if (axisLength > 0.0001f)
                    particle.orientation[axis] /= axisLength;
            }
            particle.baseScale = RandomRange(group.random, Range(object, 394, {1.0f, 1.0f}));
        }
        glm::vec3 localPosition = localOrigin;
        glm::vec3 sourceNormal = authoredDirection;
        particle.color = group.shaderColor;
        particle.colorStart = particle.color;
        particle.colorEnd = particle.color;
        const float stratifiedSample =
            spawnCount > 0 ? (static_cast<float>(index) + unit(group.random)) / static_cast<float>(spawnCount) : 0.0f;
        if (originKind == 1)
            localPosition =
                glm::mix(boxMin, boxMax, glm::vec3(unit(group.random), unit(group.random), unit(group.random)));
        else if (originKind == 2 &&
                 (externalShape != nullptr || (effect != document.Effects().end() && !effect->curvePoints.empty())))
        {
            // ChooseEmitoPos divides the curve into one interval per particle
            // and randomizes within that interval. Independent full-curve
            // samples clump short bursts and leave visible holes in rings.
            const float sample = stratifiedSample;
            const std::size_t pointCount =
                externalShape != nullptr ? externalShape->points.size() : effect->curvePoints.size();
            if (pointCount < 2)
                continue;
            const auto parameterAt = [&](const std::size_t pointIndex)
            {
                if (externalShape != nullptr && externalShape->parameters.size() == pointCount)
                    return externalShape->parameters[pointIndex];
                return externalShape != nullptr ? static_cast<float>(pointIndex) : effect->curvePoints[pointIndex].u;
            };
            const float parameterFirst = parameterAt(0);
            const float parameterLast = parameterAt(pointCount - 1);
            const float curveParameter = glm::mix(parameterFirst, parameterLast, sample);
            std::size_t first = 0;
            while (first + 2 < pointCount && parameterAt(first + 1) < curveParameter)
                ++first;
            const std::size_t second = first + 1;
            const float parameterSpan = parameterAt(second) - parameterAt(first);
            const float t = parameterSpan > 0.000001f
                                ? std::clamp((curveParameter - parameterAt(first)) / parameterSpan, 0.0f, 1.0f)
                                : 0.0f;
            glm::vec3 local;
            const glm::vec3 positionA =
                externalShape != nullptr
                    ? glm::vec3(externalShape->points[first][0], externalShape->points[first][1],
                                externalShape->points[first][2])
                    : glm::vec3(effect->curvePoints[first].position[0], effect->curvePoints[first].position[1],
                                effect->curvePoints[first].position[2]);
            const glm::vec3 positionB =
                externalShape != nullptr
                    ? glm::vec3(externalShape->points[second][0], externalShape->points[second][1],
                                externalShape->points[second][2])
                    : glm::vec3(effect->curvePoints[second].position[0], effect->curvePoints[second].position[1],
                                effect->curvePoints[second].position[2]);
            const bool hermite = externalShape != nullptr ? externalShape->hermite : effect->curveKind == 1;
            glm::vec3 tangentOut(0.0f), tangentIn(0.0f);
            if (hermite)
            {
                if (externalShape != nullptr && externalShape->tangentsOut.size() == pointCount &&
                    externalShape->tangentsIn.size() == pointCount)
                {
                    tangentOut = glm::vec3(externalShape->tangentsOut[first][0], externalShape->tangentsOut[first][1],
                                           externalShape->tangentsOut[first][2]);
                    tangentIn = glm::vec3(externalShape->tangentsIn[second][0], externalShape->tangentsIn[second][1],
                                          externalShape->tangentsIn[second][2]);
                }
                else if (externalShape == nullptr)
                {
                    tangentOut =
                        glm::vec3(effect->curvePoints[first].tangentOut[0], effect->curvePoints[first].tangentOut[1],
                                  effect->curvePoints[first].tangentOut[2]);
                    tangentIn =
                        glm::vec3(effect->curvePoints[second].tangentIn[0], effect->curvePoints[second].tangentIn[1],
                                  effect->curvePoints[second].tangentIn[2]);
                }
                const float t2 = t * t, t3 = t2 * t;
                local = (2.0f * t3 - 3.0f * t2 + 1.0f) * positionA + (t3 - 2.0f * t2 + t) * tangentOut * parameterSpan +
                        (-2.0f * t3 + 3.0f * t2) * positionB + (t3 - t2) * tangentIn * parameterSpan;
            }
            else
                local = glm::mix(positionA, positionB, t);
            localPosition = local;
            glm::vec3 tangent;
            if (hermite)
            {
                const float t2 = t * t;
                tangent = (6.0f * t2 - 6.0f * t) * positionA +
                          (3.0f * t2 - 4.0f * t + 1.0f) * tangentOut * parameterSpan +
                          (-6.0f * t2 + 6.0f * t) * positionB + (3.0f * t2 - 2.0f * t) * tangentIn * parameterSpan;
            }
            else
                tangent = positionB - positionA;
            if (directionKind == 2)
                sourceNormal = tangent;
            else if (directionKind == 3)
                sourceNormal = glm::cross(tangent, authoredDirection);
        }
        else if (originKind == 3)
        {
            struct SkeletonSegment
            {
                glm::vec3 start{}, end{};
                float densityStart = 0.0f, densityEnd = 0.0f;
                float radiusStart = 0.0f, radiusEnd = 0.0f;
                float length = 0.0f, weight = 0.0f;
            };
            std::vector<SkeletonSegment> segments;
            float totalWeight = 0.0f;
            for (const auto &property : object.properties)
            {
                if (property.eopid != 412 || property.arguments.size() < 6)
                    continue;
                const auto firstIndex = FindObjectIndex(
                    document, static_cast<std::int16_t>(property.arguments[0].intValue), placementObject);
                const auto secondIndex = FindObjectIndex(
                    document, static_cast<std::int16_t>(property.arguments[1].intValue), placementObject);
                if (firstIndex < 0 || secondIndex < 0)
                    continue;
                SkeletonSegment segment;
                segment.start = glm::vec3(resolvedTransform(firstIndex)[3]);
                segment.end = glm::vec3(resolvedTransform(secondIndex)[3]);
                segment.radiusStart = property.arguments[2].floatValue[0];
                segment.densityStart = property.arguments[3].floatValue[0];
                segment.radiusEnd = property.arguments[4].floatValue[0] >= 0.0f ? property.arguments[4].floatValue[0]
                                                                                : segment.radiusStart;
                segment.densityEnd = property.arguments[5].floatValue[0] >= 0.0f ? property.arguments[5].floatValue[0]
                                                                                 : segment.densityStart;
                segment.length = glm::length(segment.end - segment.start);
                segment.weight = 0.5f * (segment.densityStart + segment.densityEnd) * segment.length;
                if (segment.length > 0.0001f && segment.weight > 0.0f)
                {
                    totalWeight += segment.weight;
                    segments.push_back(segment);
                }
            }
            if (!segments.empty() && totalWeight > 0.0f)
            {
                float weight = stratifiedSample * totalWeight;
                const SkeletonSegment *segment = &segments.back();
                for (const auto &candidate : segments)
                {
                    if (weight <= candidate.weight)
                    {
                        segment = &candidate;
                        break;
                    }
                    weight -= candidate.weight;
                }
                const float averageDensity = 0.5f * (segment->densityStart + segment->densityEnd);
                const float distance =
                    averageDensity > 0.0001f ? std::clamp(weight / averageDensity, 0.0f, segment->length) : 0.0f;
                const float along = distance / segment->length;
                const glm::vec3 axis = SafeNormal(segment->end - segment->start);
                glm::vec3 basisX, basisY;
                NormalBasis(axis, basisX, basisY);
                const float angle = RandomRange(group.random, {-glm::pi<float>(), glm::pi<float>()});
                const float radius = glm::mix(segment->radiusStart, segment->radiusEnd, along);
                const glm::vec3 worldPosition = segment->start + axis * distance + basisX * std::cos(angle) * radius +
                                                basisY * std::sin(angle) * radius;
                localPosition = glm::vec3(glm::inverse(world) * glm::vec4(worldPosition, 1.0f));
                if (directionKind == 2)
                    sourceNormal = glm::transpose(glm::mat3(world)) * axis;
                else if (directionKind == 3)
                    sourceNormal = glm::cross(glm::transpose(glm::mat3(world)) * axis, authoredDirection);
            }
        }
        else if (originKind == 4 && effect != document.Effects().end() && !effect->meshPositions.empty() &&
                 !effect->meshTriangles.empty())
        {
            float totalArea = 0.0f;
            for (const auto &triangle : effect->meshTriangles)
                if (std::isfinite(triangle.area))
                    totalArea += (std::max)(triangle.area, 0.0f);
            if (!(totalArea > 0.000001f))
                continue;
            float area = unit(group.random) * totalArea;
            const BrxEffectMeshTriangle *selectedTriangle = &effect->meshTriangles.back();
            for (const auto &triangle : effect->meshTriangles)
            {
                area -= (std::max)(triangle.area, 0.0f);
                if (area <= 0.0f)
                {
                    selectedTriangle = &triangle;
                    break;
                }
            }
            if (selectedTriangle->indices[0] < effect->meshPositions.size() &&
                selectedTriangle->indices[1] < effect->meshPositions.size() &&
                selectedTriangle->indices[2] < effect->meshPositions.size())
            {
                const auto &av = effect->meshPositions[selectedTriangle->indices[0]];
                const auto &bv = effect->meshPositions[selectedTriangle->indices[1]];
                const auto &cv = effect->meshPositions[selectedTriangle->indices[2]];
                const glm::vec3 a(av[0], av[1], av[2]);
                const glm::vec3 b(bv[0], bv[1], bv[2]);
                const glm::vec3 c(cv[0], cv[1], cv[2]);
                const float u = unit(group.random);
                const float v = unit(group.random) * (1.0f - u);
                localPosition = a * u + b * v + c * (1.0f - u - v);
                if (directionKind == 5)
                    sourceNormal = glm::cross(b - a, c - a);
            }
        }
        glm::vec3 direction = sourceNormal;
        if (directionKind == 1) // EMITNK_Radial
        {
            const glm::vec3 radialCenter = Vector(object, 373, authoredDirection);
            direction = localPosition - radialCenter;
        }
        else if (directionKind == 4) // EMITNK_Screen
        {
            direction = glm::transpose(glm::mat3(world)) * glm::mat3(cameraWorld_) * authoredDirection;
        }
        // Curve tangent/normal, screen, and mesh-normal modes require their
        // authored source basis.  Until that basis is available, ProjectCane's
        // safe fallback is g_normalX; it never substitutes a random vector.
        else if (directionKind < 0 || directionKind > 5)
            direction = {1.0f, 0.0f, 0.0f};
        if (glm::dot(direction, direction) < 0.0001f)
            direction = {1.0f, 0.0f, 0.0f};
        direction = SafeNormal(direction);
        const glm::vec2 surfaceOffset = Range(object, 375, {0.0f, 0.0f});
        localPosition += direction * RandomRange(group.random, surfaceOffset);
        const int ringIndex = particlesPerRing > 0 ? index / particlesPerRing : 0;
        const int sliceIndex = particlesPerRing > 0 ? index % particlesPerRing : 0;
        float tilt = tiltRange.x + tiltStep * (static_cast<float>(ringIndex) + 0.5f);
        float pan = panStart + panStep * static_cast<float>(sliceIndex);
        if (randomRadius != 0.0f)
        {
            tilt += randomRadius * (unit(group.random) - 0.5f) * tiltStep;
            pan += randomRadius * (unit(group.random) - 0.5f) * panStep;
        }
        const float sphereTilt = tilt + glm::half_pi<float>();
        const float speed = RandomRange(group.random, speedRange);
        glm::vec3 localVelocity(std::cos(sphereTilt) * std::cos(pan) * speed,
                                std::cos(sphereTilt) * std::sin(pan) * speed,
                                std::sin(sphereTilt) * speed * verticalScale);
        glm::vec3 tangent, bitangent;
        NormalBasis(direction, tangent, bitangent);
        localVelocity = tangent * localVelocity.x + bitangent * localVelocity.y + direction * localVelocity.z;
        const float skip = RandomRange(group.random, Range(object, 388, {0.0f, 0.0f}));
        localPosition += localVelocity * skip;
        particle.position = glm::vec3(world * glm::vec4(localPosition, 1.0f));
        // ConvertEmitoPosVec intentionally does not rotate EMITVK_World.
        particle.velocity = velocitySpace == 3 ? localVelocity : glm::mat3(world) * localVelocity;
        const std::int32_t motionReferenceIndex = referenceIndex >= 0 ? referenceIndex : objectIndex;
        if ((velocitySpace == 0 || velocitySpace == 1) && motionReferenceIndex >= 0)
        {
            if (static_cast<std::size_t>(motionReferenceIndex) < linearVelocity_.size())
                particle.velocity += linearVelocity_[static_cast<std::size_t>(motionReferenceIndex)];
            if (velocitySpace == 1 && static_cast<std::size_t>(motionReferenceIndex) < angularVelocity_.size())
            {
                const glm::vec3 referencePosition(resolvedTransform(motionReferenceIndex)[3]);
                particle.velocity += glm::cross(angularVelocity_[static_cast<std::size_t>(motionReferenceIndex)],
                                                particle.position - referencePosition);
            }
        }
        if (particle.modelParticle)
        {
            if (particle.ripType == 24) // SHRAPNEL::InitRip / PostShrapnelEmit
            {
                const float spin = std::clamp(glm::length(particle.velocity) * 0.001f, 0.0f, 1.0f) * 10.0f;
                particle.angularVelocity =
                    glm::vec3(RandomRange(group.random, {-spin, spin}), RandomRange(group.random, {-spin, spin}),
                              RandomRange(group.random, {-spin, spin}));
                particle.renderHeightOffset = Scalar(object, 417, 0.0f);
                particle.lifetimeScale = RandomRange(group.random, {0.75f, 1.25f});
            }
            else if (particle.ripType == 22) // FLAME stores its launch axis in mat.
            {
                const glm::vec3 axis = SafeNormal(particle.velocity);
                glm::vec3 axisY = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), axis);
                if (glm::dot(axisY, axisY) < 0.0001f)
                    NormalBasis(axis, axisY, particle.orientation[2]);
                else
                {
                    axisY = glm::normalize(axisY);
                    particle.orientation[2] = glm::normalize(glm::cross(axis, axisY));
                }
                particle.orientation[0] = axis;
                particle.orientation[1] = axisY;
                particle.velocity = {};
            }
            else if (particle.ripType == 23) // PostBulletEmit
            {
                particle.position = glm::vec3(world * glm::vec4(localOrigin, 1.0f));
                particle.velocityDamping = 0.0f;
            }
        }
        if (motionKind == 2)
        {
            const auto splineTargetOid = static_cast<std::int16_t>(Integer(object, 406, -1));
            const auto splineTargetIndex = FindObjectIndex(document, splineTargetOid, placementObject);
            if (splineTargetIndex >= 0)
            {
                const auto &target = document.Objects()[static_cast<std::size_t>(splineTargetIndex)];
                glm::mat4 targetWorld = resolvedTransform(splineTargetIndex);
                const auto targetReferenceOid = static_cast<std::int16_t>(Integer(target, 425, -1));
                const auto targetReferenceIndex = FindObjectIndex(document, targetReferenceOid, target);
                if (targetReferenceIndex >= 0)
                    targetWorld = resolvedTransform(targetReferenceIndex);
                glm::vec3 targetLocal = Vector(target, 422, {});
                glm::vec3 targetCurveTangent{1.0f, 0.0f, 0.0f};
                const int targetOriginKind = Integer(target, 421, 0);
                if (targetOriginKind == 1)
                    targetLocal = glm::mix(Vector(target, 423, {}), Vector(target, 424, {}),
                                           glm::vec3(unit(group.random), unit(group.random), unit(group.random)));
                else if (targetOriginKind == 2)
                {
                    const auto targetShapeOid = static_cast<std::int16_t>(Integer(target, 426, -1));
                    const auto targetShapeIndex = FindObjectIndex(document, targetShapeOid, target);
                    if (targetShapeIndex >= 0)
                    {
                        const auto shape = std::find_if(document.ShapePaths().begin(), document.ShapePaths().end(),
                                                        [&](const BrxShapePathRecord &candidate)
                                                        { return candidate.ownerObjectIndex == targetShapeIndex; });
                        if (shape != document.ShapePaths().end() && shape->points.size() >= 2)
                        {
                            const auto targetShapeParent =
                                document.Objects()[static_cast<std::size_t>(targetShapeIndex)].parentIndex;
                            if (targetShapeParent >= 0)
                                targetWorld = resolvedTransform(targetShapeParent);
                            const float sample = (static_cast<float>(index) + unit(group.random)) /
                                                 static_cast<float>((std::max)(spawnCount, 1));
                            const float scaled = sample * static_cast<float>(shape->points.size() - 1);
                            const std::size_t first =
                                (std::min)(static_cast<std::size_t>(scaled), shape->points.size() - 2);
                            const float t = scaled - static_cast<float>(first);
                            const glm::vec3 a(shape->points[first][0], shape->points[first][1],
                                              shape->points[first][2]);
                            const glm::vec3 b(shape->points[first + 1][0], shape->points[first + 1][1],
                                              shape->points[first + 1][2]);
                            targetLocal = glm::mix(a, b, t);
                            targetCurveTangent = b - a;
                        }
                    }
                }
                glm::vec3 targetDirection = Vector(target, 428, {1.0f, 0.0f, 0.0f});
                if (Integer(target, 427, 0) == 1)
                    targetDirection = targetLocal - Vector(target, 429, targetDirection);
                else if (Integer(target, 427, 0) == 2)
                    targetDirection = targetCurveTangent;
                else if (Integer(target, 427, 0) == 3)
                    targetDirection = glm::cross(targetCurveTangent, Vector(target, 430, targetDirection));
                targetDirection = SafeNormal(targetDirection);
                targetLocal += targetDirection * RandomRange(group.random, Range(target, 431, {0.0f, 0.0f}));
                const glm::vec2 targetSpeedRange = Range(target, 433, {750.0f, 750.0f});
                const glm::vec2 targetTiltRange = Range(target, 435, {0.0f, 0.0f});
                const float targetTilt = RandomRange(group.random, targetTiltRange) + glm::half_pi<float>();
                const float targetPan = unit(group.random) * glm::two_pi<float>();
                const float targetSpeed = RandomRange(group.random, targetSpeedRange);
                glm::vec3 basisX, basisY;
                NormalBasis(targetDirection, basisX, basisY);
                glm::vec3 targetLocalVelocity =
                    basisX * (std::cos(targetTilt) * std::cos(targetPan) * targetSpeed) +
                    basisY * (std::cos(targetTilt) * std::sin(targetPan) * targetSpeed) +
                    targetDirection * (std::sin(targetTilt) * targetSpeed * Scalar(target, 434, 1.0f));
                targetLocal += targetLocalVelocity * RandomRange(group.random, Range(target, 438, {0.0f, 0.0f}));
                particle.splineTargetPosition = glm::vec3(targetWorld * glm::vec4(targetLocal, 1.0f));
                particle.splineTargetVelocity = Integer(target, 432, 0) == 3
                                                    ? -targetLocalVelocity
                                                    : -(glm::mat3(targetWorld) * targetLocalVelocity);
                if (targetReferenceIndex >= 0 && Integer(target, 432, 0) != 3)
                {
                    if (static_cast<std::size_t>(targetReferenceIndex) < linearVelocity_.size())
                        particle.splineTargetVelocity -=
                            linearVelocity_[static_cast<std::size_t>(targetReferenceIndex)];
                }
                particle.hasSplineTarget = true;
            }
        }
        particle.lifetime = lifetime * particle.lifetimeScale;
        particle.age = RandomRange(group.random, Range(object, 389, {0.0f, 0.0f}));
        particle.roll =
            Integer(object, 411, 0) != 0 ? RandomRange(group.random, {-glm::pi<float>(), glm::pi<float>()}) : 0.0f;
        if (movementKind == 2)
        {
            particle.angularSpeed = RandomRange(group.random, Range(object, 410, {0.0f, 0.0f}));
            if (Integer(object, 409, 0) == 0 && glm::dot(glm::vec3(cameraWorld_[1]), particle.velocity) > 0.0f)
                particle.angularSpeed = -particle.angularSpeed;
        }
        particle.scale = particle.baseScale * scaleClq.x;
        if (effect != document.Effects().end() && !effect->colors.empty())
        {
            std::size_t colorIndex = static_cast<std::size_t>(group.random()) % effect->colors.size();
            if (effect->colorsAreRanges)
                colorIndex &= ~std::size_t{1};
            const auto &color = effect->colors[colorIndex];
            particle.colorStart = {color[0], color[1], color[2], color[3]};
            particle.colorEnd = particle.colorStart;
            if (effect->colorsAreRanges && colorIndex + 1 < effect->colors.size())
            {
                const auto &end = effect->colors[colorIndex + 1];
                particle.colorEnd = {end[0], end[1], end[2], end[3]};
                particle.interpolateColor = true;
            }
            particle.color = particle.colorStart;
        }
        particle.baseAlpha = particle.color.a;
        particle.shaderOid = group.shaderOid;
        particle.initialTextureFrame =
            Integer(object, 404, 0) != 0 ? static_cast<std::uint16_t>(group.random() % group.textureFrameCount) : 0;
        particle.textureFrame = particle.initialTextureFrame < group.textureFrameMap.size()
                                    ? group.textureFrameMap[particle.initialTextureFrame]
                                    : particle.initialTextureFrame;
        particle.additive = group.additive;
        particle.previousPosition = particle.position;
        group.particles.push_back(particle);
    }
}

void EmitterPreviewRuntime::RebuildParticleView()
{
    particles_.clear();
    std::size_t count = 0;
    for (const auto &[objectIndex, group] : groups_)
        count += group.particles.size();
    particles_.reserve(count);
    for (const auto &[objectIndex, group] : groups_)
        particles_.insert(particles_.end(), group.particles.begin(), group.particles.end());
}
