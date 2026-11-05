#include "RuntimePreviewWorld.h"

#include "BrxDocument.h"

#include <algorithm>

bool RuntimePreviewWorld::Begin(const RuntimePreviewKind kind, const std::int32_t objectIndex,
                                const BrxDocument &document, std::string &error)
{
    error.clear();
    if (kind == RuntimePreviewKind::None || objectIndex < 0 ||
        static_cast<std::size_t>(objectIndex) >= document.Objects().size())
    {
        error = "A valid object and preview kind are required.";
        return false;
    }
    kind_ = kind;
    objectIndex_ = objectIndex;
    paused_ = false;
    time_ = 0.0f;
    return true;
}

void RuntimePreviewWorld::Stop()
{
    kind_ = RuntimePreviewKind::None;
    objectIndex_ = -1;
    paused_ = false;
    time_ = 0.0f;
}

void RuntimePreviewWorld::Update(const float deltaSeconds)
{
    if (!Active() || paused_)
        return;
    time_ += (std::clamp)(deltaSeconds, 0.0f, 0.1f);
}
