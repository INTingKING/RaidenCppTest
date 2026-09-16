#pragma once

#include <filesystem>

namespace raiden {

std::filesystem::path executableDir(const char* argv0);
std::filesystem::path findAssetRoot(const std::filesystem::path& exeDir);

}  // namespace raiden
