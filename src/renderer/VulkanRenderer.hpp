#pragma once

#include <filesystem>
#include <memory>

struct GLFWwindow;

namespace raiden {

class VulkanRenderer {
public:
    VulkanRenderer(GLFWwindow* window, std::filesystem::path assetRoot);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void drawFrame(double timeSeconds);
    void waitIdle();
    void notifyResize();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace raiden
