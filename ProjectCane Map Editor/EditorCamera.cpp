#include "EditorCamera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

void EditorCamera::Update(GLFWwindow *window, bool viewportHovered, float deltaSeconds)
{
    // Free editor navigation is horizon-locked. Cinematic CAMERA preview can
    // temporarily supply a rolled up vector through the three-argument SetView.
    up_ = glm::vec3(0.0f, 0.0f, 1.0f);
    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    const bool rotate = viewportHovered && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (rotate && rotating_)
    {
        yaw_ -= static_cast<float>(mouseX - lastMouseX_) * 0.15f;
        pitch_ -= static_cast<float>(mouseY - lastMouseY_) * 0.15f;
        pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    }
    rotating_ = rotate;
    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;

    if (!viewportHovered)
        return;

    const float yawRadians = glm::radians(yaw_);
    const float pitchRadians = glm::radians(pitch_);
    const glm::vec3 forward =
        glm::normalize(glm::vec3(std::cos(yawRadians) * std::cos(pitchRadians),
                                 std::sin(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians)));
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f)));
    // Sly world coordinates cover tens or hundreds of thousands of units, so
    // editor movement must operate at level scale rather than meter scale.
    float unitsPerSecond = 1000.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        unitsPerSecond = 5000.0f;
    else if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        unitsPerSecond = 200.0f;
    const float speed = unitsPerSecond * deltaSeconds;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        position_ += forward * speed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        position_ -= forward * speed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        position_ -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        position_ += right * speed;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        position_.z += speed;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        position_.z -= speed;
}

glm::mat4 EditorCamera::ViewMatrix() const
{
    const float yawRadians = glm::radians(yaw_);
    const float pitchRadians = glm::radians(pitch_);
    const glm::vec3 forward =
        glm::normalize(glm::vec3(std::cos(yawRadians) * std::cos(pitchRadians),
                                 std::sin(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians)));
    return glm::lookAt(position_, position_ + forward, up_);
}

void EditorCamera::FrameBounds(const glm::vec3 &minimum, const glm::vec3 &maximum)
{
    up_ = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 center = (minimum + maximum) * 0.5f;
    const float radius = std::max(glm::length(maximum - minimum) * 0.5f, 1.0f);
    yaw_ = 90.0f;
    pitch_ = -20.0f;
    const float yawRadians = glm::radians(yaw_);
    const float pitchRadians = glm::radians(pitch_);
    const glm::vec3 forward =
        glm::normalize(glm::vec3(std::cos(yawRadians) * std::cos(pitchRadians),
                                 std::sin(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians)));
    position_ = center - forward * (radius * 1.8f);
}

void EditorCamera::ResetToOrigin()
{
    position_ = glm::vec3(0.0f);
    yaw_ = 90.0f;
    pitch_ = 0.0f;
    rotating_ = false;
    up_ = glm::vec3(0.0f, 0.0f, 1.0f);
}

void EditorCamera::FocusPoint(const glm::vec3 &point, float distance)
{
    const float yawRadians = glm::radians(yaw_);
    const float pitchRadians = glm::radians(pitch_);
    const glm::vec3 forward =
        glm::normalize(glm::vec3(std::cos(yawRadians) * std::cos(pitchRadians),
                                 std::sin(yawRadians) * std::cos(pitchRadians), std::sin(pitchRadians)));
    position_ = point - forward * std::max(distance, 1.0f);
}

void EditorCamera::SetView(const glm::vec3 &position, const glm::vec3 &direction)
{
    SetView(position, direction, glm::vec3(0.0f, 0.0f, 1.0f));
}

void EditorCamera::SetView(const glm::vec3 &position, const glm::vec3 &direction, const glm::vec3 &up)
{
    if (glm::dot(direction, direction) < 0.000001f)
        return;
    const glm::vec3 forward = glm::normalize(direction);
    glm::vec3 resolvedUp = up;
    if (glm::dot(resolvedUp, resolvedUp) < 0.000001f || glm::length(glm::cross(forward, resolvedUp)) < 0.0001f)
        resolvedUp = std::abs(forward.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    resolvedUp = glm::normalize(resolvedUp - forward * glm::dot(resolvedUp, forward));
    position_ = position;
    up_ = resolvedUp;
    yaw_ = glm::degrees(std::atan2(forward.y, forward.x));
    pitch_ = glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
    rotating_ = false;
}
