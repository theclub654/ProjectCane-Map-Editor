#include "CameraRuntimePanel.h"

#include "BrxDocument.h"
#include "EditorCamera.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace
{
constexpr const char *kLookNames[]{"User", "Sniper", "Sniper track", "Dialog", "Confront"};

glm::vec3 CameraForward(const EditorCamera &camera)
{
    const float yaw = glm::radians(camera.Yaw());
    const float pitch = glm::radians(camera.Pitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)));
}
}

void CameraRuntimePanel::ResetFromWorld(const BrxDocument &document)
{
    const auto &world = document.WorldHeader();
    fov_ = targetFov_ = world.cameraFov;
    nearClip_ = world.cameraNearClip;
    farClip_ = world.cameraFarClip;
    nearFog_ = world.fogNear;
    farFog_ = world.fogFar;
    fogMaximum_ = world.fogMaximum;
    mrdRatio_ = std::clamp(world.cameraMrdRatio, 0.75f, 3.0f);
    documentKey_ = document.Path().string();
    focusObject_ = -1;
    initialized_ = true;
}

void CameraRuntimePanel::Stop(const BrxDocument &document, SceneRenderer &renderer)
{
    enabled_ = false;
    joltRemaining_ = 0.0f;
    ResetFromWorld(document);
    const auto &world = document.WorldHeader();
    renderer.UpdateProjection(world.cameraFov, world.cameraNearClip, world.cameraFarClip);
    renderer.UpdateFog(world.fogNear, world.fogFar, world.fogMaximum);
    renderer.UpdateFogColor(world.fogColor);
    renderer.UpdateCameraMrdRatio(world.cameraMrdRatio);
}

void CameraRuntimePanel::Update(const BrxDocument &document, EditorCamera &camera, SceneRenderer &renderer,
                                const float deltaSeconds, const float aspectRatio)
{
    if (!initialized_ || documentKey_ != document.Path().string())
        ResetFromWorld(document);
    if (!enabled_)
        return;
    const float dt = paused_ ? 0.0f : std::clamp(deltaSeconds, 0.0f, 0.1f);
    if (fovResponse_ <= 0.0f)
        fov_ = targetFov_;
    else
    {
        const float blend = 1.0f - std::exp(-dt / std::max(fovResponse_, 0.0001f));
        fov_ = glm::mix(fov_, targetFov_, blend);
    }
    renderer.UpdateProjection(fov_, std::max(nearClip_, 0.001f), std::max(farClip_, nearClip_ + 0.001f));
    renderer.UpdateFog(std::max(nearFog_, 0.0f), std::max(farFog_, nearFog_ + 0.001f),
                       std::clamp(fogMaximum_, 0.0f, 1.0f));
    renderer.UpdateCameraMrdRatio(std::clamp(mrdRatio_, 0.75f, 3.0f));

    glm::vec3 direction = CameraForward(camera);
    if (focusObject_ >= 0 && static_cast<std::size_t>(focusObject_) < document.Objects().size())
    {
        const auto &focus = document.Objects()[static_cast<std::size_t>(focusObject_)];
        const glm::vec3 target(focus.worldTransform[12], focus.worldTransform[13], focus.worldTransform[14]);
        const glm::vec3 toward = target - camera.Position();
        if (glm::dot(toward, toward) > 0.000001f)
        {
            direction = glm::normalize(toward);
            const float distance = glm::length(toward);
            sniperSuggestedFov_ = sniperScreenRadius_ > 0.0001f
                                      ? std::clamp(2.0f * std::atan(std::max(sniperWorldRadius_, 0.0f) /
                                                                    (sniperScreenRadius_ * distance)),
                                                   0.01f, 3.13f)
                                      : targetFov_;
        }
    }
    if (joltRemaining_ > 0.0f)
    {
        joltRemaining_ = std::max(0.0f, joltRemaining_ - dt);
        const float envelope = joltRemaining_ / 0.45f;
        const float phase = joltRemaining_ * 62.0f;
        glm::vec3 right = glm::cross(direction, glm::vec3(0.0f, 0.0f, 1.0f));
        if (glm::dot(right, right) < 0.000001f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        direction = glm::normalize(direction +
                                   glm::normalize(right) * glm::radians(joltStrength_) * std::sin(phase) * envelope);
    }
    if (focusObject_ >= 0 || joltRemaining_ > 0.0f)
        camera.SetView(camera.Position(), direction);

    const glm::vec3 delta = visibilityCenter_ - camera.Position();
    const float distance = glm::length(delta);
    const float mrdScale = std::max(1.0f, std::clamp(mrdRatio_, 0.75f, 3.0f) / std::max(fov_, 0.0001f));
    const float outer = visibilityMrd_ * mrdScale + visibilityRadius_;
    const float inner = std::max(0.0f, outer - 400.0f * mrdScale);
    visibilityAlpha_ = distance <= inner ? 1.0f
                                         : (distance < outer ? (outer * outer - distance * distance) /
                                                                   std::max(outer * outer - inner * inner, 0.0001f)
                                                             : 0.0f);
    bool inFrustum = distance <= visibilityRadius_;
    if (!inFrustum && distance > 0.0001f)
    {
        const glm::vec3 toSphere = delta / distance;
        const float sphereAngle = std::asin(std::clamp(visibilityRadius_ / distance, 0.0f, 1.0f));
        const float verticalHalf = fov_ * 0.5f + sphereAngle;
        const float horizontalHalf = std::atan(std::tan(fov_ * 0.5f) * std::max(aspectRatio, 0.01f)) + sphereAngle;
        const glm::vec3 forward = CameraForward(camera);
        glm::vec3 right = glm::cross(forward, glm::vec3(0, 0, 1));
        if (glm::dot(right, right) < 0.000001f)
            right = glm::vec3(1, 0, 0);
        right = glm::normalize(right);
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));
        inFrustum = std::abs(std::atan2(glm::dot(toSphere, right), glm::dot(toSphere, forward))) <= horizontalHalf &&
                    std::abs(std::atan2(glm::dot(toSphere, up), glm::dot(toSphere, forward))) <= verticalHalf;
    }
    visibilityResult_ = inFrustum && visibilityAlpha_ > 0.0f;
}

