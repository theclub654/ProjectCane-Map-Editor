#pragma once
#include "EditorMcpOperations.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <stb/stb_image_write.h>
#include <deque>
#include <functional>
#include <cmath>
#include <algorithm>
#include <vector>

// Optional access to existing UI controls, not a second implementation of
// hundreds of specialist editors. Input is delivered before ImGui::NewFrame.
// Nothing is injected into another application or an operating-system dialog.
class EditorMcpUi
{
    using Json = nlohmann::json;
    std::deque<std::function<void()>> events_;
    std::string token_;
    std::chrono::steady_clock::time_point captured_;
    std::uint64_t frame_ = 0, lastInputFrame_ = 0;
    std::uint64_t focusRequestedFrame_ = 0;
    int width_ = 0, height_ = 0;
    double physicalX_ = 0, physicalY_ = 0;
    std::string captureTicket_, captureError_;
    Json completedCapture_;
    bool capturePending_ = false;

    static std::string Base64(const std::vector<unsigned char> &bytes)
    {
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result; result.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const auto value = (static_cast<unsigned>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<unsigned>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? bytes[i + 2] : 0u);
            result += alphabet[(value >> 18) & 63]; result += alphabet[(value >> 12) & 63];
            result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
            result += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
        }
        return result;
    }

  public:
    bool allowed = false;
    std::string activity = "UI control idle";
    void BeginFrame() { ++frame_; }
    bool PendingInput() const { return !events_.empty(); }
    bool Busy() const { return PendingInput() || (lastInputFrame_ != 0 && frame_ <= lastInputFrame_ + 2); }
    void Clear()
    {
        events_.clear(); token_.clear(); allowed = false;
        captureTicket_.clear(); captureError_.clear(); completedCapture_ = Json(); capturePending_ = false;
        // Release only buttons/modifiers this integration can synthesize.
        auto &io = ImGui::GetIO();
        io.ClearEventsQueue(); // do not leave trickled synthetic presses behind
        if (auto *window = glfwGetCurrentContext()) io.AddFocusEvent(glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0);
        for (int button = 0; button < 3; ++button) io.AddMouseButtonEvent(button, false);
        for (ImGuiKey key : {ImGuiKey_Enter, ImGuiKey_Escape, ImGuiKey_Tab, ImGuiKey_Backspace,
             ImGuiKey_Delete, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
             ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End, ImGuiKey_A, ImGuiKey_Z,
             ImGuiKey_Y, ImGuiKey_S}) io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        lastInputFrame_ = frame_;
    }
    bool WantsCapture() const { return capturePending_ && !Busy(); }
    Json RequestCapture()
    {
        token_.clear(); captureTicket_ = EditorMcpBridge::NewSession(); captureError_.clear();
        completedCapture_ = Json(); capturePending_ = true;
        return {{"pending", true}, {"capture_ticket", captureTicket_}};
    }
    void CompleteCapture(Json image, const std::string &revision)
    {
        image["captured_revision"] = revision; completedCapture_ = std::move(image); capturePending_ = false;
    }
    void FailCapture(const std::string &error) { captureError_ = error; capturePending_ = false; }
    Json ReadCapture(const std::string &ticket)
    {
        if (ticket.empty() || ticket != captureTicket_) throw std::runtime_error("Capture request expired or was replaced");
        if (!captureError_.empty()) throw std::runtime_error(captureError_);
        if (capturePending_) return {{"pending", true}, {"capture_ticket", ticket}};
        if (completedCapture_.is_null()) throw std::runtime_error("Capture was already consumed");
        auto result = std::move(completedCapture_); completedCapture_ = Json(); return result;
    }
    void Pump(GLFWwindow *window)
    {
        if (events_.empty()) return;
        if (!glfwGetWindowAttrib(window, GLFW_FOCUSED) && frame_ <= focusRequestedFrame_ + 2) return;
        double x, y; glfwGetCursorPos(window, &x, &y);
        bool physical = std::abs(x - physicalX_) > 1 || std::abs(y - physicalY_) > 1 ||
            ImGui::GetIO().MouseWheel != 0 || ImGui::GetIO().MouseWheelH != 0;
        for (int button = 0; button < 3; ++button) physical |= glfwGetMouseButton(window, button) == GLFW_PRESS;
        for (int key = GLFW_KEY_A; key <= GLFW_KEY_Z; ++key) physical |= glfwGetKey(window, key) == GLFW_PRESS;
        for (int key = GLFW_KEY_0; key <= GLFW_KEY_9; ++key) physical |= glfwGetKey(window, key) == GLFW_PRESS;
        for (int key = GLFW_KEY_F1; key <= GLFW_KEY_F25; ++key) physical |= glfwGetKey(window, key) == GLFW_PRESS;
        for (int key : {GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE,
             GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT,
             GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT, GLFW_KEY_LEFT_SUPER, GLFW_KEY_RIGHT_SUPER,
             GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_HOME, GLFW_KEY_END,
             GLFW_KEY_DELETE, GLFW_KEY_INSERT, GLFW_KEY_SPACE, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN})
            physical |= glfwGetKey(window, key) == GLFW_PRESS;
        if (physical || !glfwGetWindowAttrib(window, GLFW_FOCUSED))
        {
            Clear(); activity = "UI control stopped after local input or focus change; inspect and re-enable it to continue";
            return;
        }
        auto event = std::move(events_.front()); events_.pop_front();
        event(); lastInputFrame_ = frame_;
        activity = events_.empty() ? "UI action delivered; capture to verify its result" : "UI action is processing";
    }
    Json Capture(GLFWwindow *window)
    {
        if (Busy()) throw std::runtime_error("UI input is still processing; capture again after it finishes");
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) throw std::runtime_error("Restore the editor window before capturing");
        glfwGetCursorPos(window, &physicalX_, &physicalY_);
        int w = 0, h = 0; glfwGetFramebufferSize(window, &w, &h);
        glfwGetWindowSize(window, &width_, &height_);
        if (w <= 0 || h <= 0 || width_ <= 0 || height_ <= 0 || static_cast<std::int64_t>(w) * h > 16000000)
            throw std::runtime_error("Editor framebuffer is unavailable or exceeds 16 megapixels");
        std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * h * 4);
        GLint framebuffer = 0, buffer = GL_BACK, packBuffer = 0, alignment = 4, rowLength = 0, skipRows = 0, skipPixels = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer); glGetIntegerv(GL_READ_BUFFER, &buffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer); glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength); glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); glReadBuffer(GL_BACK); glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer); glReadBuffer(buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer); glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength); glPixelStorei(GL_PACK_SKIP_ROWS, skipRows); glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
        const double scale = (std::min)({1.0, 1600.0 / w, 1200.0 / h});
        const int outW = (std::max)(1, static_cast<int>(w * scale)), outH = (std::max)(1, static_cast<int>(h * scale));
        std::vector<unsigned char> image(static_cast<std::size_t>(outW) * outH * 4);
        for (int y = 0; y < outH; ++y) for (int x = 0; x < outW; ++x)
        {
            const int sourceX = static_cast<int>(static_cast<std::int64_t>(x) * w / outW);
            const int sourceY = h - 1 - static_cast<int>(static_cast<std::int64_t>(y) * h / outH);
            std::copy_n(pixels.data() + (static_cast<std::size_t>(sourceY) * w + sourceX) * 4, 4,
                image.data() + (static_cast<std::size_t>(y) * outW + x) * 4);
        }
        struct Png { std::vector<unsigned char> bytes; bool failed = false; } png;
        const auto write = [](void *context, void *data, int size) {
            auto &out = *static_cast<Png *>(context);
            if (out.failed) return;
            try { const auto *bytes = static_cast<unsigned char *>(data); out.bytes.insert(out.bytes.end(), bytes, bytes + size); }
            catch (...) { out.failed = true; }
        };
        stbi_flip_vertically_on_write(0);
        if (!stbi_write_png_to_func(write, &png, outW, outH, 4, image.data(), outW * 4) || png.failed)
            throw std::runtime_error("Could not encode the editor capture");
        token_ = EditorMcpBridge::NewSession(); captured_ = std::chrono::steady_clock::now();
        return {{"png_base64", Base64(png.bytes)}, {"capture_token", token_}, {"image_width", outW}, {"image_height", outH},
            {"ui_activity", activity},
            {"client_width", width_}, {"client_height", height_},
            {"coordinates", "Normalized client coordinates: x/image_width and y/image_height, from the top left. OS title bars/dialogs are excluded."}};
    }
    Json Input(GLFWwindow *window, const Json &a)
    {
        if (!allowed) throw std::runtime_error("Enable MCP UI control in the connection panel first");
        if (Busy()) throw std::runtime_error("Wait for the previous UI action and capture again");
        if (token_.empty() || a.value("capture_token", std::string()) != token_ ||
            std::chrono::steady_clock::now() - captured_ > std::chrono::seconds(30)) throw std::runtime_error("A fresh editor UI capture is required");
        double currentX, currentY; glfwGetCursorPos(window, &currentX, &currentY);
        if (std::abs(currentX - physicalX_) > 1 || std::abs(currentY - physicalY_) > 1) throw std::runtime_error("Local pointer moved since capture; capture again");
        int w, h; glfwGetWindowSize(window, &w, &h);
        if (w != width_ || h != height_ || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) throw std::runtime_error("Editor window changed; capture again");
        const auto action = a.at("action").get<std::string>();
        std::deque<std::function<void()>> pending;
        if (action == "click" || action == "double_click" || action == "drag" || action == "scroll")
        {
            const auto point = [&](const char *x, const char *y) {
                const float px = EditorMcp::Number(a.at(x)), py = EditorMcp::Number(a.at(y));
                if (px < 0 || px >= 1 || py < 0 || py >= 1) throw std::runtime_error("Coordinates must be in [0,1)");
                return ImVec2(px * w, py * h);
            };
            const auto start = point("x", "y");
            const int button = a.contains("button") ? EditorMcp::Integer(a, "button", 0, 2) : 0;
            if (action == "scroll")
            {
                const float amount = EditorMcp::Number(a.at("amount"));
                if (std::abs(amount) > 20) throw std::runtime_error("Scroll amount exceeds 20 steps");
                pending.push_back([=] { auto &io = ImGui::GetIO(); io.AddMousePosEvent(start.x, start.y); io.AddMouseWheelEvent(0, amount); });
            }
            else
            {
                const auto end = action == "drag" ? point("end_x", "end_y") : start;
                const bool ctrl = a.value("ctrl", false), shift = a.value("shift", false), alt = a.value("alt", false);
                const auto down = [=] { auto &io = ImGui::GetIO(); io.AddMousePosEvent(start.x, start.y);
                    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl); io.AddKeyEvent(ImGuiMod_Shift, shift); io.AddKeyEvent(ImGuiMod_Alt, alt);
                    io.AddMouseButtonEvent(button, true); };
                const auto up = [=] { auto &io = ImGui::GetIO(); io.AddMousePosEvent(end.x, end.y); io.AddMouseButtonEvent(button, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false); io.AddKeyEvent(ImGuiMod_Shift, false); io.AddKeyEvent(ImGuiMod_Alt, false); };
                pending.push_back(down);
                if (action == "drag") for (int step = 1; step <= 8; ++step)
                {
                    const auto p = ImVec2(start.x + (end.x - start.x) * step / 8, start.y + (end.y - start.y) * step / 8);
                    pending.push_back([=] { ImGui::GetIO().AddMousePosEvent(p.x, p.y); });
                }
                pending.push_back(up);
                if (action == "double_click") { pending.push_back(down); pending.push_back(up); }
            }
        }
        else if (action == "text")
        {
            const auto text = a.at("text").get<std::string>();
            if (text.size() > 4096 || text.find('\0') != std::string::npos) throw std::runtime_error("Text exceeds 4096 bytes or contains NUL");
            pending.push_back([text] { ImGui::GetIO().AddInputCharactersUTF8(text.c_str()); });
        }
        else if (action == "key")
        {
            auto text = a.at("key").get<std::string>(); const bool ctrl = text.starts_with("ctrl+");
            if (ctrl) text.erase(0, 5);
            const std::pair<const char *, ImGuiKey> keys[] = {{"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape},
                {"tab", ImGuiKey_Tab}, {"backspace", ImGuiKey_Backspace}, {"delete", ImGuiKey_Delete}, {"left", ImGuiKey_LeftArrow},
                {"right", ImGuiKey_RightArrow}, {"up", ImGuiKey_UpArrow}, {"down", ImGuiKey_DownArrow}, {"home", ImGuiKey_Home},
                {"end", ImGuiKey_End}, {"a", ImGuiKey_A}, {"z", ImGuiKey_Z}, {"y", ImGuiKey_Y}, {"s", ImGuiKey_S}};
            const auto found = std::find_if(std::begin(keys), std::end(keys), [&](const auto &k) { return text == k.first; });
            if (found == std::end(keys)) throw std::runtime_error("Unsupported key");
            const auto key = found->second;
            pending.push_back([=] { auto &io = ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl, ctrl); io.AddKeyEvent(key, true); });
            pending.push_back([=] { auto &io = ImGui::GetIO(); io.AddKeyEvent(key, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); });
        }
        else throw std::runtime_error("Action must be click, double_click, drag, scroll, text or key");
        if (!glfwGetWindowAttrib(window, GLFW_FOCUSED))
        {
            glfwFocusWindow(window); focusRequestedFrame_ = frame_;
        }
        events_ = std::move(pending);
        token_.clear();
        activity = "UI action queued";
        return {{"queued", true}, {"frames", events_.size()}, {"note", "Capture after input finishes to observe the result. This uses the actual UI and can open native dialogs requiring manual interaction."}};
    }
};
