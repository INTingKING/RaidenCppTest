#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace raiden {

std::string readText(const std::filesystem::path& path);
std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path);

}  // namespace raiden
