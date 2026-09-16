#include "core/File.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace raiden {
namespace {

std::vector<char> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    const auto size = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    if (!file) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    return buffer;
}

}  // namespace

std::string readText(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    return {bytes.begin(), bytes.end()};
}

std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    if (bytes.size() % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("SPIR-V file is not a multiple of 4 bytes: " + path.string());
    }
    std::vector<std::uint32_t> code(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(code.data(), bytes.data(), bytes.size());
    return code;
}

}  // namespace raiden
