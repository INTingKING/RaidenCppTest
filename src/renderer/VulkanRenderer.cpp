#include "renderer/VulkanRenderer.hpp"

#include "core/File.hpp"
#include "core/Log.hpp"
#include "core/Mesh.hpp"
#include "core/Vertex.hpp"
#include "renderer/VkCheck.hpp"

#include <stb_image.h>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace raiden {
namespace {

constexpr uint32_t kMaxFramesInFlight = 2;
constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
constexpr const char* kPortabilitySubset = "VK_KHR_portability_subset";

bool gValidationEnabled = false;

bool wantsValidation() {
#if defined(RAIDEN_VALIDATION) && RAIDEN_VALIDATION
    return true;
#else
    return false;
#endif
}

template <typename T, typename Pred>
bool containsIf(const std::vector<T>& items, Pred pred) {
    return std::any_of(items.begin(), items.end(), pred);
}

bool hasExtension(const std::vector<VkExtensionProperties>& ext, const char* name) {
    return containsIf(ext, [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return containsIf(layers, [&](const VkLayerProperties& l) { return std::strcmp(l.layerName, name) == 0; });
}

std::vector<VkExtensionProperties> instanceExtensions() {
    uint32_t count = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> ext(count);
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, ext.data()));
    return ext;
}

std::vector<VkLayerProperties> instanceLayers() {
    uint32_t count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, nullptr));
    std::vector<VkLayerProperties> layers(count);
    VK_CHECK(vkEnumerateInstanceLayerProperties(&count, layers.data()));
    return layers;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*user*/) {
    const char* message = data && data->pMessage ? data->pMessage : "";
    if (std::strstr(message, "because it is a duplicate")) {
        return VK_FALSE;
    }
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        RAIDEN_ERR("validation: %s", message);
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    QueueFamilies families;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());
    for (uint32_t i = 0; i < count; ++i) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            families.graphics = i;
        }
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
        if (present) {
            families.present = i;
        }
        if (families.complete()) {
            break;
        }
    }
    return families;
}

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupport querySwapchainSupport(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    SwapchainSupport support;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &support.capabilities));
    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr));
    support.formats.resize(formatCount);
    if (formatCount > 0) {
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, support.formats.data()));
    }
    uint32_t modeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr));
    support.presentModes.resize(modeCount);
    if (modeCount > 0) {
        VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, support.presentModes.data()));
    }
    return support;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (const auto mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

}  // namespace

struct VulkanRenderer::Impl {
    GLFWwindow* window = nullptr;
    std::filesystem::path assets;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    QueueFamilies queues;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;

    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformMemories;
    std::vector<void*> uniformMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;
    bool samplerAnisotropy = false;
    float maxAnisotropy = 1.0f;

    Impl(GLFWwindow* w, std::filesystem::path assetRoot) : window(w), assets(std::move(assetRoot)) {
        try {
            createInstance();
            setupDebugMessenger();
            createSurface();
            pickPhysicalDevice();
            createLogicalDevice();
            createSwapchain();
            createImageViews();
            createRenderPass();
            createDescriptorSetLayout();
            createGraphicsPipeline();
            createCommandPool();
            createDepthResources();
            createFramebuffers();
            createTexture();
            createSampler();
            createMeshBuffers();
            createUniformBuffers();
            createDescriptorPool();
            createDescriptorSets();
            createCommandBuffers();
            createSyncObjects();
            createSwapchainSync();
            RAIDEN_LOG("Vulkan renderer ready (%ux%u)", swapchainExtent.width, swapchainExtent.height);
        } catch (...) {
            destroy();
            throw;
        }
    }

    ~Impl() { destroy(); }

