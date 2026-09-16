#pragma once

#include <functional>
#include <utility>

struct GLFWwindow;

namespace raiden {

struct WindowDesc {
    int width = 1280;
    int height = 720;
    const char* title = "Raiden";
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const;
    void poll() const;
    double time() const;
    GLFWwindow* handle() const { return window_; }
    std::pair<int, int> framebufferSize() const;
    void onResize(std::function<void(int, int)> callback);

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    std::function<void(int, int)> resizeCallback_;
};

}  // namespace raiden