void CameraRuntimePanel::Draw(const BrxObjectRecord &, const BrxDocument &document, EditorCamera &camera,
                              SceneRenderer &renderer, std::string &status)
{
    if (!initialized_ || documentKey_ != document.Path().string())
        ResetFromWorld(document);
    ImGui::SeparatorText("CM live camera manager");
    ImGui::TextWrapped(
        "CM owns runtime camera state. Its class-specific EOPIDs are commands, queries, or ordinary runtime setters; they are intentionally not serialized. Persistent camera defaults belong to Scene World.");
    if (ImGui::Checkbox("Enable CM runtime preview", &enabled_))
    {
        if (enabled_)
            ResetFromWorld(document);
        else
            Stop(document, renderer);
        status = enabled_ ? "CM runtime preview enabled." : "CM runtime preview stopped; Scene World camera restored.";
    }
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &paused_);
    if (ImGui::Button("Reset from Scene World"))
    {
        ResetFromWorld(document);
        status = "CM preview reset from authored Scene World camera defaults.";
    }

    ImGui::BeginDisabled(!enabled_);
    if (ImGui::CollapsingHeader("Lens, clipping, fog and MRD", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float degrees = glm::degrees(targetFov_);
        if (ImGui::SliderFloat("Target vertical FOV", &degrees, 5.0f, 170.0f, "%.1f deg"))
            targetFov_ = glm::radians(degrees);
        ImGui::DragFloat("FOV response time", &fovResponse_, 0.01f, 0.0f, 10.0f, "%.3f s");
        if (ImGui::Button("Cut to target FOV"))
            fov_ = targetFov_;
        ImGui::SameLine();
        ImGui::TextDisabled("live %.2f deg", glm::degrees(fov_));
        ImGui::DragFloat("Near clip", &nearClip_, 1.0f, 0.001f, std::max(farClip_ - 0.001f, 0.001f));
        ImGui::DragFloat("Far clip", &farClip_, 10.0f, nearClip_ + 0.001f, 1000000.0f);
        ImGui::DragFloat("Near fog", &nearFog_, 10.0f, 0.0f, std::max(farFog_, 0.0f));
        ImGui::DragFloat("Far fog", &farFog_, 10.0f, nearFog_, 1000000.0f);
        ImGui::SliderFloat("Maximum fog", &fogMaximum_, 0.0f, 1.0f);
        ImGui::DragFloat("MRD ratio", &mrdRatio_, 0.01f, 0.75f, 3.0f);
        ImGui::TextDisabled("MRD scale = max(1, ratio / FOV) = %.3f",
                            std::max(1.0f, std::clamp(mrdRatio_, 0.75f, 3.0f) / std::max(fov_, 0.0001f)));
    }
    if (ImGui::CollapsingHeader("Look policy stack", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Active: %s", lookCount_ > 0 ? kLookNames[lookStack_[lookCount_ - 1]] : "None");
        for (int i = lookCount_ - 1; i >= 0; --i)
            ImGui::BulletText("priority %d: %s", i, kLookNames[lookStack_[i]]);
        ImGui::Combo("Policy to push", &lookToPush_, kLookNames, 5);
        if (ImGui::Button("Push policy") && lookCount_ < static_cast<int>(lookStack_.size()))
            lookStack_[lookCount_++] = lookToPush_;
        ImGui::SameLine();
        if (ImGui::Button("Pop policy") && lookCount_ > 1)
            lookStack_[--lookCount_] = -1;
        ImGui::SameLine();
        if (ImGui::Button("Reset stack"))
        {
            lookStack_ = {0, -1, -1, -1};
            lookCount_ = 1;
        }
        if (lookCount_ == static_cast<int>(lookStack_.size()))
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "Runtime stack limit reached (4).");
    }
    if (ImGui::CollapsingHeader("Sniper focus and camera events", ImGuiTreeNodeFlags_DefaultOpen))
    {
        std::string focusLabel = "None";
        if (focusObject_ >= 0 && static_cast<std::size_t>(focusObject_) < document.Objects().size())
        {
            const auto &focus = document.Objects()[static_cast<std::size_t>(focusObject_)];
            focusLabel = std::string(BrxDocument::ClassName(focus.cid)) + " OID " + std::to_string(focus.oid);
        }
        if (ImGui::BeginCombo("Focus object", focusLabel.c_str()))
        {
            if (ImGui::Selectable("None", focusObject_ < 0))
                focusObject_ = -1;
            for (std::size_t i = 0; i < document.Objects().size(); ++i)
            {
                const auto &candidate = document.Objects()[i];
                if (!CidDerivesFrom(candidate.cid, 2))
                    continue;
                const std::string label = std::string(BrxDocument::ClassName(candidate.cid)) + " OID " +
                                          std::to_string(candidate.oid) + "##cmfocus" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), focusObject_ == static_cast<std::int32_t>(i)))
                    focusObject_ = static_cast<std::int32_t>(i);
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat("Sniper screen radius", &sniperScreenRadius_, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Sniper world radius", &sniperWorldRadius_, 1.0f, 0.0f, 100000.0f);
        ImGui::Text("Required target FOV: %.2f deg", glm::degrees(sniperSuggestedFov_));
        ImGui::SameLine();
        if (ImGui::SmallButton("Use required FOV"))
            targetFov_ = sniperSuggestedFov_;
        ImGui::DragFloat("Jolt strength", &joltStrength_, 0.1f, 0.0f, 30.0f, "%.1f deg");
        if (ImGui::Button("Trigger camera jolt"))
            joltRemaining_ = 0.45f;
        ImGui::SameLine();
        if (ImGui::Button("Cut / snap lens"))
            fov_ = targetFov_;
    }
    if (ImGui::CollapsingHeader("VISIBLE_SPHERE test"))
    {
        ImGui::DragFloat3("Sphere center", glm::value_ptr(visibilityCenter_), 1.0f);
        ImGui::DragFloat("Sphere radius", &visibilityRadius_, 1.0f, 0.0f, 100000.0f);
        ImGui::DragFloat("Sphere MRD", &visibilityMrd_, 10.0f, 0.0f, 1000000.0f);
        if (ImGui::Button("Use viewport focus point"))
            visibilityCenter_ = camera.Position() + CameraForward(camera) * 500.0f;
        ImGui::TextColored(visibilityResult_ ? ImVec4(0.35f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                           "%s | MRD alpha %.3f", visibilityResult_ ? "Visible" : "Not visible", visibilityAlpha_);
        ImGui::TextDisabled("Game order: frustum test, then FInsideCmMrd fade.");
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Authoring boundary");
    ImGui::BulletText("Persistent defaults: Scene World camera section");
    ImGui::BulletText("Paths/behaviors: CAMERA, TN and policy objects");
    ImGui::BulletText("CM EOPIDs 584-601: runtime inspection/simulation only");
}
