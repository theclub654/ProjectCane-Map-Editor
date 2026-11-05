#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

class EditorCamera
{
  public:
    void Update(GLFWwindow *window, bool viewportHovered, float deltaSeconds);

    [[nodiscard]] const glm::vec3 &Position() const
    {
        return position_;
    }
    [[nodiscard]] float Yaw() const
    {
        return yaw_;
    }
    [[nodiscard]] float Pitch() const
    {
        return pitch_;
    }
    [[nodiscard]] const glm::vec3 &Up() const
    {
        return up_;
    }
    [[nodiscard]] glm::mat4 ViewMatrix() const;
    void FrameBounds(const glm::vec3 &minimum, const glm::vec3 &maximum);
    void FocusPoint(const glm::vec3 &point, float distance = 500.0f);
    void SetView(const glm::vec3 &position, const glm::vec3 &direction);
    void SetView(const glm::vec3 &position, const glm::vec3 &direction, const glm::vec3 &up);
    void ResetToOrigin();

  private:
    glm::vec3 position_{0.0f, -10.0f, 4.0f};
    float yaw_ = 90.0f;
    float pitch_ = -15.0f;
    glm::vec3 up_{0.0f, 0.0f, 1.0f};
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool rotating_ = false;
};