    void destroy() {
        if (device) {
            vkDeviceWaitIdle(device);
        }
        cleanupSwapchain();
        if (device) {
            vkDestroySampler(device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
            vkDestroyImageView(device, textureView, nullptr);
            textureView = VK_NULL_HANDLE;
            vkDestroyImage(device, textureImage, nullptr);
            textureImage = VK_NULL_HANDLE;
            vkFreeMemory(device, textureMemory, nullptr);
            textureMemory = VK_NULL_HANDLE;
            vkDestroyBuffer(device, vertexBuffer, nullptr);
            vertexBuffer = VK_NULL_HANDLE;
            vkFreeMemory(device, vertexMemory, nullptr);
            vertexMemory = VK_NULL_HANDLE;
            vkDestroyBuffer(device, indexBuffer, nullptr);
            indexBuffer = VK_NULL_HANDLE;
            vkFreeMemory(device, indexMemory, nullptr);
            indexMemory = VK_NULL_HANDLE;
            for (size_t i = 0; i < uniformBuffers.size(); ++i) {
                vkDestroyBuffer(device, uniformBuffers[i], nullptr);
                vkFreeMemory(device, uniformMemories[i], nullptr);
            }
            uniformBuffers.clear();
            uniformMemories.clear();
            uniformMapped.clear();
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
            for (auto semaphore : imageAvailable) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
            imageAvailable.clear();
            for (auto fence : inFlightFences) {
                vkDestroyFence(device, fence, nullptr);
            }
            inFlightFences.clear();
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (instance) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
            if (debugMessenger) {
                auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                if (fn) {
                    fn(instance, debugMessenger, nullptr);
                }
                debugMessenger = VK_NULL_HANDLE;
            }
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
    }

    void createInstance() {
        const auto availableExt = instanceExtensions();
        const auto availableLayers = instanceLayers();

        gValidationEnabled = wantsValidation() && hasLayer(availableLayers, kValidationLayer);
        if (wantsValidation() && !gValidationEnabled) {
            RAIDEN_LOG("Validation layers requested but %s is not present; continuing without them",
                       kValidationLayer);
        }

        uint32_t glfwCount = 0;
        const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
        if (!glfwExt || glfwCount == 0) {
            throw std::runtime_error("glfwGetRequiredInstanceExtensions returned none");
        }

        std::vector<const char*> extensions(glfwExt, glfwExt + glfwCount);
        if (gValidationEnabled && hasExtension(availableExt, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        bool portability = false;
        if (hasExtension(availableExt, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            portability = true;
        }
        if (hasExtension(availableExt, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }

        std::vector<const char*> layers;
        if (gValidationEnabled) {
            layers.push_back(kValidationLayer);
        }

        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Raiden";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "Raiden";
        app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_1;

        auto debugInfo = debugMessengerInfo();
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.enabledLayerCount = static_cast<uint32_t>(layers.size());
        info.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        if (portability) {
            info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
        if (gValidationEnabled) {
            info.pNext = &debugInfo;
        }
        VK_CHECK(vkCreateInstance(&info, nullptr, &instance));
    }

    void setupDebugMessenger() {
        if (!gValidationEnabled) {
            return;
        }
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!fn) {
            return;
        }
        auto info = debugMessengerInfo();
        VK_CHECK(fn(instance, &info, nullptr, &debugMessenger));
    }

    void createSurface() {
        VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
    }

    bool deviceSupportsSwapchain(VkPhysicalDevice gpu) const {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> ext(count);
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, ext.data());
        return hasExtension(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    int scoreDevice(VkPhysicalDevice gpu) const {
        if (!findQueueFamilies(gpu, surface).complete() || !deviceSupportsSwapchain(gpu)) {
            return -1;
        }
        const auto support = querySwapchainSupport(gpu, surface);
        if (support.formats.empty() || support.presentModes.empty()) {
            return -1;
        }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }
        score += static_cast<int>(props.limits.maxImageDimension2D / 1024);
        return score;
    }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        if (count == 0) {
            throw std::runtime_error("No Vulkan physical devices found");
        }
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));

        int bestScore = -1;
        for (auto gpu : devices) {
            const int score = scoreDevice(gpu);
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(gpu, &props);
            RAIDEN_LOG("GPU: %s (score %d)", props.deviceName, score);
            if (score > bestScore) {
                bestScore = score;
                physical = gpu;
            }
        }
        if (!physical || bestScore < 0) {
            throw std::runtime_error("No suitable Vulkan GPU with swapchain support");
        }
        queues = findQueueFamilies(physical, surface);

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physical, &props);
        RAIDEN_LOG("Using GPU: %s", props.deviceName);

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physical, &features);
        samplerAnisotropy = features.samplerAnisotropy == VK_TRUE;
        maxAnisotropy = props.limits.maxSamplerAnisotropy;
    }

    void createLogicalDevice() {
        std::set<uint32_t> uniqueFamilies = {*queues.graphics, *queues.present};
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        float priority = 1.0f;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            info.queueFamilyIndex = family;
            info.queueCount = 1;
            info.pQueuePriorities = &priority;
            queueInfos.push_back(info);
        }

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &extCount, available.data());

