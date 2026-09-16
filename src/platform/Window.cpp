#include "platform/Window.hpp"

#include "core/Log.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace raiden {
namespace {

void glfwErrorCallback(int code, const char* description) {
    RAIDEN_ERR("GLFW error %d: %s", code, description ? description : "");
}

}  // namespace

Window::Window(const WindowDesc& desc) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }
    if (!glfwVulkanSupported()) {
        glfwTerminate();
        throw std::runtime_error("GLFW reports that Vulkan is not supported on this system");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void Window::poll() const {
    glfwPollEvents();
}

double Window::time() const {
    return glfwGetTime();
}

std::pair<int, int> Window::framebufferSize() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {width, height};
}

void Window::onResize(std::function<void(int, int)> callback) {
    resizeCallback_ = std::move(callback);
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->resizeCallback_) {
        self->resizeCallback_(width, height);
    }
}

}  // namespace raiden
