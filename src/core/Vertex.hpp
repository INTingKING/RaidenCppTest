#pragma once

#include <array>
#include <cstddef>

#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace raiden {

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, position);

        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, normal);

        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(Vertex, uv);
        return attrs;
    }
};

struct UniformBufferObject {
    glm::mat4 model{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec4 lightDir{0.35f, 0.8f, 0.45f, 0.0f};
};

}  // namespace raiden
