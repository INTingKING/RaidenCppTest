#pragma once

#include "core/Vertex.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace raiden {

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    static Mesh loadObj(const std::filesystem::path& path);
};

}  // namespace raiden
