#include "core/Paths.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace raiden {
namespace {

bool looksLikeAssetRoot(const std::filesystem::path& dir) {
    return std::filesystem::exists(dir / "shaders") && std::filesystem::exists(dir / "textures");
}

}  // namespace

std::filesystem::path executableDir([[maybe_unused]] const char* argv0) {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        size = PATH_MAX;
    }
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.assign(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            throw std::runtime_error("_NSGetExecutablePath failed");
        }
    }
    return std::filesystem::weakly_canonical(buffer.data()).parent_path();
#else
    char buffer[PATH_MAX]{};
    const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        return std::filesystem::path(buffer).parent_path();
    }
    if (argv0 == nullptr) {
        throw std::runtime_error("Cannot resolve executable path");
    }
    return std::filesystem::absolute(argv0).parent_path();
#endif
}

std::filesystem::path findAssetRoot(const std::filesystem::path& exeDir) {
    if (const char* env = std::getenv("RAIDEN_ASSETS")) {
        const std::filesystem::path fromEnv(env);
        if (looksLikeAssetRoot(fromEnv)) {
            return std::filesystem::weakly_canonical(fromEnv);
        }
    }

    const std::vector<std::filesystem::path> candidates = {
        exeDir / "Assets",
        std::filesystem::current_path() / "Assets",
        exeDir / ".." / "Assets",
        exeDir.parent_path() / "Assets",
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && looksLikeAssetRoot(candidate)) {
            return std::filesystem::weakly_canonical(candidate);
        }
    }

    throw std::runtime_error(
        "Could not find the Assets folder. It is copied next to the Raiden "
        "executable at build time. You can also set RAIDEN_ASSETS.");
}

}  // namespace raiden