        std::vector<const char*> deviceExt = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (hasExtension(available, kPortabilitySubset)) {
            deviceExt.push_back(kPortabilitySubset);
        }

        VkPhysicalDeviceFeatures enabled{};
        enabled.samplerAnisotropy = samplerAnisotropy ? VK_TRUE : VK_FALSE;

        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        info.pQueueCreateInfos = queueInfos.data();
        info.enabledExtensionCount = static_cast<uint32_t>(deviceExt.size());
        info.ppEnabledExtensionNames = deviceExt.data();
        info.pEnabledFeatures = &enabled;
        VK_CHECK(vkCreateDevice(physical, &info, nullptr, &device));
        vkGetDeviceQueue(device, *queues.graphics, 0, &graphicsQueue);
        vkGetDeviceQueue(device, *queues.present, 0, &presentQueue);
    }

    void createSwapchain() {
        const auto support = querySwapchainSupport(physical, surface);
        const auto format = chooseSurfaceFormat(support.formats);
        const auto presentMode = choosePresentMode(support.presentModes);
        const auto extent = chooseExtent(support.capabilities, window);
        if (extent.width == 0 || extent.height == 0) {
            return;
        }

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const uint32_t familyIndices[] = {*queues.graphics, *queues.present};
        if (*queues.graphics != *queues.present) {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = familyIndices;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        info.preTransform = support.capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = presentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain));

        swapchainFormat = format.format;
        swapchainExtent = extent;
        uint32_t actual = 0;
        VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &actual, nullptr));
        swapchainImages.resize(actual);
        VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &actual, swapchainImages.data()));
    }

    VkImageView makeImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(device, &info, nullptr, &view));
        return view;
    }

    void createImageViews() {
        swapchainViews.resize(swapchainImages.size());
        for (size_t i = 0; i < swapchainImages.size(); ++i) {
            swapchainViews[i] = makeImageView(swapchainImages[i], swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                                 VkFormatFeatureFlags features) const {
        for (VkFormat format : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &props);
            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
        throw std::runtime_error("No supported depth format");
    }

    void createRenderPass() {
        depthFormat = findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

        VkAttachmentDescription color{};
        color.format = swapchainFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth{};
        depth.format = depthFormat;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments = {color, depth};
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments = attachments.data();
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        VK_CHECK(vkCreateRenderPass(device, &info, nullptr, &renderPass));
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding ubo{};
        ubo.binding = 0;
        ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo.descriptorCount = 1;
        ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding texture{};
        texture.binding = 1;
        texture.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture.descriptorCount = 1;
        texture.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {ubo, texture};
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &descriptorSetLayout));
    }

    VkShaderModule createShaderModule(const std::vector<uint32_t>& code) const {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
        return module;
    }

    void createGraphicsPipeline() {
        const auto vertCode = readSpirv(assets / "shaders" / "mesh.vert.spv");
        const auto fragCode = readSpirv(assets / "shaders" / "mesh.frag.spv");
        VkShaderModule vert = createShaderModule(vertCode);
        VkShaderModule frag = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vert;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = frag;
        fragStage.pName = "main";

        const VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};
        const auto binding = Vertex::bindingDescription();
        const auto attributes = Vertex::attributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamics;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &msaa;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        info.subpass = 0;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = *queues.graphics;
        VK_CHECK(vkCreateCommandPool(device, &info, nullptr, &commandPool));
    }

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(physical, &mem);
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("No suitable Vulkan memory type");
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory) const {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &info, nullptr, &buffer));

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, buffer, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, properties);
        VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &memory));
        VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    }

    void createImage2D(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                       VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                       VkDeviceMemory& memory) const {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = tiling;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateImage(device, &info, nullptr, &image));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, image, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, properties);
        VK_CHECK(vkAllocateMemory(device, &alloc, nullptr, &memory));
        VK_CHECK(vkBindImageMemory(device, image, memory, 0));
    }

    VkCommandBuffer beginOneShot() const {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandPool = commandPool;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device, &alloc, &cmd));
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
        return cmd;
    }

    void endOneShot(VkCommandBuffer cmd) const {
        VK_CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, VK_NULL_HANDLE));
        VK_CHECK(vkQueueWaitIdle(graphicsQueue));
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
        VkCommandBuffer cmd = beginOneShot();
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);
        endOneShot(cmd);
    }

    void transitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                         VkImageAspectFlags aspect) const {
        VkCommandBuffer cmd = beginOneShot();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
                   newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.dstAccessMask =
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        } else {
            throw std::runtime_error("Unsupported image layout transition");
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endOneShot(cmd);
    }

    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const {
        VkCommandBuffer cmd = beginOneShot();
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        endOneShot(cmd);
    }

    void createDepthResources() {
        createImage2D(swapchainExtent.width, swapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      depthImage, depthMemory);
        depthView = makeImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        transitionImage(depthImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    void createFramebuffers() {
        framebuffers.resize(swapchainViews.size());
        for (size_t i = 0; i < swapchainViews.size(); ++i) {
            const VkImageView attachments[] = {swapchainViews[i], depthView};
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = 2;
            info.pAttachments = attachments;
            info.width = swapchainExtent.width;
            info.height = swapchainExtent.height;
            info.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]));
        }
    }

    void createTexture() {
        const auto path = assets / "textures" / "panel.png";
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("Failed to load texture: " + path.string() + " (" + stbi_failure_reason() +
                                     ")");
        }
        const VkDeviceSize size = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                     stagingMem);
        void* data = nullptr;
        VK_CHECK(vkMapMemory(device, stagingMem, 0, size, 0, &data));
        std::memcpy(data, pixels, static_cast<size_t>(size));
        vkUnmapMemory(device, stagingMem);
        stbi_image_free(pixels);

        createImage2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height), VK_FORMAT_R8G8B8A8_SRGB,
                      VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureMemory);
        transitionImage(textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT);
        copyBufferToImage(staging, textureImage, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        transitionImage(textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        textureView = makeImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void createSampler() {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.anisotropyEnable = samplerAnisotropy ? VK_TRUE : VK_FALSE;
        info.maxAnisotropy = samplerAnisotropy ? maxAnisotropy : 1.0f;
        info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        info.maxLod = 0.0f;
        VK_CHECK(vkCreateSampler(device, &info, nullptr, &sampler));
    }

    void uploadBuffer(const void* src, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                      VkDeviceMemory& memory) const {
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging,
                     stagingMem);
        void* data = nullptr;
        VK_CHECK(vkMapMemory(device, stagingMem, 0, size, 0, &data));
        std::memcpy(data, src, static_cast<size_t>(size));
        vkUnmapMemory(device, stagingMem);
        createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer,
                     memory);
        copyBuffer(staging, buffer, size);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
    }

    void createMeshBuffers() {
        const Mesh mesh = Mesh::loadObj(assets / "models" / "cube.obj");
        indexCount = static_cast<uint32_t>(mesh.indices.size());
        uploadBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexMemory);
        uploadBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexMemory);
        RAIDEN_LOG("Mesh: %zu vertices, %u indices", mesh.vertices.size(), indexCount);
    }

    void createUniformBuffers() {
        uniformBuffers.resize(kMaxFramesInFlight);
        uniformMemories.resize(kMaxFramesInFlight);
        uniformMapped.resize(kMaxFramesInFlight);
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            createBuffer(sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         uniformBuffers[i], uniformMemories[i]);
            VK_CHECK(vkMapMemory(device, uniformMemories[i], 0, sizeof(UniformBufferObject), 0, &uniformMapped[i]));
        }
    }

    void createDescriptorPool() {
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = kMaxFramesInFlight;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = kMaxFramesInFlight;
        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.poolSizeCount = 2;
        info.pPoolSizes = sizes;
        info.maxSets = kMaxFramesInFlight;
        VK_CHECK(vkCreateDescriptorPool(device, &info, nullptr, &descriptorPool));
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, descriptorSetLayout);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = descriptorPool;
        alloc.descriptorSetCount = kMaxFramesInFlight;
        alloc.pSetLayouts = layouts.data();
        descriptorSets.resize(kMaxFramesInFlight);
        VK_CHECK(vkAllocateDescriptorSets(device, &alloc, descriptorSets.data()));

        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers[i];
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = sampler;
            imageInfo.imageView = textureView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
    }

    void createCommandBuffers() {
        commandBuffers.resize(kMaxFramesInFlight);
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = kMaxFramesInFlight;
        VK_CHECK(vkAllocateCommandBuffers(device, &alloc, commandBuffers.data()));
    }

    void createSyncObjects() {
        imageAvailable.resize(kMaxFramesInFlight);
        inFlightFences.resize(kMaxFramesInFlight);
        imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            VK_CHECK(vkCreateSemaphore(device, &sem, nullptr, &imageAvailable[i]));
            VK_CHECK(vkCreateFence(device, &fence, nullptr, &inFlightFences[i]));
        }
    }

    void createSwapchainSync() {
        for (auto semaphore : renderFinished) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        renderFinished.assign(swapchainImages.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (auto& semaphore : renderFinished) {
            VK_CHECK(vkCreateSemaphore(device, &sem, nullptr, &semaphore));
        }
    }

    void cleanupSwapchain() {
        if (!device) {
            return;
        }
        for (auto fb : framebuffers) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
        framebuffers.clear();
        if (depthView) {
            vkDestroyImageView(device, depthView, nullptr);
            depthView = VK_NULL_HANDLE;
        }
        if (depthImage) {
            vkDestroyImage(device, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }
        if (depthMemory) {
            vkFreeMemory(device, depthMemory, nullptr);
            depthMemory = VK_NULL_HANDLE;
        }
        for (auto view : swapchainViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        swapchainViews.clear();
        for (auto semaphore : renderFinished) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
        renderFinished.clear();
        if (swapchain) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
        imagesInFlight.clear();
    }

    void recreateSwapchain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(device);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createFramebuffers();
        createSwapchainSync();
        imagesInFlight.assign(swapchainImages.size(), VK_NULL_HANDLE);
    }

    void updateUniform(uint32_t frame, double timeSeconds) const {
        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), static_cast<float>(timeSeconds * 0.7), glm::vec3(0.2f, 1.0f, 0.15f));
        ubo.view = glm::lookAt(glm::vec3(2.15f, 1.45f, 2.15f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f),
                                    static_cast<float>(swapchainExtent.width) /
                                        static_cast<float>(swapchainExtent.height),
                                    0.1f, 20.0f);
        ubo.proj[1][1] *= -1.0f;
        ubo.lightDir = glm::vec4(glm::normalize(glm::vec3(0.4f, 0.85f, 0.35f)), 0.0f);
        std::memcpy(uniformMapped[frame], &ubo, sizeof(ubo));
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) const {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

        std::array<VkClearValue, 2> clears{};
        clears[0].color = {{0.025f, 0.03f, 0.05f, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass;
        rp.framebuffer = framebuffers[imageIndex];
        rp.renderArea.extent = swapchainExtent;
        rp.clearValueCount = static_cast<uint32_t>(clears.size());
        rp.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchainExtent.width);
        viewport.height = static_cast<float>(swapchainExtent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = swapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSets[currentFrame], 0, nullptr);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));
    }

    void draw(double timeSeconds) {
        if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
            recreateSwapchain();
            if (swapchainExtent.width == 0 || swapchainExtent.height == 0) {
                return;
            }
        }

        VK_CHECK(vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX));

        uint32_t imageIndex = 0;
        VkResult acquired =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE,
                                  &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
            return;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
            vkCheck(acquired, "vkAcquireNextImageKHR");
        }

        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            VK_CHECK(vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
        }
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        updateUniform(currentFrame, timeSeconds);
        VK_CHECK(vkResetCommandBuffer(commandBuffers[currentFrame], 0));
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
        VK_CHECK(vkResetFences(device, 1, &inFlightFences[currentFrame]));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable[currentFrame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffers[currentFrame];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[imageIndex];
        VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submit, inFlightFences[currentFrame]));

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        VkResult presented = vkQueuePresentKHR(presentQueue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
        } else if (presented != VK_SUCCESS) {
            vkCheck(presented, "vkQueuePresentKHR");
        }

        currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
    }
};

VulkanRenderer::VulkanRenderer(GLFWwindow* window, std::filesystem::path assetRoot)
    : impl_(std::make_unique<Impl>(window, std::move(assetRoot))) {}

VulkanRenderer::~VulkanRenderer() = default;

void VulkanRenderer::drawFrame(double timeSeconds) {
    impl_->draw(timeSeconds);
}

void VulkanRenderer::waitIdle() {
    if (impl_->device) {
        vkDeviceWaitIdle(impl_->device);
    }
}

void VulkanRenderer::notifyResize() {
    impl_->framebufferResized = true;
}

}  // namespace raiden
